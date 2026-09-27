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
- **Step-7/7b bench dir ledger** (kept): `…204554…g0538677` item-3
  completion ALL-GREEN; `…213647…gaa07f5d` item-4 first (fd40 ok, badf
  EINVAL expected fail); `…215439…g1e0019f` MEMP_NUM_UDP_PCB 48 (badf
  still EINVAL); `…230644…gfd6915e` step-7 item-0 honesty red (expected);
  `…232508…ga7cc13f` item-1 break-bit fix bench. **Deleted:** `…201206…`
  and `…203058…` (87e75f2 debug-commit duplicates), `…204237…` (netsvc
  port-conflict dead run, netsvc.log only), `…210232…` (2281561 diag
  duplicate). STOP-REPORT.md deleted in this commit's sibling logs commit
  (the step-7b red was fixed by 13d9c30).
- **item 2 (keep the daemon's winning result) rode in b4231da** alongside
  item 1: the bench for b4231da covers both (its race-honor guard was
  committed pre-bench, the break-path mirror landed before the SAME bench
  ran — verified in the d960d6a→b4231da diff). No code followed the bench.
- **"break bit preserved" claims in 171f5fd / 3fb5bf9 / 0538677 and logs
  4fc7c00 / 7a55958 were FALSE** — the test printed "CTRL_C was CLEARED
  (bug)" and still reported ok (fixed in the step-7 honesty commit; the
  underlying signal behavior is fixed by the SetSignal(bits,bits) +
  end-of-path re-assert series).
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

## 10a host-refusal reds (step 10a-2 item 1)

The 10a host-refusal row was unrunnable: WinUAE slirp never answers
SYNs to closed host ports, so the nonblocking connect to 10.0.2.2:9 just
timed out with SO_ERROR 0 (auto-question 7). Replaced by the loopback-only
tc_connect_refused_nb in 7c0be65. Red evidence kept:
`…010136…g0b1273e`, `…011454…g74922d6`, `…013018…g8a68425`,
`…014650…g51bd828`, `…020141…g51bd828` (10a red, host refusal not
testable). First post-fix ALL-GREEN: `…065646…g7c0be65` (99 ok + 1
both profiles, 0 TODO).

## Unattended Installer arc (10a-2 items 2/3) - abandoned, see 10a-3

`…073408…g80396c5`, `…080809…g8bcbdb0`, `…082734…g2373f83`,
`…084203…gff32e64`, `…091723…ge0147da`, `…095706…gf20095b`,
`…103134…g17478ab`, `…111446…g48b477e`, `…114019…g565fe26`:
unattended GUI Installer run - abandoned, see 10a-3. Installer 43 is a
GUI program (welcome, messages and the novice start screen wait for a
click); tc_installer_run was removed in 6bfb7a0 and the suite is back
to the 7c0be65 row set (first post-revert ALL-GREEN:
`…124728…gf41c8ae`).
