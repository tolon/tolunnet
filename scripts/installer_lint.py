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
- every `if` has at most a then and an else statement (the real
  Installer silently ignores the rest - the fe33177 backup gate);
- every command copied to SYS:C has a backup block under
  SYS:Storage/tolunnet-backup/C/ verified by a getsize compare, and the
  undo restores it (Copy, then Delete of the backup only in the Else of
  `If WARN`) and deletes SYS:C/<x> only inside `If EXISTS` of its
  new/<x> marker; usergroup.library is backed up the same way;
- the S:tolunnet-undo textfile comes before every copy, rename, delete
  and startup, and no `if` on S:tolunnet-undo wraps the backups;
- the undo (parsed with undo_sim) starts with FailAt 21, runs
  S:tolunnet-undo-stacks first, never copies over S:User-Startup or a
  live LIBS:bsdsocket.library, uses ALL only on the marker dirs, and
  its Echo lines carry no Echo keyword and fit 255 chars;
- no file or directory name is longer than 30 characters (OFS/FFS),
  counting a (getsum ...) in a (cat ...) path as 11 digits;
- LIBS:bsdsocket.library is deleted only under a getsum compare, and
  (run) is used for the wizard only (no hidden Copy/Delete);
- the wizard run (SYS:Prefs/TolunnetSetup) comes after all copies;
- `execute` is only for DOS scripts (S:/... or *.script) - binaries
  need `run`;
- `exit` never combines text with (quiet) (the text would be hidden);
- every `makedir`'s parent is created or exists-checked first;
- no `textfile` destination is written twice;
- every C: binary documented in docs/commands.md is copied.

Exit codes: 0 = clean, 1 = findings or selftest mismatch, 2 = usage/IO
error.  `--selftest` runs the lint against older revisions via
`git show` (all must fail), against mutated copies of the working-tree
script (all must fail) and against the working-tree script itself
(must pass).
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


BK = "SYS:Storage/tolunnet-backup"
UNDO = "S:tolunnet-undo"
STACKS = "S:tolunnet-undo-stacks"
ECHO_KEYWORDS = {"NOLINE", "FIRST", "LEN"}


def walk_nodes(top):
    """Pre-order (node, ancestors) over every list node."""
    out = []

    def walk(node, anc):
        if isinstance(node, list) and node:
            out.append((node, anc))
            for child in node[1:] if not isinstance(node[0], list) \
                    else node:
                walk(child, anc + [node])

    for node in top:
        walk(node, [])
    return out


def node_strings(node):
    return [str(t) for t in subtree_tokens([node]) if isinstance(t, Str)]


def undo_text(top):
    """The S:tolunnet-undo textfile content (raw, with \\n escapes)."""
    for node, _ in walk_nodes(top):
        if node[0] == "textfile":
            dest = clause(node, "dest")
            if isinstance(dest, Str) and str(dest) == UNDO:
                app = [c for c in node[1:] if isinstance(c, list) and c
                       and c[0] == "append"]
                return "".join(node_strings(app[0])) if app else ""
    return None


def _long_names(path):
    return [n for n in path.split(":", 1)[1].split("/") if len(n) > 30]


def check_name_lengths(top):
    """OFS/FFS names stop at 30 characters. Checks every path literal,
    every (cat "<path>" (getsum ...)) (a getsum is up to 11 digits) and
    every path in the undo text."""
    import re
    pathlike = re.compile(r"^[A-Za-z]+:[^\s]*$")
    out = []
    for node, _ in walk_nodes(top):
        for c in node[1:] if not isinstance(node[0], list) else []:
            if isinstance(c, Str) and pathlike.match(str(c)):
                for n in _long_names(str(c)):
                    out.append("name over 30 characters: %s" % n)
        if node[0] == "cat" and node[1:] and isinstance(node[1], Str) \
                and pathlike.match(str(node[1])):
            s = "".join(str(c) if isinstance(c, Str) else "X" * 11
                        for c in node[1:])
            for n in _long_names(s):
                out.append("name over 30 characters (with an 11-digit "
                           "getsum): %s" % n)
    text = undo_text(top)
    for line in (text or "").split(chr(92) + "n"):
        for w in line.split():
            if pathlike.match(w):
                for n in _long_names(w):
                    out.append("undo name over 30 characters: %s" % n)
    return sorted(set(out))


def check_if_arity(top):
    """Installer (if) is cond + then + optional else; more statements
    are silently ignored by the real Installer."""
    out = []
    for node, _ in walk_nodes(top):
        if node[0] == "if" and len(node) > 4:
            out.append("(if) with %d statements - Installer runs only the "
                       "then and else; wrap a block in ((...) (...))"
                       % (len(node) - 2))
    return out


def check_order_and_gate(top):
    out = []
    nodes = walk_nodes(top)
    undo_pos = None
    for i, (node, _) in enumerate(nodes):
        if node[0] == "textfile" and isinstance(clause(node, "dest"), Str) \
                and str(clause(node, "dest")) == UNDO:
            undo_pos = i
            break
    if undo_pos is None:
        return ["no (textfile (dest \"S:tolunnet-undo\")) found"]
    for i, (node, _) in enumerate(nodes):
        if node[0] in ("copyfiles", "copylib", "rename", "delete",
                       "startup") and i < undo_pos:
            out.append("(%s ...) runs before S:tolunnet-undo is written - "
                       "an abort there leaves no undo" % node[0])
        if node[0] == "run":
            for s in node_strings(node):
                if s != "SYS:Prefs/TolunnetSetup":
                    out.append("(run %r): use copyfiles/rename/delete so "
                               "the lint can see file operations" % s)
        if node[0] == "if" and len(node) > 2 and \
                any(UNDO == s for s in node_strings(node[1])):
            body = node[2:]
            if any(n[0] == "copyfiles" for b in body
                   for n, _ in walk_nodes([b])):
                out.append("an (if) on S:tolunnet-undo gates the backups "
                           "- every file needs its own backup check")
        if node[0] == "delete" and \
                "LIBS:bsdsocket.library" in node_strings(node):
            anc = [a for a in _ancestors(nodes, i) if a[0] == "if"]
            if not any("getsum" in subtree_tokens([a[1]]) for a in anc):
                out.append("LIBS:bsdsocket.library deleted without a "
                           "getsum compare against its parked copy")
    return out


def _ancestors(nodes, i):
    return nodes[i][1]


def backup_verified(top, live, backup):
    """A copyfiles live -> backup dir exists, and an abort sits under an
    (if (<> (getsize live) (getsize backup)))."""
    copied = False
    verified = False
    bdir, _, bname = backup.rpartition("/")
    for node, anc in walk_nodes(top):
        if node[0] == "copyfiles":
            src = clause(node, "source")
            dest = clause(node, "dest")
            nn = clause(node, "newname")
            if isinstance(src, Str) and str(src) == live and \
                    isinstance(dest, Str) and str(dest) == bdir and \
                    isinstance(nn, Str) and str(nn) == bname:
                copied = True
        if node[0] == "abort":
            for a in anc:
                if a[0] == "if" and isinstance(a[1], list) and \
                        a[1] and a[1][0] == "<>":
                    s = node_strings(a[1])
                    if live in s and backup in s:
                        verified = True
    return copied, verified


def undo_structure(text, names):
    """Structural checks on the parsed undo (bugtrack 2026-10-02 9.2)."""
    import undo_sim
    out = []
    lines = text.replace(chr(92) + "n", "\n").split("\n")
    for ln in lines:
        if len(ln) > 255:
            out.append("undo line longer than 255 chars: %s..." % ln[:40])
        if ln.strip().upper().startswith("ECHO "):
            words = set(w.upper() for w in ln.split()[1:])
            if words & ECHO_KEYWORDS:
                out.append("undo Echo text contains an Echo keyword "
                           "(%s): %s" % (",".join(words & ECHO_KEYWORDS), ln))
    try:
        tree = undo_sim.parse(lines)
    except ValueError as exc:
        return ["undo does not parse: %s" % exc]
    if not tree or tree[0][0] != "failat" or tree[0][1] != 21:
        out.append("undo does not start with 'FailAt 21' (a missing file "
                   "must not abort it)")
    if len(tree) < 2 or tree[1][0] != "if" or \
            tree[1][1] != (False, "exists", STACKS) or \
            not any(n[0] == "execute" and n[1] == STACKS
                    for n in tree[1][2]):
        out.append("undo does not run S:tolunnet-undo-stacks (the "
                   "wizard's restore) right after FailAt")

    def norm(p):
        return p.lower()

    def walk(block, conds):
        prev = None
        for item in block:
            verb = item[0]
            if verb == "if":
                neg, what, path = item[1]
                walk(item[2], conds + [(neg, what, path, "then", prev)])
                walk(item[3], conds + [(neg, what, path, "else", prev)])
            elif verb == "delete":
                path, all_ = item[1]
                p = norm(path)
                if all_ and not (p.endswith("/new") or p.endswith("/ours")):
                    out.append("undo Delete ALL outside the marker dirs: %s"
                               % path)
                if p.startswith(norm(BK + "/C/")) or \
                        p.startswith(norm(BK + "/Libs/")):
                    c = conds[-1] if conds else None
                    ok = c and c[1] == "warn" and not c[0] and \
                        c[3] == "else" and c[4] and c[4][0] == "copy" and \
                        norm(c[4][1][0]) == p
                    if not ok:
                        out.append("undo deletes backup %s without a "
                                   "Copy + If WARN/Else check" % path)
                if p.startswith("sys:c/"):
                    n = path.split("/", 1)[1]
                    if not any(c[1] == "exists" and not c[0] and
                               c[3] == "then" and
                               norm(c[2]) == norm(BK + "/new/" + n)
                               for c in conds):
                        out.append("undo deletes SYS:C/%s outside If EXISTS "
                                   "of its new/ marker" % n)
            elif verb in ("copy", "rename"):
                src, dst = item[1]
                d = norm(dst)
                if d == "s:user-startup":
                    ok = verb == "rename" and any(
                        c[1] == "exists" and norm(c[2]) == d and
                        (c[0] != (c[3] == "else")) for c in conds)
                    if not ok:
                        out.append("undo writes over a live S:User-Startup "
                                   "(%s %s)" % (verb, src))
                if d == "libs:bsdsocket.library":
                    ok = any(c[1] == "exists" and norm(c[2]) == d and
                             (c[0] != (c[3] == "else")) for c in conds)
                    if not ok:
                        out.append("undo writes over a live "
                                   "LIBS:bsdsocket.library")
            prev = item

    walk(tree, [])
    flat = undo_sim.parse(lines)

    def all_items(block):
        for it in block:
            yield it
            if it[0] == "if":
                yield from all_items(it[2])
                yield from all_items(it[3])

    items = list(all_items(flat))
    for n in names:
        b = norm(BK + "/C/" + n)
        if not any(it[0] == "copy" and norm(it[1][0]) == b and
                   norm(it[1][1]) == norm("SYS:C/" + n) for it in items):
            out.append("undo does not restore SYS:C/%s" % n)
    ug = norm(BK + "/Libs/usergroup.library")
    if not any(it[0] == "copy" and norm(it[1][0]) == ug for it in items):
        out.append("undo does not restore usergroup.library")
    if not any(it[0] in ("rename", "delete") and
               norm(it[1][0]) == "s:user-startup.tolunnet-bak"
               for it in items):
        out.append("undo leaves S:User-Startup.tolunnet-bak behind (a "
                   "reinstall would reuse the stale backup)")
    return out


def backup_undo_coverage(top, names):
    findings = []
    for n in names:
        copied, verified = backup_verified(
            top, "SYS:C/" + n, BK + "/C/" + n)
        if not copied:
            findings.append("no backup block for SYS:C/%s" % n)
        elif not verified:
            findings.append("backup of SYS:C/%s is not verified (getsize "
                            "compare + abort)" % n)
    copied, verified = backup_verified(
        top, "SYS:Libs/usergroup.library", BK + "/Libs/usergroup.library")
    if not (copied and verified):
        findings.append("usergroup.library is replaced without a verified "
                        "backup")
    text = undo_text(top)
    if text is None:
        findings.append("no S:tolunnet-undo textfile")
    else:
        findings.extend(undo_structure(text, names))
    findings.extend(check_order_and_gate(top))
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
    findings.extend(check_if_arity(top))
    findings.extend(check_name_lengths(top))
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


BSN_ = chr(92) + "n"
NL_ = chr(10)


def _move_undo_after_copies(t):
    a = t.index('(textfile\n  (dest "S:tolunnet-undo")')
    b = t.index("\n)\n", t.index(";END GENERATED-UNDO", a)) + 3
    blk = t[a:b]
    t = t[:a] + t[b:]
    i = t.index(";BEGIN GENERATED-FINISH")
    return t[:i] + blk + t[i:]


# bugtrack 2026-10-02 9.2: each mutation re-creates one of the 1.x
# defects in the working-tree script; the lint must catch every one.
MUTATIONS = (
    ("undo written after the copies (1.5)", _move_undo_after_copies),
    ("backup Delete without If WARN (1.4)",
     lambda t: t.replace('"  If WARN' + BSN_ + '"',
                         '"  If EXISTS SYS:C' + BSN_ + '"')),
    ("SYS:C delete without its new/ marker (1.7)",
     lambda t: t.replace('"  If EXISTS SYS:Storage/tolunnet-backup/new/',
                         '"  If EXISTS SYS:Storage/tolunnet-backup/C/')),
    ("wizard undo-stacks not run (1.1)",
     lambda t: t.replace('"  Execute S:tolunnet-undo-stacks' + BSN_ + '"',
                         '"  Echo skipped' + BSN_ + '"')),
    ("User-Startup copied back over the live file (1.3)",
     lambda t: t.replace(
         '"    Rename >NIL: S:User-Startup.tolunnet-bak '
         'S:User-Startup.tolunnet-old' + BSN_ + '"',
         '"    Copy >NIL: S:User-Startup.tolunnet-bak S:User-Startup CLONE'
         + BSN_ + '"')),
    ("FailAt 21 missing",
     lambda t: t.replace('"FailAt 21' + BSN_ + '"', '"FailAt 10' + BSN_ + '"')),
    ("library deleted without a compare (1.2)",
     lambda t: t.replace('(set #tn-lib 0)' + NL_,
                         '(set #tn-lib 0)' + NL_ +
                         '(if (exists "LIBS:bsdsocket.library" (noreq))'
                         + NL_ + '  (delete "LIBS:bsdsocket.library")'
                         + NL_ + ')' + NL_)),
    ("file operation hidden in (run)",
     lambda t: t.replace('(delete "LIBS:bsdsocket.library")',
                         '(run "Delete >NIL: LIBS:bsdsocket.library")')),
    ("usergroup.library not backed up (1.11)",
     lambda t: t.replace(
         '(copyfiles (source "SYS:Libs/usergroup.library")',
         '(message (source "SYS:Libs/usergroup.library")')),
    ("backups behind an S:tolunnet-undo gate (1.7)",
     lambda t: t.replace(
         "; SYS:C/tolunnet\n",
         '(if (not (exists "S:tolunnet-undo" (noreq)))\n'
         '  (copyfiles (source "SYS:C/tolunnet") '
         '(dest "SYS:Storage/tolunnet-backup/C") (newname "tolunnet"))\n'
         ')\n')),
    ("ours marker name over the FFS 30-character limit",
     lambda t: t.replace('"SYS:Storage/tolunnet-backup/ours/ConfigureNetInterface/"',
                         '"SYS:Storage/tolunnet-backup/ours/ConfigureNetInterface."')),
    ("second library park name over 30 characters (1.2)",
     lambda t: t.replace(".pre-tn-newer", ".pre-tolunnet-2")),
    ("(if) with three statements",
     lambda t: t.replace('(set #tn-lib 0)',
                         '(if 1 (set #tn-lib 0) (set #tn-lib 0) '
                         '(set #tn-lib 0))')),
)


def selftest(script_path, doc_path):
    """Older revisions and every mutation must fail; the working-tree
    script must pass."""
    ok = True
    with open(doc_path, encoding="utf-8") as fh:
        doc_text = fh.read()
    with open(script_path, encoding="utf-8") as fh:
        current = fh.read()

    def run(label, text, expect_pass):
        nonlocal ok
        try:
            findings = lint_text(text, doc_text)
        except LintError as exc:
            findings = [str(exc)]
        passed = not findings
        good = passed == expect_pass
        ok = ok and good
        print("installer_lint selftest %s: %s (expected %s)%s" % (
            label, "PASS" if passed else "FAIL",
            "PASS" if expect_pass else "FAIL", "" if good else " MISMATCH"))
        for f in findings[:2]:
            print("   - %s" % f)

    for rev in ("fe3c61b", "565fe26", "1e88155", "e2d54e8", "c83af21",
                "fe33177"):
        try:
            text = git_show(rev, script_path)
        except LintError as exc:
            print("installer_lint selftest %s: %s" % (rev, exc))
            ok = False
            continue
        run(rev, text, False)
    for label, mutate in MUTATIONS:
        text = mutate(current)
        if text == current:
            print("installer_lint selftest mutation '%s': did not apply "
                  "MISMATCH" % label)
            ok = False
            continue
        run("mutation '%s'" % label, text, False)
    run("working tree", current, True)
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
            return 0 if (r1 and r2) else 1
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
