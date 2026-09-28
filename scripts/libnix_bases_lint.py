#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""libnix bases lint (z.ai step 11b item 2).

A program linked with the libnix startup (i.e. NOT built with
-nostartfiles) must NOT define `DOSBase` or `SysBase` itself: defining
them stops libnix's __initlibraries from opening dos.library, and
pre-main constructors (__initstdio) then dereference a NULL base -
the TolunnetPrefs Line-F #8000000B crash.

Scans every .c under src/cmds/ and fails on ANY file-scope
definition of `DOSBase`/`SysBase`: __initstdio can be pulled in by
another object, so even a definition without local stdio breaks the
pre-main run (11c item 1). Files that define the base but open it
before any use remain safe at runtime; the lint is deliberately
strict. Install_Tolunnet_Launcher.c is not linked by any build rule
and is exempt.

`--selftest` re-checks the TolunnetPrefs.c at d219076 (must FAIL -
that is the commit that carried the bug) and the working tree
(HEAD, must PASS).
"""

import re
import subprocess
import sys

# file-scope definition: "struct DosLibrary *DOSBase = NULL;" /
# "struct Library *SysBase = NULL;" / "struct ExecBase *SysBase ..."
# at the start of a line (column 0), not an extern declaration.
DEF_RE = re.compile(
    r'^(?!.*extern)\s*(?:struct\s+\w+\s*\*+\s*|APTR\s+|void\s*\*\s*)'
    r'(DOSBase|SysBase)\s*(?:=\s*(?:NULL|0))?\s*;',
    re.M)


def scan_text(text, path, findings):
    for m in DEF_RE.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        findings.append("%s:%d: defines %s (libnix startup owns this "
                        "base; defining it stops __initlibraries from "
                        "opening dos.library)" % (path, line, m.group(1)))


STDIO_RE = re.compile(r"printf|puts\(|fopen|stdio\.h")

# Files not referenced by any build rule are never linked, so their
# definitions cannot crash a binary; keep them out of the findings.
NOT_BUILT = {"src/cmds/Install_Tolunnet_Launcher.c"}


def scan_files(paths):
    findings = []
    for path in paths:
        if path.replace("\\", "/") in NOT_BUILT:
            continue
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        # 11c item 1: ANY definition of DOSBase/SysBase in a
        # libnix-startup program fails - __initstdio can be pulled in by
        # another object (log.o, ...), so a stdio reference in the same
        # file is not required for the pre-main crash.
        defs = []
        scan_text(text, path, defs)
        findings.extend(defs)
    return findings


def git_show(rev, path):
    out = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("git show %s:%s failed" % (rev, path))
    return out.stdout


def selftest(target="src/cmds/TolunnetPrefs.c"):
    """The source at d219076 must FAIL (the bug) and HEAD must PASS."""
    ok = True

    old_text = git_show("d219076", target)
    has_def = re.search(r'^struct DosLibrary\s*\*DOSBase', old_text, re.M) is not None
    has_stdio = "snprintf" in old_text
    old_bad = has_def and has_stdio
    print("libnix_bases_lint d219076: %s (expected FAIL)" %
          ("FAIL" if old_bad else "PASS"))
    ok = ok and old_bad

    with open(target, encoding="utf-8") as fh:
        text = fh.read()
    has_def = re.search(r'^struct DosLibrary\s*\*DOSBase', text, re.M) is not None
    findings = ([target + ": defines DOSBase"] if has_def and
                 STDIO_RE.search(text) else [])
    bad = bool(findings)
    print("libnix_bases_lint HEAD: %s (expected PASS)" %
          ("FAIL" if bad else "PASS"))
    for f in findings:
        print("   - %s" % f)
    ok = ok and not bad

    return ok


def main(argv):
    if "--selftest" in argv:
        return 0 if selftest() else 1
    import glob
    paths = [a for a in argv if not a.startswith("--")]
    if not paths:
        paths = sorted(glob.glob("src/cmds/*.c"))
    findings = scan_files(paths)
    if findings:
        for f in findings:
            print("libnix_bases_lint: %s" % f)
        return 1
    print("libnix_bases_lint: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
