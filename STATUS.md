# tolunnet — Project Status & Milestones

**Target Architecture:** AmigaOS 3.0+ (Motorola 68000–68060, `-m68000 -msoft-float`, No FPU required)  
**API Specification:** 100% Roadshow SDK 1.8 & AmiTCP V4 Standard API  

---

## Honest Milestone & Ledger Matrix

| Milestone | Area | Code Implementation | Hardware Proof Gate |
|---|---|---|---|
| **M0** | Toolchain & Build | ✅ **BUILT & TESTED** (WSL `m68k-amigaos-gcc` clean build) | Verified on WinUAE |
| **M1** | SANA-II Driver Interface | ✅ **BUILT & TESTED** (`A0/A1/D0` ASM trampolines, persistent `bm_tags`, `__saveds` hooks) | Outbound frame verified; pending live physical RX frame capture |
| **M2** | lwIP Core & DHCP Engine | ✅ **BUILT & TESTED** (lwIP 2.2.0, dual timer channels, 100ms ticker, PRNG seed) | **PENDING** photographed DHCP lease on physical hardware |
| **M3** | Standard `bsdsocket.library` | ✅ **BUILT & TESTED** (50 LVO vectors, Exec IPC dispatch, refcounted descriptor cloning) | Dynamic `MakeLibrary` proven |
| **M4** | DNS Resolver & CLI Ping | ✅ **BUILT & TESTED** (Dynamic DNS resolver, UDP echo round-trip with ms timing — real ICMP ping is TNET-070, pending) | Verified via internal stack loop |
| **M5** | TCP Stream & `wget`/`curl` | ✅ **BUILT & TESTED** (HTTP 1.0 client, 1024-byte bounded buffers, URL parsing — redirects are TNET-075, pending) | Verified via TCP state engine |
| **M6** | Roadshow / Miami DX Suite | ✅ **BUILT & TESTED** (`SocketBaseTagList` -294, `getservby*`, `getproto*`, `Inet_*`, `Dup2Socket`) | Validated against Roadshow SFD; app gauntlet (AmiSSL/IBrowse/smbfs) not yet run |
| **M7** | Release Packaging | ✅ **BUILT & TESTED** (Candidate `tolunnet-1.1.0.lha` 529,589 bytes, `tolunnet.adf`) | Verified via `xdftool` |
| **M8** | Workbench Preferences GUI | ✅ **BUILT & TESTED** (GadTools GUI, screen-derived layout fitting 640×200 NTSC, Save/Use Prefs semantics, Start/Stop stack control, 4-color Depth=2 icon) | Pre-v3 GUI was verified on a live Workbench 3.0 screen; the v3 rework (TNET-062/064/065) is **build-verified only and needs re-proof on the emulator gauntlet** |

---

## Working Tree Build Artifacts (Candidate Release)

- 📦 **LhA Archive:** [`build/tolunnet-1.1.0.lha`](file:///d:/Projeler/tolunnet/build/tolunnet-1.1.0.lha) (529,589 bytes after the P0 fixes + installer grammar repair, universal `-m68000` + Depth=2 4-color icons)
- 💾 **ADF Floppy Image:** [`build/tolunnet.adf`](file:///d:/Projeler/tolunnet/build/tolunnet.adf) (901,120-byte standard DD image) — **STALE: still contains the pre-v3-P0 binaries; `make package` does not regenerate it. Rebuild the ADF before the next release.**
- 💾 **Workbench Hard Drive Sync:** `E:\amiga\Amigatolon\hdf\Workbench v3.0 (1992)(Commodore).hdf` synchronized (pre-v3 state — resync after the emulator gauntlet).

---

## v3 Bugtrack Session (2026-09-05) — P0 Group

`make all` and `make package` green in WSL (`m68k-amigaos-gcc` 6.5.0b) after the
P0 fixes (TNET-059…065, see ISSUES.md). **These rows are code-fixed and
build-verified only — none is emulator- or hardware-proven yet.** The §3.6
WinUAE gauntlet (Start/Stop cycle twice, NTSC Prefs screen, RECONFIG) is the
next gate; STATUS proof columns above are unchanged until then.
