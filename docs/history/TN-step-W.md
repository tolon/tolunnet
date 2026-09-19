# tolunnet — step W: first-run Network Wizard (`Prefs/TolunnetSetup`). Do after step F. Rules as before (test-or-log proof, WSL toolchain, commit per step). GadTools only, screen font, must fit 640×200.

Source of truth for WiFi: `docs/history/TOLUNWIFI-prompt2-wireless-prefs.md` (verified real files) + `wireless-prefs-evidence/`. Read them first; do not invent formats.

## Flow (one window, "pages" via a CYCLE gadget + Back/Next; RETURN = Next, ESC = Cancel)
1. **Replace other stacks (default ON)**: detect Miami/MiamiDx/Roadshow/AmiTCP/Genesis by (a) `bsdsocket.library` in Exec's LibList, (b) their startup lines/`Execute` blocks in `S:User-Startup`, `S:Startup-Sequence`, `S:Network-Startup`, `WBStartup/` icons, (c) `LIBS:bsdsocket.library` on disk, (d) assigns `AmiTCP:`/`Miami:`. Page shows what was found and one checkbox "Replace with tolunnet (recommended)" checked. On Finish: comment out their startup lines with `; tolunnet-disabled: ` prefix (backup `S:User-Startup.tolunnet-bak` written once), move a disk `LIBS:bsdsocket.library` to `LIBS:bsdsocket.library.pre-tolunnet`, remove/disable WBStartup icons (rename `.info` → `.info.pre-tolunnet`), and import their settings (Miami `ENVARC:MiamiDx`/`Miami.prefs` interface+IP+DNS+hostname; Roadshow `DEVS:Internet/*`) into the wizard's fields so the user just presses Next. Never delete any file; "Undo replacement" button in Prefs reverses every rename/comment. Running stacks are asked to quit (`MiamiDx` ARexx `QUIT`/`MIAMI` port, Roadshow `NetShutdown`) — if still alive after 10 s, tell the user to reboot once.
2. **Hardware**: list `DEVS:Networks/*.device` (+ `wifipi.device`, `prism2.device` if present anywhere in path). For each: `OpenDevice` unit 0, `S2_DEVICEQUERY`; wireless = `S2_GETNETWORKS` succeeds (see addendum §D). Show friendly names ("PiStorm WiFi — wifipi.device unit 0", MAC, MTU). Pre-select the only/first working one.
3. **WiFi** (only if wireless): scan (`S2_GETNETWORKS`, tags `S2INFO_SSID/Signal/Encryption/Channel`), LISTVIEW sorted by signal, "Rescan"; passphrase STRING gadget (`GTST_EditHook` mask or `STRINGA_…`? — use a plain gadget + "Show" checkbox); write `ENVARC:Sys/Wireless.prefs` + `ENV:` per addendum §A.4 (exact field order); start/restart `WirelessManager` per §B.2/§B.4; wait for association (`S2_GETNETWORKINFO` or `S2_ONLINE`) with a progress line and 30 s timeout → actionable error ("Wrong password?" / "AP not found — 2.4 GHz only?").
4. **Address**: DHCP default; Static shows IP/mask/GW/DNS with validation (`inet_parse`). Write `DEVS:tolunnet.config`. Checkbox "Also write Roadshow-style `DEVS:Internet/interfaces` (for other tools)" using the format in addendum §C.
5. **Test**: start daemon (`SystemTags`, 32 KB stack), then ping gateway → DNS lookup `aminet.net` → HTTP HEAD; each row turns "OK"/"FAILED: <what to do>". Checkbox "Start tolunnet at boot" → adds a marked block to `S:User-Startup`. Done button.
`TolunnetPrefs` gets a "Setup…" button that runs the wizard. Installer runs it at the end.

## Proof
- Bench (uaenet, wired): scripted via the Prefs/Setup ARexx port (`PAGE n`, `SELECT n`, `NEXT`, `FINISH`) → `tc_wizard_wired` in the conformance run; screenshots in the log dir.
- WiFi path cannot run on WinUAE: ship `docs/iron-test-wizard.md` (10-line checklist for the owner's PiStorm) and mark rung L4-pending honestly.
- ISSUES **TNET-098**; README.guide "First run" section with the 5 screens.

Report: commits, bench dir, screenshots, verbatim `not ok` lines.
