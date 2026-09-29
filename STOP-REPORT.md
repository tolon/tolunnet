# STOP-REPORT — z.ai step 11i, item 1 (button row leftover), 2026-09-29

State: HEAD `5aa4267`. Item 1 is red twice in a row (benches `024216`,
`030421`) — per the order's rule I stop, with no third run. Item 2 not
started.

## Item status

1. `fix(prefs): spread the button row leftover over all buttons` —
   code in (`93fed5f` + follow-up `5aa4267`), assertion (f) added.
   RED ×2, signatures below. NOT DONE as a verified item.
2. `fix(prefs): config overwrite keeps the old file until the new one
   is in place` — NOT STARTED (no third run allowed).

Bench budget: 2 of 3 runs used. A red is not expected in this step;
both reds are honestly reported.

## Attempt 1 (`93fed5f`, bench `20260929-024216-v1.2.0-rc4-325-g93fed5f`)

Two violations on all four legs (`102 ok / 1 not ok`):

```
# tc_prefs_layout: (f) uneven button extras: gadget 12 () +33 vs gadget 15 () +72
# tc_prefs_layout: (d) last button right 548 + 8 vs inner right 552: off 4 > 2
```

Root causes found and fixed in `5aa4267`:

- The row target used `win_w - 2*TN_BORDER_PAD` while the real window
  borders are narrower than the pad (WBorLeft/Right 4, pad 8).
- Assertion (f) computed the natural width from the gadget label, but
  GadTools BUTTON_KIND labels are NOT exposed via `GadgetText` (the
  `lab` field has been empty in every dump since step 11f — the 11f
  button-width check silently skipped for the same reason). With an
  empty label the natural width degenerated to 16 px and the extras
  compared raw widths. (f) now falls back to the known button names
  (Save/Use/Start/Stop/Setup.../Undo/Ping/Cancel), and the app and
  the test measure with the same `&scr->RastPort`, so the TextLength
  values agree.

## Attempt 2 (`5aa4267`, bench `20260929-030421-v1.2.0-rc4-326-g5aa4267`)

`(f)` is now GREEN (no diagnostic printed; extras are uniform 7/8 px
across the eight buttons). `(c)` and `(e)` stay green. One violation
remains on all four legs:

```
# tc_prefs_layout: (d) last button right 540 + 8 vs inner right 552: off 4 > 2
```

Evidence that the WINDOW is right and the ASSERTION formula is wrong:

- Dump: gadget 18 rect `(469,138,71)` → the button row ends at inner
  x = 540. The window inner width is `Width - BorderLeft - BorderRight`
  = 556 - 4 - 4 = 548, so the right margin inside the window is
  548 - 540 = 8 px — exactly TN_BORDER_PAD, symmetric with the 8 px
  left margin. The row spans the real interior minus one pad per side.
- The assertion computes `inner right = pwin->Width - pwin->BorderLeft`
  = 552 — it forgets `BorderRight` (4), so it expects the row to end
  at 552 - 8 = 544.
- Step 11h item 2 did not catch this because the then-buggy spread
  (the Save button swallowing the leftover: `110/40/...` widths in the
  `021208` dump) put the row at 544 by accident. Fixing the spread
  exposed the assertion's own off-by-BorderRight.

Suggested one-line fix (NOT applied — no third run): in
`tc_prefs_layout`, compute the inner right edge as
`pwin->Width - pwin->BorderLeft - pwin->BorderRight`. With the current
window that makes (d) `540 + 8 == 548`, off 0, green on the existing
layout without touching TolunnetPrefs.c.

## What I did not do

- No third bench run (rule).
- Item 2 (`src/common/prefs.c` rewrite + `tc_prefs_save_keeps_old`) —
  not started; the working tree is clean at `5aa4267`.
