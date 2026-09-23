# Changelog

## 1.2.0-rc4 (2026-09-23)

### Fixed
- **Traceroute TTL propagation**: `traceroute` now configures socket TTL per hop via `setsockopt(IP_TTL)` and `tc_cmd_traceroute` verifies propagation.
- **Installer startup line & config**: `Install_Tolunnet` emits config-driven `Run <NIL: >NIL: C:tolunnet` and writes `DEVS:tolunnet.config` directly.
- **Installer stack collision protection**: `Install_Tolunnet` detects existing network stacks, backs up `LIBS:bsdsocket.library.<stack>`, comments existing startup lines, and emits an `S:tolunnet-undo` restoration script.
- **Installer payload**: installs the full 33-command suite and updates documentation/prompts to "68000 or higher".
- **Documentation automation**: `README.guide` and `tolunnet.readme` are generated automatically from `include/version.h` and `LICENSE`.
- **ARP command cleanup**: removed unimplemented `FLUSH/S` switch from template and documentation.
- **Soak audit strictness**: soak benchmark calculates PASS/FAIL status directly from thresholds (zero Gurus, zero not ok, cycles match expected, drift <= 8 KB).
- **Setup wizard layout**: font-metric-driven layout on PAL (640×256) and NTSC (640×200), eliminating gadget and text overlaps.
- **Preferences window handling**: `TolunnetPrefs` properly attaches to public screens using `WA_PubScreen`.
- **Default network services**: `AUTOIP=NO` and `MDNS=NO` by default, eliminating unsolicited network traffic on vintage setups.

### Added
- **`usergroup.library` v4.1**: resident identity and credentials library (39 LVOs) with in-memory database and disk file fallback.
- **Setup wizard screenshots**: 6 reference screenshots embedded in `README.md` and automated via `make screenshots`.
- **License clarity**: verbatim GPL-3.0 `LICENSE` and `SPDX-License-Identifier: GPL-3.0-or-later` in all source files.

## 1.2.0-rc3 (2026-09-22)

### Fixed
- **TNET-151**: restored signal masks (`sig_int`, `sig_io`, `sig_urg`) around the LVO sweep in conformance tests, resolving `WaitSelect` insta-EINTR.
- **TNET-152**: daemon teardown and restart rewritten using `TN_IPC_CMD_STOP`; fixed `MakeLibrary`/`FreeMem` Exec library deallocation bug in `bsdsocket.library` teardown.

## 1.2.0-rc2 (2026-09-20)

### Fixed
- **TNET-115**: the 8-month 68000 freeze class — tcp_out unacked-tail
  self-cycle (vendor patch with cycle guards + iteration cap) and the
  Nagle-blocked first-data-on-fresh-connection (TCP_NODELAY on all
  connections + 4-round output+drain flush).
- **TNET-150**: deferred gethostbyname parked the client's message with
  lwIP with no daemon-side tracking — a watchdog-abandoned (and later
  freed) message was written and replied by the late DNS callback
  (heap corruption killing loopback delivery). Daemon tracks every
  deferred lookup (dns_pending), CLOSE cancels with ECONNABORTED,
  client never frees an abandoned message mid-session, late/foreign
  replies counted (dns_late_replies), Ctrl-C stop names holders and
  reaps dead-client bases.
- **TNET-141**: three wrong LVOs in cmdlib (gethostname/gethostbyname/
  gethostbyaddr called getservbyport/ReleaseCopyOfSocket/ReleaseSocket)
  — hostname, nslookup, ShowNetStatus and GetNetStatus printed garbage
  or always failed.
- **TNET-106**: TX pipelining done properly — pool of separate
  IOSana2Req + per-slot buffers (TX_QUEUE=, bench default 4, release
  default 0), never-reuse-before-reply, AbortIO/WaitIO at shutdown.
  Pool requests must carry io_Device/io_Unit and
  ios2_BufferManagement (both found the hard way); S2_ONEVENT stays off
  while the pool is active (emulated-driver event/TX interplay).
- WaitSelect signal interrupt now follows Roadshow (returns 0, zeroed
  fd_sets, errno=EINTR); tn_ipc_call verifies reply identity;
  gethostbyname maps transport failure to NULL.
- Host test suite repaired (broken since the RxPacket freelist commit)
  and the TNET-115 scatter host test realigned with the shipped
  batch-flush design.

### Added
- Commands: hostname, nslookup, whois, traceroute, nc, sntp, telnet,
  tftp, ftp, arp (real SIOCGARP table), ShowNetStatus, TolunnetControl,
  GetNetStatus, route, AddNetRoute, DeleteNetRoute, iperf,
  AddNetInterface, ConfigureNetInterface, Online, Offline,
  CheckNetConfig, NetShutdown (TNET-141/CMD suite, CLOSE §B).
- Static routing: ROUTECTL IPC + longest-prefix table + lwIP routing
  hooks (LWIP_HOOK_IP4_ROUTE_SRC / LWIP_HOOK_ETHARP_GET_GW).
- Interface control: IFCTL IPC (LIST/UP/DOWN/SET) + Roadshow-format
  DEVS:Internet/interfaces reader.
- getaddrinfo/freeaddrinfo/getnameinfo/gai_strerror BUILT in-library
  (70 BUILT / 51 STUB / 0 BROKEN).
- DIAG=YES crash capture (trap handler, RAM:tolunnet-crash.log, log
  ring buffer) for owner-side diagnosis (TNET-139 tooling).
- iperf loopback throughput number in every bench SUMMARY
  (a1200 ~5.4 MB/s, 68000 ~1.1 MB/s loopback).
- Config keys: DNS_PENDING=, DNS_RETRIES=, TX_QUEUE=.

### Known open
- TNET-139: owner-side Guru on AmiKit-class A500+PiStorm awaiting
  retest with rc2 diagnostics (docs/OWNER-RETEST.md).
- TNET-151: suite-tail loopback degradation after the GUI wizard tests
  (daemon-side; command tests reordered ahead of the wizard tail).
- TNET-152: bench restart cycle does not currently restart the daemon
  between cycles (stop signal not reaching; TNET-059/060 proof gap).

## 1.2.0-rc1 (2026-09-18)

- 13-command suite, bsdsocktest 126/142, TNET-115 dual fix, RxPacket
  freelist, wizard W4, bsdsocktest vendored and wired into the bench.

## 1.1.0

- Initial public feature set: daemon + bsdsocket.library v4.1 core,
  ping/ifconfig/netstat/wget/curl, TolunnetPrefs, first bench rig.
