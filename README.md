# tolunnet

> An open-source, high-performance TCP/IP stack and `bsdsocket.library` v4.1 runtime for classic Commodore AmigaOS (68k), built on a pinned, unmodified copy of [lwIP 2.2.0](https://savannah.nongnu.org/projects/lwip/) running as a single dedicated Amiga task (`NO_SYS=1`).

---

## Why tolunnet?

Classic AmigaOS has lacked a modern, actively maintained, fully open-source TCP/IP stack. Roadshow is commercial and closed. Miami is discontinued. AmiTCP's last open release is 3.0b from 1994. **tolunnet** fills that gap: GPL-3.0-or-later, modern, documented, tested, and ready for release on Aminet and GitHub.

**Author:** tolon  
**Licence:** GPL-3.0-or-later — see [LICENSE](LICENSE).  
**lwIP Licence:** BSD — see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).  

> Applications communicate with tolunnet through `OpenLibrary("bsdsocket.library", 4)` — the standard AmigaOS library interface. Using tolunnet from a closed-source application does **not** GPL that application.

---

## Key Features & Architecture

- **Complete `bsdsocket.library` v4.1 API:** Standard 50-vector LVO table fully verified against Roadshow SDK `bsdsocket_lib.sfd`.
- **Roadshow / Miami DX Compatibility Layer (Tier 1):** Full support for `SocketBaseTagList` (-294), `getservbyname`, `getservbyport`, `getprotobyname`, `getprotobynumber`, `Inet_LnaOf`, `Inet_NetOf`, `Inet_MakeAddr`, `inet_network`, `gethostname`, `gethostid`, and `Dup2Socket`.
- **Zero-Allocation Exec IPC:** Fast message-passing between client applications and the network daemon with zero per-packet allocation overhead.
- **SANA-II Rev 7 Network Driver Interface:** Standard register trampolines (`A0/A1/D0`) with persistent BufferManagement and multi-request DMA/IO read pump.
- **Hardware-Seeded Entropy:** Cryptographically secure PRNG pool seeded from `GetSysTime` (microseconds), network MAC address, and memory pool allocations before core stack initialization.
- **Pure Integer Architecture (`-msoft-float`):** Runs seamlessly on standard 68EC020/68020/68030 processors without requiring an FPU coprocessor.
- **Standard CLI Network Suite:** Includes `ping` (with true bidirectional RTT timing), `ifconfig`, `netstat`, `wget`, and `curl` in `SYS:C/`.
- **Unified Text Configuration:** Single source of truth in `DEVS:tolunnet.config` (`KEY=VALUE` format) with `ENVARC:` persistent mirroring.
- **Native GadTools GUI Panel:** `SYS:Prefs/TolunnetPrefs` allows live configuration of network devices, units, DHCP vs Static IP, live ping tests, and persistent saving with zero external MUI dependencies.
- **Floppy-Optimized Packaging:** Release ADF disk image takes only **548 KB**, easily fitting standard 880 KB DD floppy disks with >330 KB free.

---

## CLI & GUI Tool Suite

| Command / Application | Location | Description |
|---|---|---|
| **`tolunnet`** | `SYS:C/tolunnet` | Core background TCP/IP daemon and `bsdsocket.library` provider |
| **`TolunnetPrefs`** | `SYS:Prefs/TolunnetPrefs` | Native GadTools GUI configuration panel |
| **`ping`** | `SYS:C/ping` | Real bidirectional round-trip time ICMP/UDP echo network diagnostic tool |
| **`ifconfig`** | `SYS:C/ifconfig` | Network interface and IP address status viewer |
| **`netstat`** | `SYS:C/netstat` | Active socket connections, routing table, and protocol statistics |
| **`wget` / `curl`** | `SYS:C/wget`, `SYS:C/curl` | HTTP client for downloading files and web pages with URL parsing |

---

## Building

Cross-compiled with `m68k-amigaos-gcc` on Linux / WSL:

```bash
# Build complete binaries, LhA release archive, and ADF disk image:
make package CROSS=/home/tolon/opt/m68k-amigaos/bin/m68k-amigaos-
```

---

## Project Structure

- `src/task/` — Network daemon core: lwIP main loop, dual timer channels, netif management, IPC dispatch
- `src/lib/` — `bsdsocket.library` LVO vectors, per-opener `SocketBase` clones, argument marshaling
- `src/sana2/` — SANA-II driver interface (`A0/A1/D0` ASM trampolines, packet queues, ring buffers)
- `src/cmds/` — CLI commands (`ping`, `ifconfig`, `netstat`, `wget`, `curl`, `TolunnetPrefs`, `TolunnetStatus`)
- `src/common/` — Logging (`tn_logf`), persistent configuration engine (`prefs.c`)
- `sfd/` — Authentic Commodore/Roadshow `bsdsocket_lib.sfd` interface specification
- `scripts/` — Automated verifiers: `gen_lvo_table.py` (SFD parser), `create_lha.py`
- `lwipopts/` — Compile-time configuration, memory pool sizing, and DoS mitigation settings
- `vendor/lwip/` — Pinned, clean lwIP 2.2.0 source tree
