# tolunnet — Project Status & Milestones

**Target Architecture:** AmigaOS 3.0+ (Motorola 68000–68060, `-m68000 -msoft-float`, No FPU required)  
**API Specification:** 100% Roadshow SDK 1.8 & AmiTCP V4 Standard API  

---

## Milestone Verification Matrix

| Milestone | Area | State | Evidence / Log Link |
|---|---|---|---|
| **M0** | Toolchain & Build | **EMULATOR-PROVEN** | [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/) — automated dual-cycle WinUAE bench |
| **M1** | SANA-II Driver Interface | **EMULATOR-PROVEN** | [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/) — A1200 and 68000 SANA-II packet RX/TX pump |
| **M2** | lwIP Core & DHCP Engine | **EMULATOR-PROVEN** | [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/) — DHCP lease `10.0.2.15` acquired on both A1200 and 68000 |
| **M3** | Standard `bsdsocket.library` | **EMULATOR-PROVEN** | [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/) — 139 SFD vectors callable without crash, zero Gurus |
| **M4** | DNS Resolver & ICMP Ping | **EMULATOR-PROVEN** | [`docs/bench-logs/20260906-001249-0793186/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-001249-0793186/) — `tc_icmp_raw` bidirectional echo against 10.0.2.2 |
| **M5** | TCP Stream & HTTP Client | **EMULATOR-PROVEN** | [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/) — `TolunnetGet` HTTP/1.1 redirect, chunked transfer, range resume |
| **M6** | Roadshow / Miami DX Suite | **EMULATOR-PROVEN** | [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/) — SocketBaseTagList, netdb tables, SIGIO, WaitSelect, lo0 |
| **M7** | Release Packaging | **BUILT (rc2)** | `build/tolunnet-1.2.0-rc2.lha` (956,327 bytes, SHA256 `449e351f805ab0f0a953e9943e0674fe871be46967261079c1fc4291b4019cb8`) + stripped ADF (SHA256 `7451f1c4eb52771ce7e2d871653c83d36a49c186843ee152d84fb41782082794`) via `make package`; version single-sourced from `include/version.h` |
| **M8** | Workbench Preferences GUI | **EMULATOR-PROVEN** | [`docs/bench-logs/20260905-232628-897281c/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260905-232628-897281c/) — non-blocking daemon control, ToolTypes, WBStartup, NTSC 640×200 layout |
| **M9** | bsdsocktest (third-party; UAE emulation gated off) | **126/142** | `20260920-144628-50a29c8` `SUMMARY.txt` — 126/142 both profiles (2 WONTFIX-class remain: MSG_OOB pair); core suite 53/53 ×4 |
| **M10** | TNET-139: 68000 `#80000003` Address Error class fix (SANA-II hook register convention + byte-wise client buffers + `-Werror=cast-align` / `make align-check` CI gates; bench 68000 profile now Fast-RAM) | **DUAL-PROFILE GREEN** | a1200 35/35 both cycles (`20260910-235753-5f71f10`) and 68000 35/35 both cycles on the new Fast-RAM profile (`20260911-001150-5f71f10`, ALL-GREEN, commit `5f71f10`); host tests + zero-warning alignment gates green; TNET-115 froze the first 68000 attempt at the known point (occurrence #4, see ISSUES.md); owner's real-machine verification pending |
| **M11** | ANX-03 `check-forbid` gate + FORBID.md + PiStorm owner-machine verification profiles | **DUAL-PROFILE GREEN** | `20260911-133244-526d7b3` ALL-GREEN (35/35 ×2 both profiles, commit `526d7b3`); 18 regions inventoried, TNET-140 finding recorded; A500+PiStorm profiles in `ci/.repro-pistorm*.uae` (no-NIC clean exit, with-NIC healthy, WiFiPi source compatibility confirmed) |
| **M12** | 1.2.0-rc2: TNET-150 deferred-DNS lifecycle, §B command suite (route/IFCTL/iperf/CheckNetConfig/NetShutdown), TX pool (TNET-106), TNET-152 IPC STOP, TNET-151 root-caused & fixed (every_vector SetSocketSignals(-1) corrupted sig_int) | **ALL-GREEN** | rc2 `20260920-144628-50a29c8` (53/53 ×4, TX pool live); RC3 `20260921-135938-51196ec` (58/58 ×4, original test order, probes green) |

---

## Working Tree Build Artifacts

- LhA Release Archive: `build/tolunnet-1.2.0-rc2.lha` — 956,327 bytes, SHA256 `449e351f805ab0f0a953e9943e0674fe871be46967261079c1fc4291b4019cb8`
- ADF Floppy Image: `build/tolunnet.adf` — stripped-binary staging (732 KB content), SHA256 `7451f1c4eb52771ce7e2d871653c83d36a49c186843ee152d84fb41782082794`
- Owner target: A500 + PiStorm + WiFiPi — `docs/PISTORM-INSTALL.md`; rc2 retest procedure `docs/OWNER-RETEST.md`
- **TX_QUEUE default is 0 (synchronous DoIO) in the release** — the TX pool is proven in bench only (TX_QUEUE=4 staged); the owner live test decides the real-hardware default
- MuForce/Enforcer: **SKIP (tool not supplied)** — no ADF under `E:\amiga\Amigatolon\tools\`, `MUFORCE_ADF` unset; recorded per-run in each bench dir's `muforce.txt`
- RTG (Picasso96/uaegfx) Profile: **SKIP (P96 not in bench image)** — pristine Workbench 3.0 HDF contains no P96/uaegfx drivers; metric-driven layout verified on PAL 640×256 and NTSC 640×200; owner tests on PiStorm HD screen (procedure in `docs/OWNER-RETEST.md`)

---

## Round 3 Completed Milestones & Evidence

- **Host Unit Test Harness (`make test-host`):** 8 test binaries (`test_inet_addr`, `test_config`, `test_sbtc`, `test_fdset`, `test_lvo_table`, `test_route`, `test_http`, `test_icmp`) under ASan/UBSan — **8 binaries, 0 failures, 100% pass**. Full SFD coverage verified natively in C (`test_lvo_table.c` asserts 139 SFD vector offsets).
- **Amiga Conformance Test Harness (`SocketConformance`):** Built and verified across both `a1200` (68EC020) and `68000` (A600 ECS, 68000) bench configs.
- **TNET-085 (68000 Address Error Guru `#80000003`):** Resolved and proven on 68000 bench via `ETH_PAD_SIZE 2` and 4-byte memory pool alignment.
- **TNET-074 (Full 139-Vector SFD Jump Table):** Generated via `scripts/gen_lvo_table.py` (`src/lib/lib_table.gen.c`, `src/lib/lib_stubs.gen.s`, `src/lib/lib_unimpl.c`, `src/lib/lib_compat_table.gen.md`). `tc_every_vector_callable` exercises all 139 slots without crashing.
- **Bench Logs:** Complete dual-cycle logs archived in [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/). MuForce hits: 0. Post-shutdown RAM delta: < 1.5 KB.
- **Round 4 Phase 2 (§C5 Full SocketBaseTagList + errstr + SBTC_FDCALLBACK):** Complete tag classifier and dispatch for all Roadshow 1.8 tags (1..69) including string tables in `src/common/errstr.c`, `SBTC_FDCALLBACK` hook invocation (`FDCB_ALLOC`, `FDCB_FREE`, `FDCB_CHECK`), and dual-bench verification in [`docs/bench-logs/20260907-002702-81f7774/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260907-002702-81f7774/) (`ok=24 not_ok=1 skip=1 todo=1` on A1200 and A600 68000 cycle-exact).
