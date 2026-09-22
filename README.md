# tolunnet

> An open-source TCP/IP stack and `bsdsocket.library` v4.1 runtime for classic Commodore AmigaOS (68k), built on a pinned, hardened copy of [lwIP 2.2.0](https://savannah.nongnu.org/projects/lwip/) running as a dedicated AmigaOS Exec task (`NO_SYS=1`).

---

## Overview & Motivation

Classic AmigaOS has long lacked an actively maintained, production-grade, 100% open-source TCP/IP stack. Roadshow is proprietary commercial software. Miami and Miami Deluxe are discontinued abandonware. AmiTCP's last open-source release dates back to version 3.0b in 1994.

**tolunnet** bridges this gap: modern, fully documented, rigorously verified, and distributed under the **GNU General Public License v3.0 (GPL-3.0-or-later)**. It provides a drop-in replacement `bsdsocket.library` for classic 68k Amiga systems (from a bone-stock Amiga 500 up to accelerated Amiga 4000 and PiStorm systems), complete with standard network diagnostic tools, a modern Setup wizard, and Workbench Preferences.

- **Author:** İsmail Öztürk
- **Licence:** GPL-3.0-or-later — see [LICENSE](LICENSE).
- **lwIP Core Licence:** BSD-3-Clause — see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

> **Compatibility Notice:** Applications communicate with tolunnet strictly via the standard AmigaOS library interface: `OpenLibrary("bsdsocket.library", 4)`. Calling `bsdsocket.library` functions from any third-party application (freeware, shareware, or commercial) does **not** subject that application to the GPL.

---

## Key Architecture & Features

<!-- lvo-stats:start -->
- **`bsdsocket.library` v4.1 runtime:** 139 total LVO slots (-30 … -858; 121 SFD functions + reserved) generated from Roadshow SDK `sfd/bsdsocket_lib.sfd` via `scripts/gen_lvo_table.py`; **70 BUILT** (vector → IPC → daemon handler, or handled in-library), 51 honest stubs returning exact Roadshow error semantics (`ENOSYS`, `ENXIO`, `NULL`, `NO_RECOVERY`, `FALSE`), 0 BROKEN (marshaled without a daemon handler). A vector-by-vector status table (`src/lib/lib_compat_table.gen.md`) is generated locally by `make python-checks`.
<!-- lvo-stats:end -->
- **Roadshow 1.8 & AmiTCP V4 API:** socket primitives (`socket`, `bind`, `listen`, `accept`, `connect`, `shutdown`, `getsockname`, `getpeername`, `send`/`recv`/`sendmsg`/`recvmsg`), `SocketBaseTagList` (-294), `WaitSelect` (timer-driven, SIGIO delivery), `IoctlSocket`, `GetSocketEvents`, `Dup2Socket`, netdb (`gethostbyname`, `getservbyname`/`byport`, `getprotobyname`/`bynumber`, `getnetbyname`/`byaddr`, `getaddrinfo`/`getnameinfo`), `inet_*` helpers, `gethostname`, `gethostid` and `vsyslog`.
- **Exec message-port IPC:** client calls reach the daemon through native `PutMsg`/`WaitPort`/`ReplyMsg`. The fast path reuses one message per library base; a per-call `MEMF_PUBLIC` message is allocated only in IPC-timeout mode.
- **Universal 68k code (`-m68000 -msoft-float`):** one binary set for 68000 … 68060, no FPU required.
- **68000 alignment defence:** 4-byte memory pool alignment (`MEM_ALIGNMENT 4`) and `ETH_PAD_SIZE 2` keep IP/TCP headers aligned. `-Werror=cast-align` and `make align-check` catch `#80000003` Address Error regressions at build time.
- **SANA-II Rev 7 driver interface:** register-convention buffer-management hooks (`A0`/`A1`/`D0`), multiple outstanding reads, `S2_ONEVENT` link tracking and an optional pipelined TX request pool (`TX_QUEUE`; the default `0` is synchronous `DoIO`). Works with standard Ethernet and wireless SANA-II drivers such as `a2065.device`, `ariadne.device`, `cnet.device` and `wifipi.device`.
- **PRNG seeding:** the lwIP random source is seeded at start-up from `GetSysTime` (µs resolution) and free Chip/Fast RAM, then from the adapter MAC address once the interface is open.
- **Text configuration:** `DEVS:tolunnet.config` (`KEY=VALUE`), described under [Configuration](#configuration). `ENV:tolunnet.prefs` overrides it when newer. Changes take effect after `tolunnet RECONFIG` or `TolunnetControl RECONFIG`.
- **`usergroup.library`:** resident library (`LIBS:usergroup.library`, 39 public LVOs) for user/group identity and credentials (`getuid`, `geteuid`, `getpwuid`, `getpwnam`, `getgrnam`, `getgroups`, `crypt`, …).
  - It has a built-in in-memory database: users `root`, `amiga`, `nobody`; groups `wheel`, `staff`, `nobody`.
  - The first `passwd`/`group` file found in `AmiTCP:db/`, `DEVS:Internet/` or `DEVS:tolunnet/` overrides that database.
  - Partial implementations are listed under [Known limitations](STATUS.md#known-limitations).
- **AutoIP (RFC 3927):** when no DHCP server answers, the stack falls back to a link-local `169.254.x.x/16` address with Address Conflict Detection (lwIP DHCP/AutoIP cooperation, about 3–4 s). On by default; `AUTOIP=NO` disables it.
- **mDNS responder (RFC 6762):** answers `<hostname>.local` on `224.0.0.251:5353` and publishes a `_workstation._tcp` DNS-SD service (`model=Amiga`, `os=AmigaOS`, `stack=tolunnet`). On by default; `MDNS=NO` disables it.
- **Workbench GUI:**
  - `TolunnetSetup`: first-run wizard whose layout is computed from font metrics (`TextLength()`, `tf_YSize`). The bench verifies it on 640×200 NTSC and 640×256 PAL. The same code supports RTG screens, but the bench image has no RTG drivers to test them.
  - `TolunnetPrefs`: GadTools preferences editor with non-blocking daemon Start/Stop, ToolTypes (`TOOLPRI`, `PUBSCREEN`) and WBStartup support.

---

## Commands

The release archive ships the following programs, in `C/` unless noted. The installer copies the ones marked **I**. If you need the others, copy them from the archive's `C/` drawer to `SYS:C/`.

| Command | ReadArgs template / purpose |
|---|---|
| `tolunnet` **I** | Stack daemon and `bsdsocket.library` provider. `START/S,STOP/S,STATUS/S,RECONFIG/S,STATS/S,RAW/S,WATCH/N,DEVICE,UNIT/N,IP,NETMASK,GATEWAY`. With no arguments it reads `DEVS:tolunnet.config`. |
| `TolunnetControl` | `COMMAND/A`: `START`, `STOP`, `RESTART`, `STATUS`, `RECONFIG`, `STATS`, `VERSION`. |
| `TolunnetSetup` **I** (`SYS:Prefs/`) | First-run network wizard: hardware detection, DHCP/static setup, connection test. |
| `TolunnetPrefs` **I** (`SYS:Prefs/`) | Preferences editor with stack Start/Stop. |
| `TolunnetStatus` **I**, `ifconfig` **I**, `netstat` **I** | One read-only binary that takes no arguments. The name it is started under picks the output: stack status, interface summary, or socket/route summary. |
| `ShowNetStatus` **I** | `INTERFACES/S,ROUTES/S,DNS/S,SOCKETS/S,FULL/S` |
| `GetNetStatus` | `ONLINE/S,ADDRESS/S,GATEWAY/S,DNS/S`: status query for scripts, answered through the return code. |
| `TolunnetPing` **I**, `ping` **I** | `HOST/A,COUNT/N,SIZE/N,INTERVAL/N,TTL/N,TIMEOUT/N,QUIET/S,UDP/S`: ICMP echo with min/avg/max/mdev. |
| `TolunnetGet` **I**, `wget` **I**, `curl` **I** | `URL/A,PORT/N,PATH,TO/K,QUIET/S`: HTTP/1.1 client with 301/302/303/307/308 redirects, chunked transfer and Range resume. |
| `route` | `SHOW/S,ADD/S,DEST/K,NETMASK/K,GATEWAY/K,DELETE/S,DEFAULT/S` |
| `AddNetRoute`, `DeleteNetRoute` | `DEST/A,MASK/K,GATEWAY/K` and `DEST/A,MASK/K` (Roadshow-style names). |
| `AddNetInterface` | `FILE` (Roadshow-style interface control). |
| `ConfigureNetInterface` | `NAME/M,ADDRESS/K,NETMASK/K,GATEWAY/K,DHCP/K` |
| `Online`, `Offline` | `NAME` |
| `CheckNetConfig` | `FILE`: syntax check of a tolunnet config file. |
| `NetShutdown` | `FORCE/S`: orderly stack shutdown. |
| `arp` | `SHOW/S,FLUSH/S`: shows the ARP cache. `FLUSH` is not implemented yet. |
| `hostname` **I** | `HOSTNAME,SAVE/S` |
| `nslookup` | `NAME/A,SERVER`: forward (A) and reverse (PTR) lookups. |
| `traceroute` **I** | Sends UDP probes to port 33434 and reads ICMP replies (see [Known limitations](STATUS.md#known-limitations)). |
| `whois` | `QUERY/A,SERVER` (default server `whois.iana.org`). |
| `telnet` | `HOST/A,PORT/N`: raw TCP terminal. Telnet option negotiation (IAC/SB) is filtered out, not negotiated. |
| `nc` | `HOST/A,PORT/N,UDP/S,LISTEN/S,TIMEOUT/N` |
| `ftp` | `HOST,PORT/N,USER,PASS,SCRIPT,QUIET/S,PASVANY/S`: passive-mode FTP client. The PASV address must match the control connection's peer unless `PASVANY` is given. |
| `tftp` | `HOST/A,GET/S,PUT/S,FILE/A,LOCAL`: RFC 1350, octet mode. |
| `sntp` | `HOST,SET/S,OFFSET/N` |
| `iperf` | `CLIENT/K,SERVER/S,PORT/N,SECONDS/N`: simple TCP throughput sink/source with its own format, default port 5201. Not wire-compatible with iperf2 or iperf3. |
| `TestSocket` | Minimal socket smoke test. |
| `usergroup.library` **I** (`LIBS:`) | See [Key Architecture & Features](#key-architecture--features). |

---

## Configuration

`DEVS:tolunnet.config` holds one `KEY=VALUE` per line. `TolunnetSetup` and `TolunnetPrefs` write it.

| Key | Meaning |
|---|---|
| `DEVICE`, `UNIT` | SANA-II driver and unit |
| `DHCP` | `YES`/`NO` |
| `IP`, `NETMASK`, `GATEWAY` | static addressing (aliases `IP_ADDR`, `MASK`, `GW`) |
| `DNS`, `DNS2` | resolvers (aliases `DNS1`, `NAMESERVER`) |
| `HOSTNAME`, `MTU` | host name, interface MTU |
| `AUTOIP`, `MDNS` | link-local fallback and mDNS responder (both default on) |
| `TX_QUEUE` | TX request pool depth (`0` = synchronous, the release default) |
| `LOG`, `LOGLEVEL`, `DEBUG`, `SYSLOG` | logging |
| `PRIORITY`, `SELECTORS`, `STATS`, `S2EVENTS`, `DATABASE_ORDER` | daemon tuning |
| `DNS_PORT`, `DNS_PENDING`, `DNS_RETRIES` | resolver tuning |
| `FONT` | font for `TolunnetSetup` (default: the screen font) |

Minimal example:

```ini
DEVICE=ethernet.device
UNIT=0
DHCP=YES
DNS=1.1.1.1
LOG=NIL:
```

---

## Verification

AmigaOS has no memory protection, so one unaligned access or use-after-free ends in a Guru Meditation. Every change therefore goes through three layers of testing. [STATUS.md](STATUS.md) has the current results.

### 1. Host unit tests (`make test-host`)
The 20 test programs in `tests/host/` are built with the host compiler under AddressSanitizer and UndefinedBehaviorSanitizer (`-Werror`). They compile against the real project headers, with a mock lwIP/Exec layer underneath:

`test_config`, `test_constants`, `test_dns_pending`, `test_errstr`, `test_fdset`, `test_http`, `test_ifreader`, `test_inet_addr`, `test_ipc`, `test_ipc_dispatch`, `test_lvo_table`, `test_queues`, `test_route`, `test_sbtc`, `test_slot_table`, `test_sockaddr`, `test_sockopt`, `test_stats`, `test_usergroup`, `test_wizard_config`.

After the tests, `make test-host` runs `python-checks`: the LVO table generators must be up to date, icon formats are validated, and a gate checks the `Forbid()`/`Disable()` regions.

### 2. Emulated conformance bench (`ci/bench.sh`)
A headless WinUAE harness boots a clean Workbench 3.0 hard-disk image on Kickstart 3.1, in two profiles:
1. **a1200**: 68EC020, AGA, PAL.
2. **68000**: A600-class 68000, ECS, NTSC, Fast RAM.

Each run does two cycles in the same OS session, without rebooting:
- **Cycle 1:**
  - Start the daemon and obtain a DHCP lease.
  - Run the `SocketConformance` suite: TCP/UDP/raw ICMP, non-blocking `WaitSelect`, socket events, a call into each of the 139 LVO slots, and every bundled command.
  - Run the third-party `bsdsocktest` suite, then stop the daemon.
- **Cycle 2:** relaunch the daemon, which re-opens the SANA-II device and gets a fresh lease, then repeat the tests.

A run only counts if the tree was clean (`dirty: NO`). The harness has a MuForce/Enforcer hook, but it currently reports SKIP because the tool image is not part of the bench.

### 3. Session-profile soak (`ci/bench.sh soak`)
Hobbyist Amigas are used for a few hours at a time, so the soak models many short sessions rather than long uptime. It runs 12 cycles of 10 minutes on the a1200 profile with `TX_QUEUE=4`:
- Each cycle runs periodic loopback `ping` and `wget` requests and takes an `Avail` snapshot, then `TolunnetControl STOP` / `START`.
- The run is then reviewed for Gurus and freezes, `not ok` lines, one fresh lwIP initialisation per cycle, and Fast RAM drift (budget: 8 KB or less).

### Real hardware
The owner's target machine is an Amiga 500 with a PiStorm and `wifipi.device`. Real-hardware retests are done by hand on that machine.

---

## Building from Source

Cross-compile with `m68k-amigaos-gcc` on Linux or WSL:

```bash
git clone https://github.com/tolon/tolunnet.git
cd tolunnet

# Binaries, LhA archive and ADF image (version from include/version.h)
make package CROSS=/path/to/m68k-amigaos/bin/m68k-amigaos-

# Host unit tests (ASan/UBSan) + generator/lint checks
make test-host

# Host-gcc strict cast-alignment gate over src/
make align-check
```

Outputs in `build/`:
- `build/tolunnet-<version>.lha`: the full release, with binaries, icons, installer, `README.guide` and licences.
- `build/tolunnet.adf`: an 880 KB floppy image with only the stripped `C/` binaries and `usergroup.library`. It has no installer and no GUI icons.

---

## Installation

### Installer (recommended)
1. Extract `tolunnet-<version>.lha` to `RAM:` or any drawer.
2. Double-click **`Install_Tolunnet`**:
   - It asks for the SANA-II device name and unit, and for DHCP or static addressing.
   - It copies the programs marked **I** above plus `usergroup.library`.
   - It adds a start line to `S:User-Startup`.
3. At the end it launches `SYS:Prefs/TolunnetSetup`, which writes `DEVS:tolunnet.config` and tests the connection.

### Manual
1. Copy files from the archive:
   - the `C/` programs you want, to `SYS:C/`;
   - `Libs/usergroup.library` to `LIBS:`;
   - `TolunnetPrefs` and `TolunnetSetup` with their `.info` files to `SYS:Prefs/`.
2. Run `TolunnetSetup` once, or create `DEVS:tolunnet.config` by hand (see [Configuration](#configuration)).
3. Start the stack from `S:User-Startup`:
   ```
   Stack 32768
   Run <NIL: >NIL: C:tolunnet
   ```

---

## Status & Known Limitations

Release state, test results and known limitations are tracked in [STATUS.md](STATUS.md).

---

## License & Third-Party Credits

- **tolunnet** is Copyright (C) 2026 **İsmail Öztürk**, licensed under the **GNU General Public License v3.0 or later**. See [LICENSE](LICENSE).
- **lwIP** (BSD-3-Clause) and the other third-party material, including the Roadshow SDK headers and the BSD network headers, are listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) with their notices.
