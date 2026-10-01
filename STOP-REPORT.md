# STOP-REPORT — 11ad item 1 (floppy row root cause): budget exhausted, causes found and fixed, one confirming bench left

## What the diagnostics proved (bench 20261002-002719 a1200 leg)
1. **`Work:` is NOT full** - hypothesis A dead: the volume is a host
   mount with ~8.5 GB free before AND after the run.
2. **Both volume copies succeed in isolation**: `Copy tolunnet1:
   Work:tninst2 ALL CLONE` rc=0 and `Copy tolunnet2: ...` rc=0.
3. **The "missing" file copies fine in isolation**:
   `Copy tolunnet2:TolunnetSetup.info <dest> CLONE` rc=0, dest size 630 -
   identical to its working twin. Hypotheses B (protection) dead.
4. **The walked listing CONTAINS `TolunnetSetup.info 630`** - the file
   IS in the copied tree. It simply sits FIRST in the FFS hash order of
   the root directory.
5. The script's own run ends rc=10; its captured output (1827 bytes)
   shows the disk-1 Copy transcript and then the Installer-style
   "..copied." transcript - the end-of-script behavior needs the tail
   printed (the row now prints head+tail; tapf truncates long blocks).

## The two real defects - BOTH harness-side, both fixed
1. **First-line probe bug** (commit c6154a6): the manifest compare
   probes `"\n<line>\n"` - the walked listing's FIRST line has no
   leading newline, so whichever file the FFS hash order puts first
   (here `TolunnetSetup.info`) was ALWAYS reported missing. Fixed by
   newline-prefixing both buffers before the compare.
2. **`Disk.info` missing from the expected list** (commit 08be407):
   the script copies the WHOLE volume, so `Disk.info` legitimately
   lands in the destination, but the expected list excluded it - a
   guaranteed `gotn != mirrored` mismatch. Fixed: Disk.info is now in
   the expected list.

## Why the stop
The counted-bench budget (3) was consumed by the diagnostic chain plus
two harness/ops accidents (a leftover WinUAE, a still-running wrapper's
cleanup deleting the shared config, a datestamp dirty-trap, and - my
own rule violation - editing `ci/bench.sh` while a bench was running,
which killed the last run's 68000 leg with a bogus EOF). The fixes are
committed but NOT yet proven by a bench: the next session's FIRST bench
should show `tc_floppy_install ok` with an empty missing list, plan
`1..112`, `core: 110 ok / 0 not ok` (the pretend row remains a SKIP
candidate per STOP-REPORT 11ac until its own fix lands).
