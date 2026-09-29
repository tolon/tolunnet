# STOP-REPORT — z.ai step 11q, item 2 (wizard layout row), 2026-09-30

State: HEAD `1cf8b9b` (label restored; logs committed). Item 2 is red
twice in a row (benches `235034`, `001415`) — per the order's rule I
stop, with no third run. Item 1 is DONE and ALL-GREEN.

## Item status

1. `fix(setup): wizard labels stay inside the panel` — DONE: `af03c70`.
   Bench `20260929-232445-v1.2.0-rc4-367-gaf03c70`, four legs
   `core: 108 ok / 0 not ok; external: 1 skipped`, `1..109`. Logs
   commit `84fbaa2`. I re-read the new screenshots myself (IFF->PNG
   parse): page 4 (IP) — the Roadshow label reads
   "Also write _Roadshow NetInterfaces (backup kept)" and ends well
   inside the panel, nothing clipped; page 5 (Test) — "Checks:" is
   complete on its own line below the page title rule, the checkbox
   row sits clear below the listview with no stray glyph, on PAL
   (640x256) and NTSC (640x200) alike.

2. `test: wizard layout row and Test-page screenshot with real
   results` — rows in (`10496d6`: TEST verb + wizard-5 shot +
   tc_wizard_layout; `62fc770`: title-match fix), RED ×2, plus a
   freeze on the NTSC leg. NOT DONE as a verified item.

Counted runs: 2 (item 1 used 1; its FATAL-free). The Roadshow label
revert was committed (`b23e622`) and restored (`f042c54`) — the
working tree carries the FIXED label.

## Run 1 (`b23e622`, bench `235034`) — wrong-failure finding

`tc_wizard_layout` matched the window by "tolunnet Network Setup",
but the wizard renames its window to "Network Setup" via
SetWindowTitles — so the row reported "wizard window not found" on
all five pages and could not exercise (b) at all. Fixed in `62fc770`
(match "Network Setup", exclude "Advanced" like find_wizard_window
does).

## Run 2 (`62fc770`, bench `001415`) — two separate problems

a1200 (PAL) leg completed (`108 ok / 1 not ok`) with the layout row
printing (c) false positives, then the 68000 (NTSC) leg froze inside
tc_wizard_layout (conformance.log ends after "# enter
tc_wizard_layout"; 37 ok lines; no crash-*.iff produced):

```
# tc_wizard_layout: (c) page 0 gadgets 4 and 9 (gid 222/222) overlap
# tc_wizard_layout: (c) page 0 gadgets 5 and 8 (gid 222/222) overlap
# tc_wizard_layout: (c) page 0 gadgets 5 and 9 (gid 222/222) overlap
... (8 such lines, all gid 222 pairs on page 0)
not ok 38 - tc_wizard_layout # wizard layout violates the panel
```

The (c) pairs are all gid-222 gadgets sharing coordinates — the
wizard's stack page builds its per-stack rows from a template, so
unshown rows keep template rects on hidden/off-page gadgets that my
copy loop still sees. The overlap check needs to skip gadgets the
page does not actually show (or pairs with identical rects whose
page is not the current one) before it means anything. The (b)
label assertion never got a chance to fire — the label revert was
still active in this run, so the "can fail" proof for (b) is still
owed.

The NTSC freeze has no crash dump: the leg stops right after
"# enter tc_wizard_layout". Suspects (not diagnosed, no third run):
LockIBase/scr usage around the copy loop, or the wizard's own
event loop interacting with the ARexx PAGE flood — needs a bench
with just this row enabled on the NTSC leg to isolate.

## Suggested next step (NOT applied)

1. Copy-loop hygiene: skip gadgets with GadgetID 0 AND empty label
   (template/hidden), and assert (c) only among gadgets whose rect
   lies inside the CURRENT page's pane (pane_l/pane_t/pane_w/pane_h
   from the geometry report that tc_wizard_ntsc already reads).
2. Isolate the NTSC freeze: run tc_wizard_layout alone on the NTSC
   leg (bench variant) before adding it back to the full suite.
3. Then re-run the label-revert proof for (b).

## DNS 2 in DHCP mode (answer from the code, nothing changed)

Yes, conditionally: `tn_write_tolunnet_config` (net_test.c:92)
writes DNS2 in DHCP mode (ip_mode == 0) only when the
"Use DHCP DNS, fall back to DNS 2" checkbox is set
(`ws->dhcp_fallback_dns2`); otherwise DNS2 is left empty. In Static
mode DNS2 always applies. So DNS2 is shown alone on the IP page in
DHCP mode as a fallback, not as a primary resolver.
