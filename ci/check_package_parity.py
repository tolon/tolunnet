#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""check_package_parity.py — the archive must equal the release tree
(z.ai step 11y item 3).

scripts/create_lha.py writes Level-0 LhA with the -lh0- (stored)
method only, so a minimal reader is enough: walk the member headers,
read each member's bytes, and require md5 parity with
build/release/tolunnet/. Also asserts:
  - the archive member `tolunnet/Install_Tolunnet` is byte-identical
    to the repo's `Install_Tolunnet.script` (the file the bench stages
    and installer_lint lints), and
  - the member list is exactly the expected package list.

Usage:
  check_package_parity.py <archive.lha> <release_dir>
Prints one `name md5 OK/DIFF` line per member. Exit 0 only when every
check passes.
"""

import hashlib
import os
import struct
import sys

# The shipped C/ contents (36 names, incl. the Commodore Installer and
# the ifconfig/netstat/wget/curl/ping name copies and C/tolunnet.info).
EXPECTED_C = [
    "AddNetInterface", "AddNetRoute", "CheckNetConfig",
    "ConfigureNetInterface", "DeleteNetRoute", "GetNetStatus",
    "Installer", "NetShutdown", "Offline", "Online", "ShowNetStatus",
    "TestSocket", "TolunnetControl", "TolunnetGet", "TolunnetPing",
    "TolunnetSetup", "TolunnetStatus", "arp", "curl", "ftp", "hostname",
    "ifconfig", "iperf", "nc", "netstat", "nslookup", "ping", "route",
    "sntp", "telnet", "tftp", "tolunnet", "tolunnet.info", "traceroute",
    "wget", "whois",
]

EXPECTED_ROOT = [
    "Install_From_Floppies",
    "Install_Tolunnet", "Install_Tolunnet.info", "Installer",
    "Libs/usergroup.library", "LICENSE", "README.guide",
    "README.guide.info", "THIRD_PARTY_LICENSES.md", "TolunnetPrefs",
    "TolunnetPrefs.info", "TolunnetSetup", "TolunnetSetup.info",
    "tolunnet.info",  # drawer icon, archive top level (11z item 1)
    "tolunnet.readme",
]


def read_lha_members(data):
    """Yield (name, bytes) for every -lh0- member; refuse anything the
    minimal reader cannot handle honestly."""
    pos = 0
    members = []
    while pos < len(data):
        if data[pos] == 0:
            break  # end of archive
        hdr_size = data[pos]
        hdr = data[pos + 2:pos + 2 + hdr_size]
        if len(hdr) < 22:
            raise SystemExit("FAIL: truncated LHA header at offset %d" % pos)
        method = hdr[0:5]
        if method != b"-lh0-":
            raise SystemExit(
                "FAIL: member method %r is not -lh0-; the minimal reader "
                "only handles stored members (create_lha.py writes -lh0-)"
                % method)
        comp_size, orig_size = struct.unpack_from("<II", hdr, 5)
        # hdr layout: method(5) comp(4) orig(4) time(4) attr(1) level(1)
        # fn_len(1) filename crc(2)  ->  fn_len at 19, name at 20
        fn_len = hdr[19]
        name = hdr[20:20 + fn_len].decode("latin1")
        if comp_size != orig_size:
            raise SystemExit("FAIL: member %s claims compression" % name)
        start = pos + 2 + hdr_size
        members.append((name, data[start:start + comp_size]))
        pos = start + comp_size
    return members


def md5(b):
    return hashlib.md5(b).hexdigest()


def script_dir():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(argv):
    # usage: check_package_parity.py <archive.lha> <release_dir>
    #        [--adfs <disk1.adf> <disk2.adf>]   (11aa item 2)
    rest = argv[1:]
    adfs = []
    if "--adfs" in rest:
        i = rest.index("--adfs")
        adfs = rest[i + 1:i + 3]
        if len(adfs) != 2:
            print(__doc__)
            return 2
        rest = rest[:i] + rest[i + 3:]
    if len(rest) != 2:
        print(__doc__)
        return 2
    archive, release_dir = rest[0], rest[1]
    data = open(archive, "rb").read()
    members = read_lha_members(data)

    print("[check_package_parity] archive: %s (%d bytes, %d members)"
          % (archive, len(data), len(members)))

    bad = 0
    seen = []
    print("[check_package_parity] %-42s %-32s %s" % ("member", "md5", "parity"))
    for name, blob in members:
        rel = name
        for prefix in ("tolunnet/",):
            if rel.startswith(prefix):
                rel = rel[len(prefix):]
        seen.append(rel)
        # 11z item 1: the drawer icon is stored at the archive top
        # level from assets/, not from the release tree.
        if rel == "tolunnet.info":
            icon_md5 = md5(open(os.path.join(script_dir(),
                                             "assets", "tolunnet_drawer.info"),
                                 "rb").read())
            ok = icon_md5 == md5(blob)
            print("[check_package_parity] %-42s %-32s %s"
                  % (rel, icon_md5, "OK" if ok else "DIFF"))
            if not ok:
                bad += 1
            continue
        disk_path = os.path.join(release_dir, *rel.split("/"))
        if not os.path.isfile(disk_path):
            print("[check_package_parity] %-42s %-32s %s"
                  % (rel, md5(blob), "DIFF (missing on disk)"))
            bad += 1
            continue
        disk_md5 = md5(open(disk_path, "rb").read())
        ok = disk_md5 == md5(blob)
        print("[check_package_parity] %-42s %-32s %s"
              % (rel, disk_md5, "OK" if ok else "DIFF"))
        if not ok:
            bad += 1

    # installer parity: the archived installer IS the linted script
    script_path = os.path.join(script_dir(), "Install_Tolunnet.script")
    script_md5 = md5(open(script_path, "rb").read())
    inst = dict((n, b) for n, b in members).get("tolunnet/Install_Tolunnet")
    if inst is None:
        print("[check_package_parity] FAIL: no tolunnet/Install_Tolunnet member")
        bad += 1
    elif md5(inst) != script_md5:
        print("[check_package_parity] FAIL: archived Install_Tolunnet md5 %s "
              "!= Install_Tolunnet.script md5 %s" % (md5(inst), script_md5))
        bad += 1
    else:
        print("[check_package_parity] Install_Tolunnet == Install_Tolunnet.script "
              "(md5 %s) OK" % script_md5)

    expected = sorted(["C/" + n for n in EXPECTED_C] + EXPECTED_ROOT)
    if sorted(seen) != expected:
        missing = sorted(set(expected) - set(seen))
        extra = sorted(set(seen) - set(expected))
        print("[check_package_parity] FAIL: member list mismatch; "
              "missing=%s extra=%s" % (missing, extra))
        bad += 1
    else:
        print("[check_package_parity] member list == expected (%d members) OK"
              % len(expected))

    if bad:
        print("[check_package_parity] FAIL: %d problem(s)" % bad)
        return 1
    print("[check_package_parity] OK: archive parity verified")

    if adfs:
        adf_bad = check_adf_set(adfs, release_dir)
        bad += adf_bad

    if bad:
        print("[check_package_parity] FAIL: %d problem(s) total" % bad)
        return 1
    return 0


def check_adf_set(adfs, release_dir):
    """11aa item 2: unpack both Gotek disks and require that their
    union reproduces the package tree byte for byte (same member
    list, same md5, no path on both disks). Disk.info is the volume
    icon of disk 1, compared against the repo root file."""
    import shutil
    import subprocess
    import tempfile

    xdf = os.environ.get(
        "XDFTOOL", os.path.expanduser("~/.local/bin/xdftool"))
    # 9.15: execute the path that was found (a PATH install is found by
    # name only), and never skip under CI
    found = None
    for cand in (xdf, os.path.basename(xdf)):
        if os.path.isfile(cand) and os.access(cand, os.X_OK):
            found = cand
            break
        found = shutil.which(cand)
        if found:
            break
    if not found:
        if os.environ.get("CI", "").lower() in ("1", "true", "yes"):
            print("[check_package_parity] FAIL: xdftool not found and "
                  "CI is set - ADF set parity cannot be skipped")
            return 1
        print("[check_package_parity] WARNING: xdftool not found - "
              "ADF set parity skipped")
        return 0
    xdf = found

    # the union must equal the 52 package members (+ Disk.info chrome)
    want = {}
    for dirpath, dirnames, filenames in os.walk(release_dir):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, release_dir).replace("\\", "/")
            want[rel] = md5(open(full, "rb").read())
    disk_info = os.path.join(script_dir(), "Disk.info")
    disk_info_md5 = md5(open(disk_info, "rb").read())

    union = {}
    dup = []
    bad = 0
    for adf in adfs:
        if not os.path.isfile(adf):
            print("[check_package_parity] FAIL: ADF missing: %s" % adf)
            return 1
        tmp = tempfile.mkdtemp(prefix="adfcheck-")
        res = subprocess.run([xdf, "-f", adf, "unpack", tmp],
                             capture_output=True, text=True)
        if res.returncode != 0:
            print("[check_package_parity] FAIL: cannot unpack %s" % adf)
            shutil.rmtree(tmp, ignore_errors=True)
            return 1
        # xdftool unpack nests everything under <image-basename>/
        # and drops <image-basename>.xdfmeta/.blkdev sidecars - strip
        # the sidecars, then descend into the single content dir
        for entry in list(os.listdir(tmp)):
            if entry.endswith(".xdfmeta") or entry.endswith(".blkdev"):
                os.remove(os.path.join(tmp, entry))
        entries = os.listdir(tmp)
        if len(entries) == 1 and os.path.isdir(os.path.join(tmp,
                                                            entries[0])):
            tmp = os.path.join(tmp, entries[0])
        for dirpath, dirnames, filenames in os.walk(tmp):
            for fn in filenames:
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, tmp).replace("\\", "/")
                digest = md5(open(full, "rb").read())
                if rel in union:
                    print("[check_package_parity] FAIL: %s is on BOTH "
                          "disks" % rel)
                    dup.append(rel)
                union[rel] = digest
        shutil.rmtree(tmp, ignore_errors=True)

    for rel in sorted(union):
        if rel == "Disk.info":
            ok = union[rel] == disk_info_md5
            print("[check_package_parity] %-42s %-32s %s (volume icon)"
                  % (rel, union[rel], "OK" if ok else "DIFF"))
            if not ok:
                bad += 1
            continue
        if rel not in want:
            print("[check_package_parity] %-42s %-32s %s"
                  % (rel, union[rel], "DIFF (not a package member)"))
            bad += 1
            continue
        ok = union[rel] == want[rel]
        print("[check_package_parity] %-42s %-32s %s (on disks)"
              % (rel, union[rel], "OK" if ok else "DIFF"))
        if not ok:
            bad += 1
    for rel in sorted(want):
        if rel not in union:
            print("[check_package_parity] %-42s %-32s %s"
                  % (rel, want[rel], "DIFF (missing from the disk set)"))
            bad += 1
    if dup:
        bad += len(dup)
    if bad:
        print("[check_package_parity] FAIL: ADF set parity: %d problem(s)"
              % bad)
    else:
        print("[check_package_parity] OK: ADF set union == package tree "
              "(%d members + Disk.info), no duplicates" % len(want))
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))
