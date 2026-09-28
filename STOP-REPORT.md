# STOP-REPORT — z.ai step 11c, item 2 (tc_prefs_layout), 2026-09-28

State: HEAD `302f4ea`. Item 1 is done and ALL-GREEN. Item 2 is red
three times (benches `145814`, `154147`, `161852`) — per the order's
rule I stop. Item 3 not started.

## Item status

1. `bench: no stale artifacts` — DONE: `df1e227`. Bench
   `20260928-143638-v1.2.0-rc4-290-gdf1e227` ALL-GREEN:
   `conformance.log: core: 102 ok / 0 not ok; external: 1 skipped (skip=2 todo=0)`
   `conformance2.log: core: 102 ok / 0 not ok; external: 1 skipped (skip=2 todo=0)`
   (68000 legs identical.) No crash-*.iff in the artifacts; STOP-REPORT
   deleted; strict lint green after the four tools dropped their own
   base definitions.

2. `test: Prefs layout assertion` — code in (`dd5d820` + follow-ups
   `2d5e03a`, `302f4ea`), row `tc_prefs_layout` registered before the
   stop/start row. RED ×3, identical signature every time:

   All four logs end at the same place:
   ```
   ok 100 - tc_prefs_opens
   # enter tc_prefs_layout
   ```
   then nothing — the leg times out, cycle 2 never starts. 1..N is
   missing from conformance.log and conformance2.log is empty.

3. `diag(prefs): layout from font metrics` — NOT DONE.

## What was tried (all three attempts, same hang point)

- Attempt 1 (`145814`, dd5d820): the per-gadget type dump ran tapf
  INSIDE LockIBase — my bug, fixed in `2d5e03a` (types copied under
  the lock, dumped after UnlockIBase).
- Attempt 2 (`154147`, 2d5e03a): STILL hangs before any row output.
- Attempt 3 (`161852`, 302f4ea): all SpecialInfo dereferences removed
  from the copy loop (only rects + GadgetText + type word copied;
  StringInfo.Buffer read only for type codes STRGADGET(3)/12 after
  UnlockIBase; cycle min-width uses the compile-time widest label).
  STILL hangs at the identical point.

## Last 5 lines of a1200/conformance.log (verbatim)

```
ok 100 - tc_prefs_opens
# enter tc_prefs_layout
```
(the log's true tail is these two lines — the preceding lines are
`ok 99 - net_cmd_nslookup_ptr` and its `# enter`.) a1200/
conformance2.log, 68000/conformance.log, 68000/conformance2.log: all
empty (cycle 2 / leg 2 never start; bench TIMEOUT at 900 s).

Bench dir: `docs/bench-logs/20260928-161852-v1.2.0-rc4-294-g302f4ea`.

## Analysis left for the next step

The row hangs between the `# enter` line and the first of its own
outputs. Bounded stages before that point: DeleteFile/Open of three
T:/NIL: handles, an async SystemTags launch, and the 10 s window-poll
loop. The unbounded-looking stage is the gadget-copy walk under
LockIBase over `pwin->FirstGadget` — but attempt 3 copies only plain
fields there (no string/pointer chasing), so a hang in that walk
would mean something else holds Intuition or the window list is
cyclic while a second TolunnetPrefs is launching. Note the row runs
immediately after tc_prefs_opens, which Signals CTRL_C to
TolunnetPrefs; a second instance launched while the first is still
tearing down may deadlock inside Intuition. Suggested next step: add
tapf markers between EVERY stage (after launch, after poll, per
gadget copied) and/or wait until the previous TolunnetPrefs task is
gone (FindTask NULL loop) before launching the new one. The
tc_prefs_opens row itself proved every one of these operations works
when run in isolation — the difference here is only the launch
collision with the previous instance's teardown.
