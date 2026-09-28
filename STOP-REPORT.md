# STOP-REPORT — z.ai step 11d, item 1 (suite-owned library bases), 2026-09-28

State: HEAD `fd78888`. Item 1's code is in but the bench is red three
times (171926, 175538, 183012); per the order's rule I stop. Items 2
and 3 not started.

## Item status

1. `test: the suite owns the library bases` — code in (`fae1d8f` +
   close-wait follow-up `fd78888`), lint rule in, selftest green —
   but the bench does NOT reach `1..N`: NOT DONE as a verified item.
2. `test: tc_prefs_layout reads gadgets correctly` — NOT DONE.
3. `fix(prefs): layout from font metrics` — NOT DONE.

## Final bench 20260928-183012-v1.2.0-rc4-297-gfd78888 (HAS-FAILURES)

- a1200: `conformance.log: core: 99 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)`
- a1200 conformance2.log: (missing) — cycle 2 never started.
- 68000: no artifacts — its leg also timed out.
- Both legs: TIMEOUT waiting for bench-done after 900 s.

## Last lines of a1200/conformance.log (verbatim)

```
# tc_prefs_layout: gadget 3 (Ġ) TopEdge 0 < BorderTop 11
# tc_prefs_layout: gadget 4 () TopEdge 9 < BorderTop 11
# tc_prefs_layout: gadget 5 () TopEdge 9 < BorderTop 11
# tc_prefs_layout: gadget 4 () width 56 < TextLength(ethernet.device)+8=128
# tc_prefs_layout: gadget 5 () width 12 < TextLength(0)+8=16
# tc_prefs_layout: gadget 7 () width 12 < TextLength(0)+8=16
# tc_prefs_layout: gadget 11 () width 56 < TextLength(127.0.0.1)+8=80
not ok 101 - tc_prefs_layout # layout violations found (see # lines above)
```
(the log's true tail — nothing follows the not-ok; there is no
`# enter tc_cmd_stop_start` in this run.)

Bench dirs: `20260928-171926-gfae1d8f` (attempt 1), `20260928-175538`
(attempt 2, close-wait not yet in), `20260928-183012-gfd78888`
(attempt 3, close-wait in).

## The remaining hang (same in all three)

tc_prefs_layout completes (violations printed, row marked not ok) and
then the whole leg stops producing output — this run even before
`# enter tc_cmd_stop_start`. The close-wait follow-up (waiting <= 20 s
for the TolunnetPrefs window/task to disappear) did not cure it, so
the lingering-GUI theory is dead or incomplete.

Next investigation, in order:
1. The crash guard + leak checks run after tc_prefs_layout's tc()
   under the NEW always-open IntuitionBase — dump a marker between the
   row's not-ok and TN_RUN's leak/watchdog checks, and between those
   and tn_crash_guard, to find which post-row step stalls.
2. tc_prefs_layout signals CTRL_C but its TolunnetPrefs child may sit
   on the Software-Failure/exit path with the window list changing;
   check whether LockIBase inside a LATER row's TN_RUN is now
   re-entering while intuition holds an internal lock during GUI
   teardown.
3. Try suspending (not killing) TolunnetPrefs — SetTaskPri to -128 —
   instead of CTRL_C, so nothing tears down while the assertions run.

No further benches were run for item 1 after the second consecutive
red, per the order's rule.
