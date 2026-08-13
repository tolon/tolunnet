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
| Milestone     | M0 (scaffold) — M1 source added, unproven      |
| lwIP          | 2.2.0 (vendored, unmodified) — see vendor/     |
| Toolchain     | **amiga-gcc 6.5.0b (2026-07-31)** in WSL Ubuntu; gcc+libnix+libgcc built. Works: `make all` produces `build/tolunet-hello`. |
| CI            | workflow added; first green pending            |
| Last proven    | hello-task **compiles & links** → AmigaOS loadseg() exe (3472 bytes, confirmed by `file`) |
| Next exit test | M0: hello task **runs** in WinUAE, writes WORK: (BLOCKED — see below) |

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

## M0 exit test — BLOCKED on WinUAE bench automation

Automated WinUAE testing was attempted but could not boot the bench:
- WB39.hdf is a **bare hardfile with no RDB** (starts `DOS\0` at offset 0);
  WinUAE needs explicit geometry for such images and the tried configs did
  not boot (no output written to the WORK: host dir after >90 s in 3 attempts).
- WinUAE runs headless here (`use_gui=no`) and screen capture could not be
  focused on the emulator window, so the boot state could not be diagnosed
  visually.
- Artifacts prepared for a manual run: `ci/tolunet-m0.uae` (bench config,
  needs geometry/RDB fixing), `ci/User-Startup` (adds `C:tolunet-hello` to the
  boot). The bench WB39.hdf was restored to its original (hello-task +
  User-Startup edits reverted) so the human starts from a clean image.
- **Human action needed**: load `ci/tolunet-m0.uae` in the WinUAE GUI, fix the
  hardfile/geometry (or boot from a known-good OS 3.x HDF), confirm `WORK:`
  mounts, and run the hello-task. Paste the `WORK:tolunet-hello.log` line here.

## Proven

_(nothing yet — M0 exit test has not run)_

## Built, unproven

These are written but have **not** been seen running in WinUAE. Treat as
drafts. The Makefile currently builds only the M0 hello-task; M1 sources are
not yet wired into a build target (M1 wiring lands once the SANA-II header
paths/fields are confirmed on the bench — every unverified symbol is tagged
`/* VERIFY: sana2.h (Rev 7) */` in the source and tracked in QUESTIONS.md #15).

- **M0 scaffold** — repo, LICENSE, README, tracking docs, lwIP 2.2.0 vendored
  (unmodified), `include/tolunet/{protocol,config}.h`, `lwipopts/lwipopts.h`,
  Makefile (`make all` → `build/tolunet-hello`), two-job CI workflow,
  `docs/{bench,architecture,protocol,compat}.md`, tests skeleton.
  - The hello-task is the only thing intended to *build and run* in M0.
  - Proven-by: M0 exit test (human runs hello-task in WinUAE, pastes output).
- **M1 SANA-II raw** (source only):
  - `src/sana2/sana2_netif.[ch]` — shared open (never exclusive), copyfunc tag
    list (`S2_CopyToBuff`/`S2_CopyFromBuff`), `S2_DEVICEQUERY`,
    `S2_CONFIGINTERFACE` + `S2_ONLINE`, ≥4 outstanding async `CMD_READ`
    (`tn_s2_arm_reads`), `S2_BROADCAST`/`CMD_WRITE` send, clean
    `S2_OFFLINE` + `AbortIO`/`WaitIO` + `CloseDevice`.
  - `src/sana2/buffers.[ch]` — task-owned copy ring skeleton for >4 KB
    payloads (§5). 8×16 KB start slots; tune only with measurements.
  - `src/cmds/TolunetStatus.c` — M1 exit-test tool. Opens device/unit (argv),
    online, arms reads, sends one broadcast, polls ~8 s logging length +
    EtherType to `WORK:tolunet-sana2.log`, shuts down cleanly.
  - VERIFY density: high. Confirmed on the web (SANA-II Rev 7 wiki) but **not**
    against the bench's local `include/devices/sana2.h` — see QUESTIONS.md #15.
  - Proven-by: M1 exit test (human runs TolunetStatus in the §8 bench, pastes
    "broadcast sent" + ≥1 "frame len=... type=0x..." line).

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

## M0 exit test (target)

```
1. Human: stage bench per docs/bench.md (WinUAE, OS 3.2, 68040, host dir as WORK:)
2. Human: run the hello-task artefact in WinUAE
3. Human: confirm a line was written to WORK:
4. Human: paste the line + run context here, under "Proven"
```

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
  - **WinUAE bench automation failed**: WB39.hdf is RDB-less (bare hardfile),
    WinUAE needs explicit geometry and the tried configs did not boot; screen
    capture could not focus the emulator window to diagnose. Bench HDF
    restored to original; ci/ holds the config skeleton + User-Startup for a
    human-driven run. **M0 runtime exit test still pending — needs human.**
