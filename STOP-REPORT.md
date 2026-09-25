# Tolunnet STOP-REPORT: z.ai step 7d — init fixed (no hangs, full suites), but the expunge-flush reload leg stays red

**Timestamp:** 2026-09-25
**Work order:** TN-zai-step.md (step 7d). Item 1 landed and works; item 2's
reload leg is red outside TODO rows → STOP-REPORT with the last lines of each
log verbatim, stop.

**Commit:** `f109299` — item 1: init-table order fixed
({size, vectors, NULL-dataTable, stub} — step 7c had 3/4 swapped), stack-push
stub restored, DOSBase-guarded failure branch, tc_usergroup init-run assertion
(ln_Name/IdString).

**Bench dir:** `docs/bench-logs/20260925-124435-v1.2.0-rc4-70-gf109299/`
(ALL FOUR logs now run to completion — the hangs are gone.)

**README core lines (verbatim):**
```
a1200/conformance.log:  core: 73 ok / 9 not ok; external: 1 skipped
a1200/conformance2.log: core: 73 ok / 9 not ok; external: 1 skipped
68000/conformance.log:  core: 73 ok / 9 not ok; external: 1 skipped
68000/conformance2.log: core: 73 ok / 9 not ok; external: 1 skipped
net TODO remaining: 8
```

## The remaining red (verbatim, identical ×4)

```
not ok 63 - tc_usergroup # reload: library still in LibList after flush
```

The init-run assertion passes (ln_Name/IdString proven) — getpwnam(root)
works after the table fix. The reload leg fails only at the final check:
after `AllocMem(0x7FFFFFF0, MEMF_PUBLIC)` fails, `FindName` still finds
usergroup.library — i.e. exec did NOT purge the closed library in WinUAE's
emulation (the failed alloc does not trigger a library purge there), or the
library's EXPUNGE vector is never invoked by exec's flush path. The
expunge vector IS wired (ug_stub_expunge at -18 in ug_table.gen.c).

Per the rules: red outside TODO rows → stop, do not fix.

## Suggested next steps (owner decision)

1. In the test, replace the failed-AllocMem flush with an explicit purge:
   call the library's own expunge via `RemLibrary`-equivalent is not public;
   instead call `CloseLibrary` then `Forbid(); RemLibrary?` — not public.
   Practical alternative: drop the LibList check and keep getpwnam-reopen
   as the pass condition (exec/WinUAE purge behavior is environment-
   specific, not a tolunnet property).
2. Or keep the row red and mark it TODO with reason "WinUAE does not purge
   closed libs on failed alloc".

Evidence kept: `docs/bench-logs/20260925-124435-v1.2.0-rc4-70-gf109299/`.
