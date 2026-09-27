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
- a `run` touching SYS:Prefs/ must sit inside an `(if (> @user-level 0) ...)`
  guard (the wizard is a GUI program; nobody clicks in an unattended run);
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


def guarded_by_userlevel(ancestors):
    for anc in ancestors:
        if isinstance(anc, list) and anc and anc[0] == "if":
            toks = subtree_tokens(anc[1:2])
            for i in range(len(toks) - 2):
                if toks[i] == ">" and toks[i + 1] == "@user-level" \
                        and toks[i + 2] == "0":
                    return True
    return False


def lint_top(top, findings):
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
            if head == "welcome":
                for child in kids:
                    if isinstance(child, list) and child and child[0] == "text":
                        findings.append(
                            "welcome takes plain strings; a (text ...) clause "
                            "inside welcome is not Installer 43")
            elif head == "exists":
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
            elif head == "run":
                cmd = next((c for c in kids if isinstance(c, Str)), None)
                if cmd is not None and "SYS:Prefs/" in str(cmd):
                    if not guarded_by_userlevel(ancestors):
                        findings.append(
                            "run of a SYS:Prefs/ tool outside an "
                            "(if (> @user-level 0) ...) guard")
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


def lint_text(script_text, doc_text):
    findings = []
    try:
        top = parse(script_text)
    except LintError as exc:
        return [str(exc)]
    lint_top(top, findings)
    copied = copied_sources(top)
    for name in doc_c_commands(doc_text):
        if ("C/" + name) not in copied:
            findings.append("docs/commands.md C: binary not copied: %s" % name)
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


def selftest(script_path, doc_path):
    """The lint must fail on fe3c61b and 565fe26, pass on HEAD."""
    ok = True
    with open(doc_path, encoding="utf-8") as fh:
        doc_text = fh.read()
    for rev, expect_pass in (("fe3c61b", False), ("565fe26", False),
                             ("HEAD", True)):
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
            return 0 if selftest(script, doc) else 1
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
