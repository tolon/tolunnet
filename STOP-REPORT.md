# STOP-REPORT — z.ai step 11e, item 3 (layout from font metrics), 2026-09-28

State: HEAD `7b40347`. Items 1 and 2 are done and verified. Item 3's
rework is in (`66a14bd` + `94743dd` + `7b40347`) and fixed the ordered
faults (a)/(b)/(c) — the Device/Unit/MTU/cycle rows now have metric
widths and sit below BorderTop — but tc_prefs_layout is still not ok:
the only remaining violations are the WINDOW'S OWN TITLE-BAR CHROME
gadgets (two window border gadgets with TopEdge 0, anchored to the
right edge with negative coordinates), which the row counts as
overlays. Two consecutive red benches on this item (221403, 223053)
→ per the order's rule I stop with no third run.

## Item status

1. `test: tc_prefs_layout never closes async handles` — DONE:
   `e7c5756`. Bench `20260928-213554-v1.2.0-rc4-301-g5562aa8`:
   `conformance.log: core: 102 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)`
   `conformance2.log: core: 102 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)`
   (68000 legs identical.) 1..104 in all four logs; the one not-ok is
   the expected tc_prefs_layout red. Logs commit `23d924d`.

2. `test: tc_prefs_layout reads gadgets correctly` — DONE:
   `7889bb6`. Bench `20260928-215535-v1.2.0-rc4-303-g7889bb6`:
   `conformance.log: core: 102 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)`
   `conformance2.log: core: 102 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)`
   (68000 legs identical.) Real evidence captured: `TopEdge 9 <
   BorderTop 11`, `Device gadget width 56 < 128`, `MTU:0`. Labels
   come back empty — GadTools stores GadgetText as IntuiText whose
   IText is rendered but the row's dump shows the label buffer empty;
   the item-3 layout rework makes the label contents moot. Logs
   commit `2579a4b`.

3. `fix(prefs): layout from font metrics` — code in, bench RED ×2:
   NOT DONE as a verified item.
   - `66a14bd`: rows start at the BorderTop estimate (scr->WBorTop +
     fh + 1 + 4); value column = max(TextLength("255.255.255.255"),
     "DHCP (Automatic)", "Static IP (Manual)", "ethernet.device") + 8;
     win_w clamped to the screen; MTU shows 1500 when the config has
     none.
   - Bench `20260928-221403-v1.2.0-rc4-305-g66a14bd`: ordered faults
     (a)/(b) gone, MTU shows 1500 — remaining: Unit gadget 12 px <
     16, MTU gadget 36 px < 40.
   - `94743dd`: Unit sized from "000"+16, MTU from "65535"+24.
   - Bench `20260928-223053-v1.2.0-rc4-306-g94743dd`: Unit/MTU fixed —
     remaining: only gadgets 0/1, TopEdge 0, the window's own
     title-bar border gadgets (negative right-anchored coordinates).
   - `7b40347`: skip negative-coordinate (sign-bit) gadgets in the
     copy loop. NOT benched (no third run allowed).

## Last lines of a1200/conformance.log (verbatim, bench 223053)

```
# tc_prefs_layout: gadget 0 ((none)) TopEdge 0 < BorderTop 11
# tc_prefs_layout: gadget 1 ((none)) TopEdge 0 < BorderTop 11
not ok 101 - tc_prefs_layout # layout violations found (see # lines above)
# enter tc_daemon_noconfig_start
ok 103 - tc_daemon_noconfig_start
# enter tc_undo_sandbox
ok 104 - tc_undo_sandbox
1..104
# bench: asking daemon to stop (restart-cycle proof)
```
(All four logs reach 1..104; the suite no longer hangs. The only not
ok is tc_prefs_layout on the two chrome gadgets.)

## The one remaining violation, precisely

The window's own chrome: OpenWindow installs two border gadgets
(drag/depth bar pieces) with TopEdge 0 and huge/negative left edges
anchored to the right edge. They are Intuition chrome, not prefs
layout. `7b40347` skips negative-coordinate gadgets in the copy loop
(sign-bit test) and has NOT been benched. If the next step prefers a
positive identification: skip gadgets with
`gd->GadgetType & GTYP_SYSGADGET` or those whose IText is NULL AND
whose rect intersects the window's border strips
(y < BorderTop or x >= Width - BorderRight).

No further benches were run for item 3 after the second consecutive
red, per the order's rule.
