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

## bench.sh

`ci/bench.sh` is the main WinUAE bench driver. It runs from Windows and builds in WSL `Ubuntu-24.04`.
- `ci/bench.sh` runs the dual-cycle conformance bench on both profiles.
- `ci/bench.sh soak` runs the 2 h session-profile soak: 12 cycles of 10 minutes on the a1200 profile
  with `TX_QUEUE=4`, driven by `User-Startup-Soak` and `Soak-Cycle`.

Logs go to `docs/bench-logs/<date>-<time>-<sha>[-dirty|-soak-...]/`, which is local and not tracked.

Environment variables:
- Build and run: `CONFIGS`, `CROSS`, `SKIP_BUILD`, `ALLOW_DIRTY`, `LOG_ROOT`, `TIMEOUT_SECS`, `GRACE_SECS`, `TN_DIAG`.
- Bench network: `BENCH_CFG`, `BENCH_DNS_PORT`, `BENCH_EXTERNAL`.
- MuForce: `MUFORCE_ADF` points to the MuForce ADF. If it is unset, the MuForce check reports SKIP.
- Soak: `SOAK_HOURS`, `SOAK_LIMIT_SECS`.

The paths to the WSL toolchain and the repo are hard-coded near the top of the script.

## WinUAE bench configs (`*.uae`)

- `tolunnet-a1200.uae`: bench profile, 68EC020 / AGA / PAL, Kickstart 3.1.
- `tolunnet-68000.uae`: bench profile, A600-class 68000 / ECS / NTSC with Fast RAM.
- `tolunnet-pistorm-68000.uae`: an approximation of the owner's A500 + PiStorm.
- `tolunnet.config`: the `DEVS:tolunnet.config` staged for the bench.
- `tolunet-m0.uae`, `tolunet-m1.uae`, `tolunet-m2.uae` — historical M0–M2
  bench skeletons, using the old `tolunet` name and kept for provenance. They
  mount a bare hardfile (no RDB), and WinUAE cannot guess its geometry.
- `test_adf.uae` — config for booting the release ADF.

## User-Startup variants

- Bench:
  - `User-Startup-Boot`: bootstrap.
  - `User-Startup-Conformance`: dual-cycle conformance run.
  - `User-Startup-Soak` and `Soak-Cycle`: soak run.
  - `diag-User-Startup`.

  The bench starts the daemon with no arguments
  (`Run >WORK:daemon.log C:tolunnet`), so the daemon reads `DEVS:tolunnet.config`.
- Historical:
  - `User-Startup`, `User-Startup-M4`, `User-Startup-M5`: old `tolunet` name, static 10.0.2.15.
  - `User-Startup-Normal`, `User-Startup-PrefsTest`, `User-Startup-InstallTest`,
    `User-Startup-LauncherTest`.

The daemon refuses to exit while clients still have the library open. Started
without arguments, it reads `DEVS:tolunnet.config` or `ENV:tolunnet.prefs`.

## mkicon.py, tolunnet.info

`mkicon.py` generates 4-colour (Depth=2) `.info` icons;
`ci/tolunnet.info` is the `C:tolunnet` icon copied into the release archive.
Icon format is validated by `scripts/verify_icons.py` (runs under
`make test-host`).

## Debugger tooling

`ci/debugger/` holds scripts for driving the WinUAE debugger; its README has the details.
The dot-files `ci/.repro*` and `ci/.stage-repro*.sh` are one-off repro rigs.

## GitHub Actions

`.github/workflows/build.yml` has two jobs:
- `host` runs `make test-host`.
- `amiga` builds in the `sebastianbergmann/amiga-gcc` container, runs `make package`, and
  uploads the `tolunnet-lha` and `tolunnet-release` artefacts.
