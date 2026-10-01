# STOP-REPORT — 11ab: floppy-install row still red after the stale-disk fix

## Item 4 stop (superseded by the findings below)
Item 4 burned its 3 counted benches (20261001-132627, -135130, -141853)
against STALE floppy images: 11aa's `scripts/build_adf.py` hardcoded the
removed `Install_From_Floppies.info` in `package_members()`, so every
`make package` since 11ab item 1 died in the ADF phase and never
rebuilt the disks - all three reds executed the OLD one-argument
script. The builder is fixed (commit 49eaf0c) and the disks now carry
the current script (unpack md5 868cfaed == repo).

## Item 6 stop (two counted reds in a row - stopping per the rules)
With FRESH disks the row got much further, but still fails on exactly
one file:
1. `20261001-144941-v1.2.0-rc4-476-gb0dbd36` (dest T:tninst):
   mounted=YES, script listed=YES, probe copy of the script off the
   volume OK (head ".key"), `Execute rc=10` (the script's own
   `If WARN`/`Quit 10` fired), tree walked 948 bytes - everything of
   disk 1 plus MOST of disk 2 copied; exactly ONE file missing:
   `TolunnetSetup.info 630`.
2. `20261001-151935-v1.2.0-rc4-477-gd30dc3a` (dest Work:tninst, after
   removing the T:-RAM-disk theory): byte-identical failure - the same
   single file missing, `Execute rc=10`, all other 50 members present.

So the script now works: both volume copies run, 50 of 51 members land
with correct sizes. The remaining defect is a deterministic single-file
failure inside AmigaDOS `Copy tolunnet2: "<dest>" ALL CLONE` on
`TolunnetSetup.info` (630 B, protection ----rwed, sibling of
`TolunnetPrefs.info` which copies fine from the same FFS image). Cause
not identified within the bench budget - the one-file Copy failure is
in the emulated DOS/FFS layer, not visibly in the script.

## State
- Suite: `110 ok / 1 not ok`, plan `1..112` on all four legs
  (everything else green, including the extended `tc_undo_sandbox`).
- Release assets in `build/release-assets/` are built and parity-checked;
  the LHA itself is unaffected by the floppy issue.
- README says the floppy-install path is untested until this row is
  green; STATUS quotes the honest 110/1 numbers.
- Not fixed (next session, no budget left): why `Copy` rejects exactly
  `TolunnetSetup.info` from the FFS image - candidate probes: retry the
  single file after the bulk copy (does a direct
  `Copy tolunnet2:TolunnetSetup.info <dest> CLONE` succeed?), compare
  the FFS directory entry of the two .info siblings, or re-order the
  manifest so the icon is not the entry that trips the walk.
