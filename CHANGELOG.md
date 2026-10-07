# Changelog

## 1.2.0-rc6 (2026-10-07)

### Security

Fixes from the run-1 security audit (host ASan/UBSan tests, sandbox
harnesses, m68k cross-build, and a headless WinUAE bench on A1200/68020 and
A600/68000: SocketConformance core 112/112 both legs, bsdsocktest within
gate, net TODO 0).

- **lwIP `dns_compare_name()` out-of-bounds read** (vendored): a crafted DNS
  response whose question name continued past the queried name walked the
  compare pointer past `entry->name` and the static `dns_table`. The compare
  now stops at the end of the queried name and never steps over its
  terminator. Upstream-generic.
- **lwIP `dhcp_parse_reply()` out-of-bounds read** (vendored): an option skip
  that spanned more than one pbuf of a reassembled reply read past the
  current pbuf. The advance now loops across chained pbufs. Upstream-generic.
- **Predictable TCP ISN**: initial sequence numbers came from lwIP's default
  counter (the documented randomization was never wired). Replaced with an
  RFC 6528 keyed ISN via `LWIP_HOOK_TCP_ISN`.
- **Weak shared PRNG**: `tn_rand()` was one invertible xorshift32 whose output
  was its full state, so one observed DNS transaction id/port revealed future
  values. Replaced with a keyed, non-invertible SipHash-2-4 stream
  (`src/common/tn_csprng.c`); DNS id/port, DHCP xid and ephemeral ports no
  longer leak the generator.
- **DNS reply mis-binding**: a late answer for a cancelled `gethostbyname`
  could complete a different later lookup (matched by message pointer only).
  The callback now also matches the requested name.
- **TCP receive-queue pool exhaustion**: an established socket held the
  driver's `PBUF_POOL` pbufs, so one slow reader could pin the whole pool and
  stall all inbound traffic. Received segments are now cloned into `PBUF_RAM`.
- **Datagram receive accounting**: UDP/raw queues are bounded by bytes against
  `SO_RCVBUF`, not only packet count.
- **SANA-II `CopyToBuff` bound**: the RX copy hook clamps a driver-supplied
  length to the receive-buffer capacity, so an oversize frame cannot overflow
  the buffer.
- **tftp first-reply binding**: a GET/PUT now requires the first reply to come
  from the server's IP before locking the transfer id (RFC 1350).

## 1.2.0-rc5 (2026-10-03)

### Fixed

- **Compiler miscompile in 1.2.0-rc3 and rc4**: the Amiga GCC 6.5
  back end merged a 16-bit and a 32-bit zero store into one 32-bit
  clear, leaving half of the second variable uninitialised. In the
  published rc3/rc4 daemon this hits lwIP `tcp_write()`, where a
  garbage `concat_p` can be chained into an outgoing TCP segment
  (wrong data or memory corruption on small, back-to-back sends).
  Everything is now built with `-fno-peephole2`, a python-check fails
  if the flag is dropped, and `make toolchain-scan` lists every site
  the bad rule would touch. Reported upstream
  (AmigaPorts/m68k-amigaos-gcc#118); fix and reproducer in
  `toolchain/amiga-gcc-combine-clr/`.
- **Socket defaults**: the daemon saw the BSD header's `TCP_MSS` (512)
  instead of lwIP's 1460, so `SO_SNDBUF`, `SO_RCVBUF` and `TCP_MAXSEG`
  defaults were 512-based.
- **SANA-II station address**: an unconfigured card now uses its
  factory address (current, then factory, then a random locally
  administered one), and the address the driver actually configured is
  read back.
- **Bug sweep 2026-10-02**: TIME_WAIT pcbs are no longer touched after
  lwIP frees them; listen-socket close out-of-bounds write; blocking
  `send` parks with `SO_SNDTIMEO`; WaitSelect high word, break mask and
  app SIGIO bits; `SocketBaseTagList` returns the failing tag index;
  multi-iovec TCP `sendmsg` blocks like `send`; byte-wise client
  structure access on the 68000; frames over 1500 bytes no longer
  overrun the RX buffer; client `syslog()` reaches the daemon log.
- **Installer and undo**: the generated script's backup block really
  runs (an `(if)` takes one statement), the undo script is written
  before any copy, the old `bsdsocket.library` is parked by rename,
  copies are verified before deletes, `usergroup.library` is backed up,
  and the undo never copies a backup over a live `S:User-Startup`.
- **Commands**: tftp PUT resend, nslookup PTR bounds and transaction id,
  TolunnetGet return code on truncation, ftp Ctrl-C, telnet options,
  traceroute reply matching; Setup Wi-Fi SSID selection and safe
  `Wireless.prefs` replace.
- **usergroup.library**: DES `crypt(3)` (existing AmiTCP/Roadshow
  passwd hashes validate), per-task contexts released on last close,
  per-task `getpwent` cursor.

- **Ctrl-C interrupts blocking calls**: the CANCEL path in the library
  preserves the break signal after an aborted blocking call, wakes the
  IPC watchdog wait, drains both replies before returning, and the
  daemon never replies a dead or late client twice.
- **WaitSelect per 4.4BSD**: descriptors 0-63 handled with dual-word
  sets, `nfds==0` blocks, retries re-arm the break mask, and the tick
  budget saturates instead of wrapping.
- **Blocking recv and errno truth**: `recv` parks until data or timeout;
  `recv` on an unconnected TCP socket returns ENOTCONN instead of
  parking forever; unconnected UDP/RAW `send` reports EDESTADDRREQ (was
  ENOTCONN); connected RAW `sendto` to a new address reports EISCONN;
  oversize datagrams report EMSGSIZE.
- **68000 freeze class closed**: `shutdown(SHUT_RDWR)` detaches the
  socket before `tcp_close` so a freed pcb is never touched; slot reuse
  resets the shutdown flags and sending in CLOSE_WAIT works
  (bsdsocktest row 36 green on both profiles).
- **Daemon robustness**: early-RX rejections leave the pbuf untouched,
  queued receive bytes are capped by the rcvbuf setting, the UDP PCB
  pool is 48, command-line arguments are copied before FreeArgs, parked
  accept/connect requests are reaped silently on client death, a second
  parked STOP answers EALREADY, and the shutdown timer re-arms after a
  refused shutdown.
- **Command output numbers**: RawDoFmt `%d/%u/%x` read only 16 bits of
  LONG arguments — every command output path uses `%ld` now; `/N`
  arguments are LONG pointers; Inet_NtoA takes its address in D0;
  htons replaces the byte-swapping hs() helper.
- **`ftp`**: RFC 959 reply handling, one PASV per transfer, ls/get/put
  work, and the command returns a real exit code.
- **`nc`**: stdin can come from files, EOF half-closes the send side,
  LISTEN is a real listener, and UDP connect works.
- **`TolunnetGet`** (wget/curl): resumes only with CONTINUE, reads the
  directory entry via AllocDosObject, and stops counting at body bytes.
- **`whois`/`sntp`**: PORT option for non-standard servers.
- **`nslookup`**: PTR without SERVER asks the daemon's configured DNS
  server and says `no DNS server configured - use SERVER <ip>` when
  none is set.
- **Status tools read live daemon state**: `GetNetStatus`,
  `ShowNetStatus` and `TolunnetStatus` report the daemon's real
  address/gateway/DNS over IPC instead of guesses.
- **`usergroup.library` loads**: the RomTag/AUTOINIT stub uses exec's
  register ABI (D0=segList, A0=libBase) and the init routine is
  init-table entry 4 — the library loads and reloads cleanly.
- **Installer**: valid Installer 43 script; replaced C: files are
  backed up and the undo script restores them and never deletes a
  restored tool; undo survives missing files; novice mode runs without
  GUI; the first-run wizard configures the network at the end. The
  welcome names the exact release version (generated from
  `include/version.h`); the script refuses to run on `exec.library`
  older than V39; the Prefs tools install with their icons; the
  generated undo also removes the two preference tools and their icons
  (usergroup.library is deliberately kept - other software may use it).
- **TolunnetPrefs**: layout comes from font metrics, buttons and labels
  clear their text and stay inside the window interior, config writes
  keep the old file until the new one is fully in place
  (tn_safe_replace), and DOSBase comes from libnix.
- **Setup wizard Test page tells the truth**: in DHCP mode the gateway
  and DNS checks use what the daemon actually reports (no invented
  192.168.1.1 on foreign routers); the DNS check reports the resolved
  ANSWER address, not the address on nslookup's `Server:` line;
  skipped checks are shown SKIPPED with an honest
  "n passed, m failed, k skipped" summary; the IP row is
  titled "IP address"; DNS tries google.com, then cloudflare.com.
- **Setup wizard rendering**: the checklist wraps text pixel-measured
  without overflow (long words are hard-split, no inserted spaces) and
  measures on the screen RastPort; labels stay inside panels; mnemonic
  underscores are consumed by GadTools and never reach the screen;
  shrunken listviews keep their original bottoms.

### Added

- **Gotek two-disk ADF set**: `tolunnet-<version>-disk1.adf` and
  `-disk2.adf` (FFS DD, explicit manifest) whose union reproduces the
  package tree byte for byte, plus `build/release-assets/` bundling the
  LHA, both disks and `SHA256SUMS.txt`.
- **`Install_From_Floppies`**: plain-AmigaDOS bootstrap that copies
  disk 1 and disk 2 into one drawer (two-argument `Copy` with a
  per-disk `If WARN`/`Quit` check) and hands over to the tested
  `Install_Tolunnet` (unattended `DEST` + `NORUN` form included).
- **No invented network values**: the Manual-mode wizard fields start
  empty and the first empty/invalid field names itself on Next; the
  Prefs "Live Ping" uses the configured gateway (or the stack's
  current default gateway) and never silently pings a third party.
- **Three distinct tool icons**: TolunnetSetup, TolunnetPrefs and the
  tolunnet drawer each carry their own generated Workbench icon.
- **`$VER:` tags**: every shipped binary (daemon, all C/ commands,
  TolunnetSetup, TolunnetPrefs, usergroup.library idstring,
  bsdsocket.library idstring) now answers `Version` and Workbench
  Information with `1.2.0-rc5`.
- **Drawer icon**: the archive carries `tolunnet.info` at its top
  level, so the extracted drawer shows the tolunnet icon.
- `PORT/K/N` option for `whois` and `sntp`; real LISTEN mode for `nc`.
- Build-time `lvo-check` verifies the bsdsocket name→offset table
  against the sfd source.

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
