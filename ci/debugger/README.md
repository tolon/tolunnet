# WinUAE debugger automation

Headless/scripted access to the WinUAE built-in debugger on Windows, built
while diagnosing the 68000 `#80000003` Address Error reports.

## Launch with a console log

```
"C:\Program Files\WinUAE\winuae64.exe" -f <config.uae> -conlogfile <out.log>
```

`-conlogfile` (WinUAE 6.0+) mirrors everything the debugger prints into a
file — no screenshot/OCR needed. Enter the debugger with **Shift+F12** while
the emulation window has focus.

## Reliable keyboard input to the debugger console

The debugger reads input from its console window (`ConsoleWindowClass`,
title `Arabuusimiehet.WinUAE`). `SetForegroundWindow` + `SendKeys` is
unreliable (Windows foreground lock); typing a key with
`WriteConsoleInput` into `CONIN$` works without any focus games:

```
powershell -NoProfile -File ci/debugger/type-console.ps1 <winuae-pid> "il 8"
powershell -NoProfile -File ci/debugger/type-console.ps1 <winuae-pid> "g"
```

- `break-hotkey.ps1 <UaePid> [cmd …]` focuses the emulation window, sends
  Shift+F12 (debugger entry), types the given debugger commands (default `r`)
  and dumps the console.
- `dump-console.ps1 <conhost-pid> [file]` reads the debugger console's screen
  buffer via `AttachConsole` + `ReadConsoleOutputCharacter` (works even while
  the console renders black to GDI capture). Default output:
  `.dbg-console.txt` in the repo root.
- `capture-window.ps1`, `hotkey-hard.ps1`, `send-keys.ps1` — window capture
  and alternative key-injection helpers.

The usage comments inside the scripts still use their old scratch names
(`.dbg-break2.ps1`, `.dbg-dumpconsole.ps1`, `.dbg-type.ps1`).

## The 68000 repro rig

`ci/.repro139.uae` (+ `-ks205`/`-ks204` variants), `ci/.repro139-startup`,
`ci/.repro139-script`, `ci/.repro139-config` and `ci/.stage-repro139.sh`
boot a 68000 close to the owner's machine (Fast RAM, KS 2.04/2.05,
static wizard-format config, `C:tolunnet` from User-Startup) with a Wait 60
window for arming `il 8` before the daemon starts. The rig files contain
absolute paths to the owner's Amiga image directory; adjust them locally.
