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
| **M7** | Release Packaging | **BUILT** | [`build/tolunnet-1.2.0-rc1.lha`](file:///d:/Projeler/tolunnet/build/tolunnet-1.2.0-rc1.lha) and [`build/tolunnet.adf`](file:///d:/Projeler/tolunnet/build/tolunnet.adf) generated via `make package` (`xdftool`) |
| **M8** | Workbench Preferences GUI | **EMULATOR-PROVEN** | [`docs/bench-logs/20260905-224422-897281c/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260905-224422-897281c/) — non-blocking daemon control, ToolTypes, WBStartup, NTSC 640×200 layout |

---

## Working Tree Build Artifacts

- 📦 **LhA Release Archive:** [`build/tolunnet-1.2.0-rc1.lha`](file:///d:/Projeler/tolunnet/build/tolunnet-1.2.0-rc1.lha) (585,480 bytes, universal `-m68000`, 4-color Depth=2 icons)
- 💾 **ADF Floppy Image:** [`build/tolunnet.adf`](file:///d:/Projeler/tolunnet/build/tolunnet.adf) (901,120-byte standard DD floppy image formatted and packed via `xdftool`)

---

## Round 3 Completed Milestones & Evidence

- **Host Unit Test Harness (`make test-host`):** 8 test binaries (`test_inet_addr`, `test_config`, `test_sbtc`, `test_fdset`, `test_lvo_table`, `test_route`, `test_http`, `test_icmp`) under ASan/UBSan — **8 binaries, 0 failures, 100% pass**. Full SFD coverage verified natively in C (`test_lvo_table.c` asserts 139 SFD vector offsets).
- **Amiga Conformance Test Harness (`SocketConformance`):** Built and verified across both `a1200` (68EC020) and `68000` (A600 ECS, 68000) bench configs.
- **TNET-085 (68000 Address Error Guru `#80000003`):** Resolved and proven on 68000 bench via `ETH_PAD_SIZE 2` and 4-byte memory pool alignment.
- **TNET-074 (Full 139-Vector SFD Jump Table):** Generated via `scripts/gen_lvo_table.py` (`src/lib/lib_table.gen.c`, `src/lib/lib_stubs.gen.s`, `src/lib/lib_unimpl.c`, `src/lib/lib_compat_table.gen.md`). `tc_every_vector_callable` exercises all 139 slots without crashing.
- **Bench Logs:** Complete dual-cycle logs archived in [`docs/bench-logs/20260906-014843-c744255/`](file:///d:/Projeler/tolunnet/docs/bench-logs/20260906-014843-c744255/). MuForce hits: 0. Post-shutdown RAM delta: < 1.5 KB.
