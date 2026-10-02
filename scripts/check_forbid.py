#!/usr/bin/env python3
"""
check_forbid — static gate for forbidden calls inside Forbid()/Disable()
regions (ANX-03).

Rules enforced:
- Inside a Forbid()..Permit() region none of the banned blocking/allocating
  calls may appear: Wait, WaitPort, WaitIO, DoIO, Delay, ObtainSemaphore,
  AllocVec, AllocMem, OpenLibrary, OpenDevice, Open(, Lock(, Execute, PutStr,
  Printf, tn_logf, CreateMsgPort, Close(, Read(, Write(, FPrintf.
- Inside a Disable()..Enable() region ANY call except Permit/Enable/Forbid/
  Disable is banned (interrupts are off; nothing may touch shared state).
- Token scanner with brace depth (9.8): an early-exit Permit() nested
  deeper than its Forbid() (followed by return/break/goto) does not end
  the region; the rest of the Forbid() line is scanned. The stack resets
  at the next function boundary (a '}' at column 0). Comments and string
  literals are stripped first.

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
    # 9.8: port allocation and blocking DOS I/O
    "CreateMsgPort", "Close", "Read", "Write", "FPrintf",
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


STRING_RE = re.compile(r'"(?:\\.|[^"\\])*"' + r"|'(?:\\.|[^'\\])+'")
# 9.8: one token stream per line, in source order, so the remainder of a
# Forbid() line and the head of a Permit() line are scanned too.
TOKEN_RE = re.compile(r"[{}]|\b(?:return|break|continue|goto)\b|"
                      r"\b[A-Za-z_][A-Za-z0-9_]*\s*\(")
JUMP_WORDS = {"return", "break", "continue", "goto"}
REGION_WORDS = {"Forbid", "Disable", "Permit", "Enable"}


def scan_file(path, known):
    """Brace-depth region matcher (9.8). A region opens at Forbid()/
    Disable() and records the brace depth there. A Permit()/Enable() at
    that depth (or shallower) closes it. One at a DEEPER depth is an
    early-exit path ("if (!port) { Permit(); return; }"): when a
    return/break/continue/goto follows it in the same block it does
    NOT close the region; when the block ends without a jump (if/else
    branches that each Permit) the region closes at that block's '}'."""
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    findings = []
    regions = []
    stack = []  # entries: dict(type, start, calls, depth, pending, jumped)
    func = "?"
    depth = 0
    in_block_comment = False  # strip /* */ and // so prose cannot open regions

    def close(ent, lineno):
        regions.append((rel, ent["type"], ent["start"], lineno, func,
                        ent["calls"]))

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
            while "/*" in line:
                start = line.find("/*")
                if "*/" in line[start:]:
                    line = line[:start] + line[line.find("*/", start) + 2:]
                else:
                    line = line[:start]
                    in_block_comment = True
            line = STRING_RE.sub('""', line)
            if "//" in line:
                line = line[:line.find("//")]
            if line.lstrip().startswith("#"):
                continue  # preprocessor lines carry no calls or braces

            if raw.startswith("}"):  # function boundary (col 0)
                for ent in stack:
                    close(ent, lineno - 1)
                stack = []
                func = "?"
                depth = 0
                continue

            m = FUNC_HEAD_RE.match(line)
            if m and "(" in line and "{" not in line.split("(")[0]:
                name = m.group(1).strip().split()[-1]
                if name and not name.startswith(("if", "for", "while", "switch", "return")):
                    func = name

            for tm in TOKEN_RE.finditer(line):
                tok = tm.group(0)
                top = stack[-1] if stack else None
                if tok == "{":
                    depth += 1
                    continue
                if tok == "}":
                    depth -= 1
                    # the branch that Permit()ed ends here: with a jump the
                    # region resumes after it, without one it ends with it
                    while (top is not None and top["pending"] is not None
                           and depth < top["pending"]):
                        if top["jumped"]:
                            top["pending"] = None
                            break
                        close(stack.pop(), lineno)
                        top = stack[-1] if stack else None
                    continue
                suspended = (top is not None and top["pending"] is not None
                             and depth >= top["pending"])
                if tok in JUMP_WORDS:
                    if suspended:
                        top["jumped"] = True
                    continue
                call = re.match(r"[A-Za-z_][A-Za-z0-9_]*", tok).group(0)
                if suspended:
                    continue  # after an early-exit Permit(): not inside
                if call in ("Forbid", "Disable"):
                    stack.append({"type": call.lower(), "start": lineno,
                                  "calls": [], "depth": depth,
                                  "pending": None, "jumped": False})
                    continue
                if call in ("Permit", "Enable"):
                    if top is not None:
                        if depth <= top["depth"]:
                            close(stack.pop(), lineno)
                        else:
                            top["pending"] = depth
                            top["jumped"] = False
                    continue
                if top is not None and call not in REGION_WORDS:
                    if top["type"] == "forbid":
                        if call in BANNED_IN_FORBID:
                            top["calls"].append((lineno, call))
                    else:  # disable region: everything but the allowlist
                        if call not in ALLOWED_IN_DISABLE:
                            top["calls"].append((lineno, call))

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
