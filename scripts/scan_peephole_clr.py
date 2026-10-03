#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""scan_peephole_clr.py — amiga-gcc "combine clr" peephole2 miscompile guard.

amiga-gcc 6.5.0b (m68k.md, the peephole2 commented "combine clr if
possible ... .w #0,x(a0), #0,x+2(a0) -> .l #0,x(a0)") checks only the FIRST
store's mode. A word clear at x followed by a LONG clear at x+2 is merged
into one clr.l at x: the long's low half is never zeroed. The Makefile
builds with -fno-peephole2 because of it.

  --check-makefile   (make python-checks) fail unless the Makefile CFLAGS
                     still carry -fno-peephole2. No cross compiler needed.
  -- <cflags...>     (make toolchain-scan) compile every source with the
                     given flags and RTL dumps; for each firing of the
                     peephole, look up the two deleted stores per function
                     and report the pair when their modes differ. Pass the
                     flags WITHOUT -fno-peephole2 to list the sites the
                     flag protects (expected today: tn_boot_block_apply,
                     lwIP tcp_write).
"""

import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CC = os.environ.get("CC", "m68k-amigaos-gcc")
SOURCES = ["src/**/*.c", "tests/amiga/*.c",
           "vendor/lwip/src/core/**/*.c", "vendor/lwip/src/netif/*.c"]
INSN_RE = re.compile(r"^\((?:insn|jump_insn|call_insn)(?:/\w+)? (\d+) ")


def check_makefile():
    text = open(os.path.join(ROOT, "Makefile")).read()
    m = re.search(r"^CFLAGS\s*=(.*?)(?<!\\)$", text, re.M | re.S)
    if m is None or "-fno-peephole2" not in m.group(1):
        print("FAIL: Makefile CFLAGS lost -fno-peephole2 (amiga-gcc combine-clr "
              "peephole2 miscompiles word+long zero stores; see "
              "scripts/scan_peephole_clr.py)")
        return 1
    print("[scan_peephole_clr] Makefile CFLAGS carry -fno-peephole2 OK")
    return 0


def by_function(lines):
    out, name = {}, None
    for line in lines:
        if line.startswith(";; Function "):
            name = line.split()[2]
            out.setdefault(name, [])
        elif name:
            out[name].append(line)
    return out


def insn_map(lines):
    m, cur, buf = {}, None, []
    for line in lines:
        mm = INSN_RE.match(line)
        if mm or line.startswith(("(note", "(barrier", "(code_label")):
            if cur:
                m[cur] = "\n".join(buf)
            cur, buf = (mm.group(1), [line]) if mm else (None, [])
        elif cur:
            buf.append(line)
    if cur:
        m[cur] = "\n".join(buf)
    return m


def scan(flags):
    flags = [f for f in flags if f not in ("-MMD", "-MP")]
    srcs = sorted({f for p in SOURCES
                   for f in glob.glob(os.path.join(ROOT, p), recursive=True)})
    fired = bad = 0
    failed = []
    for src in srcs:
        rel = os.path.relpath(src, ROOT)
        with tempfile.TemporaryDirectory() as td:
            r = subprocess.run([CC] + flags + ["-fdump-rtl-all", "-dumpdir", td + "/",
                                "-c", rel, "-o", td + "/x.o"], cwd=ROOT,
                               capture_output=True, text=True)
            if r.returncode != 0:
                failed.append(rel)
                continue
            dumps = sorted(glob.glob(td + "/*r.*"),
                           key=lambda p: int(re.search(r"\.(\d+)r\.", p).group(1)))
            p2 = [d for d in dumps if d.endswith("peephole22")]
            if not p2:
                continue
            idx = dumps.index(p2[0])
            pre = by_function(open(dumps[idx - 1], errors="replace").read().splitlines())
            post = by_function(open(p2[0], errors="replace").read().splitlines())
            for func, plines in post.items():
                umap = insn_map(pre.get(func, []))
                for i, line in enumerate(plines):
                    # gen_peephole2_N numbering shifts with the md include
                    # order: match any peephole2 that deleted zero stores
                    if not line.startswith("Splitting with gen_peephole2_"):
                        continue
                    dels = [m.group(1) for m in
                            (re.match(r"deleting insn with uid = (\d+)\.", n)
                             for n in plines[i + 1:i + 8]) if m]
                    info = []
                    for u in sorted(dels, key=int):
                        t = umap.get(u, "")
                        mode = re.search(r"\(set \(mem[/\w]*:(\w+) ", t)
                        # a store of #0: "(set (mem:M ...) (const_int 0 [0]))"
                        if mode is None or "(const_int 0 [0]))" not in t:
                            continue
                        off = re.search(r"\(const_int (-?\d+) ", t)
                        sl = re.findall(r"(\S+\.[ch]):(\d+)", t)
                        info.append("%s@%s (%s)" % (mode.group(1),
                                    off.group(1) if off else "?",
                                    "%s:%s" % sl[-1] if sl else "?"))
                    if len(info) >= 2:
                        fired += 1
                    if len({x.split("@")[0] for x in info}) > 1:
                        bad += 1
                        print("MISCOMPILE %s %s: %s" % (rel, func, " + ".join(info)))
    print("[scan_peephole_clr] sources=%d failed=%d zero-store merges=%d "
          "mixed-mode=%d" % (len(srcs), len(failed), fired, bad))
    for f in failed:
        print("  not compiled: " + f)
    return 0


def main(argv):
    if argv[1:2] == ["--check-makefile"]:
        return check_makefile()
    if "--" in argv:
        return scan(argv[argv.index("--") + 1:])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
