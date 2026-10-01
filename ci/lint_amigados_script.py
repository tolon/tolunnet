#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""lint_amigados_script.py — structural lint for the shipped
AmigaDOS scripts (11ab item 1).

Rules for each script given on the command line:
  - the FIRST THREE lines are the dot commands (.key/.bra/.ket);
    comments come after them;
  - every `Copy` line carries TWO non-keyword arguments (FROM and TO)
    before any ALL/CLONE option — the one-argument form
    `Copy tolunnet1:{DEST} ALL CLONE` shipped once and copied nothing;
  - every `If` has a matching `EndIf`;
  - LF line endings only, Latin-1 clean, lines at most 255 chars.

Exit 1 on any finding. Run by `make python-checks`.
`--selftest` lints the real script plus a built-in bad sample and
requires the bad one to fail.
"""

import io
import sys

KEYWORDS = {"ALL", "CLONE", "QUIET", "BUF=", "COM", "NOREQ"}
DOT_COMMANDS = (".key", ".bra", ".ket", ".def")


def strip_comment(line):
    """A `;` starts a comment unless inside quotes."""
    out = []
    inq = False
    for ch in line:
        if ch == '"':
            inq = not inq
        if ch == ";" and not inq:
            break
        out.append(ch)
    return "".join(out).rstrip()


def lint_script(text, name):
    """Return a list of findings."""
    bad = []
    if "\r" in text:
        bad.append("%s: CR found - LF only" % name)
    try:
        text.encode("latin-1")
    except UnicodeEncodeError:
        bad.append("%s: not Latin-1 clean" % name)
    lines = text.split("\n")
    for i, ln in enumerate(lines, 1):
        if len(ln) > 255:
            bad.append("%s:%d: line longer than 255 chars" % (name, i))
    stripped = [l for l in lines]
    for i in range(min(3, len(stripped))):
        s = stripped[i].strip()
        if not s.startswith(DOT_COMMANDS):
            bad.append("%s:%d: line %d of the first three is not a dot "
                       "command: %r" % (name, i + 1, i + 1, s))
    depth = 0
    for i, raw in enumerate(lines, 1):
        s = strip_comment(raw).strip()
        if not s:
            continue
        first = s.split()[0]
        if first.upper() == "IF":
            depth += 1
        elif first.upper() == "ENDIF":
            depth -= 1
            if depth < 0:
                bad.append("%s:%d: EndIf without If" % (name, i))
                depth = 0
        elif first.upper() == "COPY":
            toks = s.split()[1:]
            args = []
            for t in toks:
                u = t.upper()
                if u in KEYWORDS or u.startswith("BUF=") or u == "COM":
                    break
                args.append(t)
            if len(args) != 2:
                bad.append("%s:%d: Copy needs FROM and TO (got %d "
                           "argument(s)): %r" % (name, i, len(args), s))
    if depth != 0:
        bad.append("%s: %d If without EndIf" % (name, depth))
    return bad


BAD_SAMPLE = (
    ".key DEST/A,NORUN/S\n"
    ".bra {\n"
    ".ket }\n"
    "Copy tolunnet1:{DEST} ALL CLONE\n"
    "If WARN\n"
    "Quit 10\n"
    "EndIf\n"
)


def selftest():
    good = io.open("Install_From_Floppies", encoding="utf-8").read()
    ok_bad = lint_script(BAD_SAMPLE, "old-one-arg-form")
    print("lint_amigados_script: shipped script findings: %d"
          % len(lint_script(good, "Install_From_Floppies")))
    if lint_script(good, "Install_From_Floppies"):
        print("lint_amigados_script: FAIL - shipped script is not clean")
        return 1
    if not ok_bad:
        print("lint_amigados_script: FAIL - the old one-argument Copy "
              "must fail the lint")
        return 1
    print("lint_amigados_script: selftest OK (old one-arg form fails, "
          "shipped script passes)")
    return 0


def main(argv):
    if argv[1:2] == ["--selftest"]:
        return selftest()
    if len(argv) < 2:
        print(__doc__)
        return 2
    allbad = []
    for path in argv[1:]:
        with io.open(path, encoding="utf-8", errors="replace") as f:
            allbad += lint_script(f.read(), path)
    for b in allbad:
        print("lint_amigados_script: " + b)
    if allbad:
        print("lint_amigados_script: FAIL: %d finding(s)" % len(allbad))
        return 1
    print("lint_amigados_script: OK (%d script(s))" % (len(argv) - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
