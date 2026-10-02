#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""undo_sim.py - simulate the tolunnet install and its undo against a
dict filesystem (z.ai step 10e item 2, bugtrack 2026-10-02 9.2).

Two small interpreters:

* DOS: the AmigaDOS subset the undo uses - `FailAt <n>`,
  `If [NOT] EXISTS <path>` / `If [NOT] WARN` / `Else` / `EndIf`,
  `Copy >NIL: <src> <dst> CLONE`, `Delete >NIL: <path> [ALL] QUIET`,
  `Rename >NIL: <from> <to>`, `Echo ...`, `Execute <script>` and the
  final `Run >NIL: Delete ...`. Every command sets the return code; a
  code above the current FailAt (default 10) aborts the script. Delete
  of a missing path fails (20), Delete of a non-empty directory without
  ALL fails (20), Rename onto an existing name fails (20), and any write
  to a path in `fs.readonly` or with a name over the 30-character FFS
  limit fails (20) - so Copy-then-Delete without a check is caught.
* Installer: the Installer 44 subset Install_Tolunnet.script uses
  (if/set/cat/exists/getsum/getsize/copyfiles/copylib/textfile/makedir/
  rename/delete/startup/run/abort/exit, @pretend, @user-level). `if`
  takes exactly cond/then/else - more arguments are an error (that is
  how the real Installer silently skipped the old 35-statement gate).

`--selftest` installs the CURRENT Install_Tolunnet.script into seeded
systems, lets the wizard hook drop an S:tolunnet-undo-stacks, runs the
undo the installer WROTE, and checks the result: first install over
Roadshow, reinstall, reinstall over a newer stack, an rc5-era (legacy)
system, an install aborted half way, a Copy failure inside the undo,
the undo without FailAt, a stale User-Startup backup, PRETEND mode -
plus the bench sandbox undo (gen_installer --undo-root) and the c26f2e9
flat If/If regression, which must still FAIL.

Exit codes: 0 = all cases behaved, 1 = mismatch, 2 = usage error.
"""

import os
import subprocess
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = "T:tnsbx/"
FAIL = 20
DIR = object()


class Abort(Exception):
    pass


class Exit(Exception):
    pass


# ---------------------------------------------------------------- fs --

ASSIGNS = (("libs:", "sys:libs/"), ("c:", "sys:c/"), ("s:", "sys:s/"),
           ("devs:", "sys:devs/"), ("env:", "sys:env/"))


def norm(path):
    p = path.strip().strip('"').lower()
    if not p.startswith("sys:"):
        for a, b in ASSIGNS:
            if p.startswith(a):
                p = b + p[len(a):]
                break
    return p.rstrip("/")


class FS(dict):
    """norm(path) -> str content, or DIR for an explicit directory.
    A path with children is an (implicit) directory."""

    def __init__(self, init=None, readonly=()):
        dict.__init__(self)
        self.readonly = set(norm(p) for p in readonly)
        for k, v in (init or {}).items():
            self[norm(k)] = v

    def get_(self, path):
        return self.get(norm(path))

    def kind(self, path):
        p = norm(path)
        if p.endswith(":") or p == "":
            return 2
        if p in self:
            return 2 if self[p] is DIR else 1
        if any(k.startswith(p + "/") for k in self):
            return 2
        return 0

    def children(self, path):
        p = norm(path) + "/"
        return [k for k in self if k.startswith(p)]

    def writable(self, path):
        """Not write-protected, and every name within the 30-character
        OFS/FFS limit (a longer name fails like a full disk)."""
        p = norm(path)
        names = p.split(":", 1)[-1].split("/")
        return p not in self.readonly and all(len(n) <= 30 for n in names)


# --------------------------------------------------------------- DOS --

def tokenize_line(line):
    """(verb, arg) for one script line; None for blanks and comments."""
    t = line.strip()
    if not t or t.startswith(";"):
        return None
    words = t.split()
    w0 = words[0].upper()
    if w0 == "FAILAT":
        return ("failat", int(words[1]))
    if w0 == "IF":
        rest = [w.upper() for w in words[1:]]
        neg = False
        if rest and rest[0] == "NOT":
            neg = True
            rest = rest[1:]
            words = words[:1] + words[2:]
        if rest and rest[0] == "EXISTS":
            return ("if", (neg, "exists", words[2]))
        if rest and rest[0] == "WARN":
            return ("if", (neg, "warn", None))
        raise ValueError("undo_sim: unknown If form: %s" % t)
    if w0 == "ELSE":
        return ("else", None)
    if w0 == "ENDIF":
        return ("endif", None)
    if w0 == "RUN":
        # the background self-delete: modelled as a Delete
        inner = t.split(None, 1)[1]
        if inner.startswith(">NIL:"):
            inner = inner[5:].strip()
        return tokenize_line(inner)
    args = [w for w in words[1:] if w.upper() != ">NIL:"]
    if w0 == "COPY":
        return ("copy", (args[0], args[1]))
    if w0 == "DELETE":
        flags = set(a.upper() for a in args[1:])
        return ("delete", (args[0], "ALL" in flags))
    if w0 == "RENAME":
        return ("rename", (args[0], args[1]))
    if w0 == "ECHO":
        return ("echo", t[len(words[0]):].strip())
    if w0 == "EXECUTE":
        return ("execute", args[0])
    raise ValueError("undo_sim: unknown line: %s" % t)


def parse(lines):
    """lines -> nested block: ('if', cond, body, ebody) or (verb, arg)."""
    fixed = []
    stack = [fixed]
    for line in lines:
        tok = tokenize_line(line)
        if tok is None:
            continue
        verb, arg = tok
        if verb == "if":
            node = ["if", arg, [], []]
            stack[-1].append(node)
            stack.append(node[2])
        elif verb == "else":
            stack.pop()
            stack.append(stack[-1][-1][3])
        elif verb == "endif":
            stack.pop()
        else:
            stack[-1].append([verb, arg, None, None])
    if len(stack) != 1:
        raise ValueError("unterminated If block")
    return fixed


def run_dos(lines, fs, log=None, depth=0):
    """Execute an AmigaDOS script against fs. Returns (aborted, rc)."""
    if not isinstance(fs, FS):
        fs = FS(fs)
    st = {"failat": 10, "aborted": False, "rc": 0, "worst": 0}
    log = log if log is not None else []

    def fail(code=FAIL):
        st["rc"] = code
        st["worst"] = max(st["worst"], code)
        if code >= st["failat"]:
            st["aborted"] = True

    def exec_block(block):
        for item in block:
            if st["aborted"]:
                return
            verb = item[0]
            if verb == "if":
                neg, what, path = item[1]
                if what == "exists":
                    c = fs.kind(path) != 0
                else:
                    c = st["rc"] >= 5
                exec_block(item[2] if c != neg else item[3])
                continue
            st["rc"] = 0
            if verb == "failat":
                st["failat"] = item[1]
            elif verb == "echo":
                log.append(item[1])
            elif verb == "copy":
                src, dst = item[1]
                if fs.kind(src) != 1 or not fs.writable(dst):
                    fail()
                else:
                    fs[norm(dst)] = fs.get_(src)
            elif verb == "delete":
                path, all_ = item[1]
                k = fs.kind(path)
                kids = fs.children(path)
                if k == 0 or not fs.writable(path) or \
                        any(not fs.writable(c) for c in kids):
                    fail()
                elif k == 2 and kids and not all_:
                    fail()  # directory not empty
                else:
                    for c in kids:
                        del fs[c]
                    fs.pop(norm(path), None)
            elif verb == "rename":
                a, b = item[1]
                if fs.kind(a) != 1 or fs.kind(b) != 0 or \
                        not fs.writable(a) or not fs.writable(b):
                    fail()
                else:
                    fs[norm(b)] = fs.pop(norm(a))
            elif verb == "execute":
                if fs.kind(item[1]) != 1 or depth > 4:
                    fail()
                else:
                    ab, rc = run_dos(fs.get_(item[1]).split("\n"), fs,
                                     log, depth + 1)
                    st["rc"] = rc
                    if rc >= st["failat"]:
                        st["aborted"] = True

    exec_block(parse(lines))
    return st["aborted"], st["rc"]


def run_undo(lines, fs):
    """Compatibility wrapper: (fs, aborted, rc)."""
    if not isinstance(fs, FS):
        fs = FS(fs)
    aborted, rc = run_dos(lines, fs)
    return fs, aborted, rc


# --------------------------------------------------------- Installer --

def unescape(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append({"n": "\n", "t": "\t"}.get(s[i + 1], s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


class Installer:
    def __init__(self, fs, package, pretend=False, user_level=0,
                 wizard=None, abort_after_copies=None):
        from installer_lint import Str
        self.Str = Str
        self.fs = fs
        self.pkg = package
        self.pretend = pretend
        self.user_level = user_level
        self.wizard = wizard
        self.abort_after = abort_after_copies
        self.copies = 0
        self.vars = {}

    def clause(self, node, name):
        for c in node[1:]:
            if isinstance(c, list) and c and c[0] == name:
                return c[1:]
        return None

    def ev(self, node):
        Str = self.Str
        if isinstance(node, Str):
            return unescape(str(node))
        if not isinstance(node, list):
            a = str(node)
            if a.lstrip("-").isdigit():
                return int(a)
            if a == "@pretend":
                return 1 if self.pretend else 0
            if a == "@user-level":
                return self.user_level
            if a.startswith("#"):
                if a not in self.vars:
                    raise ValueError("unset variable %s" % a)
                return self.vars[a]
            raise ValueError("unknown atom %s" % a)
        if not node:
            return None
        if isinstance(node[0], list):  # ((stmt) (stmt)) block
            r = None
            for s in node:
                r = self.ev(s)
            return r
        h = node[0]
        f = getattr(self, "f_" + h.replace("-", "_"), None)
        ops = {"=": lambda a, b: a == b, "<>": lambda a, b: a != b,
               "<": lambda a, b: a < b, ">": lambda a, b: a > b,
               "<=": lambda a, b: a <= b, ">=": lambda a, b: a >= b}
        if h in ops:
            return 1 if ops[h](self.ev(node[1]), self.ev(node[2])) else 0
        if f is None:
            raise ValueError("undo_sim: Installer function not modelled: "
                             "%s" % h)
        return f(node)

    def run(self, top):
        try:
            for n in top:
                self.ev(n)
        except Exit:
            pass

    # control
    def f_if(self, n):
        if len(n) > 4:
            raise ValueError("(if) with %d statements - Installer runs only "
                             "then/else" % (len(n) - 2))
        if self.ev(n[1]):
            return self.ev(n[2])
        return self.ev(n[3]) if len(n) > 3 else None

    def f_set(self, n):
        r = None
        for i in range(1, len(n) - 1, 2):
            r = self.vars[str(n[i])] = self.ev(n[i + 1])
        return r

    def f_and(self, n):
        return 1 if all(self.ev(x) for x in n[1:]) else 0

    def f_or(self, n):
        return 1 if any(self.ev(x) for x in n[1:]) else 0

    def f_not(self, n):
        return 0 if self.ev(n[1]) else 1

    def f_shiftleft(self, n):
        return self.ev(n[1]) << self.ev(n[2])

    def f_cat(self, n):
        return "".join(str(self.ev(x)) for x in n[1:])

    def f_welcome(self, n):
        return None

    f_message = f_welcome

    def f_abort(self, n):
        raise Abort(self.ev(n[1]))

    def f_exit(self, n):
        raise Exit()

    def f_askbool(self, n):
        d = self.clause(n, "default")
        return self.ev(d[0]) if d else 0

    # functions
    def f_exists(self, n):
        return self.fs.kind(self.ev(n[1]))

    def f_getsum(self, n):
        v = self.fs.get_(self.ev(n[1]))
        if not isinstance(v, str):
            raise ValueError("getsum of a missing file")
        return zlib.crc32(v.encode()) & 0x7fffffff

    def f_getsize(self, n):
        v = self.fs.get_(self.ev(n[1]))
        if not isinstance(v, str):
            raise ValueError("getsize of a missing file")
        return len(v)

    def f_getversion(self, n):
        return 40 << 16

    # actions (none of them write in PRETEND mode)
    def _src(self, path):
        if ":" in path:
            return self.fs.get_(path)
        return self.pkg.get(path)

    def _write(self, path, data):
        if not self.fs.writable(path):
            raise Abort("write-protected: %s" % path)
        parent = norm(path).rsplit("/", 1)[0]
        if "/" in norm(path) and self.fs.kind(parent) != 2:
            raise Abort("no such directory: %s" % parent)
        self.fs[norm(path)] = data

    def f_copyfiles(self, n):
        src = self.ev(self.clause(n, "source")[0])
        dest = self.ev(self.clause(n, "dest")[0])
        nn = self.clause(n, "newname")
        name = self.ev(nn[0]) if nn else src.replace(":", "/").split("/")[-1]
        data = self._src(src)
        if data is None:
            raise Abort("copyfiles: no source %s" % src)
        if dest.startswith("SYS:C") and not src.startswith("SYS:"):
            self.copies += 1
            if self.abort_after is not None and \
                    self.copies > self.abort_after:
                raise Abort("user abort during the copies")
        if self.pretend:
            return 1
        sep = "" if dest.endswith(":") else "/"
        self._write(dest + sep + name, data)
        if self.clause(n, "infos") is not None and \
                self._src(src + ".info") is not None:
            self._write(dest + sep + name + ".info", self._src(src + ".info"))
        return 1

    f_copylib = f_copyfiles

    def f_textfile(self, n):
        dest = self.ev(self.clause(n, "dest")[0])
        app = self.clause(n, "append")
        data = "".join(str(self.ev(a)) for a in app) if app else ""
        if not self.pretend:
            self._write(dest, data)
        return 1

    def f_makedir(self, n):
        d = self.ev(n[1])
        if not self.pretend:
            if not self.fs.writable(d):
                raise Abort("makedir failed: %s" % d)
            self.fs[norm(d)] = DIR
        return 1

    def f_delete(self, n):
        p = self.ev(n[1])
        if self.pretend:
            return 1
        if self.fs.kind(p) != 1 or not self.fs.writable(p):
            raise Abort("delete failed: %s" % p)
        del self.fs[norm(p)]
        return 1

    def f_rename(self, n):
        a, b = self.ev(n[1]), self.ev(n[2])
        if self.pretend:
            return 0
        if self.fs.kind(a) != 1 or self.fs.kind(b) != 0 or \
                not self.fs.writable(a) or not self.fs.writable(b):
            return 0
        self.fs[norm(b)] = self.fs.pop(norm(a))
        return 1

    def f_startup(self, n):
        name = self.ev(n[1])
        cmd = "".join(self.ev(c) for c in self.clause(n, "command"))
        if self.pretend:
            return 1
        us = self.fs.get_("S:User-Startup") or ""
        block = ";BEGIN %s\n%s\n;END %s\n" % (name, cmd.rstrip("\n"), name)
        b, e = ";BEGIN %s\n" % name, ";END %s\n" % name
        if b in us and e in us:
            i = us.index(b)
            j = us.index(e, i) + len(e)
            us = us[:i] + block + us[j:]
        else:
            us = us + block
        self._write("S:User-Startup", us)
        return 1

    def f_run(self, n):
        cmd = self.ev(n[1])
        if self.pretend:
            return 0
        if cmd == "SYS:Prefs/TolunnetSetup":
            if self.wizard:
                self.wizard(self.fs)
            return 0
        raise ValueError("undo_sim: (run %r) not modelled" % cmd)


# ---------------------------------------------------------- scenarios --

def script_text(rev=None):
    if rev is None:
        with open("Install_Tolunnet.script", encoding="utf-8") as fh:
            return fh.read()
    return subprocess.run(
        ["git", "show", "%s:Install_Tolunnet.script" % rev],
        capture_output=True, text=True, check=True).stdout


def get_undo_text(rev=None, root=ROOT):
    import gen_installer
    return gen_installer.sandbox_undo(script_text(rev), root)


def package(names, tag="v2"):
    pkg = {"C/" + n: "TN-%s-%s" % (n, tag) for n in names}
    pkg["Libs/usergroup.library"] = "TN-UG-" + tag
    for p in ("TolunnetPrefs", "TolunnetSetup"):
        pkg[p] = "TN-%s-%s" % (p, tag)
        pkg[p + ".info"] = "TN-ICON"
    return pkg


def wizard_hook(fs):
    """What the setup wizard does to the undo contract (1.1): it writes
    S:tolunnet-undo-stacks, which restores its own changes and deletes
    itself, and never touches S:tolunnet-undo."""
    fs[norm("SYS:WBStartup/Miami.info.pre-tolunnet")] = "MIAMI-ICON"
    fs[norm("S:tolunnet-undo-stacks")] = (
        "FailAt 21\n"
        "If EXISTS SYS:WBStartup/Miami.info.pre-tolunnet\n"
        "  Rename >NIL: SYS:WBStartup/Miami.info.pre-tolunnet "
        "SYS:WBStartup/Miami.info\n"
        "EndIf\n"
        "Delete >NIL: S:tolunnet-undo-stacks QUIET\n")


def roadshow_system():
    return {
        "SYS:C/ping": "RS-PING",
        "SYS:C/ifconfig": "RS-IFC",
        "SYS:C/Dir": "OS-DIR",
        "LIBS:bsdsocket.library": "RS-LIB",
        "SYS:Libs/usergroup.library": "AMITCP-UG",
        "S:User-Startup": "orig\n",
        "SYS:Prefs": DIR,
        "SYS:Devs": DIR,
    }


def install(fs, names, **kw):
    from installer_lint import parse as iparse
    ins = Installer(fs, kw.pop("pkg", None) or package(names),
                    wizard=kw.pop("wizard", wizard_hook), **kw)
    ins.run(iparse(script_text()))
    return ins


def undo(fs, strip_failat=False):
    text = fs.get_("S:tolunnet-undo")
    lines = text.split("\n")
    if strip_failat:
        lines = [x for x in lines if not x.strip().upper().startswith(
            "FAILAT")]
    log = []
    aborted, rc = run_dos(lines, fs, log)
    return aborted, log


def tolunnet_left(fs):
    # usergroup.library is never deleted by the undo (other software
    # may use it) - only restored when it had an original
    return sorted(k for k, v in fs.items()
                  if isinstance(v, str) and v.startswith("TN-")
                  and not k.startswith(norm("SYS:Storage"))
                  and k != norm("SYS:Libs/usergroup.library"))


def selftest():
    import gen_installer
    from installer_lint import doc_c_commands
    ok = True

    def check(name, cond, detail=""):
        nonlocal ok
        ok = ok and bool(cond)
        print("undo_sim %s: %s%s" % (name, "PASS" if cond else "FAIL",
                                     "" if cond else " (%s)" % detail))

    with open("docs/commands.md", encoding="utf-8") as fh:
        names = doc_c_commands(fh.read())
    for n in gen_installer.copied_c_names(script_text()):
        if n not in names:
            names.append(n)

    # A. first install over Roadshow, wizard runs, then undo
    fs = FS(roadshow_system())
    install(fs, names)
    undo_written = fs.get_("S:tolunnet-undo")
    check("A install: wizard left S:tolunnet-undo alone (1.1)",
          undo_written and "tolunnet-undo-stacks" in undo_written)
    check("A install: live library parked by rename (1.2)",
          fs.get_("LIBS:bsdsocket.library.pre-tolunnet") == "RS-LIB" and
          fs.kind("LIBS:bsdsocket.library") == 0)
    check("A install: usergroup.library backed up (1.11)",
          fs.get_("SYS:Storage/tolunnet-backup/Libs/usergroup.library")
          == "AMITCP-UG")
    us = fs.get_("S:User-Startup")
    check("A install: boot block guarded (1.3)",
          "If EXISTS C:tolunnet\nStack 32768\nRun <NIL: >NIL: C:tolunnet\n"
          "EndIf" in us, repr(us))
    fs.readonly.clear()
    fs[norm("S:User-Startup")] = us + "Assign Later: Work:Later\n"
    aborted, log = undo(fs)
    check("A undo completes", not aborted)
    check("A undo: wizard's undo-stacks ran first and is gone (1.1)",
          fs.get_("SYS:WBStartup/Miami.info") == "MIAMI-ICON" and
          fs.kind("S:tolunnet-undo-stacks") == 0)
    check("A undo: originals back",
          fs.get_("SYS:C/ping") == "RS-PING" and
          fs.get_("SYS:C/ifconfig") == "RS-IFC" and
          fs.get_("SYS:C/Dir") == "OS-DIR")
    check("A undo: no tolunnet file left", not tolunnet_left(fs),
          tolunnet_left(fs))
    check("A undo: library restored",
          fs.get_("LIBS:bsdsocket.library") == "RS-LIB" and
          fs.kind("LIBS:bsdsocket.library.pre-tolunnet") == 0)
    check("A undo: usergroup.library restored (1.11)",
          fs.get_("SYS:Libs/usergroup.library") == "AMITCP-UG")
    us = fs.get_("S:User-Startup") or ""
    check("A undo: later User-Startup edit kept (1.3)",
          "Assign Later: Work:Later" in us and us.startswith("orig\n"), us)
    check("A undo: .tolunnet-bak moved aside (1.3)",
          fs.kind("S:User-Startup.tolunnet-bak") == 0 and
          fs.get_("S:User-Startup.tolunnet-old") == "orig\n")
    check("A undo: backup dir and the undo itself gone",
          fs.kind("SYS:Storage/tolunnet-backup") == 0 and
          fs.kind("S:tolunnet-undo") == 0)

    # B. install twice (reinstall), then undo: tolunnet's own binaries
    # must not come back as "originals" (1.7)
    fs = FS(roadshow_system())
    install(fs, names)
    install(fs, names)
    aborted, log = undo(fs)
    check("B reinstall+undo: no tolunnet file left (1.7)",
          not aborted and not tolunnet_left(fs), tolunnet_left(fs))
    check("B reinstall+undo: originals back",
          fs.get_("SYS:C/ping") == "RS-PING" and
          fs.get_("LIBS:bsdsocket.library") == "RS-LIB" and
          fs.get_("SYS:Libs/usergroup.library") == "AMITCP-UG")

    # C. user installs a newer stack library and its own nc over
    # tolunnet, then reinstalls tolunnet (1.2, 1.7)
    fs = FS(roadshow_system())
    install(fs, names)
    fs[norm("LIBS:bsdsocket.library")] = "RS-LIB-NEWER"
    fs[norm("SYS:C/nc")] = "OTHER-NC"
    install(fs, names)
    check("C reinstall: newer library kept under its own name (1.2)",
          fs.get_("LIBS:bsdsocket.library.pre-tolunnet") == "RS-LIB" and
          fs.get_("LIBS:bsdsocket.library.pre-tn-newer")
          == "RS-LIB-NEWER")
    check("C reinstall: foreign nc backed up (1.7)",
          fs.get_("SYS:Storage/tolunnet-backup/C/nc") == "OTHER-NC")
    aborted, log = undo(fs)
    check("C undo: newest library and foreign nc back",
          not aborted and
          fs.get_("LIBS:bsdsocket.library") == "RS-LIB-NEWER" and
          fs.get_("LIBS:bsdsocket.library.pre-tolunnet") == "RS-LIB" and
          fs.get_("SYS:C/nc") == "OTHER-NC")
    # a third, different library while both parked copies exist: abort
    fs = FS(roadshow_system())
    install(fs, names)
    fs[norm("LIBS:bsdsocket.library")] = "RS-LIB-NEWER"
    install(fs, names)
    fs[norm("LIBS:bsdsocket.library")] = "RS-LIB-THIRD"
    try:
        install(fs, names)
        aborted_ins = False
    except Abort:
        aborted_ins = True
    check("C third library: install aborts, nothing deleted (1.2)",
          aborted_ins and fs.get_("LIBS:bsdsocket.library") ==
          "RS-LIB-THIRD")
    # an identical library is not parked twice
    fs = FS(roadshow_system())
    install(fs, names)
    fs[norm("LIBS:bsdsocket.library")] = "RS-LIB"
    install(fs, names)
    check("C identical library: dropped, one parked copy",
          fs.kind("LIBS:bsdsocket.library") == 0 and
          fs.kind("LIBS:bsdsocket.library.pre-tn-newer") == 0 and
          fs.get_("LIBS:bsdsocket.library.pre-tolunnet") == "RS-LIB")

    # D. upgrade over an rc5-era install (undo present, no markers)
    old = roadshow_system()
    old.update({
        "SYS:C/ping": "TN-ping-v1",
        "SYS:C/nc": "TN-nc-v1",
        "SYS:Libs/usergroup.library": "TN-UG-v1",
        "SYS:Storage/tolunnet-backup/C/ping": "RS-PING",
        "S:tolunnet-undo": "; rc5 undo\n",
        "S:User-Startup.tolunnet-bak": "orig\n",
        "LIBS:bsdsocket.library.pre-tolunnet": "RS-LIB",
    })
    del old["LIBS:bsdsocket.library"]
    fs = FS(old)
    install(fs, names)
    check("D legacy: rc5 binaries not backed up as originals (1.7)",
          fs.kind("SYS:Storage/tolunnet-backup/C/nc") == 0 and
          fs.kind("SYS:Storage/tolunnet-backup/Libs/usergroup.library")
          == 0 and
          fs.get_("SYS:Storage/tolunnet-backup/C/ping") == "RS-PING")
    aborted, log = undo(fs)
    check("D legacy undo: originals back, no tolunnet file left",
          not aborted and fs.get_("SYS:C/ping") == "RS-PING" and
          fs.kind("SYS:C/nc") == 0 and
          fs.get_("LIBS:bsdsocket.library") == "RS-LIB" and
          not tolunnet_left(fs), tolunnet_left(fs))

    # E. install aborted half way through the copies (1.5)
    fs = FS(roadshow_system())
    try:
        install(fs, names, abort_after_copies=5)
    except Abort:
        pass
    check("E abort: S:tolunnet-undo already written (1.5)",
          fs.kind("S:tolunnet-undo") == 1)
    aborted, log = undo(fs)
    check("E abort+undo: system as before",
          not aborted and fs.get_("SYS:C/ping") == "RS-PING" and
          fs.get_("SYS:C/ifconfig") == "RS-IFC" and
          fs.get_("LIBS:bsdsocket.library") == "RS-LIB" and
          not tolunnet_left(fs), tolunnet_left(fs))

    # L. the install stops while it is still taking backups (the
    # backup of ping cannot be written): the undo already exists and
    # must leave every not-yet-backed-up original alone
    sys_l = roadshow_system()
    sys_l["SYS:C/telnet"] = "RS-TELNET"
    fs = FS(sys_l, readonly=["SYS:Storage/tolunnet-backup/C/ping"])
    try:
        install(fs, names)
        stopped = False
    except Abort:
        stopped = True
    fs.readonly.clear()
    aborted, log = undo(fs)
    check("L abort during backups: originals untouched by the undo",
          stopped and not aborted and
          fs.get_("SYS:C/ping") == "RS-PING" and
          fs.get_("SYS:C/ifconfig") == "RS-IFC" and
          fs.get_("SYS:C/telnet") == "RS-TELNET" and
          fs.get_("LIBS:bsdsocket.library") == "RS-LIB" and
          not tolunnet_left(fs), tolunnet_left(fs))

    # F. a Copy fails inside the undo: the backup must survive (1.4),
    # the rest still runs, the backup dir stays
    fs = FS(roadshow_system())
    install(fs, names)
    fs.readonly.add(norm("SYS:C/ping"))
    aborted, log = undo(fs)
    check("F failed restore: backup kept, rest restored (1.4)",
          not aborted and
          fs.get_("SYS:Storage/tolunnet-backup/C/ping") == "RS-PING" and
          fs.get_("SYS:C/ifconfig") == "RS-IFC" and
          any("SYS:C/ping not restored" in x for x in log), log)
    # G. same failure without FailAt 21: the non-empty backup dir
    # Delete aborts the script - FailAt is load-bearing
    fs = FS(roadshow_system())
    install(fs, names)
    fs.readonly.add(norm("SYS:C/ping"))
    aborted, log = undo(fs, strip_failat=True)
    check("G undo without FailAt aborts (FailAt 21 needed)", aborted)

    # H. a stale User-Startup backup (no undo beside it) is replaced
    sys_h = roadshow_system()
    sys_h["S:User-Startup.tolunnet-bak"] = "ancient\n"
    fs = FS(sys_h)
    install(fs, names)
    check("H stale .tolunnet-bak refreshed (1.3)",
          fs.get_("S:User-Startup.tolunnet-bak") == "orig\n" and
          fs.get_("S:User-Startup.tolunnet-old") == "ancient\n")
    # I. User-Startup gone at undo time: the backup comes back
    fs = FS(roadshow_system())
    install(fs, names)
    del fs[norm("S:User-Startup")]
    undo(fs)
    check("I no live User-Startup: backup restored",
          fs.get_("S:User-Startup") == "orig\n")

    # J. PRETEND mode writes nothing and does not abort
    fs = FS(roadshow_system())
    before = dict(fs)
    try:
        install(fs, names, pretend=True)
        ab = False
    except Abort as exc:
        ab = str(exc)
    check("J pretend: no abort, nothing written", not ab and
          dict(fs) == before, ab)

    # K. bench sandbox undo (gen_installer --undo-root, staged by
    # ci/User-Startup-Conformance)
    lines = get_undo_text().split("\n")
    seed = {
        ROOT + "C/NetShutdown": "OUR-BIN",
        ROOT + "C/nc": "OUR-NC",
        ROOT + "Storage/tolunnet-backup/new/nc": "",
        ROOT + "Storage/tolunnet-backup/C/NetShutdown": "ROADSHW",
        ROOT + "S/User-Startup.tolunnet-bak": "orig\n",
        ROOT + "LIBS/bsdsocket.library.pre-tolunnet": "rs",
        ROOT + "Prefs/TolunnetPrefs": "OURS",
        ROOT + "Prefs/TolunnetPrefs.info": "OURS",
        ROOT + "Prefs/TolunnetSetup": "OURS",
        ROOT + "Prefs/TolunnetSetup.info": "OURS",
        ROOT + "LIBS/usergroup.library": "UGLIB",
        ROOT + "C/Unknown": "NOT-OURS",
        # a documented tool name with neither backup nor marker: an
        # original the install never got to - must survive
        ROOT + "C/telnet": "RS-TELNET",
    }
    fs2, aborted, _ = run_undo(lines, FS(seed))
    check("K sandbox: backup restored",
          not aborted and fs2.get_(ROOT + "C/NetShutdown") == "ROADSHW")
    check("K sandbox: marked new tool deleted",
          fs2.kind(ROOT + "C/nc") == 0)
    check("K sandbox: unmarked file left alone (1.7)",
          fs2.get_(ROOT + "C/Unknown") == "NOT-OURS" and
          fs2.get_(ROOT + "C/telnet") == "RS-TELNET")
    check("K sandbox: user-startup restored",
          fs2.get_(ROOT + "S/User-Startup") == "orig\n")
    check("K sandbox: library restored",
          fs2.get_(ROOT + "LIBS/bsdsocket.library") == "rs")
    check("K sandbox: prefs files deleted",
          all(fs2.kind(ROOT + "Prefs/" + n) == 0 for n in
              ("TolunnetPrefs", "TolunnetPrefs.info",
               "TolunnetSetup", "TolunnetSetup.info")))
    check("K sandbox: usergroup.library survives",
          fs2.get_(ROOT + "LIBS/usergroup.library") == "UGLIB")
    check("K sandbox: backup dir gone",
          fs2.kind(ROOT + "Storage/tolunnet-backup") == 0)

    # regression: the undo generated at c26f2e9 (flat If/If) DELETES the
    # file it just restored - the sim must expose that.
    old_lines = get_undo_text("c26f2e9").split("\n")
    fs4, _, _ = run_undo(old_lines, FS(seed))
    old_ok = fs4.get_(ROOT + "C/NetShutdown") == "ROADSHW" and \
        not fs4.children(ROOT + "Storage/tolunnet-backup/C")
    check("c26f2e9 shape fails (regression)", not old_ok,
          "old shape unexpectedly passed")
    # regression: the fe33177 install skipped its backups (35-statement
    # (if) gate) - the Installer model must refuse it
    from installer_lint import parse as iparse
    try:
        Installer(FS(roadshow_system()), package(names),
                  wizard=wizard_hook).run(iparse(script_text("fe33177")))
        old_ins = True
    except (ValueError, Abort):
        old_ins = False
    check("fe33177 install shape fails (regression)", not old_ins)
    return ok


def main(argv):
    if "--selftest" in argv:
        try:
            return 0 if selftest() else 1
        except (Abort, ValueError) as exc:
            # an install the scenarios expected to finish aborted
            print("undo_sim: FAIL - unexpected stop: %s" % exc)
            return 1
    print("usage: undo_sim.py --selftest")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
