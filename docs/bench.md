# bench.md — Test Bench & Validation Guide

Canonical bench configuration for testing `tolunnet` on WinUAE and physical Amiga hardware.

---

## 1. Emulated Test Bench (WinUAE)

- **Emulator:** WinUAE (<https://www.winuae.net/>)
- **CPU:** Motorola 68020 / 68030 / 68040 (24-bit or 32-bit addressing) — the
  shipped binaries are universal `-m68000` and also run on 68000; use an A500
  (68000, NTSC 640x200) config for the TolunnetPrefs small-screen test (§3.6 /
  TNET-062).
- **FPU:** None required (`-msoft-float`)
- **OS:** AmigaOS 3.0 / 3.1 / 3.2 (Clean Workbench install)
- **RAM:** 2 MB Chip RAM + 4–8 MB Fast RAM
- **Network Interface:** A2065 (or `uaenet.device`), connected to host slirp NAT.

---

## 2. WORK: — Host-Shared Directory

A host directory is mounted into WinUAE as **`WORK:`** so that test outputs and logs survive reboots.

### Setup Instructions:
1. Create a host folder (e.g. `E:\amiga\Amigatolon\work`).
2. In WinUAE → Hard drives → "Add Directory or Archive":
   - Device name: `WORK`
   - Volume label: `WORK`
   - Path: `E:\amiga\Amigatolon\work`
   - Mode: Read-Write.

---

## 3. Verification Commands

### Step 1: Launch tolunnet Daemon
```amiga
; Start tolunnet in background using default DEVS:tolunnet.config (or custom args):
Run >NIL: C:tolunnet ethernet.device 0
```

### Step 2: Test Network Connectivity
```amiga
; Test bidirectional ping with real RTT measurement:
ping 10.0.2.2 4

; Inspect network interface and IP configuration:
ifconfig

; Inspect active routing table and listening sockets:
netstat
```

### Step 3: Test HTTP Web Fetch
```amiga
; Download file or page using wget/curl:
wget aminet.net 80 /recent.txt
curl http://aminet.net/recent.txt
```

### Step 4: Workbench Preferences GUI
```amiga
; Launch native preferences control panel:
SYS:Prefs/TolunnetPrefs
```
