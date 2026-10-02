#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""check_release_consistency.py — every release surface must name the
same version and every README link must resolve (11aa item 5).

Checks:
  - include/version.h == top CHANGELOG section == STATUS.md latest-rc
    line == README.guide $VER == tolunnet.readme == the LHA/ADF file
    names in build/;
  - TOLUNNET_VER_DATE in version.h == the CHANGELOG section date;
  - every docs/screenshots/*.png referenced in README.md exists, and
    every PNG in docs/screenshots/ is referenced;
  - every relative link in README.md and STATUS.md resolves on disk.

With --write-notes <path>: writes a release-notes file = the CHANGELOG
rc5 section + a "which file do I download" block + known limitations +
the installer redistribution note.
Run by `make python-checks`; exit 1 on any inconsistency.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAD = []


def fail(msg):
    BAD.append(msg)
    print("[check_release_consistency] FAIL: " + msg)


def read(path):
    return open(os.path.join(ROOT, path), encoding="utf-8",
                errors="replace").read()


def main(argv):
    vtxt = read("include/version.h")
    m = re.search(r'#define TOLUNNET_VERSION "(.*?)"', vtxt)
    version = m.group(1)
    m = re.search(r'#define TOLUNNET_VER_DATE "(.*?)"', vtxt)
    ver_date = m.group(1)
    print("[check_release_consistency] version.h: %s (%s)" % (version,
                                                              ver_date))

    if "--selftest-notes" in argv[1:]:
        return 0 if notes_selftest() else 1

    # CHANGELOG top section (ISO date; version.h stores dd.mm.yyyy)
    chlog = read("CHANGELOG.md")
    m = re.search(r"^## (1\.2\.0-\S+) \((\d{4}-\d\d-\d\d)\)", chlog,
                  re.M)
    iso = "%s-%s-%s" % (ver_date[6:10], ver_date[3:5], ver_date[0:2])
    if not m or m.group(1) != version:
        fail("CHANGELOG top section is %r, version.h says %r"
             % (m and m.group(0), version))
    elif m.group(2) != iso:
        fail("CHANGELOG date %s != version.h TOLUNNET_VER_DATE %s"
             % (m.group(2), iso))
    else:
        print("[check_release_consistency] CHANGELOG: %s (%s) OK"
              % (m.group(1), m.group(2)))

    # STATUS.md latest-rc line
    status = read("STATUS.md")
    if "**%s**" % version not in status:
        fail("STATUS.md does not name %s" % version)
    else:
        print("[check_release_consistency] STATUS.md: %s OK" % version)

    # generated docs $VER / version string
    guide = read("README.guide")
    if "@$VER: tolunnet.guide %s" % version not in guide:
        fail("README.guide $VER does not carry %s" % version)
    else:
        print("[check_release_consistency] README.guide $VER OK")
    readme = read("tolunnet.readme")
    if version not in readme:
        fail("tolunnet.readme does not name %s" % version)
    # 11af item 1: the real-Installer row must stay skipped in the
    # bench - the real Installer is GUI-bound headlessly and a run
    # hangs the leg (STOP-REPORT 11ac).
    suite = read("tests/amiga/SocketConformance.c")
    guarded = "#if TN_REAL_INSTALLER" in suite
    if "TN_RUN(tc_installer_pretend)" in suite and not guarded:
        fail("tc_installer_pretend is run unguarded - the real "
             "Installer hangs the headless bench")
    if re.search(r"#\s*define\s+TN_REAL_INSTALLER\s+1", suite):
        fail("TN_REAL_INSTALLER is defined to 1 - the GUI-bound "
             "Installer row would run in the bench")
    if not BAD:
        print("[check_release_consistency] installer row skip guard OK")

    # 11ag item 2: no invented third-party ping target may come back
    # in the commands (net_test.c is the documented exemption).
    cmds_dir = os.path.join(ROOT, "src", "cmds")
    hits = []
    for fn in sorted(os.listdir(cmds_dir)):
        if not fn.endswith(".c"):
            continue
        rel = "src/cmds/" + fn
        if rel == "src/cmds/net_checks.c" or rel == "src/cmds/net_test.c":
            continue
        body = open(os.path.join(cmds_dir, fn), encoding="utf-8",
                    errors="replace").read()
        if "1.1.1.1" in body:
            hits.append(rel)
    if hits:
        fail("hard-coded 1.1.1.1 back in: %s" % hits)
    else:
        print("[check_release_consistency] no 1.1.1.1 literal in src/cmds OK")

    # 11ab item 2c: the installer welcome names the same version
    script = read("Install_Tolunnet.script")
    if "Welcome to the tolunnet %s Installation" % version not in script:
        fail("Install_Tolunnet.script welcome does not carry %s "
             "(run 'make installer')" % version)
    else:
        print("[check_release_consistency] installer welcome OK")

    # archive + ADF file names carry the version. A tree without ANY
    # release artefacts (fresh clone, no make package yet) skips this
    # part - parity with nothing is not an inconsistency.
    build_dir = os.path.join(ROOT, "build")
    artefacts = [f for f in os.listdir(build_dir)
                 if f.startswith("tolunnet-")
                 and (f.endswith(".lha") or f.endswith(".adf"))] \
        if os.path.isdir(build_dir) else []
    if not artefacts:
        print("[check_release_consistency] no release artefacts in "
              "build/ - artefact name check skipped")
    else:
        for pat in ("build/tolunnet-%s.lha" % version,
                    "build/tolunnet-%s-disk1.adf" % version,
                    "build/tolunnet-%s-disk2.adf" % version):
            if not os.path.isfile(os.path.join(ROOT, *pat.split("/"))):
                fail("missing release artefact %s" % pat)
            else:
                print("[check_release_consistency] artefact %s OK"
                      % os.path.basename(pat))

    # screenshot references: README <-> disk, both directions
    rdm = read("README.md")
    refs = set(re.findall(r"docs/screenshots/([\w.\-]+\.png)", rdm))
    disk = set(f for f in os.listdir(os.path.join(ROOT,
                                                  "docs/screenshots"))
               if f.endswith(".png"))
    if refs - disk:
        fail("README references missing screenshots: %s" % sorted(refs - disk))
    if disk - refs:
        fail("unreferenced screenshots on disk: %s" % sorted(disk - refs))
    if not (refs - disk) and not (disk - refs):
        print("[check_release_consistency] screenshots: %d referenced, "
              "%d on disk, OK" % (len(refs), len(disk)))

    # relative links in README.md / STATUS.md resolve
    for doc in ("README.md", "STATUS.md"):
        text = read(doc)
        base = os.path.dirname(os.path.join(ROOT, doc))
        for target in re.findall(r"\]\(([^)#]+?)(?:#[^)]*)?\)", text):
            if "://" in target or target.startswith("/"):
                continue
            p = os.path.normpath(os.path.join(base, target))
            if not os.path.exists(p):
                fail("%s: broken relative link %s" % (doc, target))
    if not BAD:
        print("[check_release_consistency] relative links OK")

    # 11ai item 1: --write-notes rewrites the file FIRST; the
    # checksum checks below then read the ON-DISK notes, so the
    # notes can never lag behind build/release-assets/ again.
    if len(argv) > 1 and argv[1] == "--write-notes":
        write_notes(version)
    check_asset_checksums(version)

    if BAD:
        print("[check_release_consistency] FAIL: %d problem(s)"
              % len(BAD))
        return 1
    print("[check_release_consistency] OK: release surfaces consistent")
    return 0


def release_asset_digests(sums_path):
    """digest -> asset name, from a SHA256SUMS-format file."""
    digests = {}
    for line in open(sums_path, encoding="utf-8"):
        parts = line.split(maxsplit=1)
        if len(parts) == 2:
            digests[parts[0]] = parts[1].strip()
    return digests


def notes_digest_findings(body, digests):
    """11ai item 3: pure helper. Returns (missing, stale) - the
    SHA256SUMS digests absent from the notes body, and 64-hex digest
    lines in the body naming a tolunnet archive that SHA256SUMS does
    not list (stale lines from an older package)."""
    import re
    missing = [d for d in digests if d not in body]
    stale = []
    for m in re.finditer(r"^([0-9a-f]{64})\s+(tolunnet-\S+\.(?:lha|adf))\s*$",
                         body, re.M):
        if digests.get(m.group(1)) != m.group(2):
            stale.append("%s (%s)" % (m.group(2), m.group(1)[:12]))
    return missing, stale


def check_asset_checksums(version):
    """11ai item 1: the ON-DISK release notes (and STATUS) must quote
    the checksums of the files actually in build/release-assets/."""
    sums = os.path.join(ROOT, "build", "release-assets", "SHA256SUMS.txt")
    if not os.path.isfile(sums):
        print("[check_release_consistency] no build/release-assets - "
              "checksum check skipped")
        return
    digests = release_asset_digests(sums)
    status_body = read("STATUS.md")
    status_current = all(digest in status_body
                         for digest in digests)
    if not status_current:
        # 11aj item 2: a fresh-clone rebuild produces new digests
        # (LhA header timestamps); STATUS.md legitimately quotes the
        # last OFFICIAL build. Warn, do not fail - the hard gate is
        # the on-disk notes below (the GitHub release body).
        print("[check_release_consistency] WARNING: STATUS.md quotes "
              "a different build (expected after a rebuild); refresh "
              "it before tagging")
    notes_path = notes_file_for(version)
    if not os.path.isfile(notes_path):
        print("[check_release_consistency] no RELEASE-NOTES file - "
              "notes checksum check skipped")
        return
    body = open(notes_path, encoding="utf-8", errors="replace").read()
    missing, stale = notes_digest_findings(body, digests)
    for d in missing:
        fail("RELEASE-NOTES is missing the current %s checksum %s"
             % (digests[d], d[:12]))
    for st in stale:
        fail("RELEASE-NOTES carries a stale checksum line: %s" % st)
    if not any("checksum" in b for b in BAD):
        print("[check_release_consistency] RELEASE-NOTES checksums "
              "match build/release-assets OK"
              + ("; STATUS refreshed separately" if not status_current
                 else "; STATUS current"))


def notes_selftest():
    """11ai item 3: a wrong digest must FAIL, the right one must PASS."""
    import tempfile
    da = "a" * 64
    db = "b" * 64
    d = tempfile.mkdtemp(prefix="crc-notes-selftest-")
    sums = os.path.join(d, "SHA256SUMS.txt")
    open(sums, "w", encoding="utf-8").write(
        "%s  tolunnet-1.2.0-rc5.lha\n"
        "%s  tolunnet-1.2.0-rc5-disk1.adf\n" % (da, db))
    digests = release_asset_digests(sums)

    bad_body = ("%s  tolunnet-1.2.0-rc5.lha\n"
                "%s  tolunnet-1.2.0-rc5-disk1.adf\n"
                % ("c" * 64, db))
    missing, stale = notes_digest_findings(bad_body, digests)
    bad_ok = bool(missing or stale)

    good_body = ("%s  tolunnet-1.2.0-rc5.lha\n"
                 "%s  tolunnet-1.2.0-rc5-disk1.adf\n" % (da, db))
    missing, stale = notes_digest_findings(good_body, digests)
    good_ok = (not missing and not stale)

    print("check_release_consistency notes selftest: wrong digest FAIL=%s"
          % bad_ok)
    print("check_release_consistency notes selftest: right digest PASS=%s"
          % good_ok)
    return bad_ok and good_ok


def notes_file_for(version):
    return os.path.join(ROOT, "build", "RELEASE-NOTES-%s.md"
                        % version.split("-")[-1])


def write_notes(version):
    chlog = read("CHANGELOG.md")
    sec = chlog.index("## %s" % version)
    nxt = chlog.find("\n## 1.", sec + 1)
    section = chlog[sec:nxt if nxt > 0 else len(chlog)].rstrip()
    status = read("STATUS.md")
    lim = status[status.index("## Known limitations"):]
    notes = "\n\n".join([
        section,
        "## Which file do I download?\n"
        "- Gotek / FlashFloppy / HxC: the TWO `.adf` images — copy both\n"
        "  to the USB stick and run `Install_From_Floppies` from disk 1.\n"
        "- PiStorm / CF / hard disk: the `.lha` archive.\n"
        "- `SHA256SUMS.txt` lists the checksums of all three files.\n"
        "- Requirements: AmigaOS 3.0+, 68000+, ~1.2 MB free for the\n"
        "  unpacked tree.",
        lim.strip(),
        "## Installer redistribution note\n"
        "The archive bundles the Commodore `Installer` binary (1999).\n"
        "Its redistribution terms are NOT yet verified — the owner has\n"
        "to confirm the licence before publishing this release.\n",
    ])
    # 11af item 3: quote the release-asset checksums in the notes
    sums = os.path.join(ROOT, "build", "release-assets",
                        "SHA256SUMS.txt")
    if os.path.isfile(sums):
        notes += "\n## Release asset checksums\n```\n" + \
            open(sums, encoding="utf-8").read() + "```\n"
    out = notes_file_for(version)
    open(out, "w", encoding="utf-8").write(notes + "\n")
    print("[check_release_consistency] wrote %s" % out)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
