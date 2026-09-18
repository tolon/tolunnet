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
| **M7** | Release Packaging | **BUILT** | [`build/tolunnet-1.2.0-rc1.lha`](file:///d:/Projeler/tolunnet/build/tolunnet-1.2.0-rc1.lha) and [`build/tolunnet.adf`](file:///d:/Projeler/tolunnet/build/tolunnet.adf) generated via `make package` (`xdftool`) — LhA 805,911 bytes, SHA256 `548837b1451579d7bea656b3d06351400cd65a7deaee414cb7c87e986013b19a`; ADF 901,120 bytes, SHA256 `9da3f0cfce86ec9de0a6a93683c59590c05d6a982bc275ca0a4ccbba68b50fab` (rebuild of 2026-09-10, includes the W4-part-3 wizard and ANX-01 bsdsocktest bench tooling state) |
| **M8** | Workbench Preferences GUI | **EMULATOR-PROVEN** | [`docs/bench-logs/20260905-232628-897281c/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260905-232628-897281c/) — non-blocking daemon control, ToolTypes, WBStartup, NTSC 640×200 layout |
| **M9** | bsdsocktest (third-party bsdsocket conformance; UAE emulation gated off) | **BENCH-BASELINED 102/142** | [`docs/bench-logs/20260909-170622-20d951e/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260909-170622-20d951e/) `SUMMARY.txt` — `bsdsocktest: 102/142` identical on a1200 and 68000, core 35/35 both; 25 OPEN rows in ISSUES.md (TNET-114…TNET-138) |
| **M10** | TNET-139: 68000 `#80000003` Address Error class fix (SANA-II hook register convention + byte-wise client buffers + `-Werror=cast-align` / `make align-check` CI gates; bench 68000 profile now Fast-RAM) | **DUAL-PROFILE GREEN** | a1200 35/35 both cycles (`20260910-235753-5f71f10`) and 68000 35/35 both cycles on the new Fast-RAM profile (`20260911-001150-5f71f10`, ALL-GREEN, commit `5f71f10`); host tests + zero-warning alignment gates green; TNET-115 froze the first 68000 attempt at the known point (occurrence #4, see ISSUES.md); owner's real-machine verification pending |
| **M11** | ANX-03 `check-forbid` gate + FORBID.md + PiStorm owner-machine verification profiles | **DUAL-PROFILE GREEN** | `20260911-133244-526d7b3` ALL-GREEN (35/35 ×2 both profiles, commit `526d7b3`); 18 regions inventoried, TNET-140 finding recorded; A500+PiStorm profiles in `ci/.repro-pistorm*.uae` (no-NIC clean exit, with-NIC healthy, WiFiPi source compatibility confirmed) |

---

## Working Tree Build Artifacts

- 📦 **LhA Release Archive:** [`build/tolunnet-1.2.0-rc1.lha`](file:///d:/Projeler/tolunnet/build/tolunnet-1.2.0-rc1.lha) — **2026-09-18 CMD suite build (commit with 7 new CLI commands: hostname, nslookup, whois, traceroute, nc, arp, ShowNetStatus): SHA256 `6f872321aedbe0f2dd36fbf2150961969061ca6bcd577eed37caa4e12c00f32b`**; stripped package C:tolunnet smoke-proven on the 68ec020+FastRAM PiStorm profile ("network task running")
- 💾 **ADF Floppy Image:** [`build/tolunnet.adf`](file:///d:/Projeler/tolunnet/build/tolunnet.adf) — 901,120 bytes, SHA256 `36cc1a49727903a23a868237b21078ba317a32bfd17c3e52b986be801964a4d9` (2026-09-18 13-command suite)
- 🖥️ **Owner target:** A500 + PiStorm + WiFiPi — install steps and expectations: [`docs/PISTORM-INSTALL.md`](file:///d:/Projeler/tolunnet/docs/PISTORM-INSTALL.md)

---

## Round 3 Completed Milestones & Evidence

- **Host Unit Test Harness (`make test-host`):** 8 test binaries (`test_inet_addr`, `test_config`, `test_sbtc`, `test_fdset`, `test_lvo_table`, `test_route`, `test_http`, `test_icmp`) under ASan/UBSan — **8 binaries, 0 failures, 100% pass**. Full SFD coverage verified natively in C (`test_lvo_table.c` asserts 139 SFD vector offsets).
- **Amiga Conformance Test Harness (`SocketConformance`):** Built and verified across both `a1200` (68EC020) and `68000` (A600 ECS, 68000) bench configs.
- **TNET-085 (68000 Address Error Guru `#80000003`):** Resolved and proven on 68000 bench via `ETH_PAD_SIZE 2` and 4-byte memory pool alignment.
- **TNET-074 (Full 139-Vector SFD Jump Table):** Generated via `scripts/gen_lvo_table.py` (`src/lib/lib_table.gen.c`, `src/lib/lib_stubs.gen.s`, `src/lib/lib_unimpl.c`, `src/lib/lib_compat_table.gen.md`). `tc_every_vector_callable` exercises all 139 slots without crashing.
- **Bench Logs:** Complete dual-cycle logs archived in [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/). MuForce hits: 0. Post-shutdown RAM delta: < 1.5 KB.
- **Round 4 Phase 2 (§C5 Full SocketBaseTagList + errstr + SBTC_FDCALLBACK):** Complete tag classifier and dispatch for all Roadshow 1.8 tags (1..69) including string tables in `src/common/errstr.c`, `SBTC_FDCALLBACK` hook invocation (`FDCB_ALLOC`, `FDCB_FREE`, `FDCB_CHECK`), and dual-bench verification in [`docs/bench-logs/20260907-002702-81f7774/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260907-002702-81f7774/) (`ok=24 not_ok=1 skip=1 todo=1` on A1200 and A600 68000 cycle-exact).
