# WinUAE debugger automation (TNET-139 tooling)

Headless/scripted access to the WinUAE built-in debugger on Windows, built
while diagnosing the `#80000003` Address Error reports
(docs/history/TOLUNNET-FIX-guru-80000003.md method).

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

`break-hotkey.ps1 <pid>` focuses the emulation window and sends Shift+F12
(debugger entry). `dump-console.ps1 <pid> <file>` reads the console screen
buffer via `AttachConsole` + `ReadConsoleOutputCharacter` (works even while
the debugger console renders black to GDI capture).

## The repro rig (TNET-139)

`ci/.repro139.uae` (+ `-ks205`/`-ks204` variants), `ci/.repro139-startup`,
`ci/.repro139-script`, `ci/.repro139-config` and `ci/.stage-repro139.sh`
boot a 68000 with owner-like environment deltas (Fast RAM, KS 2.04/2.05,
static wizard-format config, `C:tolunnet` from User-Startup) with a Wait 60
window for arming `il 8` before the daemon starts.

Findings from that rig are recorded in the TNET-139 row in ISSUES.md.
