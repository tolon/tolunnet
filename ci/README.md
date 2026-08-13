# ci/

Build/test automation helpers. Not shipped in the release archive.

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

## tolunet-m0.uae

WinUAE bench config skeleton for the M0 exit test (master prompt §8). It mounts
the bench's WB39.hdf as the boot drive and the host dir `E:\amiga\Amigatolon\work`
as `WORK:` (logs survive reboot).

**Known issue (2026-08-13):** the bench's WB39.hdf is a *bare* hardfile (no
RDB — it starts `DOS\0` at offset 0). WinUAE needs explicit geometry for such
images and the config's `hardfile=` line did not boot in automated runs. To use
it: load in the WinUAE GUI, fix the hardfile geometry (or boot from a known-good
OS 3.x image), confirm `WORK:` mounts, then run the hello-task.

## User-Startup

An AmigaDOS snippet appended to `S:User-Startup` on the bench boot drive so the
hello-task runs at boot and writes `WORK:tolunet-hello.log`:

```
;BEGIN tolunet M0 exit test
If EXISTS WORK:
  C:tolunet-hello
EndIf
;END tolunet
```

To install on the bench: `xdftool -f WB39.hdf write User-Startup S/User-Startup`
(after deleting the old one), and copy `build/tolunet-hello` to `C:` on the
same image. See STATUS.md "M0 exit test" section.
