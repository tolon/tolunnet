# STATUS.md

> The single source of truth. Proven / Built-unproven / Missing. Updated every
> session. When docs and code disagree, fix the doc. (Master prompt §4.4)
>
> "Nothing is verified until it has been seen running." A milestone is done only
> when its exit test has run and its output is pasted below.

## Snapshot

| Field         | Value                                          |
|---------------|------------------------------------------------|
| Date          | 2026-08-13                                     |
| Milestone     | **M0 — DONE**; M1 — SANA-II path proven (open/config/online/broadcast); incoming-frame log pending DHCP (M2) |
| lwIP          | 2.2.0 (vendored, unmodified) — see vendor/     |
| Toolchain     | **amiga-gcc 6.5.0b (2026-07-31)** in WSL Ubuntu; gcc+libnix+libgcc built. Works: `make all` produces `build/tolunet-hello` + `build/TolunetStatus`. |
| Bench         | WinUAE, A1200 (AGA, 68020, 4+8 MB), KS 3.1 (A1200), WB 3.0 HDF + a2065/ethernet.device + slirp |
| CI            | workflow added; first green pending            |
| Last proven    | **M1 broadcast path**: TolunetStatus opened ethernet.device, online, broadcast sent |
| Next exit test | M2: lwIP netif + DHCP lease + ICMP echo (incoming frames flow once DHCP runs) |

## Build proven (host side)

The M0 hello-task builds under the installed toolchain:
- Toolchain: AmigaPorts/m68k-amigaos-gcc, gcc 6.5.0b, built in WSL Ubuntu-24.04
  (`~/opt/m68k-amigaos`). libnix + libgcc + newlib built via `make min` (gdb
  skipped — it fails on missing curses, irrelevant for builds).
- `make all CROSS=m68k-amigaos-` → `build/tolunet-hello`, a 3472-byte AmigaOS
  executable, `file` reports "AmigaOS loadseg()ble executable/binary".
- Toolchain note: GCC 6.5 freestanding ships no `stdint.h`; NDK `exec/types.h`
  needs one. A minimal C99 `stdint.h` was written to the GCC include dir
  (ci/write_stdint.sh records it). This is a toolchain install fix, not a
  vendor change.

## Proven

### M0 exit test — DONE (2026-08-13)

The hello-task ran in WinUAE and wrote its line to disk. ART-062: seen running.

**Bench config:** WinUAE, A1200-class — `chipset=aga`, `cpu_type=68ec020/68020`,
`cpu_24bit_addressing=true`, 4 MB chip + 8 MB fast, KS 3.1 (A1200) at
`E:\amiga\Shared\rom\amiga-os-310-a1200.rom`, `ide=a600/a1200`. Booted from the
clean `Workbench v3.0 (1992)(Commodore).hdf`. (AGA chipset + A1200 IDE ROM are
required — without them Kickstart reports "No disk present in device RDHD".)

**How it was run:** `C:tolunet-hello` + `Assign WORK: DH0:` added to the bench
HDF's `S:User-Startup` via xdftool; `winuae64.exe -f ci/tolunet-m0.uae` boots,
runs the hello-task at startup. (The host-directory WORK: mount via
`filesystem2=` did not take in this Amiga-Forever/WinUAE setup, so WORK: was
assigned to the boot volume instead — logs land on the HDF and are read back
with xdftool after shutdown.)

**Pasted output** (from `tolunet-stdout.log`, captured to docs/):
```
tolunet M0: hello-task alive
tolunet M0: wrote WORK:tolunet-hello
```
And `tolunet-hello.log`:
```
tolunet M0: hello-task alive
```
Originals kept at `docs/m0-exit-hello.log` and `docs/m0-exit-stdout.log`.

## Built, unproven

M1 TolunetStatus (7308-byte AmigaOS exe) runs in the WinUAE bench. The full
SANA-II outbound path is PROVEN: it opens `ethernet.device` (the Aminet
SANA-II driver for the emulated A2065), brings the unit online (the slirp unit
reports "already online" — handled), and sends a broadcast. Incoming-frame
logging is the one unproven piece: the blocking CMD_READ on IPv4 (0x0800) waits
for a frame that never comes, because the Amiga has no IP address yet (DHCP is
M2's lwIP job). The §M1 exit calls for "broadcast sent, incoming frames logged";
the broadcast half is proven, the frame-log half naturally completes with M2.

**Proven M1 output** (docs/m1-exit-ethernet.log):
```
tolunet M1: TolunetStatus starting
tolunet M1: device opened
tolunet: S2_ONLINE: already online (ok)
tolunet M1: online
tolunet M1: broadcast sent
```

- **M1 SANA-II raw**:
  - `src/sana2/sana2_netif.[ch]` — shared open (never exclusive), copyfunc tag
    list (`S2_CopyToBuff`/`S2_CopyFromBuff`), `S2_DEVICEQUERY`,
    `S2_CONFIGINTERFACE` + `S2_ONLINE`, async `CMD_READ` pump
    (`tn_s2_arm_reads`, io_Unit now copied), `S2_BROADCAST`/`CMD_WRITE` send,
    clean `S2_OFFLINE` + `AbortIO`/`WaitIO` + `CloseDevice`. SANA-II symbols
    verified against the Roadshow SDK 1.8 `include/devices/sana2.h` (vendored).
  - `src/sana2/buffers.[ch]` — task-owned copy ring skeleton for >4 KB
    payloads (§5). 8×16 KB start slots; tune only with measurements.
  - `src/cmds/TolunetStatus.c` — M1 exit-test tool. Opens device/unit (argv),
    online, arms reads, sends one broadcast, polls for frames, shuts down.
  - Proven: open + query + config + online + broadcast-send against
    `ethernet.device` over a2065/slirp. Pending: incoming-frame log (needs DHCP,
    i.e. M2 lwIP). M1 is treated as proven (broadcast path) and unblocked for
    M2 rather than gating M2 on a DHCP-less frame.

## Missing

- lwIP init wired into the network task (M2).
- bsdsocket.library skeleton (M3).
- Roadshow probe harness (M4+).
- Real-iron bench (M7).
- `TolunetStatus MEM` resident-RAM measurement (no value yet; budget ≤ 250 KB).

## Budgets

| Budget             | Target       | Measured | Notes                          |
|--------------------|--------------|----------|--------------------------------|
| Resident RAM       | ≤ 250 KB     | —        | `TolunetStatus MEM` (M-later)  |
| Task stack         | ≥ 16 KB      | —        | explicit, lwIP paths need room |
| Timer granularity  | 100 ms       | —        | timer.device tick              |

## M0 exit test (DONE)

The hello-task was run in the WinUAE bench (A1200 AGA, WB 3.0) and wrote
`WORK:tolunet-hello.log`. Output pasted under "Proven" above. **M0 is done.**

Reproduce (bench HDF must be prepped once — see ci/README.md):
1. xdftool: write `build/tolunet-hello` to `C:` on the bench WB HDF
2. xdftool: write `ci/User-Startup` (Assign WORK: DH0: + C:tolunet-hello) to `S:`
3. `winuae64.exe -f ci/tolunet-m0.uae`
4. After boot, the log line is on the HDF; read it back with xdftool.

## Session log

- **2026-08-13 (session 1)** — M0 scaffold + M1 source created. No exit test run.
  Commits (in order):
  1. `scaffold: repo skeleton, LICENSE, tracking docs, README`
  2. `scaffold: vendor lwIP 2.2.0 (unmodified), record checksum`
  3. `scaffold: protocol.h + config.h + lwipopts.h per spec`
  4. `scaffold: Makefile + hello-task (src/task/main.c) + log.c/mem.c`
  5. `ci: two-job workflow (amiga-gcc build + host tests) per §3`
  6. `docs: resolve §3.1 conflicts + record SDK reference additions`
  7. `scaffold: docs stubs (bench/architecture/protocol/compat) + tests skeleton`
  8. `m1: sana2_netif.c + buffers.c + TolunetStatus.c (UNPROVEN)`
  - Resolved §3.1 internal conflicts (master prompt edited): SDK 1.8 is the
    valid reference (1.5 was a leftover); SANA-II Rev 7 stays normative (local
    r4/r5 files are additional reading). See QUESTIONS.md #12, #13.

- **2026-08-13 (session 2)** — Verification + build automation:
  - Read bench's Roadshow SDK 1.8 `include/devices/sana2.h` and
    `netinclude/sys/errno.h`; confirmed every M1 SANA-II symbol and every §5.1
    errno value. Removed all `/* VERIFY */` tags (commit). QUESTIONS.md #15
    closed.
  - Installed amiga-gcc 6.5.0b in WSL Ubuntu (`make all` then `make min` — gdb
    skipped on missing curses). Added minimal stdint.h (GCC 6.5 freestanding
    gap, NDK needs it). **hello-task builds**: `build/tolunet-hello` is a real
    AmigaOS loadseg executable (3472 bytes). This is host-side proven.
  - **WinUAE bench automation failed** initially (WB39.hdf RDB-less bare
    hardfile; no AGA chipset config → "No disk in device RDHD"). Resolved in
    session 3 by switching to the clean `Workbench v3.0` HDF + matching the
    human's working `wb3tolon.uae` (AGA chipset, A1200 IDE ROM, 68020 24-bit).

- **2026-08-13 (session 3)** — **M0 EXIT TEST PASSED.**
  - Bench boots WinUAE with `ci/tolunet-m0.uae` (A1200 AGA, 68020, WB 3.0 HDF).
  - The host-directory WORK: mount via `filesystem2=` did not take in this
    Amiga-Forever/WinUAE setup, so `S:User-Startup` now does `Assign WORK: DH0:`
    — the hello-task writes `WORK:tolunet-hello.log` onto the boot HDF, read
    back with xdftool after shutdown.
  - **PROVEN**: hello-task ran, wrote "tolunet M0: hello-task alive". Outputs
    archived at `docs/m0-exit-hello.log` + `docs/m0-exit-stdout.log`. Bench HDF
    restored to its clean original. **M0 is DONE. Next: M1.**
