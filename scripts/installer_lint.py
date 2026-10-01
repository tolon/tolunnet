#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static lint for Install_Tolunnet.script (z.ai step 10a-3 item 3).

Parses the Installer S-expressions and enforces the rules that keep the
script safe to run unattended:
- only known Installer 43 functions; `action`, `complete` and a `text`
  clause inside `welcome` fail;
- every `exists` carries `(noreq)` (no insert-disk requesters);
- a `copyfiles` whose dest ends in a file name without `(newname)` fails
  (dest is a directory - the SYS:C/tolunnet/tolunnet bug of 10a-2);
- `copylib` is allowed only for Libs/*.library;
- every command copied to SYS:C has a backup block under
  SYS:Storage/tolunnet-backup/C/ and appears in the undo restore/delete
  logic (restore from the backup, or delete when it was new);
- the wizard run (SYS:Prefs/TolunnetSetup) comes after all copies;
- `execute` is only for DOS scripts (S:/... or *.script) - binaries
  need `run`;
- `exit` never combines text with (quiet) (the text would be hidden);
- every `makedir`'s parent is created or exists-checked first;
- no `textfile` destination is written twice;
- the undo starts with FailAt 21 and deletes C: files only inside
  If EXISTS guards (Delete of a missing file aborts the script);
- every C: binary documented in docs/commands.md is copied.

Exit codes: 0 = clean, 1 = findings or selftest mismatch, 2 = usage/IO
error.  `--selftest` runs the lint against `fe3c61b`, `565fe26` (both
must fail) and `HEAD` (must pass) via `git show`.
"""

import io
import subprocess
import sys

FORBIDDEN = {"action", "complete"}

ALLOWED = {
    # flow / top level
    "welcome", "message", "exit", "abort", "if", "select", "while",
    "until", "foreach", "next", "set", "transcript",
    # user interaction
    "askbool", "askchoice", "askdir", "askdisk", "askfile", "asknumber",
    "askoptions", "askpassword", "askstring",
    # file operations
    "copyfiles", "copylib", "copypart", "delete", "rename", "protect",
    "textfile", "makedir", "startup", "tooltype", "user", "group",
    # clauses / predicates
    "command", "newname", "source", "dest", "patterns", "infos",
    "optional", "confirm", "help", "prompt", "default", "choices",
    "range", "noreq", "all", "append", "prepend", "newpath", "newpaths",
    "quiet", "text", "verify", "safe", "newfile", "infolist",
    # execution / logic / functions
    "run", "execute", "rexx", "arexx", "exists", "not", "and", "or",
    "=", "<>", "<", ">", "<=", ">=", "+", "-", "*", "/", "cat",
    "getsize", "getsum", "getdiskspace", "getdevice", "getenv",
    "tackon", "path", "fileonly", "pathonly", "version", "onerror",
    "pset", "mydir-list", "getassign", "getversion",
    "resident",  # 11ab item 2b: (getversion "exec.library" (resident))
    "shiftleft",  # 11ac item 1: (shiftleft 39 16), NOT 39<<16
}


class Str(str):
    """A quoted string literal (distinct from a bare atom)."""


class LintError(Exception):
    pass


def tokenize(text):
    toks = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == ";":
            while i < n and text[i] != "\n":
                i += 1
        elif c in "()":
            toks.append(c)
            i += 1
        elif c == '"':
            j = i + 1
            buf = []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    buf.append(text[j:j + 2])
                    j += 2
                else:
                    buf.append(text[j])
                    j += 1
            if j >= n:
                raise LintError("unterminated string literal")
            toks.append(Str("".join(buf)))
            i = j + 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '();"':
                j += 1
            toks.append(text[i:j])
            i = j
    return toks


def parse(text):
    toks = tokenize(text)
    pos = [0]

    def parse_list():
        pos[0] += 1  # consume '('
        node = []
        while pos[0] < len(toks) and toks[pos[0]] != ")":
            node.append(parse_item())
        if pos[0] >= len(toks):
            raise LintError("unterminated ( list")
        pos[0] += 1  # consume ')'
        return node

    def parse_item():
        t = toks[pos[0]]
        if t == "(":
            return parse_list()
        if t == ")":
            raise LintError("unexpected )")
        pos[0] += 1
        return t

    top = []
    while pos[0] < len(toks):
        top.append(parse_item())
    return top


def clause(node, name):
    """Payload of a (name X) clause inside node, else None."""
    for child in node[1:]:
        if isinstance(child, list) and child and child[0] == name:
            return child[1] if len(child) > 1 else True
    return None


def subtree_tokens(nodes):
    out = []
    for node in nodes:
        if isinstance(node, list):
            out.extend(subtree_tokens(node))
        else:
            out.append(node)
    return out


def lint_top(top, findings):
    makedir_state = (set(), set())
    textfile_dests = set()

    def walk(node, ancestors):
        if not (isinstance(node, list) and node):
            return
        head = node[0]
        kids = node[1:]
        if isinstance(head, str) and not isinstance(head, Str):
            if head in FORBIDDEN:
                findings.append("forbidden function: (%s ...)" % head)
            elif head not in ALLOWED:
                findings.append("unknown function: (%s ...)" % head)
            if head == "execute":
                cmd = next((c for c in kids if isinstance(c, Str)), None)
                if isinstance(cmd, Str):
                    t = str(cmd)
                    if not (t.startswith("S:") or t.endswith(".script")):
                        findings.append(
                            "execute runs a DOS script, not a binary: %s "
                            "(use run for programs)" % t)
            if head == "exit":
                has_text = any(isinstance(c, Str) for c in kids)
                has_quiet = any(isinstance(c, list) and c and c[0] == "quiet"
                                for c in kids)
                if has_text and has_quiet:
                    findings.append(
                        "(quiet) suppresses the exit text the user is "
                        "meant to see")
            if head == "makedir":
                target = next((c for c in kids if isinstance(c, Str)), None)
                if isinstance(target, Str):
                    seen_made, seen_exists = makedir_state
                    t = str(target)
                    vol, sep, rest = t.partition(":")
                    if sep and "/" in rest:
                        parent = vol + ":" + rest.rsplit("/", 1)[0]
                        if parent not in seen_made and                                 parent not in seen_exists:
                            findings.append(
                                "makedir '%s': parent '%s' is not created "
                                "or checked first" % (t, parent))
                    seen_made.add(t)
            if head == "textfile":
                dest = clause(node, "dest")
                if isinstance(dest, Str):
                    if str(dest) in textfile_dests:
                        findings.append(
                            "textfile to the same dest twice: %s (the "
                            "second write replaces the first)" % dest)
                    textfile_dests.add(str(dest))
            if head == "welcome":
                for child in kids:
                    if isinstance(child, list) and child and child[0] == "text":
                        findings.append(
                            "welcome takes plain strings; a (text ...) clause "
                            "inside welcome is not Installer 43")
            elif head == "exists":
                tgt = next((c for c in kids if isinstance(c, Str)), None)
                if isinstance(tgt, Str):
                    makedir_state[1].add(str(tgt))
                if not any(isinstance(c, list) and c and c[0] == "noreq"
                           for c in kids):
                    findings.append("exists without (noreq)")
            elif head == "copyfiles":
                dest = clause(node, "dest")
                newname = clause(node, "newname")
                if isinstance(dest, Str):
                    vol, sep, rest = str(dest).partition(":")
                    if sep and "/" in rest and not rest.endswith("/") \
                            and not newname:
                        findings.append(
                            "copyfiles dest '%s' ends in a file name without "
                            "(newname); dest is a directory" % dest)
            elif head == "copylib":
                src = clause(node, "source")
                if not (isinstance(src, Str) and str(src).startswith("Libs/")
                        and str(src).endswith(".library")):
                    findings.append(
                        "copylib is only allowed for Libs/*.library (got %s)"
                        % src)

        for child in kids:
            walk(child, ancestors + [node])

    for node in top:
        walk(node, [])


def doc_c_commands(doc_text):
    """C: binary names from docs/commands.md (first cell of command
    tables; GUI Tools and API sections excluded)."""
    names = []
    section = ""
    for line in io.StringIO(doc_text):
        line = line.rstrip("\n")
        if line.startswith("## "):
            section = line[3:].strip()
            continue
        if line.startswith("| `") and not section.startswith("GUI Tools") \
                and not section.startswith("API"):
            for tok in line.split("|")[1].split("`"):
                tok = tok.strip()
                if tok and tok[0].isalnum() and " " not in tok \
                        and tok not in names:
                    names.append(tok)
    return names


def copied_sources(top):
    out = set()

    def walk(node):
        if isinstance(node, list) and node:
            if node[0] in ("copyfiles", "copylib"):
                src = clause(node, "source")
                if isinstance(src, Str):
                    out.add(str(src))
            for child in node[1:]:
                walk(child)

    for node in top:
        walk(node)
    return out


def all_strings(top):
    out = []

    def walk(node):
        if isinstance(node, list):
            for child in node:
                if isinstance(child, Str):
                    out.append(str(child))
                else:
                    walk(child)

    for node in top:
        walk(node)
    return out


def wizard_after_copies(top):
    """Pre-order position of the wizard invocation must exceed every
    copyfiles/copylib anywhere in the tree."""
    wizard_pos = None
    last_copy_pos = None
    counter = [0]

    def walk(node):
        if not (isinstance(node, list) and node):
            return
        head = node[0]
        pos = counter[0]
        counter[0] += 1
        if head in ("copyfiles", "copylib"):
            globals_["last_copy"] = pos
        if head in ("run", "execute"):
            for child in node[1:]:
                if isinstance(child, Str) and                         "SYS:Prefs/TolunnetSetup" in str(child):
                    globals_["wizard"] = pos
        for child in node[1:]:
            walk(child)

    globals_ = {"wizard": None, "last_copy": None}
    for node in top:
        walk(node)
    wizard_pos = globals_["wizard"]
    last_copy_pos = globals_["last_copy"]
    if wizard_pos is None:
        return "no wizard run (SYS:Prefs/TolunnetSetup) found"
    if last_copy_pos is not None and wizard_pos < last_copy_pos:
        return "the wizard run must come after the copies"
    return None


def backup_undo_coverage(top, names):
    findings = []
    strings = all_strings(top)
    backed_up = set()

    def walk(node):
        if isinstance(node, list) and node:
            if node[0] == "copyfiles":
                dest = clause(node, "dest")
                if isinstance(dest, Str) and                         str(dest) == "SYS:Storage/tolunnet-backup/C":
                    newname = clause(node, "newname")
                    src = clause(node, "source")
                    if isinstance(newname, Str):
                        backed_up.add(str(newname))
                    elif isinstance(src, Str):
                        backed_up.add(str(src).split("/")[-1])
            for child in node[1:]:
                walk(child)

    for node in top:
        walk(node)
    for n in names:
        if n not in backed_up:
            findings.append("no backup block for SYS:C/%s" % n)
            continue
        undo_restore = any(
            ("SYS:Storage/tolunnet-backup/C/" + n) in t and
            ("SYS:C/" + n) in t for t in strings)
        if not undo_restore:
            findings.append("undo does not restore SYS:C/%s" % n)
            continue
        # 10e item 2: the delete of a restored name must sit inside the
        # Else of its own backup If - the flat If/If shape deleted what
        # the Copy had just restored.
        idx_bif = next(i for i, t in enumerate(strings)
                       if "If EXISTS SYS:Storage/tolunnet-backup/C/" + n in t)
        idx_copy = next(i for i, t in enumerate(strings)
                        if "Copy >NIL: SYS:Storage/tolunnet-backup/C/" + n in t)
        else_nl = "Else" + chr(92) + "n"
        idx_else = next((i for i, t in enumerate(strings)
                         if i > idx_copy and t.strip() in ("Else", else_nl)), None)
        idx_gif = next((i for i, t in enumerate(strings)
                        if i > (idx_else or 0) and
                        "If EXISTS SYS:C/" + n in t), None)
        idx_del = next((i for i, t in enumerate(strings)
                        if i > (idx_gif or 0) and
                        "Delete >NIL: SYS:C/" + n in t), None)
        if idx_else is None or idx_gif is None or idx_del is None:
            findings.append(
                "undo block for SYS:C/%s is not "
                "If EXISTS backup / Copy / Else / If EXISTS / Delete "
                "(the flat If/If shape deletes the restored file)" % n)
    if not any("FailAt 21" in t for t in strings):
        findings.append(
            "undo has no 'FailAt 21' (a missing file must not abort it)")
    return findings


def lint_atoms(top, findings):
    """11ac item 1: every bare atom must be a number, an @variable,
    a #variable that a (set ...) assigned earlier in the file, or a
    known function name. `39<<16` was one such unknown atom and the
    REAL Installer aborts on it."""
    import re
    num = re.compile(r"^-?\d+$")
    seen_sets = set()

    def walk(node):
        if not (isinstance(node, list) and node):
            return
        head = node[0]
        if isinstance(head, str) and not isinstance(head, Str):
            if head == "set":
                for kid in node[1:]:
                    if (not isinstance(kid, list)
                            and not isinstance(kid, Str)
                            and str(kid).startswith("#")):
                        seen_sets.add(str(kid))
        for kid in node[1:]:
            if isinstance(kid, list):
                walk(kid)
            elif isinstance(kid, Str):
                continue
            else:
                a = str(kid)
                if num.match(a) or a.startswith("@"):
                    continue
                if a.startswith("#") and a in seen_sets:
                    continue
                if a in ALLOWED:
                    continue
                findings.append("bare atom %r: not a number, @variable, known function, or #variable set earlier" % a)

    walk(top)


def lint_text(script_text, doc_text):
    findings = []
    try:
        top = parse(script_text)
    except LintError as exc:
        return [str(exc)]
    lint_top(top, findings)
    lint_atoms(top, findings)
    copied = copied_sources(top)
    c_names = [c[2:] for c in sorted(copied) if c.startswith("C/")]
    for name in doc_c_commands(doc_text):
        if ("C/" + name) not in copied:
            findings.append("docs/commands.md C: binary not copied: %s" % name)
    findings.extend(backup_undo_coverage(top, c_names))
    wiz = wizard_after_copies(top)
    if wiz:
        findings.append(wiz)
    return findings


def lint_files(script_path, doc_path):
    with open(script_path, encoding="utf-8") as fh:
        script_text = fh.read()
    with open(doc_path, encoding="utf-8") as fh:
        doc_text = fh.read()
    return lint_text(script_text, doc_text)


def git_show(rev, path):
    out = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise LintError("git show %s:%s failed: %s" %
                        (rev, path, out.stderr.strip()))
    return out.stdout


def atom_selftest():
    """11ac item 1: the two shapes that fooled the old lint must
    FAIL the atom check."""
    bad_shift = lint_text(
        '(welcome "x")\n'
        '(if (< (getversion "exec.library" (resident)) 39<<16)\n'
        '  (abort "old"))\n', "")
    bad_var = lint_text(
        '(welcome "x")\n'
        '(if (> @user-level 0) (message #undefined-var))\n', "")
    ok = True
    if not any("39<<16" in f or "bare atom" in f for f in bad_shift):
        print("installer_lint atom selftest: FAIL - 39<<16 passed")
        ok = False
    if not any("#undefined-var" in f or "undefined" in f.lower()
               for f in bad_var):
        print("installer_lint atom selftest: FAIL - undefined #variable "
              "passed")
        ok = False
    if ok:
        print("installer_lint atom selftest: OK (39<<16 and undefined "
              "#variable both fail)")
    return ok


def selftest(script_path, doc_path):
    """The lint must fail on fe3c61b and 565fe26, pass on HEAD."""
    ok = True
    with open(doc_path, encoding="utf-8") as fh:
        doc_text = fh.read()
    for rev, expect_pass in (("fe3c61b", False), ("565fe26", False),
                             ("1e88155", False), ("e2d54e8", False),
                             ("c83af21", False), ("HEAD", True)):
        try:
            findings = lint_text(git_show(rev, script_path), doc_text)
        except LintError as exc:
            findings = [str(exc)]
        passed = not findings
        good = passed == expect_pass
        ok = ok and good
        print("installer_lint selftest %s: %s (expected %s)%s" % (
            rev, "PASS" if passed else "FAIL",
            "PASS" if expect_pass else "FAIL", "" if good else " MISMATCH"))
        for f in findings[:3]:
            print("   - %s" % f)
    return ok


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    flags = {a for a in argv if a.startswith("--")}
    script = args[0] if args else "Install_Tolunnet.script"
    doc = "docs/commands.md"
    try:
        if "--selftest" in flags:
            r1 = selftest(script, doc)
            r2 = atom_selftest()
            return 0 if (r1 == 0 and r2 == 0) else 1
        findings = lint_files(script, doc)
    except (LintError, OSError) as exc:
        print("installer_lint: ERROR: %s" % exc)
        return 2
    if findings:
        for f in findings:
            print("installer_lint: %s" % f)
        return 1
    print("installer_lint: %s clean" % script)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
