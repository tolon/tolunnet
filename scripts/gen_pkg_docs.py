#!/usr/bin/env python3
"""
scripts/gen_pkg_docs.py — Regenerate README.guide and tolunnet.readme from
include/version.h and LICENSE (Item 6 of TN-plan.md).
Ensures release documentation is never hand-edited and reflects true version
and GPL-3.0-or-later license terms.
"""

import os
import re
import sys
import subprocess
from datetime import datetime

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

def get_version():
    vh = os.path.join(ROOT_DIR, "include", "version.h")
    with open(vh, "r", encoding="utf-8") as f:
        content = f.read()
    m = re.search(r'#define\s+TOLUNNET_VERSION\s+"([^"]+)"', content)
    if not m:
        raise ValueError("Could not find TOLUNNET_VERSION in include/version.h")
    return m.group(1)

def get_release_date():
    # Use git commit date if in a git repo, otherwise current date
    try:
        ts = subprocess.check_output(
            ["git", "log", "-1", "--format=%cd", "--date=format:%d.%m.%Y"],
            cwd=ROOT_DIR,
            stderr=subprocess.DEVNULL
        ).decode().strip()
        if ts:
            return ts
    except Exception:
        pass
    return datetime.now().strftime("%d.%m.%Y")

def update_readme_guide(version, date_str):
    guide_path = os.path.join(ROOT_DIR, "README.guide")
    with open(guide_path, "r", encoding="latin1") as f:
        lines = f.readlines()

    out = []
    in_license_node = False
    license_replaced = False

    for line in lines:
        # Update @$VER: line
        if line.startswith("@$VER:"):
            out.append(f"@$VER: tolunnet.guide {version} ({date_str})\n")
            continue

        if line.startswith('@node License'):
            in_license_node = True
            out.append(line)
            continue

        if in_license_node:
            if line.startswith('@endnode'):
                in_license_node = False
                out.append(line)
                continue
            if not license_replaced:
                license_block = (
                    "@{b}7. License & Credits@{ub}\n\n"
                    "* @{b}tolunnet Core & Architecture:@{ub} Copyright (c) 2026 Tolon (Ismail Ozturk).\n"
                    "  Licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later).\n"
                    "  See the accompanying LICENSE file for the full license terms.\n\n"
                    "* @{b}lwIP TCP/IP Stack:@{ub} Copyright (c) 2001-2023 Swedish Institute of\n"
                    "  Computer Science. Licensed under the BSD License.\n\n"
                    "* @{b}SANA-II Specifications:@{ub} Commodore-Amiga, Inc. standard.\n\n"
                )
                out.append(license_block)
                license_replaced = True
            # Skip old license content until @endnode
            continue

        out.append(line)

    with open(guide_path, "w", encoding="latin1", newline="\n") as f:
        f.writelines(out)
    print(f"[gen_pkg_docs] README.guide updated for {version} ({date_str})")

def update_tolunnet_readme(version):
    readme_path = os.path.join(ROOT_DIR, "tolunnet.readme")
    with open(readme_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Update Version: line
    content = re.sub(r"^Version:\s+.*$", f"Version:      {version}", content, flags=re.MULTILINE)

    # Remove references to docs/ not shipped in package
    old_docs_ref = (
        "See README.guide for complete details; A500+PiStorm+WiFiPi owners see\n"
        "docs/PISTORM-INSTALL.md and the owner test procedure in docs/OWNER-RETEST.md."
    )
    new_docs_ref = "See README.guide for complete details."
    content = content.replace(old_docs_ref, new_docs_ref)

    # Ensure tool count reflects full command set
    content = re.sub(
        r"- 22 CLI tools:.*?- Native 3D",
        "- 33 CLI tools: ping, ifconfig, netstat, wget, curl, hostname, nslookup,\n"
        "  whois, traceroute, nc, sntp, telnet, tftp, ftp, arp, iperf, route,\n"
        "  ShowNetStatus, GetNetStatus, TolunnetControl, CheckNetConfig, NetShutdown,\n"
        "  AddNetRoute, DeleteNetRoute, AddNetInterface, ConfigureNetInterface,\n"
        "  Online, Offline, TolunnetPing, TolunnetGet, TolunnetStatus, TestSocket\n"
        "- usergroup.library v1.0 compatibility layer for multi-user tools\n"
        "- Native 3D",
        content,
        flags=re.DOTALL
    )

    with open(readme_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print(f"[gen_pkg_docs] tolunnet.readme updated for {version}")

def main():
    version = get_version()
    date_str = get_release_date()
    update_readme_guide(version, date_str)
    update_tolunnet_readme(version)

if __name__ == "__main__":
    main()
