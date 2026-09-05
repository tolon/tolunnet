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

- **`bsdsocket.library` v4.1 runtime:** 50-vector LVO table generated from and validated against the Roadshow SDK `bsdsocket_lib.sfd` (`scripts/gen_lvo_table.py`). Semantics are implemented and build-verified; probe-vs-Roadshow oracle validation is tracked in `TOLUNNET-COMPAT.md` §4.
- **Roadshow / Miami DX Compatibility Layer (Tier 1):** Full support for `SocketBaseTagList` (-294), `getservbyname`, `getservbyport`, `getprotobyname`, `getprotobynumber`, `Inet_LnaOf`, `Inet_NetOf`, `Inet_MakeAddr`, `inet_network`, `gethostname`, `gethostid`, and `Dup2Socket`.
- **Zero-Allocation Exec IPC:** Fast message-passing between client applications and the network daemon with zero per-packet allocation overhead.
- **SANA-II Rev 7 Network Driver Interface:** Standard register trampolines (`A0/A1/D0`) with persistent BufferManagement and multi-request DMA/IO read pump.
- **Hardware-Seeded Entropy:** Cryptographically secure PRNG pool seeded from `GetSysTime` (microseconds), network MAC address, and memory pool allocations before core stack initialization.
- **Universal 68k Architecture (`-m68000 -msoft-float`):** One binary runs on all Motorola 68k processors (68000 through 68060) without an FPU; shipped binaries carry no 68020+ opcodes (see ISSUES.md TNET-074 for the `objdump` scan evidence).
- **Standard CLI Network Suite:** Includes `ping` (UDP echo probe with true round-trip timing — real ICMP ping is tracked as TNET-070), `ifconfig`, `netstat`, `wget`, and `curl` in `SYS:C/`.
- **Unified Text Configuration:** `DEVS:tolunnet.config` (`KEY=VALUE`) is the persistent source of truth — `DEVICE`, `UNIT`, `DHCP`, `IP`, `NETMASK`, `GATEWAY`, `DNS`, `DNS2`, `HOSTNAME`, `MTU`, `DEBUG` — with Amiga Prefs `Use`/`Save` semantics via `ENV:`/`ENVARC:` mirroring.
- **Native GadTools GUI Panel:** `SYS:Prefs/TolunnetPrefs` — screen-derived layout that fits a 640×200 NTSC Workbench, live Start/Stop stack control, and configuration of all keys above.
- **Floppy-Optimized Packaging:** Release ADF disk image (`build/tolunnet.adf`, 901,120-byte standard DD image) carries the ~530 KB LhA archive's install set with room to spare.

---

## CLI & GUI Tool Suite

| Command / Application | Location | Description |
|---|---|---|
| **`tolunnet`** | `SYS:C/tolunnet` | Core background TCP/IP daemon and `bsdsocket.library` provider |
| **`TolunnetPrefs`** | `SYS:Prefs/TolunnetPrefs` | Native GadTools GUI configuration panel |
| **`ping`** | `SYS:C/ping` | Round-trip echo diagnostic (UDP echo probe; ICMP via raw sockets tracked as TNET-070) |
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
