# STOP-REPORT — 11ab item 4 (floppy install row): 3 counted reds

## What was attempted
`tc_floppy_install` (restored by reverting fb25192: commits cb920eb,
26b750f, 40d60f4, 906500a) executes
`tolunnet1:Install_From_Floppies T:tninst NORUN` on the bench legs with
disk1/disk2 attached as DF0/DF1 and compares the copied tree with the
manifest.

## The three counted reds (all `not ok 40 - tc_floppy_install`)
1. `20261001-132627-v1.2.0-rc4-471-g26b750f`: 110 ok / 1 not ok —
   "walked 0 bytes".
2. `20261001-135130-v1.2.0-rc4-472-g40d60f4`: 110 ok / 1 not ok —
   diagnostics: `tolunnet1: mounted=YES`, `Execute rc=20`, script
   output empty.
3. `20261001-141853-v1.2.0-rc4-473-g906500a`: 110 ok / 1 not ok —
   probes: script LISTED on the volume (YES), single-file Copy off the
   volume rc=0 but the copied bytes were the OLD script, `Execute rc=20`.

## Root cause (proven AFTER the stop, no further benches)
The bench attaches `build/tolunnet-1.2.0-rc5-disk[12].adf` — and those
images were STALE: 11aa's `scripts/build_adf.py` hardcoded
`Install_From_Floppies.info` in `package_members()`, while 11ab item 1
removed that file — so every `make package` since 11ab item 1 died in
the ADF phase (`manifest misses members`) and never rebuilt the disks.
All three reds therefore executed the OLD one-argument script shipped
in 11aa, not the fixed script of 11ab item 1. Fix applied after the
stop (commit "fix(package): the two-disk builder drops the removed
icon"): the builder matches the manifest again; `make package` is green
and the disk now carries the current script (md5 868cfaed verified by
unpacking).

## Consequence
Per the work order the README says the floppy-install script is
UNTESTED IN THE EMULATOR (as of this stop). The step-6 final bench runs
on the freshly rebuilt disks; its result is reported in the step-6
section and, if green, supersedes this note.
