# tolunnet 1.2.0-rc6 — owner manual-test card

Plain checklist. Real Amiga (OS 3.0+, 68000+), Gotek or floppies.
Photograph every screen asked about; report pass/fail per line.

## 1. Gotek install (two disks)

- [ ] Copy `tolunnet-1.2.0-rc6-disk1.adf` and `disk2.adf` from the GitHub release page to the USB stick.
- [ ] Shell, disk 1 in DF0: `Execute tolunnet1:Install_From_Floppies Work:tolunnet NORUN`
- [ ] Expected: `... copying disk 1 to Work:tolunnet ...`, then
      `Disk 1 copied. Insert disk 2 (tolunnet2) now`.
- [ ] Select disk 2 on the Gotek (DOS asks for the `tolunnet2` volume if it is not mounted).
- [ ] Expected last lines: `Both disks copied into Work:tolunnet.` and
      `NORUN: files are in place. To install, run: Installer Install_Tolunnet`
- [ ] `List Work:tolunnet FILES` matches the shipped package list
      (the generated expected list is `ci/.adf-expected.txt` in the repo).

## 2. Workbench install (pretend run)

- [ ] Double-click `Install_Tolunnet` (from `Work:tolunnet` or the
      extracted LHA). Default mode is PRETEND: nothing is written.
- [ ] Expected panels: welcome -> pretend transcript listing the copy
      operations (`SYS:C/tolunnet` first, TolunnetSetup/TolunnetPrefs
      copies last) -> no write happens.
- [ ] The transcript must contain no `error`, `Undefined` or `unknown`.

## 3. Real install + undo

- [ ] Shell, from the package drawer:
      `Installer Install_Tolunnet NOPRETEND`
- [ ] Files land in `SYS:C/`, `SYS:Libs/usergroup.library`,
      `SYS:Prefs/TolunnetSetup` + `TolunnetPrefs` (with icons).
- [ ] Run `S:tolunnet-undo`: replaced C: tools come back, the two Prefs
      tools and their icons are removed, a pre-existing
      `usergroup.library` is restored (ours stays when there was none).
- [ ] `S:User-Startup` is left in place; its tolunnet block is guarded by
      `If EXISTS C:tolunnet` and the undo says it can be removed by hand.
      `S:User-Startup.tolunnet-bak` is now `S:User-Startup.tolunnet-old`.
- [ ] Reboot: the leftover block does nothing (no `C:tolunnet`).

## 4. Manual mode in the wizard

- [ ] `TolunnetSetup`, choose Manual on the IP page: all four fields
      (IP, netmask, gateway, DNS) start EMPTY.
- [ ] Next with fields empty: refused, e.g.
      `Fill in the IP address first`.
- [ ] Invalid value (`999.1.1.1`): refused with
      `The <field> is not a valid address`.
- [ ] Back and return: typed values still there.

## 5. Prefs Live Ping

- [ ] `TolunnetPrefs`, NO gateway, stack offline, press `Ping`:
      requester `No gateway known - bring the interface up or enter a
      gateway first.` (must NOT silently ping 1.1.1.1).
- [ ] Interface up, `Ping` again: pings the real gateway.

## 6. What to photograph / report

- [ ] Gotek install shell output (step 1).
- [ ] Pretend transcript (step 2).
- [ ] Manual-mode refusal status line (step 4).
- [ ] Prefs "No gateway known" requester (step 5).
- [ ] Pass/fail per line, Amiga model, OS version, Gotek firmware.
