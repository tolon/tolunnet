#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""check_version_tags.py — every shipped binary carries its $VER tag
(11aa item 1).

Scans build/release/tolunnet and requires every file to contain the
string `$VER: <anything> <TOLUNNET_VERSION> (<date>)`. Non-binary
payloads (docs, icons, the Commodore Installer we cannot re-tag) are
allow-listed. Prints `name ver OK/MISSING`; any MISSING exits 1.

Usage: check_version_tags.py <release_dir> [<version>]
       (version defaults to the include/version.h value)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# payloads that cannot or need not carry our tag
ALLOW = {
    "C/Installer", "Installer",          # Commodore Installer 43.x binary
    "Install_Tolunnet",                  # Installer-43 script (text, not
                                         #   a binary - no $VER carrier)
    "Install_From_Floppies",             # AmigaDOS script (same reason)
    "C/tolunnet.info", "tolunnet.info",  # Amiga icons (binary by design)
    "Install_Tolunnet.info", "TolunnetPrefs.info", "TolunnetSetup.info",
    "README.guide.info", "Disk.info",
    "README.guide", "tolunnet.readme",   # docs
    "LICENSE", "THIRD_PARTY_LICENSES.md",
}


def version_from_header():
    txt = open(os.path.join(ROOT, "include", "version.h"),
               encoding="utf-8").read()
    m = re.search(r'#define TOLUNNET_VERSION "(.*?)"', txt)
    return m.group(1)


def main(argv):
    release = argv[1] if len(argv) > 1 else os.path.join(
        ROOT, "build", "release", "tolunnet")
    version = argv[2] if len(argv) > 2 else version_from_header()
    if not os.path.isdir(release):
        print("[check_version_tags] no release tree at %s - skipped"
              % release)
        return 0
    tag = re.compile(rb"\$VER: [ -~]+ " + re.escape(version.encode())
                     + rb" \(\d\d\.\d\d\.\d{4}\)")
    bad = 0
    print("[check_version_tags] expecting version %s" % version)
    for dirpath, dirnames, filenames in os.walk(release):
        for fn in sorted(filenames):
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, release).replace("\\", "/")
            if rel in ALLOW:
                print("[check_version_tags] %-32s %s (allow-listed)"
                      % (rel, "ver --"))
                continue
            blob = open(full, "rb").read()
            if tag.search(blob):
                print("[check_version_tags] %-32s ver OK" % rel)
            else:
                print("[check_version_tags] %-32s ver MISSING" % rel)
                bad += 1
    if bad:
        print("[check_version_tags] FAIL: %d file(s) without the %s tag"
              % (bad, version))
        return 1
    print("[check_version_tags] OK: every shipped file carries the %s tag"
          % version)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
