#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""build_adf.py — the two-disk Gotek ADF set + release assets dir
(11aa item 2).

The single 880 KB ADF could not hold the installer script, so the
release ships TWO FFS DD disks built from the stripped release tree:

  disk1  install bootstrap (Install_From_Floppies + icon), the tested
         installer script, the Commodore Installer binary (both
         copies), docs, the daemon and the smallest C/ commands
  disk2  the rest, incl. TolunnetSetup, TolunnetPrefs and
         Libs/usergroup.library

The assignment lives in scripts/adf_manifest.txt - an explicit list,
no size heuristic. The build FAILS when a package member is unassigned,
assigned twice, unknown to the manifest, or when a disk does not fit.
Every candidate disk is judged by an actual trial pack (the ADF file
system rounds files to 512-byte blocks and adds header/extension and
~56 metadata blocks, so no byte model predicts "fits"; FFS is
mandatory - OFS stores only 488 bytes per block).

Without xdftool the image build is skipped with a WARNING and `make
package` still produces the LHA (CI); with xdftool a pack error of the
manifest-fixed content fails the build.

Usage: build_adf.py [lha_path]
Writes build/tolunnet-<version>-disk1.adf / -disk2.adf and, when lha
path is given, assembles build/release-assets/ (lha + 2 disks +
SHA256SUMS.txt).
"""

import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASE = os.path.join(ROOT, "build", "release", "tolunnet")
PKG = STAGING_BASE = os.path.join(ROOT, "build", "adf-pkg")
TRIAL = os.path.join(ROOT, "build", "adf-trial.adf")
CAPACITY = 901120                      # 880 KB DD floppy, bytes
MANIFEST = os.path.join(ROOT, "scripts", "adf_manifest.txt")
XDFTOOL = os.environ.get("XDFTOOL",
                         os.path.expanduser("~/.local/bin/xdftool"))

# members that live in the repo, not in the release tree
REPO_FILES = {
    "Install_From_Floppies": os.path.join(ROOT, "Install_From_Floppies"),
    "Install_From_Floppies.info":
        os.path.join(ROOT, "Install_From_Floppies.info"),
    "Disk.info": os.path.join(ROOT, "Disk.info"),
}


def fail(msg):
    print("ADF FAIL: " + msg)
    sys.exit(1)


def read_manifest():
    """Return (assignment, disk_of) with validation."""
    assign = {}
    for line in open(MANIFEST, encoding="utf-8"):
        line = line.split("#")[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2 or parts[1] not in ("1", "2"):
            fail("bad manifest line: %r" % line)
        path, disk = parts[0], int(parts[1])
        if path in assign:
            fail("manifest assigns %s twice" % path)
        assign[path] = disk
    return assign


def package_members():
    """Every member of the shipped tree (52): the release tree plus
    Install_From_Floppies and its icon (11aa item 2)."""
    members = set()
    for dirpath, dirnames, filenames in os.walk(RELEASE):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            members.add(os.path.relpath(full, RELEASE).replace("\\", "/"))
    members.add("Install_From_Floppies")
    members.add("Install_From_Floppies.info")
    return members


def source_of(path):
    if path in REPO_FILES:
        return REPO_FILES[path]
    return os.path.join(RELEASE, *path.split("/"))


def have_xdftool():
    return (os.access(XDFTOOL, os.X_OK) or shutil.which(XDFTOOL)
            or shutil.which(os.path.basename(XDFTOOL))) is not None


def trial_pack(staging, image):
    """Pack staging (FFS) to image; return (ok, used_blocks)."""
    if os.path.exists(image):
        os.remove(image)
    res = subprocess.run([XDFTOOL, "-f", image, "pack", staging, "ffs"],
                         capture_output=True, text=True)
    if res.returncode != 0:
        return False, -1
    info = subprocess.run([XDFTOOL, "-f", image, "info"],
                          capture_output=True, text=True)
    for line in info.stdout.splitlines():
        if line.startswith("used:"):
            return True, int(line.split()[1])
    return True, -1


def main(argv):
    if not os.path.isdir(os.path.join(RELEASE, "C")):
        fail("release tree missing - run `make release-stage` first")
    if not os.path.isfile(os.path.join(ROOT, "Install_From_Floppies")):
        fail("Install_From_Floppies missing at the repo root")

    assign = read_manifest()
    members = package_members()

    unassigned = sorted(m for m in members if m not in assign)
    unknown = sorted(p for p in assign if p not in members
                     and p != "Disk.info")
    if unassigned:
        fail("manifest misses members: %s" % unassigned)
    if unknown:
        fail("manifest lists unknown paths: %s" % unknown)

    if not have_xdftool():
        print("WARNING: xdftool not found, ADF skipped")
        return 0

    if os.path.exists(PKG):
        shutil.rmtree(PKG)
    images = {}
    for disk in (1, 2):
        paths = sorted(p for p, d in assign.items() if d == disk)
        staging = os.path.join(PKG, "tolunnet%d" % disk)
        used_bytes = 0
        for path in paths:
            dst = os.path.join(staging, *path.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(source_of(path), dst)
            used_bytes += os.path.getsize(source_of(path))
        image = os.path.join(
            ROOT, "build", "tolunnet-%s-disk%d.adf" % (version(), disk))
        ok, blocks = trial_pack(staging, image)
        if not ok:
            fail("disk %d does not fit (manifest is fixed - move files "
                 "to the other disk in scripts/adf_manifest.txt)" % disk)
        print("disk%d: used %d of %d bytes, free %d, %d of 1760 filesystem "
              "blocks (FFS), files: %d"
              % (disk, used_bytes, CAPACITY, CAPACITY - used_bytes,
                 blocks, len(paths)))
        images[disk] = image

    # release assets: lha + both disks + SHA256SUMS.txt
    lha = argv[1] if len(argv) > 1 else None
    if lha and os.path.isfile(lha):
        assets = os.path.join(ROOT, "build", "release-assets")
        os.makedirs(assets, exist_ok=True)
        names = [os.path.basename(lha),
                 os.path.basename(images[1]), os.path.basename(images[2])]
        for src, name in zip([lha, images[1], images[2]], names):
            shutil.copy2(src, os.path.join(assets, name))
        lines = []
        for name in names:
            digest = hashlib.sha256(
                open(os.path.join(assets, name), "rb").read()).hexdigest()
            lines.append("%s  %s" % (digest, name))
        open(os.path.join(assets, "SHA256SUMS.txt"), "w",
             encoding="utf-8").write("\n".join(lines) + "\n")
        print("release assets: %s (%s)" % (assets, ", ".join(names)))
    return 0


def version():
    import re
    txt = open(os.path.join(ROOT, "include", "version.h"),
               encoding="utf-8").read()
    return re.search(r'#define TOLUNNET_VERSION "(.*?)"', txt).group(1)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
