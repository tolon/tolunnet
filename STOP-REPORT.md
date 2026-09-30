# STOP-REPORT — z.ai step 11u, item 2 (readable result rows), 2026-09-30

State: HEAD `93bc320` (logs committed). Item 2 has two unintended
reds in a row (benches `144924`, `151535`) — per the order's rule I
stop. Item 1 is DONE (both commits, benches, all four legs green).

## Item status

1. Underscores — DONE.
   - Commit A `2414115` (`test: no mnemonic underscore may reach the
     screen`): assertion (d) in tc_wizard_layout + tc_prefs_layout.
     Red proof bench `20260930-132030-v1.2.0-rc4-398-g2414115`
     (109 ok / 1 not ok, both legs), the (d) lines quoted:
     `# tc_wizard_layout: (d) page 0 gadget 10 (gid 110) label contains '_': "_Replace with tolunnet (recommended, non-destructive)"` plus gids 147/151/154 on page 3/4. Logs `0ccefa2`.
   - Commit B `94608f9` (`fix(setup): mnemonic underscores are
     consumed by GadTools`): GT_Underscore added to SIX gadgets
     (gids 110, 147, 151, 154 + the Advanced-window DEVS:_Internet
     and DHCP-fallback-DNS2 checkboxes); the eleven button/cycle
     tags already existed; TolunnetPrefs.c has zero mnemonic labels
     (grep count 0). Green bench `20260930-134458-v1.2.0-rc4-399-g94608f9`:
     all four legs `core: 110 ok / 0 not ok`, `1..111` — (d) green
     because GadTools consumes the underscore from its own label
     copy. Logs `5d23e98`. Item-1 runs: 2 (one intended red + one
     green).

2. Result rows — code in (`ad627cc` layout + wrapped checklist;
   `22aabf4` row-width assertion; `6cc2d53` screen-RastPort wrap
   measurement). RED ×2 unintended. NOT DONE.

Counted runs: 2 (`144924`, `151535`). One proof run (`126820e`,
intended red) is excluded by the step rule. Infra FATAL: none.

## The chain of evidence

- Proof run `126820e` (temp commit, reverted in `a6cea31`): the
  three planted defects were caught — `(b) ... right label ends at
  1388 > inner right 636` for the stretched Roadshow label, the
  checklist grew to `nodes=16` with the giant-word advice row, and
  the checklist went red (stale nodes==5 assertion — that stale
  check is what item 2 fixed to `nodes >= 5` + the maxtext check).
- Clean run `144924` (counted 1): RED — `maxtext=304 > interior=268`.
  The wrap measured with the WINDOW RastPort and a budget of
  cw_list-24, both wrong: the suite measures on the SCREEN RastPort
  and the real listview interior is cw_list minus frame and scroller
  (268 px measured).
- Fix `6cc2d53`: tn_wrap_px measures on the screen RastPort, wrap
  budget = cw_list - 60 px. Clean run `151535` (counted 2): RED
  again, but much closer — `maxtext=272 > interior=268`, 4 px over,
  identical on both legs (a1200 109 ok? no: 108 ok / 2 not ok with
  the row-width line on both).

## Remaining gap (NOT applied — budget spent)

The 4 px residual is indent/space-width accounting: tn_wrap_px
appends a 4-space indent when WRITING the row but never includes it
in the width test (the indent is 32 px in topaz 8, yet only ~4 px
appears — the residual suggests the written row is wider than the
measured candidate by the space-collapsing of the first word). The
next attempt should measure the INDENTED candidate string inside
tn_wrap_px (prefix included) and keep the cw_list-60 budget, or
simply widen the check to measure exactly the string stored in
g_check_lines (the suite already measures that string via maxtext).

Bench numbers this step: `144924` a1200 437s / 68000 900s;
`151535` both legs completed (elapsed in READMEs), no infra FATAL
reruns, no split runs — the 1500 s headroom and heap messages hold.

## 11u item 1 note

The two docs commit ids quoted in my 11t report (f661d29/f44646b)
were wrong; the real ones are 30e5014/587fffe as you stated — ids
are now copied from `git log` only.

Working tree clean at `93bc320`.
