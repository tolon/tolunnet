# bench.md — Test Bench & Validation Guide

Canonical bench configuration for testing `tolunnet` on WinUAE and physical Amiga hardware.

---

## 1. Automated Test Bench (`ci/bench.sh`)

Automated dual-cycle test runs are driven by `ci/bench.sh` from Git Bash on Windows.

### Prerequisites:
- Git Bash on Windows (`"C:\Program Files\Git\bin\bash.exe"`)
- Headless WinUAE (`winuae64.exe` or `winuae.exe` in PATH or standard installation directory)
- WSL2 with `amitools` (`xdftool`) for staging HDF hard drive images
- Pristine Workbench 3.0 HDF template (`E:\amiga\Amigatolon\hdf\Workbench v3.0 (1992)(Commodore).hdf`)

### Execution:
```bash
# In Git Bash:
./ci/bench.sh
```

The script automatically executes:
1. Fresh build of binaries and Amiga-side TAP harness `SocketConformance`.
2. Staging a throwaway copy of the WB3.0 HDF into `ci/stage/`.
3. Writing the latest binaries and test scripts into the HDF using `xdftool`.
4. Launching headless WinUAE sequentially across both test configurations.
5. Capturing and verifying TAP test logs, serial logs, and MuForce output.
6. Archiving test results under `docs/bench-logs/<timestamp>-<git-hash>/`.

---

## 2. Hardware Configurations Tested

### Config 1: `a1200` (Amiga 1200 / 68EC020)
- **Configuration File:** `ci/tolunnet-a1200.uae`
- **CPU:** Motorola 68EC020 @ 14 MHz, 32-bit addressing
- **Chipset:** AGA
- **Memory:** 2 MB Chip RAM + 8 MB Fast RAM
- **Network Device:** A2065 (SANA-II) bridged to host SLIRP NAT (`10.0.2.2`)
- **OS:** AmigaOS 3.0 (Clean Commodore install)

### Config 2: `68000` (Amiga 600 / 68000)
- **Configuration File:** `ci/tolunnet-68000.uae`
- **CPU:** Motorola 68000 @ 7.09 MHz (Strict 16-bit bus, odd address alignment trap Guru `#80000003`)
- **Chipset:** ECS (NTSC 60Hz 640×200 display)
- **Memory:** 2 MB Chip RAM + 4 MB Bogo RAM
- **Network Device:** A2065 (SANA-II) bridged to host SLIRP NAT (`10.0.2.2`)
- **OS:** AmigaOS 3.1 / 3.0

---

## 3. Dual-Cycle Conformance & MuForce Verification

The conformance test harness runs **two consecutive cycles** on each boot:
- **Cycle 1:** Daemon startup, SANA-II online, DHCP lease (`10.0.2.15`), socket operations, descriptor cloning, loopback sockets, ICMP ping, WaitSelect SIGIO, 139 SFD vector exercise, and graceful shutdown.
- **Cycle 2:** Immediate daemon restart without rebooting AmigaOS, verifying driver re-open (`S2ERR_BAD_STATE` / `S2WERR_IS_CONFIGURED` tolerance), re-lease, socket recreation, and clean shutdown.

### MuForce & Memory Leak Verification:
Before and after the dual-cycle test, memory availability is tracked via `AvailMem(MEMF_PUBLIC|MEMF_CHIP)`:
- MuForce / Enforcer illegal access count: **0 hits** (Zero Guru `#80000003`, zero `AN_LibChkSum`).
- RAM Delta Gate: Net RAM consumption after 100 library opens and dual daemon cycles returns to baseline within **< 2 KB** (clean Exec memory list reclamation).

| Metric | A1200 (68EC020) | A600 (68000 ECS) |
|---|---|---|
| **CPU Architecture** | 68EC020 14MHz | 68000 7.09MHz |
| **Initial Free Fast RAM** | ~7,850 KB | ~3,820 KB |
| **Active Stack Footprint** | ~240 KB (lwIP + pools) | ~238 KB (lwIP + pools) |
| **Post-Shutdown RAM Delta** | **< 1.5 KB** | **< 1.5 KB** |
| **MuForce Hits** | **0** | **0** |
| **SFD Vectors Callable** | **139 / 139** | **139 / 139** |
| **Status** | **PASS (Dual Cycle)** | **PASS (Dual Cycle)** |

---

## 4. Throughput Numbers (`tc_iperf_loopback`)

CLOSE §B.6 / ANX-18g: `tc_iperf_loopback` (SocketConformance) moves a
nonblocking 4 KB blast between both ends of a TCP loopback pair for ~2 s
(send path + RX-freelist drain end to end). The per-profile number is
printed in the TAP stream and collected into the bench `SUMMARY.txt`.

| Profile | Loopback throughput | Evidence |
|---------|--------------------|----------|
| a1200 (68EC020) | pending green bench | — |
| 68000 (A600-class) | pending green bench | — |

The RX-freelist (TNET-107, commit 639c903) predates this measurement
tool, so no "before freelist" baseline exists; these numbers are the
baseline for the §C TX IORequest-pool work (before/after pool).

## 5. Manual Verification Commands (Workbench CLI)

### Step 1: Control tolunnet Daemon
```amiga
; Start daemon:
tolunnet START
; or in background with custom device/unit:
Run >NIL: C:tolunnet ethernet.device 0

; Query daemon status:
tolunnet STATUS

; Reload configuration:
tolunnet RECONFIG

; Stop daemon safely:
tolunnet STOP
```

### Step 2: Network Connectivity & Diagnostics
```amiga
; ICMP bidirectional ping with microsecond timing and stats:
ping 10.0.2.2 COUNT 4

; Network interface and IP status:
ifconfig

; Active sockets (real TCP/UDP/RAW rows) & routing table:
netstat
```

### Step 3: Web Fetch
```amiga
; Download file or page using HTTP/1.1 client:
wget http://aminet.net/recent.txt
curl http://aminet.net/recent.txt
```

### Step 4: Workbench Preferences GUI
```amiga
; Launch native GadTools preferences panel:
SYS:Prefs/TolunnetPrefs
```
