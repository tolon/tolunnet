#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""build_adf.py — pack the stripped release tree into the ADF floppy
image, by priority, honestly (11z item 2).

An 880 KB floppy cannot hold the 1.10 MB release tree, so:
  - must-haves: C/*, Libs/usergroup.library, TolunnetSetup, LICENSE —
    if these do not fit, the build FAILS;
  - then the priority list until the packer says full: Disk.info,
    Install_Tolunnet + .info, TolunnetPrefs + .info (a pair skips
    together, no orphan icon), README.guide, tolunnet.readme,
    THIRD_PARTY_LICENSES.md;
  - everything copied from the STRIPPED release tree (make
    release-stage) — no second strip pass;
  - the packer is the only honest accountant: the ADF file system
    rounds every file up to whole 512-byte blocks, adds header and
    extension blocks and ~57 metadata blocks, so each candidate is
    judged by an actual trial pack (FFS - OFS stores only 488 bytes
    per block and cannot hold even the must-haves);
  - without xdftool the image build is skipped with a WARNING (CI);
    with xdftool present a pack error of the must-haves fails.

Prints `ADF: used N of 901120 bytes, free M, files: ...` and, when
built, the xdftool listing.
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE = os.path.join(ROOT, "build", "release", "tolunnet")
STAGING = os.path.join(ROOT, "build", "adf-pkg", "tolunnet")
ADF = os.path.join(ROOT, "build", "tolunnet.adf")
TRIAL = os.path.join(ROOT, "build", "adf-trial.adf")
CAPACITY = 901120                      # 880 KB DD floppy, bytes
XDFTOOL = os.environ.get("XDFTOOL",
                         os.path.expanduser("~/.local/bin/xdftool"))

# (destination in the floppy root, source). Must-haves are packed
# first and unconditionally; this list continues by priority.
PRIORITY = [
    ("Disk.info", os.path.join(ROOT, "Disk.info")),
    ("Install_Tolunnet", os.path.join(RELEASE, "Install_Tolunnet")),
    ("Install_Tolunnet.info", os.path.join(RELEASE, "Install_Tolunnet.info")),
    ("TolunnetPrefs", os.path.join(RELEASE, "TolunnetPrefs")),
    ("TolunnetPrefs.info", os.path.join(RELEASE, "TolunnetPrefs.info")),
    ("README.guide", os.path.join(ROOT, "README.guide")),
    ("tolunnet.readme", os.path.join(ROOT, "tolunnet.readme")),
    ("THIRD_PARTY_LICENSES.md", os.path.join(ROOT, "THIRD_PARTY_LICENSES.md")),
]


def fail(msg):
    print("ADF FAIL: " + msg)
    sys.exit(1)


def main():
    if not os.path.isdir(os.path.join(RELEASE, "C")):
        fail("release tree missing - run `make release-stage` first")

    if not (os.access(XDFTOOL, os.X_OK) or shutil.which(XDFTOOL)
            or shutil.which(os.path.basename(XDFTOOL))):
        print("WARNING: xdftool not found, ADF skipped")
        return 0

    if os.path.exists(STAGING):
        shutil.rmtree(STAGING)
    os.makedirs(os.path.join(STAGING, "C"))
    os.makedirs(os.path.join(STAGING, "Libs"))

    used = 0            # file bytes, for the printed line
    files = []
    blocks = -1

    def place(dst, src):
        nonlocal used
        shutil.copy2(src, os.path.join(STAGING, dst))
        used += os.path.getsize(src)
        files.append("%s (%d B)" % (dst, os.path.getsize(src)))

    def unplace(dst, src):
        nonlocal used
        os.remove(os.path.join(STAGING, dst))
        used -= os.path.getsize(src)
        files[:] = [f for f in files if not f.startswith(dst + " ")]

    def trial_pack():
        """Pack to a trial image; report (ok, used_blocks)."""
        if os.path.exists(TRIAL):
            os.remove(TRIAL)
        res = subprocess.run([XDFTOOL, "-f", TRIAL, "pack", STAGING, "ffs"],
                             capture_output=True, text=True)
        if res.returncode != 0:
            return False, -1
        info = subprocess.run([XDFTOOL, "-f", TRIAL, "info"],
                              capture_output=True, text=True)
        for line in info.stdout.splitlines():
            if line.startswith("used:"):
                return True, int(line.split()[1])
        return True, -1

    # must-haves: if the packer cannot take them, the build fails
    for name in sorted(os.listdir(os.path.join(RELEASE, "C"))):
        place("C/" + name, os.path.join(RELEASE, "C", name))
    place("Libs/usergroup.library",
          os.path.join(RELEASE, "Libs", "usergroup.library"))
    place("TolunnetSetup", os.path.join(RELEASE, "TolunnetSetup"))
    place("LICENSE", os.path.join(ROOT, "LICENSE"))
    ok, blocks = trial_pack()
    if not ok:
        fail("must-haves (C/, Libs/, TolunnetSetup, LICENSE) do not fit "
             "on an 880 KB FFS floppy")

    # priority list until the packer says full; an icon whose binary
    # did not fit skips with it (no orphan icon on the floppy).
    # Disk.info is the volume icon itself - no binary pair.
    for dst, src in PRIORITY:
        if dst.endswith(".info") and dst != "Disk.info":
            binary = dst[:-len(".info")]
            if not any(f == binary or f.startswith(binary + " ")
                       for f in files):
                print("ADF: %s SKIPPED (the %s binary did not fit)"
                      % (dst, binary))
                continue
        place(dst, src)
        ok, b = trial_pack()
        if not ok:
            unplace(dst, src)
            print("ADF: %s SKIPPED (no room)" % dst)
        else:
            blocks = b

    print("ADF: used %d of %d bytes, free %d, files: %s"
          % (used, CAPACITY, CAPACITY - used, ", ".join(files)))
    print("ADF: %s of 1760 filesystem blocks allocated (FFS)"
          % ("measured " + str(blocks) if blocks >= 0 else "n/a"))

    # authoritative final pack of the surviving staging tree
    if os.path.exists(TRIAL):
        os.remove(TRIAL)
    res = subprocess.run([XDFTOOL, "-f", TRIAL, "pack", STAGING, "ffs"])
    if res.returncode != 0:
        fail("final pack failed after the priority loop")

    if os.path.exists(ADF):
        os.remove(ADF)
    shutil.copy2(TRIAL, ADF)
    print("ADF successfully created: %s" % ADF)

    lst = subprocess.run([XDFTOOL, "-f", ADF, "list"], capture_output=True,
                         text=True)
    print(lst.stdout, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
