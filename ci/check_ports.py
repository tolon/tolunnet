#!/usr/bin/env python3
"""check_ports.py — drift guard between ci/netsvc.ports (runtime truth)
and tests/amiga/netsvc_ports.h (compile-time copy used by the suite).

The two files must list the same ports under the same names; if one
changes without the other the bench connections silently go to the
wrong port. Exits 1 and prints the drift when they differ.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORTS_FILE = os.path.join(HERE, "netsvc.ports")
HEADER_FILE = os.path.join(HERE, "..", "tests", "amiga", "netsvc_ports.h")


def read_ports_file(path):
    ports = {}
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"^([A-Z_]+)=\s*(\d+)\s*$", line.strip())
            if m:
                ports[m.group(1)] = int(m.group(2))
    return ports


def read_header_file(path):
    ports = {}
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            m = re.match(r"^#define\s+NETSVC_([A-Z_]+)\s+(\d+)", line.strip())
            if m:
                ports[m.group(1)] = int(m.group(2))
    return ports


def main():
    rt = read_ports_file(PORTS_FILE)
    hdr = read_header_file(HEADER_FILE)
    problems = []
    for name in sorted(set(rt) | set(hdr)):
        a = rt.get(name)
        b = hdr.get(name)
        if a is None:
            problems.append(f"{name}: in netsvc_ports.h ({b}) but not in netsvc.ports")
        elif b is None:
            problems.append(f"{name}: in netsvc.ports ({a}) but not in netsvc_ports.h")
        elif a != b:
            problems.append(f"{name}: netsvc.ports={a} != netsvc_ports.h={b}")
    if problems:
        print("check_ports: DRIFT between ci/netsvc.ports and tests/amiga/netsvc_ports.h:")
        for pr in problems:
            print("  -", pr)
        return 1
    print(f"check_ports: {len(rt)} ports in sync")
    return 0


if __name__ == "__main__":
    sys.exit(main())
