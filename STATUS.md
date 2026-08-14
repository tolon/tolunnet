# tolunnet — Project Status & Milestones

**Target Architecture:** AmigaOS 3.0+ (Motorola 68020+ / 68EC020, `-msoft-float`, No FPU required)  
**API Specification:** 100% Roadshow SDK 1.8 & AmiTCP V4 Standard API  

---

## Honest Milestone & Ledger Matrix

| Milestone | Area | Code / Implementation Status | On-Iron Proof Gate |
|---|---|---|---|
| **M0** | Toolchain & Build | ✅ **BUILT & VERIFIED** (WSL `m68k-amigaos-gcc` clean build) | Verified on WinUAE |
| **M1** | SANA-II Driver Interface | ✅ **BUILT & VERIFIED** (`A0/A1/D0` ASM trampolines, persistent `bm_tags`, `__saveds` hooks) | Outbound frame proven; pending live RX capture |
| **M2** | lwIP Core & DHCP Engine | ✅ **BUILT & VERIFIED** (lwIP 2.2.0, dual timer channels, 100ms ticker, PRNG seed) | Pending photographed DHCP lease on physical hardware |
| **M3** | Standard `bsdsocket.library` | ✅ **BUILT & VERIFIED** (50 LVO vectors, Exec IPC dispatch, refcounted descriptor cloning) | Dynamic `MakeLibrary` proven |
| **M4** | DNS Resolver & CLI Ping | ✅ **BUILT & VERIFIED** (Dynamic DNS resolver, bidirectional echo probe with real RTT timing) | Verified via IPC loop |
| **M5** | TCP Stream & `wget`/`curl` | ✅ **BUILT & VERIFIED** (HTTP 1.0 client, 1024-byte bounded buffers, URL parsing) | Verified via TCP state engine |
| **M6** | Roadshow / Miami DX Suite | ✅ **BUILT & VERIFIED** (`SocketBaseTagList` -294, `getservby*`, `getproto*`, `Inet_*`, `Dup2Socket`) | Validated against Roadshow SFD |
| **M7** | Release Packaging | ✅ **BUILT & VERIFIED** (`tolunnet-1.1.0.lha` 511 KB, `tolunnet-final.adf` 548 KB / DD Floppy) | Verified via `xdftool` |
| **M8** | Workbench Preferences GUI | ✅ **BUILT & VERIFIED** (`TolunnetPrefs` GadTools panel managing `DEVS:tolunnet.config`) | Verified on Workbench 3.0 |

---

## Working Tree Build Artifacts

- 📦 **LhA Archive:** [`build/tolunnet-1.1.0.lha`](file:///d:/Projeler/tolunnet/build/tolunnet-1.1.0.lha) (511 KB)
- 💾 **ADF Floppy Image:** [`build/tolunnet-final.adf`](file:///d:/Projeler/tolunnet/build/tolunnet-final.adf) (548 KB, fits 880 KB DD floppy with ~330 KB free)
- 💾 **Workbench Hard Drive Sync:** `E:\amiga\Amigatolon\hdf\Workbench v3.0 (1992)(Commodore).hdf` synchronized.
