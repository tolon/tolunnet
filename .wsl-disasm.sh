#!/bin/bash
export PATH=/home/tolon/opt/m68k-amigaos/bin:$PATH
cd /mnt/d/Projeler/tolunnet || exit 1
m68k-amigaos-objdump -d build/tolunnet > /tmp/daemon.dis 2>/dev/null
python3 - <<'PY'
import re
lines = open('/tmp/daemon.dis', errors='replace').read().split('\n')
# build (offset, insn-text) list and function map
funcs = []   # (start_off, name)
insns = []   # (offset, text)
cur = None
for l in lines:
    m = re.match(r'^([0-9a-f]{8}) ([0-9a-f]{8}) (\S+):$', l)
    if m:
        cur = int(m.group(2), 16)
        funcs.append((cur, m.group(3)))
        continue
    m2 = re.match(r'^\s+([0-9a-f]+):\t([0-9a-f ]+)\t(.*)$', l)
    if m2:
        insns.append((int(m2.group(1), 16), m2.group(3).strip()))
# find the live loop: moveq #0,d1 ; move.w (a0)+,d1 ; add.l d1,d2 ; dbf d0,-8
for i in range(len(insns) - 3):
    t1, t2, t3, t4 = insns[i][1], insns[i+1][1], insns[i+2][1], insns[i+3][1]
    if ('moveq #' in t1 and ',d1' in t1 and 'move.w (a0)+,d1' == t2
            and 'add.l d1,d2' == t3 and t4.startswith('dbf')):
        print("LOOP at file offset 0x%x: %s" % (insns[i][0], t1))
        base = 0x00C6352A - insns[i][0]
        print("BASE = 0x%06X" % base)
        rets = [0x00C634E8, 0x00C6360A, 0x00C6A06C, 0x00C42FB8, 0x00C6946A,
                0x00C55178, 0x00C54D54, 0x00C51B7E, 0x00C5E212, 0x00C4E206, 0x00C01B28]
        import bisect
        starts = [f[0] for f in funcs]
        for r in rets:
            off = r - base
            idx = bisect.bisect_right(starts, off) - 1
            if 0 <= idx < len(funcs):
                print("ret 0x%06X -> %s+0x%x" % (r, funcs[idx][1], off - funcs[idx][0]))
            else:
                print("ret 0x%06X -> outside (%x)" % (r, off))
        break
PY
