#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""undo_sim.py - a tiny AmigaDOS-script interpreter for the generated
tolunnet undo (z.ai step 10e item 2).

Understands the constructs the undo uses: `FailAt <n>`,
`If EXISTS <path>` / `Else` / `EndIf`, `Copy >NIL: <src> <dst> CLONE`
and `Delete >NIL: <path> QUIET`, executed against a dict filesystem
(path -> str). `Delete` of a missing path fails with 20; the script
aborts when a failure exceeds the current FailAt (default 10) - the
semantics that broke the 10c/10d benches.

`--selftest` runs four cases against the undo generated from HEAD and
one against the undo generated at c26f2e9 (the flat If/If shape that
deleted what it had just restored - it must FAIL):
  1. a replaced tool comes back from the backup and the backup dir is
     removed;
  2. a tool that was new (no backup) is deleted;
  3. a missing tool (curl) does not stop the script;
  4. no S:User-Startup backup: the script still completes.

Exit codes: 0 = all cases behaved, 1 = mismatch, 2 = usage error.
"""

import subprocess
import sys

ROOT = "T:tnsbx/"
FAIL_DELETE_MISSING = 20


def tokenize_line(line):
    """(verb, arg) for one undo line; None for blanks and comments."""
    t = line.strip()
    if not t or t.startswith(";"):
        return None
    if t.startswith("FailAt "):
        return ("failat", int(t.split()[1]))
    if t.startswith("If EXISTS "):
        return ("if", t[len("If EXISTS "):].strip())
    if t == "Else":
        return ("else", None)
    if t == "EndIf":
        return ("endif", None)
    if t.startswith("Run "):
        # background command (the sandbox self-delete) - no filesystem
        # effect the sim needs to model
        return ("run", t)
    if t.startswith("Copy "):
        rest = t[len("Copy "):].split(">NIL: ", 1)[1]
        parts = rest.split()
        return ("copy", (parts[0], parts[1]))
    if t.startswith("Delete "):
        rest = t[len("Delete "):].split(">NIL: ", 1)[1]
        return ("delete", rest.split()[0])
    raise ValueError("undo_sim: unknown line: %s" % t)


def parse(lines):
    """lines -> nested block: items are ('failat', n), ('copy', (src, dst)),
    ('delete', path) or ('if', cond, body, ebody)."""
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


def run_undo(lines, fs):
    """Execute the parsed undo against fs. Returns (fs, aborted, code)."""
    state = {"failat": 10, "aborted": False, "code": 0}

    def exec_block(block):
        for item in block:
            if state["aborted"]:
                return
            verb = item[0]
            if verb == "if":
                exec_block(item[2] if item[1] in fs else item[3])
            elif verb == "failat":
                state["failat"] = item[1]
            elif verb == "copy":
                src, dst = item[1]
                if src not in fs:
                    state["code"] = FAIL_DELETE_MISSING
                    if FAIL_DELETE_MISSING > state["failat"]:
                        state["aborted"] = True
                        return
                else:
                    fs[dst] = fs[src]
            elif verb == "delete":
                path = item[1]
                if path in fs:
                    del fs[path]
                else:
                    # a directory: remove everything beneath it
                    under = [k for k in fs
                             if k.startswith(path + "/")]
                    if not under:
                        state["code"] = FAIL_DELETE_MISSING
                        if FAIL_DELETE_MISSING > state["failat"]:
                            state["aborted"] = True
                            return
                    for k in under:
                        del fs[k]

    exec_block(parse(lines))
    return fs, state["aborted"], state["code"]


def get_undo_text(rev=None, root=ROOT):
    import gen_installer
    if rev is None:
        with open("Install_Tolunnet.script", encoding="utf-8") as fh:
            text = fh.read()
    else:
        text = subprocess.run(
            ["git", "show", "%s:Install_Tolunnet.script" % rev],
            capture_output=True, text=True, check=True).stdout
    return gen_installer.sandbox_undo(text, root)


def seeded_fs():
    return {
        ROOT + "C/NetShutdown": "OUR-BIN",
        ROOT + "C/nc": "OUR-NC",
        ROOT + "C/telnet": "OUR-TELNET",
        ROOT + "Storage/tolunnet-backup/C/NetShutdown": "ROADSHW",
        ROOT + "Storage/tolunnet-backup/C/telnet": "RS-TELNET",
        ROOT + "S/User-Startup": "boot shim\n",
        ROOT + "S/User-Startup.tolunnet-bak": "pre-install\n",
        ROOT + "LIBS/bsdsocket.library.pre-tolunnet": "rs",
    }


def selftest():
    ok = True

    def check(name, cond, detail=""):
        nonlocal ok
        ok = ok and cond
        print("undo_sim %s: %s%s" % (name, "PASS" if cond else "FAIL",
                                     "" if cond else " (%s)" % detail))

    lines = get_undo_text().split("\n")

    # cases 1-3: backup restored, new tool deleted, missing curl stops
    # nothing (telnet sorts after curl and must still be restored).
    fs2, aborted, _ = run_undo(lines, seeded_fs())
    check("backup restored", fs2.get(ROOT + "C/NetShutdown") == "ROADSHW")
    check("new tool deleted", ROOT + "C/nc" not in fs2)
    check("missing curl stops nothing",
          not aborted and fs2.get(ROOT + "C/telnet") == "RS-TELNET",
          "aborted=%s" % aborted)
    check("user-startup restored",
          fs2.get(ROOT + "S/User-Startup") == "pre-install\n")
    check("library restored",
          fs2.get(ROOT + "LIBS/bsdsocket.library") == "rs")
    check("backup dir gone",
          not any(k.startswith(ROOT + "Storage/tolunnet-backup")
                  for k in fs2))

    # case 4: no User-Startup backup - still completes
    fs3 = {k: v for k, v in seeded_fs().items()
           if k != ROOT + "S/User-Startup.tolunnet-bak"}
    fs3, aborted3, _ = run_undo(lines, fs3)
    check("no user-startup backup survives",
          not aborted3 and
          fs3.get(ROOT + "S/User-Startup") == "boot shim\n",
          "aborted=%s" % aborted3)

    # regression: the undo generated at c26f2e9 (flat If/If) DELETES the
    # file it just restored - the sim must expose that.
    old_lines = get_undo_text("c26f2e9").split("\n")
    fs4, _, _ = run_undo(old_lines, seeded_fs())
    old_ok = fs4.get(ROOT + "C/NetShutdown") == "ROADSHW" and \
        not any(k.startswith(ROOT + "Storage/tolunnet-backup/C/")
                for k in fs4)
    check("c26f2e9 shape fails (regression)", not old_ok,
          "old shape unexpectedly passed")

    return ok


def main(argv):
    if "--selftest" in argv:
        return 0 if selftest() else 1
    print("usage: undo_sim.py --selftest")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
