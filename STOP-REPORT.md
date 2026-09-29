# STOP-REPORT — z.ai step 11r, item 2 (trustworthy tc_wizard_layout), 2026-09-30

State: HEAD `f2e8c67` (TN_CHK_ROADSHOW restored; logs committed).
Item 2 is red twice in a row (benches `011240`, `020908`) and its
3-run budget is spent — I stop. Item 1 is DONE and ALL-GREEN.

## Item status

1. `test: wizard messages never live on a dead stack frame` — DONE:
   `fc92ff4`. Bench `20260930-004920-v1.2.0-rc4-375-gfc92ff4`,
   four legs `core: 108 ok / 0 not ok; external: 1 skipped`,
   `1..109`. Logs commit `421abd0`. Notable: the 68000 leg walked
   all five wizard pages with NO freeze — the heap-message change
   removed the 11q NTSC hang, confirming the dead-stack-frame
   diagnosis.

2. `test: wizard layout row with shared label text and a diagnosed
   freeze` — rows in (`73819f9` shared macros + diag + port wait +
   stage prints; `71d3a6c` domain-overlap fix + gid-147 diag;
   `8a7a1b1` full-label copy + correct PLACETEXT bits). RED ×2,
   budget spent. NOT DONE.

Counted runs: `011240` (1), `014954` (2), `020908` (3). One
non-counted FATAL: `013350` died before any TAP line — the host
netsvc ports were still held by the previous run (TCP 15021 busy);
cleared, rerun as `014954`.

## What each run established

- `011240` (stretched label): tc_wizard_layout caught the
  Domain-vs-Advanced REAL overlap (gid 149/160) — fixed in
  `71d3a6c` (the Domain field now ends one fx gap before the
  Advanced button; the screenshots could not show it because the
  button hid under the field). But the (b) assertion did not fire.
- `014954` (same tree): ALL-GREEN 110 ok / 0 not ok — but with a
  diag that explained why (b) was silent, and this run is the
  closest thing to the item's expected result so far:
  `# tc_wizard_layout: diag page3 gid147 flags=8006 placetext=6 lab="Also write _Roadshow NetInterfaces (backup kept"`
- `020908` (stretched label, after `8a7a1b1` fixed two (b) bugs —
  the real PLACETEXT bits are LEFT 0x1 / RIGHT 0x2 / ABOVE 0x4 /
  IN 0x10 (checkbox carries RIGHT|ABOVE = 6, so == RIGHT never
  matched) and the checkbox label DOES come back through
  GadgetText but my 48-byte copy truncated it):

  The required (b) fail-proof line, verbatim (both legs):
  ```
  # tc_wizard_layout: (b) page 3 gadget 10 (gid 147) right label ends at 1392 > inner right 636 ("Also write _Roadshow NetInterfaces (backup kept) and also this label is deliberately stretched far beyond the panel for the fail-proof run 1234567890")
  ```
  TN_CHK_ROADSHOW was then restored (`08899b5`).

## Why I still cannot claim 109 ok / 0 not ok

The same 020908 run exposed two new facts that a green run must
first address:

1. (b) above-label model error: my ABOVE check assumes the label
   starts at the gadget's left edge, but GadTools centers an ABOVE
   label, so `(b) page 0 gadget 9 (gid 222) right label ends at
   786 > inner right 636 ("Found on this system:")` and
   `(b) page 1 gadget 9 (gid 122) ... 682 ...` are false positives
   (a centered label cannot reach x + TL). The above check needs a
   centered model (ends at x + (cw + TL) / 2) or should only assert
   the horizontal half that a centered label can actually violate.
2. The run split: the a1200 leg's conformance.log ends at 99
   processed lines (98 ok + 1 not ok) without 1..110, and the
   68000 conformance2.log is empty (0/0) — the suite did not
   complete its second pass on either image. No crash-*.iff. With
   tc_wizard_layout commented out (item 1's run) both images
   completed all passes, so the row (or its CANCEL path) is
   involved in the split.

Next step (NOT applied — budget spent): fix the centered-above
model, then run tc_wizard_layout alone on the NTSC image to
isolate the split before it rejoins main().

Working tree clean at `f2e8c67`; TN_CHK_ROADSHOW carries the fixed
11q text.
