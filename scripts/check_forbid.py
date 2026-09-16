#!/usr/bin/env python3
"""
check_forbid — static gate for forbidden calls inside Forbid()/Disable()
regions (ANX-03).

Rules enforced:
- Inside a Forbid()..Permit() region none of the banned blocking/allocating
  calls may appear: Wait, WaitPort, WaitIO, DoIO, Delay, ObtainSemaphore,
  AllocVec, AllocMem, OpenLibrary, OpenDevice, Open(, Lock(, Execute, PutStr,
  Printf, tn_logf.
- Inside a Disable()..Enable() region ANY call except Permit/Enable/Forbid/
  Disable is banned (interrupts are off; nothing may touch shared state).
- Line-based scanner: keeps a nesting stack per function; the stack resets
  at the next function boundary (a '}' at column 0). Comments and strings
  are not parsed away — keep such regions clean of banned words.

Known exceptions: scripts/check-forbid-known.txt, lines of
  path:function:reason
A finding whose file+function matches an exception line is reported as
excused (not a gate failure) but still listed.

With --md the scanner prints a Markdown inventory of every region for
docs/FORBID.md (file:lines | function | calls seen | excuse).
"""

import os
import re
import sys

BANNED_IN_FORBID = [
    "Wait", "WaitPort", "WaitIO", "DoIO", "Delay", "ObtainSemaphore",
    "AllocVec", "AllocMem", "OpenLibrary", "OpenDevice", "Open", "Lock",
    "Execute", "PutStr", "Printf", "tn_logf",
]

ALLOWED_IN_DISABLE = {"Permit", "Enable", "Forbid", "Disable"}

CALL_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
FUNC_HEAD_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_ \*]*)\(")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_known():
    known = {}
    path = os.path.join(ROOT, "scripts", "check-forbid-known.txt")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split(":", 2)
                if len(parts) == 3:
                    key = (parts[0].replace("\\", "/"), parts[1])
                    known[key] = parts[2]
    return known


def scan_file(path, known):
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    findings = []
    regions = []
    stack = []  # entries: [type, start_line, calls[list]]
    func = "?"
    in_block_comment = False  # strip /* */ and // so prose cannot open regions

    with open(path, encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")

            # comment stripping (line-based, adequate for this scanner)
            if in_block_comment:
                end = line.find("*/")
                if end < 0:
                    line = ""
                else:
                    line = line[end + 2:]
                    in_block_comment = False
            if "/*" in line:
                start = line.find("/*")
                if "*/" in line[start:]:
                    line = line[:start] + line[line.find("*/", start) + 2:]
                else:
                    line = line[:start]
                    in_block_comment = True
            if "//" in line:
                line = line[:line.find("//")]

            if raw.startswith("}"):  # function boundary (col 0)
                for ent in stack:
                    regions.append((rel, ent[0], ent[1], lineno - 1, func, ent[2]))
                stack = []
                func = "?"
                continue

            m = FUNC_HEAD_RE.match(line)
            if m and "(" in line and "{" not in line.split("(")[0]:
                name = m.group(1).strip().split()[-1]
                if name and not name.startswith(("if", "for", "while", "switch", "return")):
                    func = name

            opens_forbid = re.search(r"\bForbid\s*\(", line)
            opens_disable = re.search(r"\bDisable\s*\(", line)
            if opens_forbid:
                stack.append(["forbid", lineno, []])
                continue
            if opens_disable:
                stack.append(["disable", lineno, []])
                continue

            if re.search(r"\bPermit\s*\(", line) or re.search(r"\bEnable\s*\(", line):
                if stack:
                    ent = stack.pop()
                    regions.append((rel, ent[0], ent[1], lineno, func, ent[2]))
                continue

            if stack:
                for cm in CALL_RE.finditer(line):
                    call = cm.group(1)
                    if stack[-1][0] == "forbid":
                        if call in BANNED_IN_FORBID:
                            stack[-1][2].append((lineno, call))
                    else:  # disable region: everything but the allowlist
                        if call not in ALLOWED_IN_DISABLE:
                            stack[-1][2].append((lineno, call))

    return findings, regions


def regions_with_findings(regions, known):
    out = []
    for rel, rtype, start, end, func, calls in regions:
        excused = (rel, func) in known
        out.append((rel, rtype, start, end, func, calls, excused))
    return out


def main():
    known = load_known()
    md = "--md" in sys.argv

    all_regions = []
    src_root = os.path.join(ROOT, "src")
    for dirpath, _dirs, files in os.walk(src_root):
        for fn in sorted(files):
            if fn.endswith(".c"):
                _f, regs = scan_file(os.path.join(dirpath, fn), known)
                all_regions.extend(regs)

    all_regions = regions_with_findings(all_regions, known)

    if md:
        print("| dosya:satır | tip | işlev | bölgedeki çağrılar | istisna |")
        print("|---|---|---|---|---|")
        for rel, rtype, start, end, func, calls, excused in all_regions:
            calls_s = ", ".join(sorted({c for _l, c in calls})) or "—"
            print("| `%s:%d-%d` | %s | `%s` | %s | %s |" % (
                rel, start, end, rtype, func, calls_s,
                "EVET" if excused else "—"))
        return 0

    hard = 0
    for rel, rtype, start, end, func, calls, excused in all_regions:
        for lineno, call in calls:
            if excused:
                print("EXCUSED %s:%d %s in %s() [%s]" % (rel, lineno, call, func, rtype))
            else:
                print("FINDING %s:%d %s() calls %s inside %s()" % (
                    rel, lineno, func, call, rtype))
                hard += 1

    print("check-forbid: %d region(s), %d unexcused finding(s)" %
          (len(all_regions), hard))
    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
