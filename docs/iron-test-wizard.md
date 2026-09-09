# Physical Hardware Verification Checklist: PiStorm WiFi (`wifipi.device`)

This checklist is for bare-metal / iron verification of the TolunnetSetup wizard (UI v2, TNET-110) on Amiga systems equipped with PiStorm WiFi (`wifipi.device`). WinUAE cannot emulate 802.11 association frames; hence the WiFi path is certified via this procedure (Rung L4-pending).

The v2 wizard is driven the same way on PAL and NTSC: on screens shorter than 240 lines (e.g. a 640×200 NTSC Workbench) it switches to the compact layout automatically — verify the iron unit under whichever video standard it boots.

## 12-Step Verification Checklist

1. [ ] Boot AmigaOS with `wifipi.device` installed in `DEVS:Networks/` and PiStorm WiFi firmware running on Raspberry Pi.
2. [ ] Launch `SYS:Prefs/TolunnetSetup` from Workbench or execute `C:TolunnetSetup` from Shell. Verify the step rail on the left shows all 5 steps and the screen title reads `tolunnet Network Setup 1.2 - 1 / 5`.
3. [ ] **Screen 1 (Replace Stacks):** Verify any previously installed stacks (e.g. MiamiDx, Roadshow) are listed in the listview; leave "Replace with tolunnet" checked.
4. [ ] Click **Next >** to advance to **Screen 2 (Hardware)**. Verify `wifipi.device` unit 0 is detected and displayed with hardware MAC and 1500 MTU; press **Test adapter** and check the status line verdict.
5. [ ] Click **Next >** to advance to **Screen 3 (WiFi Setup)**. Click **Scan APs** and verify nearby SSIDs are displayed with channel, signal bars and security.
6. [ ] Select target access point (2.4 GHz WPA/WPA2), enter WPA passphrase, and toggle "Show" checkbox to verify input. Leaving the page must show live "Associating ... (Ns)" progress (no system freeze) and either succeed within 30 s or show actionable failure text.
7. [ ] Click **Next >** to advance to **Screen 4 (IP Address)**. Select DHCP (Automatic) mode; fill Host and Domain. Press **Advanced...**: the sibling window opens, main Back/Next/Cancel grey out; set Priority/Log file/DATABASE_ORDER, check "Also write Roadshow-style DEVS:Internet files" and "Use DHCP DNS, fall back to DNS 2", press **OK** — the main buttons must re-enable. Reopen and press **Cancel** — the values must be discarded.
8. [ ] Click **Next >** to advance to **Screen 5 (Test & Finish)**. Press **Run tests again** (also the RETURN key until every check passes): verify the 5 checks — Start stack, DHCP lease, Ping gateway, DNS lookup, HTTP HEAD — each report OK/FAILED with detail and a "what to do" hint on failure.
9. [ ] Press **Save log...**: choose a file in the ASL requester (with asl.library absent the log must still land in `RAM:tolunnet-setup.log` and the path must be shown); confirm the log content lists all 5 checks and the configuration summary.
10. [ ] Leave "Start at boot" checked (and optionally "Open Prefs after finish"). Click **Finish**. Verify `ENVARC:Sys/Wireless.prefs`, `ENV:Sys/Wireless.prefs` (selected network with `priority=`), `DEVS:tolunnet.config` (PRIORITY=/LOG=/DATABASE_ORDER=/DNS2= when set) and `DEVS:Internet/name_resolution` + `DEVS:Internet/routes` (when the Advanced checkbox was set) are written. With "Open Prefs after finish" the TolunnetPrefs window must open, showing the "Large text (wizard)" checkbox which writes `FONT=topaz/11` into the config on Save/Use.
11. [ ] Relaunch TolunnetSetup (after a Save with "Large text" checked) and verify it now renders in topaz/11; untick in Prefs, Save, relaunch — back to the screen font.
12. [ ] Reboot Amiga and verify automatic connection at boot via `S:User-Startup` block.
