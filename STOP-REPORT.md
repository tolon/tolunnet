# STOP-REPORT — z.ai step 10f, item 3 (crash-requester guard), 2026-09-28

State: HEAD `dd99569`. Items 1 and 2 are done and green. Item 3 is red
twice in a row (benches `20260928-031321` and `20260928-033259`);
per the order's rule I stop instead of a fourth attempt. Item 4 was
started (TolunnetPrefs trace code sits uncommitted in the working
tree) and is NOT counted as done.

## Item status

1. `test: finish 10e item 1` — DONE. `d11498d` (+ src handles
   `8541a29`): all SystemTags open fresh NIL: handles,
   `git grep -n "SYS_Input, (BPTR)0"` returns nothing; all screen/window
   walks run under LockIBase(0)/UnlockIBase with data copied out.
   Benches `20260928-010739` and `20260928-012616` ALL-GREEN.
2. `bench: HDF headroom >= 2048 KB` — DONE. `52c492d`: reclaim list
   extended (every removal named in the commit), `hdf free: 2254 KB`,
   bench `20260928-015107` ALL-GREEN.
3. `test: crash-requester guard` — code in (`fc43403`, follow-ups
   `974460c`, `dd99569`) but the guard never fires although the
   "Software Failure" requester IS on screen. RED twice: see below.
4. `diag(prefs): stage trace` — NOT DONE (TolunnetPrefs.c edits sit in
   the working tree, uncommitted, not counted).

## Final bench 20260928-033259-v1.2.0-rc4-271-gdd99569 (HAS-FAILURES)

- a1200: `conformance.log: core: 101 ok / 1 not ok; external: 1 skipped (skip=2 todo=1)`
- a1200 conformance2: identical.
- 68000: `conformance.log: core: 100 ok / 2 not ok; external: 1 skipped (skip=2 todo=1)`
- 68000 conformance2: identical.
- The extra 68000 not-ok: `not ok 5 - net_recv_ctrlc # expected -1/EINTR
  within ~2 s` — second consecutive bench with this 68000-only flake
  (031321 and 033259). Separate issue, worth its own investigation.

## Last 5 lines of each log (verbatim)

a1200/conformance.log:
```
not ok 100 - tc_prefs_opens # TODO TolunnetPrefs never opens its window - fixed in step 11
# enter tc_undo_sandbox
ok 103 - tc_undo_sandbox
1..103
# bench: asking daemon to stop (restart-cycle proof)
```
a1200/conformance2.log: identical to a1200/conformance.log.
68000/conformance.log:
```
# enter tc_undo_sandbox
ok 103 - tc_undo_sandbox
1..103
# bench: asking daemon to stop (restart-cycle proof)
```
68000/conformance2.log: identical to 68000/conformance.log.
(The 68000 net_recv_ctrlc not-ok is at row 5, outside these tails.)

Bench dir: `docs/bench-logs/20260928-033259-v1.2.0-rc4-271-gdd99569`.
No crash-*.iff was produced: the guard did not fire.

## The item-3 mystery (evidence for the next attempt)

- Both profiles: tc_prefs_opens' own title dump shows the requester —
  `title: Software Failure` — found by strstr over
  `IntuitionBase->FirstScreen` windows, immediately after the row.
- The guard runs right after the row (the `# enter` markers prove the
  macro executes) and uses the same strstr over the same lists, with
  intuition.library opened into the global when it was NULL (follow-up
  dd99569). It still reports nothing.
- Differences left to probe: (a) the guard's scan may run while the
  requester belongs to a CLOSED screen list state (IntuitionBase opened
  fresh sees a different IntuitionBase global inside intuition.library
  itself — the library keeps its own screen list; a second
  OpenLibrary shares it, so this should not matter, but verify by
  dumping ALL titles from inside the guard); (b) `g_crash_reported`
  may already be 1 from an earlier false set; (c) the macro's `#tc`
  stringification inside the nested for/if could be miscompiled by the
  68000 gcc at -O2 — try printing an unconditional debug line from the
  guard. Next step: dump every title from the guard itself; the row
  and the guard disagree about what is on screen, and that disagreement
  is the bug.

No further benches were run for item 3 after the second consecutive
red, per the order's rule.
