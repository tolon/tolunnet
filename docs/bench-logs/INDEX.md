# Bench log index

Per-commit evidence dirs. The newest full run per commit is the citable one;
`SUMMARY.txt` inside each dir carries the per-profile scores.

## Record corrections (z.ai step 5 item 0)

- **item 4 (expunge / LIBF_DELEXP / tn_daemon_is_stopping) → ba29856**
  (src/lib/lib_vectors.c, daemon_main.c, task_ctx.h) — the code rode in a
  commit titled as a test change. Rule from now on: any `src/` change is in a
  commit whose title names its item.
- Evidence dir mapping for the step-4 follow-through: `…121642…gba29856` is
  ba29856's bench (tc_lib_expunge_survives `ok 2` both profiles);
  `…130226…g6df32d9` is 6df32d9's bench (ALL-GREEN + 8 TODO rows).
- `…123456…g6df32d9` (netsvc.log only) was a staging leftover where the
  netsvc host services failed their readiness probe before any emulator leg
  started — deleted, not evidence.
- **`…145035…g2461b7b` was RED, not green** — the suite stopped after test 3
  on both profiles (3 ok rows, conformance2 empty, TODO 0: the net rows were
  never reached). The stack-CANCEL corruption froze the suite right after a
  successful Ctrl-C interrupt; fixed by d960d6a. Lesson: a logs commit must
  quote the real `core:` line from README.txt, never a hand-written summary.


## Intermittent hang watch

- `…115201…g7c38237`: 68000 leg froze after `ok 34 - tc_wizard_wired`
  (conformance.log 33 ok + last line, conformance2 empty; daemon log ends
  normally → GUI/test-side hang, not the stack). First occurrence in ~40
  benches. Kept as evidence; rate tracked in ISSUES.md (TNET-153).
