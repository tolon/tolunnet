# ci/

Bench and automation helpers. Not shipped in the release archive (except the
files the `package` target copies explicitly).

## write_stdint.sh

The amiga-gcc 6.5.0b freestanding toolchain ships `stdbool.h` but not
`stdint.h`; the NDK `exec/types.h` does `#include <stdint.h>`. This script
writes a minimal C99 `stdint.h` (m68k ABI: 32-bit int/long/ptr, big-endian)
into the GCC include dir. Run once after installing the toolchain:

```bash
wsl -d Ubuntu-24.04 -- bash /mnt/d/<repo>/ci/write_stdint.sh
```

This is a toolchain install fix, not a vendor change (the vendored tree is
untouched). If a future toolchain ships stdint.h, this becomes a no-op.

## WinUAE bench configs (`*.uae`)

- `tolunet-m0.uae`, `tolunet-m1.uae`, `tolunet-m2.uae` — historical M0–M2
  bench skeletons (kept for provenance). Known issue (2026-08-13): they mount
  a bare hardfile (no RDB) whose geometry WinUAE does not guess; fix the
  geometry in the GUI or boot a known-good OS 3.x image instead.
- `test_adf.uae` — config for booting the release ADF.

## User-Startup variants

`User-Startup`, `User-Startup-M4`, `User-Startup-M5`, `User-Startup-Normal`,
`User-Startup-PrefsTest`, `User-Startup-InstallTest`,
`User-Startup-LauncherTest` — bench snippets for the various milestone /
installer runs. The plain `User-Startup` runs
`Run <NIL: >NIL: C:tolunnet ethernet.device 0` at boot. Note for reinstalls
after the v3 P0 work: the daemon refuses to exit while clients are open
(TNET-059) and no-argument startup now reads `DEVS:tolunnet.config` /
`ENV:tolunnet.prefs`, so the device/unit arguments are optional.

## mkicon.py, tolunnet.info

`mkicon.py` generates 4-colour (Depth=2) `.info` icons;
`ci/tolunnet.info` is the `C:tolunnet` icon copied into the release archive.
Icon format is validated by `scripts/verify_icons.py` (runs under
`make test-host`).

## GitHub Actions

`.github/workflows/build.yml` still references removed artifacts
(`build/tolunet-hello`, artifact name `tolunet-amiga-build`) and its
host-tests job does nothing useful — tracked as TNET-073 in ISSUES.md;
fix pending.
