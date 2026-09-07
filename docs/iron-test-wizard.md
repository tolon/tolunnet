# Physical Hardware Verification Checklist: PiStorm WiFi (`wifipi.device`)

This checklist is for bare-metal / iron verification of the TolunnetSetup wizard on Amiga systems equipped with PiStorm WiFi (`wifipi.device`). WinUAE cannot emulate 802.11 association frames; hence the WiFi path is certified via this procedure (Rung L4-pending).

## 10-Step Verification Checklist

1. [ ] Boot AmigaOS with `wifipi.device` installed in `DEVS:Networks/` and PiStorm WiFi firmware running on Raspberry Pi.
2. [ ] Launch `SYS:Prefs/TolunnetSetup` from Workbench or execute `C:TolunnetSetup` from Shell.
3. [ ] **Screen 1 (Replace Stacks):** Verify any previously installed stacks (e.g. MiamiDx, Roadshow) are listed; leave "Replace with tolunnet" checked.
4. [ ] Click **Next >** to advance to **Screen 2 (Hardware)**. Verify `wifipi.device` unit 0 is detected and displayed with hardware MAC and 1500 MTU.
5. [ ] Click **Next >** to advance to **Screen 3 (WiFi Setup)**. Click **Scan APs** and verify nearby SSIDs are displayed with channel, signal %, and security.
6. [ ] Select target access point (2.4 GHz WPA/WPA2), enter WPA passphrase, and toggle "Show" checkbox to verify input.
7. [ ] Click **Next >** to advance to **Screen 4 (IP Address)**. Select DHCP (Automatic) mode.
8. [ ] Click **Next >** to advance to **Screen 5 (Test & Finish)**. Click **Run Tests**; verify daemon starts, gateway ping succeeds, and DNS resolves.
9. [ ] Click **Finish**. Verify `ENVARC:Sys/Wireless.prefs`, `ENV:Sys/Wireless.prefs`, and `DEVS:tolunnet.config` are written.
10. [ ] Reboot Amiga and verify automatic connection at boot via `S:User-Startup` block.
