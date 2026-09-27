# STOP-REPORT — z.ai step 10c, item 2 (tc_undo_sandbox), 2026-09-27

State: HEAD `e3b98f7`. Item 1 is done and ALL-GREEN. Item 2 is red on
both profiles and both cycles for the third time; the order's rule says
stop after two consecutive reds on the same item, so I am stopping
instead of burning a fourth bench.

## Item status

1. `fix(installer): run the wizard, visible exit text, safe makedir,
   User-Startup marker` — DONE: `56bbbdd`. Bench
   `docs/bench-logs/20260927-190135-v1.2.0-rc4-242-g56bbbdd`:
   `conformance.log: core: 100 ok / 0 not ok; external: 1 skipped (skip=2 todo=0)`
   `conformance2.log: core: 100 ok / 0 not ok; external: 1 skipped (skip=2 todo=0)`
   Logs commit with the core: lines: `9d38656`-era `docs(bench): logs
   for 56bbbdd`.

2. `test: lint + undo proven in the bench` — code is in
   (`24643a0`: lint rules + selftest + gen_installer --undo-root +
   bench.sh staging + tc_undo_sandbox row; `6b9d5bc`: repo-relative
   staging path; `e3b98f7`: in-process Execute + diagnostics) but the
   row is RED. Three bench attempts:
   - attempt 1 `20260927-191428-g24643a0`: aborted at STAGING (Git Bash
     `$WORK_DIR` path is invisible to the WSL xdftool); no emulator leg
     ran; empty log dir removed; fix `6b9d5bc`.
   - attempt 2 `20260927-195506-g6b9d5bc`:
     `not ok 102 - tc_undo_sandbox # undo did not restore C/NetShutdown from backup`
   - attempt 3 `20260927-201340-g3b98f7/e3b98f7`: same red, with
     diagnostics.

3. `docs: OWNER-RETEST` undo semantics + no-User-Startup case —
   NOT DONE (item 2 red twice; stopping per the rule).

## Last 5 lines of each log (final bench, verbatim)

a1200/conformance.log:
```
ok 101 - tc_daemon_noconfig_start
# tc_undo_sandbox: sizes: backup=0 dst=0 nc=0
not ok 102 - tc_undo_sandbox # undo did not restore C/NetShutdown from backup
1..102
# bench: asking daemon to stop (restart-cycle proof)
```
a1200/conformance2.log:
```
ok 101 - tc_daemon_noconfig_start
# tc_undo_sandbox: sizes: backup=0 dst=0 nc=0
not ok 102 - tc_undo_sandbox # undo did not restore C/NetShutdown from backup
1..102
# bench: asking daemon to stop (restart-cycle proof)
```
68000/conformance.log: identical to a1200/conformance.log (same 5
lines). 68000/conformance2.log: identical as well.

Bench dir: `docs/bench-logs/20260927-201340-v1.2.0-rc4-246-ge3b98f7`
(RESULT: HAS-FAILURES; everything else is green — 100 ok / 1 not ok,
the lone red being tc_undo_sandbox).

## What is known (evidence, not speculation)

- The generated sandbox undo itself is GOOD: a manual UAE probe
  (boot-shell `Execute S:tolunnet-undo-sandbox` on the staged 68000
  HDF, seeded exactly like the row) restored NetShutdown, kept
  User-Startup/bsdsocket restores intact, removed the backup dir, and
  printed `after-undo rc=0`. The script is not the problem.
- In the row, BOTH invocation layers fail identically:
  `SystemTags("Execute S:tolunnet-undo-sandbox >NIL:", SYS_Asynch,
  FALSE, ...)` (195506) and in-process
  `Execute("S:tolunnet-undo-sandbox", 0, 0)` (201340). After Execute,
  T:tnsbx/C/NetShutdown does not read "ROADSHW" — consistent with the
  undo's per-name `Else` branch (delete) having run instead of the
  `If EXISTS <backup>` branch, i.e. the backup file was not seen at
  undo time, or the whole script aborted before the Copy.
- The row's seeding is verified before Execute (tn_copy_file /
  tn_write_file return codes are asserted), so the backup existed when
  Execute was called.
- Known row bug for the next attempt: my `tn_file_size_of` uses
  `Seek(fh, 0, OFFSET_END)` and reads the return value as the size —
  Seek returns the position BEFORE seeking (0), so the diagnostic
  `sizes: backup=0 dst=0 nc=0` line is meaningless. Use
  Examine/fib_Size instead. (The pass/fail assertion itself uses
  Open+Read+strncmp and is independent of this bug.)
- Next investigation, in order: (a) capture the undo's own output
  (Execute into a WORK: file instead of NIL) to see whether the script
  aborts early and with which message; (b) check whether
  `Execute(S:..., 0, 0)` from a process whose ConsoleHandle is a
  redirected log file behaves differently for `If EXISTS`/`Copy >NIL:`
  chains; (c) try the row's Execute from a System child WITH a real
  WORK: console file instead of NIL.

No further benches were run for item 2 after the second consecutive
red bench (195506, 201340), per the order's rule.
