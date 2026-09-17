# tolunnet — step W4: TolunnetSetup UI v2 (TNET-110). One commit. Reference: `docs/design/wizard-v2.html` + `docs/design/wizard-{1-replace,2-hardware,3-wifi,4-address,5-test}.png` — match them. STOP RULES: bench red twice in a row or 3 h without commit → STOP-REPORT.md.

Grid: two equal columns; every string gadget the same width; labels right-aligned to the column; ≤60 chars per line. All sizes derive from the font (`fh = font->ta_YSize`): row pitch `fh+6`, list row `fh+3`, button height `fh+6`. Font = screen font (`scr->Font`); config/ToolType `FONT=name/size` (`diskfont.library` `OpenDiskFont`, fallback topaz/8); Prefs "Large text" checkbox writes `FONT=topaz/11`. Custom-drawn text (step list, group titles, OK/FAILED) uses `SetSoftStyle(FSF_BOLD)`; GadTools labels stay plain. No grey text: everything TEXTPEN.

Window: PAL 632×240; if `scr->Height < 240` use 632×176 with 4-row lists. Title `Network Setup`, `WA_ScreenTitle` = `tolunnet Network Setup 1.2 — n / 5`.
- Left column 110 px: step list drawn by `render_frames()` (`RectFill` + `Text`): done = `✓ ` prefix, active = `FILLPEN` box + `FILLTEXTPEN`, pending = plain. Redrawn on page change and `IDCMP_REFRESHWINDOW`.
- Right pane: `DrawBevelBox` recessed; group frames = `DrawBevelBox` + title text over the top edge (clear with `RectFill` in BACKGROUNDPEN, then `Text`).
- Status line under the pane: TEXT_KIND with `GTTX_Border`; updated by async steps from a 1 s `timer.device` tick in the `Wait()` mask. No `Delay()` anywhere.
- Button bar: `Cancel` left; `< Back`, `Next >` right. Next/Finish default (RETURN), ESC = Cancel, `GT_Underscore` on all, `GA_TabCycle` on strings.

Pages (labels exactly as in the PNGs):
1 Replace: LISTVIEW_KIND of detected stacks (name · evidence); checkboxes "Replace them with tolunnet (recommended)" (default on) and "Import Roadshow interface settings" (only when found); two-line note.
2 Hardware: LISTVIEW_KIND adapters: name · device · unit · type/MTU; unusable ones with their S2 error; buttons Rescan, Test adapter (S2_DEVICEQUERY + S2_ONLINE, result in status line).
3 WiFi (wireless only): LISTVIEW_KIND: SSID (`<hidden>` if empty), bars from `S2INFO_Signal` dBm (≥-50→5, -60→4, -70→3, -80→2, else 1), channel, security from `S2INFO_Encryption` (open/WEP/WPA/WPA2); Scan button; Network + Password strings (password masked with `●`, "Show" checkbox); "Add hidden…", "Forget"; up to 4 saved networks with `priority=` in Wireless.prefs. Status line shows "Associating… N s / 30 s"; failure text "Wrong password?" or "Network not found — 2.4 GHz only?".
4 Address: Mode cycle (Automatic (DHCP) / Manual); manual group `GA_Disabled` while DHCP: IP, Netmask, Gateway, DNS 1, DNS 2, MTU (576–1500, blank = driver default); Host, Domain; "Advanced…" button → small window: priority, log file, `DATABASE_ORDER`, "Also write Roadshow-style DEVS:Internet files"; checkbox "Use DHCP DNS, fall back to DNS 2". Validate on Next (`inet_parse`, contiguous netmask); `EasyRequest` + activate the bad gadget. Keys: `DNS2=`, `MTU=` (existing config grammar).
5 Test: LISTVIEW_KIND of 5 checks: OK/FAILED + detail + "→ what to do"; buttons "Run tests again" (default until all pass), "Save log…" (ASL requester); checkboxes "Start at boot", "Open Prefs after finish"; Next becomes Finish.

Proof: existing `tc_wizard_wired` via ARexx port stays green; new `tc_wizard_ntsc` (opens on a 640×200 screen, no gadget below `scr->Height`); screenshots of the 5 pages PAL + NTSC in the bench log dir; `docs/iron-test-wizard.md` updated. Report: commit, bench dir, verbatim `not ok` lines.
