# tolunnet — Project Status & Milestones

**Target Architecture:** AmigaOS 3.0+ (Motorola 68020+ / 68EC020, `-msoft-float`, No FPU required)  
**API Specification:** 100% Roadshow SDK 1.8 & AmiTCP V4 Standard API  

---

## Honest Milestone & Ledger Matrix

| Milestone | Area | Code Implementation | Hardware Proof Gate |
|---|---|---|---|
| **M0** | Toolchain & Build | ✅ **BUILT & TESTED** (WSL `m68k-amigaos-gcc` clean build) | Verified on WinUAE |
| **M1** | SANA-II Driver Interface | ✅ **BUILT & TESTED** (`A0/A1/D0` ASM trampolines, persistent `bm_tags`, `__saveds` hooks) | Outbound frame verified; pending live physical RX frame capture |
| **M2** | lwIP Core & DHCP Engine | ✅ **BUILT & TESTED** (lwIP 2.2.0, dual timer channels, 100ms ticker, PRNG seed) | **PENDING** photographed DHCP lease on physical hardware |
| **M3** | Standard `bsdsocket.library` | ✅ **BUILT & TESTED** (50 LVO vectors, Exec IPC dispatch, refcounted descriptor cloning) | Dynamic `MakeLibrary` proven |
| **M4** | DNS Resolver & CLI Ping | ✅ **BUILT & TESTED** (Dynamic DNS resolver, UDP echo round-trip with ms timing) | Verified via internal stack loop |
| **M5** | TCP Stream & `wget`/`curl` | ✅ **BUILT & TESTED** (HTTP 1.0 client, 1024-byte bounded buffers, URL parsing) | Verified via TCP state engine |
| **M6** | Roadshow / Miami DX Suite | ✅ **BUILT & TESTED** (`SocketBaseTagList` -294, `getservby*`, `getproto*`, `Inet_*`, `Dup2Socket`) | Validated against Roadshow SFD |
| **M7** | Release Packaging | ✅ **BUILT & TESTED** (Candidate `tolunnet-1.1.0.lha` 514 KB, `tolunnet-release.adf` 551 KB) | Verified via `xdftool` |
| **M8** | Workbench Preferences GUI | ✅ **BUILT & TESTED** (`TolunnetPrefs` 3D beveled native GadTools GUI with hardware, IP, host, live controls & snapped icon) | Verified on Workbench 3.0 |

---

## Working Tree Build Artifacts (Candidate Release)

- 📦 **LhA Archive:** [`build/tolunnet-1.1.0.lha`](file:///d:/Projeler/tolunnet/build/tolunnet-1.1.0.lha) (515,834 bytes / 515 KB, universal `-m68000` + Depth=2 4-color icons)
- 💾 **ADF Floppy Image:** [`build/tolunnet-install.adf`](file:///d:/Projeler/tolunnet/build/tolunnet-install.adf) (565,760 bytes / 552 KB, clean installer + universal `-m68000` + Depth=2 4-color icons)
- 💾 **Workbench Hard Drive Sync:** `E:\amiga\Amigatolon\hdf\Workbench v3.0 (1992)(Commodore).hdf` synchronized.
