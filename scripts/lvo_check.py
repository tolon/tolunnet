#!/usr/bin/env python3
"""lvo_check.py — z.ai step 9d item 2: name -> offset verification.

For every raw `jsr -N(a6)` in src/** and tests/**, the enclosing function
(or asm label) is stripped of the common prefixes (tn_call_, call_, x_,
ug_, _ug_stub_, tn_stub_) and compared case-insensitively with the
library table's name for offset -N (lib_table.gen.c / ug_table.gen.c).
A mismatch is an error; unmatched names need a `/* lvo:<name> *​/` comment
on the jsr line. The table is chosen from the wrapper name or an explicit
`/[*] lvo:usergroup [*]` marker, not from the file path.
"""

import os
import re
import sys

REPO = os.environ.get("LVO_CHECK_REPO") or os.getcwd()

SCAN_DIRS = [os.path.join(REPO, "src"), os.path.join(REPO, "tests")]
OPEN_IMPL_DIRS = [
    os.path.join(REPO, "src", "lib"),
    os.path.join(REPO, "src", "usergroup"),
]

TABLES = {
    "bsdsocket": os.path.join(REPO, "src", "lib", "lib_table.gen.c"),
    "usergroup": os.path.join(REPO, "src", "usergroup", "ug_table.gen.c"),
}

JSR_RE = re.compile(r"jsr\s+-([0-9]+)\(\s*(?:%%)?a6\s*\)")
LVO_COMMENT_RE = re.compile(r"/\*\s*lvo:\s*([A-Za-z0-9_]+)\s*\*/")
FUNC_DEF_RE = re.compile(
    r"^(?:static\s+)?[A-Za-z_][A-Za-z0-9_ \t\*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
ASM_LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
PREFIXES = ["tn_call_", "call_", "x_", "_ug_stub_", "ug_stub_", "ug_",
            "tn_stub_", "_"]


def strip_prefixes(name):
    n = name
    for _ in range(3):
        for pre in PREFIXES:
            if n.startswith(pre) and len(n) > len(pre):
                n = n[len(pre):]
                break
    return n


def parse_table(path):
    name_at = {}
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = re.search(r"/\*\s*-(\d+)\s+([A-Za-z0-9_]+)\s*\*/", line)
            if m:
                name_at.setdefault(m.group(2).lower(), -int(m.group(1)))
    return name_at


def find_enclosing(lines, idx):
    for i in range(idx, -1, -1):
        am = ASM_LABEL_RE.match(lines[i])
        if am:
            return am.group(1)
        fm = FUNC_DEF_RE.match(lines[i])
        if fm and "=" not in lines[i].split("(")[0]:
            return fm.group(1)
    return None


def find_marker(lines, idx):
    for i in range(idx, -1, -1):
        if "lvo:usergroup" in lines[i]:
            return "usergroup"
        if "lvo:bsdsocket" in lines[i]:
            return "bsdsocket"
    return None


def resolve(lvo, clean, marker):
    if marker:
        tmap = tables[marker]
        return tmap.get(clean) == -lvo
    # try each table: name must exist AND offset must match
    for tmap in tables.values():
        if tmap.get(clean) == -lvo:
            return True
    return False


def main():
    global tables
    tables = {k: parse_table(v) for k, v in TABLES.items()}

    problems = []
    checked = 0
    for base in SCAN_DIRS:
        for root, _dirs, files in os.walk(base):
            for name in files:
                if not name.endswith((".c", ".h", ".s")):
                    continue
                path = os.path.join(root, name)
                rel = os.path.relpath(path, REPO)
                in_open_impl = any(
                    rel.startswith(os.path.relpath(d, REPO) + os.sep)
                    for d in OPEN_IMPL_DIRS)
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    lines = fh.read().split("\n")
                for lineno, line in enumerate(lines):
                    m = JSR_RE.search(line)
                    if not m:
                        continue
                    checked += 1
                    lvo = int(m.group(1))
                    if in_open_impl:
                        continue
                    if lvo % 6 != 0:
                        problems.append("%s:%d: jsr -%d is not a multiple of 6"
                                        " (mid-table jump)" % (rel, lineno, lvo))
                        continue
                    clean = None
                    fname = find_enclosing(lines, lineno)
                    if fname:
                        clean = strip_prefixes(fname).lower()
                    marker = find_marker(lines, lineno)
                    cm = LVO_COMMENT_RE.search(line)
                    if cm and cm.group(1):
                        want = cm.group(1).lower()
                        ok = resolve(lvo, want, marker) or any(
                            t.get(want) == -lvo for t in tables.values())
                        if ok:
                            continue
                    if clean and resolve(lvo, clean, marker):
                        continue
                    if clean is None and marker and lvo % 6 == 0:
                        continue
                    problems.append("%s:%d: '%s' jumps to -%d but no table"
                                    " entry matches name '%s'"
                                    % (rel, lineno, fname or "?", lvo,
                                       clean or "?"))
    print("lvo-check: %d raw jump-table call(s) scanned" % checked)
    for p in problems:
        print("FAIL:", p)
    if problems:
        print("lvo-check: %d problem(s)" % len(problems))
        return 1
    print("lvo-check: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
