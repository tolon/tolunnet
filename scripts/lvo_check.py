#!/usr/bin/env python3
"""lvo_check.py — z.ai step 9c item 2.

Scans src/** and tests/** for raw bsdsocket/UserGroup jump-table calls
(`jsr -N(%%a6)` / `jsr -N(a6)` where a6 carries SocketBase or
UserGroupBase) and fails when the offset is not a multiple of 6 or is not
present in the library's own function table
(src/lib/lib_table.gen.c / src/usergroup/ug_table.gen.c).

A `jsr -6` outside the library open implementations (src/lib, src/usergroup)
is always a mistake: -6 is LIB_OPEN, reached when someone accidentally uses
the open-library offset instead of the function they meant (seen in
TolunnetPing.c: jsr -6 for "setsockopt").
"""

import os
import re
import sys

# The tree under check: cwd by default so a worktree can be scanned,
# overridable with LVO_CHECK_REPO.
REPO = os.environ.get("LVO_CHECK_REPO") or os.getcwd()

SCAN_DIRS = [os.path.join(REPO, "src"), os.path.join(REPO, "tests")]
# Library open implementations may legitimately jump to -6.
OPEN_IMPL_DIRS = [
    os.path.join(REPO, "src", "lib"),
    os.path.join(REPO, "src", "usergroup"),
]

JSR_RE = re.compile(r"jsr\s+-([0-9]+)\((?:%%|\s*)?a6\)")


def table_lvos(path):
    """Collect the negative offsets listed in a library jump-table source."""
    lvos = set()
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            for m in re.finditer(r"-(\d+)", line):
                lvos.add(int(m.group(1)))
    return lvos


def source_files():
    for base in SCAN_DIRS:
        for root, _dirs, files in os.walk(base):
            for name in files:
                if name.endswith((".c", ".h", ".s")):
                    yield os.path.join(root, name)


def main():
    socket_table = os.path.join(REPO, "src", "lib", "lib_table.gen.c")
    ug_table = os.path.join(REPO, "src", "usergroup", "ug_table.gen.c")
    allowed_socket = table_lvos(socket_table)
    allowed_ug = table_lvos(ug_table)

    problems = []
    checked = 0
    for path in source_files():
        rel = os.path.relpath(path, REPO)
        in_open_impl = any(rel.startswith(os.path.relpath(d, REPO) + os.sep)
                           for d in OPEN_IMPL_DIRS)
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for lineno, line in enumerate(fh, 1):
                m = JSR_RE.search(line)
                if not m:
                    continue
                checked += 1
                lvo = int(m.group(1))
                if lvo % 6 != 0:
                    problems.append(f"{rel}:{lineno}: jsr -{lvo} is not a "
                                    f"multiple of 6 (mid-table jump)")
                    continue
                # pick the table by directory: usergroup code jumps through
                # UserGroupBase, everything else through SocketBase
                allowed = (allowed_ug if "usergroup" in rel.replace(os.sep, "/")
                           else allowed_socket)
                if lvo not in allowed:
                    problems.append(f"{rel}:{lineno}: jsr -{lvo} is not in "
                                    f"the library function table")
                    continue
                if lvo == 6 and not in_open_impl:
                    problems.append(f"{rel}:{lineno}: jsr -6 is LIB_OPEN — a "
                                    f"wrong offset used from a command/test")
    print(f"lvo-check: {checked} raw jump-table call(s) scanned")
    for p in problems:
        print("FAIL:", p)
    if problems:
        print(f"lvo-check: {len(problems)} problem(s)")
        return 1
    print("lvo-check: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
