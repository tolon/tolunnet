# TOLUNET — Master Prompt v3.0
# Open-source TCP/IP stack for classic AmigaOS (68k). Working title.
# This document is the contract. Build what it says, in order. Never claim
# untested behaviour. Conflicts: this document wins. Gaps: QUESTIONS.md.

# 0. CONTEXT
No modern open-source TCP/IP stack exists for classic AmigaOS (Roadshow:
commercial/closed. Miami: dead. AmiTCP: only 3.0b/1994 open. TheWire13:
freeware, no source, OS1.3 focus). tolunet fills the gap.
Author: tolon. Licence: GPL-3.0-or-later. `Copyright (C) 2026 tolon` in
LICENSE + README. lwIP BSD notices in THIRD_PARTY_LICENSES.md. README
notes: apps use tolunet via OpenLibrary(); closed apps are not GPL'd by it.

# 1. SCOPE
v1: (1) network task (lwIP, one Amiga task, NO_SYS=1); (2)
bsdsocket.library; (3) SANA-II glue (any driver); (4) DHCP default +
static config + shell tools; (5) TolunetPrefs MUI GUI (M8).
NOT v1 (record in README): PPP/SLIP, firewall, IPv6, TLS, >1 simultaneous
interface (design allows, ship one), Roadshow-private tags (QUESTIONS).

## 1.1 TARGET MATRIX
| Tier | CPU/OS | Delivers | When |
|---|---|---|---|
| 1 | 68020+ · OS 3.1/3.2/3.9 | full stack + GUI | v1 (M0–M9) |
| 2 | 68000/010 · OS 2.04+ | `tolunet000`: §6 pools halved, stats off, GadTools status applet if no MUI | M10 |
| 3 | 68000 · OS 1.3 | experimental; static IP first; shell only | M11 (optional) |
| — | OS4/MorphOS/AROS | not targets (native stacks) | — |
Tier 1 is never held back by 2/3. Each shipped tier has its own exit test
on its own bench config. README claims support only per-tier, with proof.

# 2. REPO LAYOUT (create in M0)
```
tolunet/
├── LICENSE  README.md  THIRD_PARTY_LICENSES.md
├── STATUS.md  ISSUES.md  QUESTIONS.md  Makefile
├── vendor/lwip/            pinned release, unmodified
├── vendor/patches/         only if unavoidable, applied by build
├── include/tolunet/protocol.h  config.h
├── src/task/               main.c dispatch.c timers.c netif_mgr.c
├── src/bsdsocket/          libinit.c socket_tab.c calls_*.c errno.c
├── src/sana2/              sana2_netif.c buffers.c
├── src/cmds/               TolunetStatus.c TolunetPing.c TolunetGet.c
├── src/common/             log.c mem.c
├── lwipopts/lwipopts.h
├── tests/host/  tests/amiga/
├── docs/  architecture.md protocol.md bench.md compat.md probes/
└── ci/
```

# 3. TOOLCHAIN
- bebbo amiga-gcc cross, Linux host (Docker image or cached CI build;
  record exact version in STATUS.md). Fallback vbcc only via QUESTIONS.md.
- Target OS 3.1+, 68020+, no FPU. `-O2 -fomit-frame-pointer -m68020
  -noixemul`. No libnix/ixemul/stdio in resident code; log via DOS.
- CI (M0): every push, every branch. Job 1: build in amiga-gcc container,
  upload artefact tree. Job 2: tests/host native.
- Sources of truth: NDK 3.2 headers; SANA-II Rev 7 spec; bsdsocket
  autodoc. Every constant/tag/struct field must be traceable to these or
  vendored lwIP. If you cannot cite it, do not type it.
- 68k is big-endian = network order; htons/htonl are identity but keep
  the macros.

# 3.1 RESOURCES
Normative:
- bsdsocket autodoc: https://wiki.amigaos.net/amiga/autodocs/bsdsocket.doc.txt
- SANA-II Rev 7: https://wiki.amigaos.net/wiki/SANA-II_Revision_7
- Exec messages/ports/tasks/signals + timer.device: wiki.amigaos.net
  (autodoc index: /wiki/AmigaOS_Manuals:_Autodocs)
- App-side usage: /wiki/Developing_Network_Applications
Build/import:
- lwIP: https://savannah.nongnu.org/projects/lwip/ (pin latest stable tag)
- amiga-gcc: https://github.com/bebbo/amiga-gcc
- SDI headers: https://aminet.net/dev/c/SDI_headers.lha
Read-only, never copy code (§4.2):
- AmiTCP 3.0b: https://aminet.net/comm/net/AmiTCP-bin-30b2.lha (+src)
- AROS bsdsocket: https://github.com/aros-development-team/AROS
Roadshow SDK 1.8 — on bench at `E:\amiga\Amigatolon\roadshow\
Roadshow-SDK-1.8`. Use as primary local reference:
- `doc/bsdsocket.doc` — the bsdsocket autodoc, locally (same text as the
  wiki URL; cite this copy).
- `doc/SANA-II.pdf`, `doc/sana2r4.html`, `doc/sana2r5.html` — SANA-II
  specs locally.
- `netinclude/` — real net headers; `netinclude/sys/errno.h` is the
  errno value source §5.1 requires. Build against these; check
  `netinclude.readme` for their redistribution terms before vendoring.
- `include/devices/sana2.h`, `sana2specialstats.h`.
- `sfd/` + `interfaces/bsdsocket.xml` — function definitions; generate
  the library jump table from the SFD, do not hand-write it.
- `locale/bsdsocket.cd` — catalog description reference for tolunet's
  own .catalog work (M8).
- `source_code/` — 4.4BSD-Lite2, libpcap, tcpdump, tftp-client, all
  BSD-licensed reference source. Reading allowed; do not import —
  tolunet's protocol code is lwIP only (§4.2).
Hardware lanes (drivers to support well):
- wifipi.device — PiStorm/Emu68 WiFi. Primary lane, M7.
- PaulaNET — https://github.com/RobSmithDev/PaulaNET. Pinned facts:
  `PaulaNET.device` standard SANA-II driver over trackdisk (mounts as
  DF1:); config via `AmigaConfig` GadTools tool (SSID/password/hostname →
  Pico flash); repo: NetDevice, AmigaConfig, MakeDiskImage, Pico, Gerber;
  tested with Roadshow; ~43 KB/s, 80–120 ms ping, runs on stock 68000
  (Tier-2 use case). Licence unclear ("All rights reserved" README line);
  human is asking the author — until answered: interface facts usable,
  no code copying. Second real-iron lane in M7; latency-diverse bench in
  M6.5.
- prism2.device + WirelessManager — PCMCIA WiFi lane.
Bench: WinUAE (https://www.winuae.net/), A2065/uaenet + slirp. Oracle:
Roadshow demo (http://roadshow.apc-tcp.de/index-en.php).
On dev bench already (`E:\amiga\Amigatolon`) — ask human to stage, do not
re-download: NDK 3.2, NDK39, DevPack (MUI 5 SDK), Roadshow SDK 1.8
AmiSSL-v5-OS3, Amelinium, amipkg, pfs3aio, wifipi packages, WinUAE,
7-Zip, amitools. OS/Kickstart media are the human's licensed copies;
the project never redistributes them.

# 4. LAW (re-read each session)
1. No invented APIs. Unsure → `/* VERIFY */` + QUESTIONS.md entry + stop.
2. No stack code copied from AmiTCP/Roadshow/Miami/AROS/PaulaNET.
   Reading for understanding allowed. lwIP is the only imported protocol
   code.
3. Milestone done = exit test RAN, output pasted in STATUS.md.
   "Compiles" is not a test. "Should work" is forbidden.
4. STATUS.md: proven / built-unproven / missing. Doc-vs-code conflict =
   fix the doc.
5. Defects = TNET-xxx in ISSUES.md immediately, with severity.
6. Small commits, imperative messages, every commit builds.
7. Session ritual: read STATUS.md → `make all` green before changes →
   work current milestone only → run it → update STATUS.md → commit.

# 5. PROTOCOL (library ⇄ task)
One task-owned MsgPort. Requests are Exec messages: library fills,
PutMsg, waits (blocking) or registers signal (M4).
```c
#define TN_PROTO_VERSION 1
typedef enum { TN_REQ_SOCKET, TN_REQ_CONNECT, TN_REQ_BIND, TN_REQ_LISTEN,
  TN_REQ_ACCEPT, TN_REQ_SEND, TN_REQ_RECV, TN_REQ_CLOSE,
  TN_REQ_SELECT_ARM, TN_REQ_SELECT_CANCEL, TN_REQ_IOCTL, TN_REQ_SETOPT,
  TN_REQ_GETOPT, TN_REQ_RESOLVE, TN_REQ_UDP_SENDTO, TN_REQ_UDP_RECVFROM,
  TN_REQ_STATUS, TN_REQ_APPLY_CONFIG, TN_REQ_SHUTDOWN } TnReqKind;
typedef struct TnRequest {
  struct Message msg;
  uint16 proto_version;   /* TN_PROTO_VERSION */
  uint16 kind;            /* TnReqKind */
  int32  sock;            /* task-side socket id */
  int32  result;          /* OUT */
  int32  err;             /* OUT errno */
  union { /* per-kind structs, added only when a milestone needs them */ } u;
} TnRequest;
```
Rules: every union field documented in docs/protocol.md same commit; task
keeps no pointers into library memory after ReplyMsg; buffers >4 KB via
task-owned copy ring (buffers.c); measure before optimising.

## 5.1 errno MAP (errno.c table; values from NDK netinclude, no literals)
ERR_OK→0 · ERR_MEM/ERR_BUF→ENOBUFS · ERR_TIMEOUT→ETIMEDOUT ·
ERR_RTE→EHOSTUNREACH · ERR_INPROGRESS→EINPROGRESS · ERR_VAL/ERR_ARG→EINVAL
· ERR_WOULDBLOCK→EWOULDBLOCK · ERR_USE→EADDRINUSE · ERR_ALREADY→EALREADY
· ERR_ISCONN→EISCONN · ERR_CONN/ERR_CLSD→ENOTCONN · ERR_ABRT→ECONNABORTED
· ERR_RST→ECONNRESET · ERR_IF→ENETDOWN. Break during blocking call →
EINTR. DNS failures set h_errno (HOST_NOT_FOUND, TRY_AGAIN…), not errno.

## 5.2 CONFIG FILE `DEVS:tolunet.config` (v1 grammar, exact)
```
# comments with #; KEY=VALUE per line; keys case-insensitive
DEVICE=wifipi.device
UNIT=0
DHCP=YES            # NO → IP/MASK/GATEWAY/DNS1 required
IP=192.168.1.40
MASK=255.255.255.0
GATEWAY=192.168.1.1
DNS1=192.168.1.1
DNS2=1.1.1.1
HOSTNAME=amiga
MTU=1500            # optional; default from driver
DEBUG=0             # 0..2; ENV:TOLUNET_DEBUG mirrors
```
Unknown keys: warn once, preserve on rewrite, never crash. Missing file:
polite error naming the file. WiFi credentials are NOT here — the driver
owns them (§M8-WiFi).

# 6. lwipopts.h START VALUES
NO_SYS=1, LWIP_SOCKET=0, LWIP_NETCONN=0 (RAW/callback API + own blocking
layer), LWIP_DHCP=1, LWIP_DNS=1, LWIP_TCP=1, LWIP_UDP=1, MEM_SIZE=64*1024,
PBUF_POOL_SIZE=24, TCP_MSS=1460, TCP_WND=8*TCP_MSS, TCP_SND_BUF=8*TCP_MSS,
LWIP_STATS only in debug builds. Timers: timer.device 100 ms tick →
sys_check_timeouts(). Tune only with measurements pasted in STATUS.md.

# 7. bsdsocket.library
## 7.0 Skeleton (settled)
- Exec runtime library: ROMTag + InitTable, SDI macros. `$VER:
  tolunet.library X.Y (dd.mm.yyyy)` accurate in every binary.
- Per-opener base clone (errno ptr, signal masks, h_errno, fd table are
  per opener). Global semaphore-protected registry for
  Obtain/ReleaseSocket handoff.
- CloseLibrary closes the opener's leaked sockets (autodoc requirement;
  confirm Roadshow parity by probe).
- Blocking calls run on caller's task: request → PutMsg →
  Wait(replysig|SIGBREAKF_CTRL_C|sbtc_breakmask); break → EINTR + cancel
  request to task (no leaked armed waits).
- Library never calls lwIP directly; it marshals only. Do not optimise
  across this boundary.
## 7.1 File name `bsdsocket.library`; internal ID "tolunet
bsdsocket.library". If another bsdsocket.library is live, refuse to start
with a clear message.
## 7.2 Function inventory by milestone
M3: socket connect send recv CloseSocket Errno SetErrnoPtr + init/cleanup.
M4: WaitSelect (fd sets + timeout + Amiga signal mask), IoctlSocket
(FIONBIO, FIONREAD), SocketBaseTagList (SBTC_ERRNOPTR, SBTC_HERRNOPTR,
SBTC_SIGIOMASK, SBTC_BREAKMASK), Shutdown, getsockopt, setsockopt,
getsockname, getpeername, Dup2Socket, ObtainSocket, ReleaseSocket,
ReleaseCopyOfSocket, non-blocking connect (EINPROGRESS).
M5: sendto recvfrom bind listen accept gethostbyname gethostbyaddr
(h_errno) getservbyname getservbyport inet_ntoa inet_addr gethostname
SetSocketSignals.
Later (QUESTIONS first): GetSocketEvents, Roadshow route/iface tags,
getprotobyname.
Semantics: autodoc first; ambiguity → probe vs Roadshow demo (§8).

# 8. TESTS
- bench.md (M0): WinUAE, clean OS 3.2, 68040, A2065/uaenet+slirp, host
  dir mounted as WORK: for logs. Every exit test reproducible from
  bench.md alone.
- Oracle: for each disputed semantic, probe program run under Roadshow
  demo AND tolunet; outputs archived in docs/probes/NNN-name/.
- Wire disputes settled by pcap capture, not prints.
- tests/host/: protocol encode/decode, socket table logic.
- STATUS.md template: snapshot table; Proven (each line links pasted
  output); Built-unproven; Next; Budgets measured.

# 9. BUDGETS
- Useful on 68020 + 4 MB + OS 3.1.
- Resident RAM (task+library+pools) ≤ 250 KB default; `TolunetStatus MEM`
  prints it; measured value in STATUS.md.
- Task stack ≥ 16 KB explicit (4 KB default overflows lwIP paths).
- Never busy-wait; idle = Wait(). Ctrl-C aborts tools cleanly; shutdown
  closes SANA-II device.
- No debug output by default; ENV:TOLUNET_DEBUG tiers 0..2.
- Timer granularity 100 ms.

# 10. MILESTONES (strict order; exit output pasted in STATUS.md)
M0 Scaffold: §2 tree, docs seeded, lwIP vendored+pinned, CI green,
bench.md written. Exit: hello task runs in WinUAE, writes a line to WORK:.
M1 SANA-II raw: open device/unit from config with S2_CopyToBuff/
S2_CopyFromBuff copyfuncs (drivers require them); S2_DEVICEQUERY (MTU,
addr); configure address; S2_ONLINE; ≥4 outstanding CMD_READ; map
S2ERR/S2WERR to logged reasons; shared open (never exclusive); clean
S2_OFFLINE+close. Exit: broadcast sent, incoming frames logged with types.
M2 IP alive: lwIP netif over M1; DHCP lease in WinUAE-slirp; ICMP echo.
Exit: lease logged on Amiga AND host `ping <amiga-ip>` replies.
M3 First socket: §7.2-M3; TolunetGet http://example.com/ → WORK:. Exit:
byte count + first lines.
M4 WaitSelect & signals: write tests/amiga/m4_expectations.md FIRST, then
implement, then probe vs Roadshow. Seed rows (extend, never shrink):
 1 WaitSelect all-NULL fdsets + sigmask = Wait(); returns 0, mask bit set.
 2 ready-readable socket → immediate return, count 1.
 3 timeout {0,0} → pure poll.
 4 SIGBREAKF_CTRL_C in blocked recv → -1 EINTR, socket still usable.
 5 non-blocking connect → EINPROGRESS; writable on completion; SO_ERROR
   read-then-clear.
 6 FIONREAD with 100 bytes queued → 100.
 7 recv after peer close → 0, not error.
 8 send on RST socket → -1 ECONNRESET.
 9 Dup2Socket: task socket lives until last reference closes.
 10 CloseLibrary with 2 open sockets → task shows both closed.
Exit: table all green + probe pairs archived.
M5 Names & datagrams: §7.2-M5; TolunetPing aminet.net resolves+pings; NTP
tool prints network time. Exit: both outputs.
M6 Real apps, in order: AmiSSL, amiget, Amelinium, smbfs. Regression
probe per fix. Exit: compat.md PASS/FAIL with logs; no unproven PASS.
M6.5 Throughput: TolunetBench TCP bulk pull vs host netcat, KB/s; same
run under Roadshow demo, same config. Exit: both numbers; tuning only
with before/after pairs.
M7 Real iron: lane A = A500 + PiStorm/Emu68 + wifipi.device; lane B =
PaulaNET card. Exit: DHCP + ping + one amiget download per lane; human
supplies logs/photos.
M8 TolunetPrefs (MUI, five pages, talks §5 protocol; TN_REQ_APPLY_CONFIG
documented here):
 1 Interface: SANA-II device picker + unit, MTU display, Online/Offline
   with live state.
 2 Network: DHCP toggle; static fields greyed under DHCP; hostname; Save
   rewrites config (§5.2 unknown-key-preserving); Apply = live reconfig.
 3 Status: address/lease+expiry, DNS, RX/TX packet+byte counters (1 s
   refresh), resident memory.
 4 WiFi: per-driver adapters — wifipi lane (write the file its docs
   name; path is a VERIFY item, never guessed); prism2+WirelessManager
   lane (same rule); PaulaNET lane (credentials on adapter: show link
   state, point to/launch AmigaConfig); unknown → manual SSID note.
   Passphrase masked, never logged. Scan only if driver documents it; no
   fake scan button. Apply = offline→online cycle.
 5 About: $VER version, copyright, GPL notice, acknowledgements (lwIP,
   RobSmithDev/PaulaNET, SANA-II/bsdsocket docs, testers).
Constraints: runs on 640×256 PAL non-RTG; keyboard navigable; stock MUI
classes only; strings in .catalog-ready table (English built-in, Turkish
first catalog); MUI absent → point to shell tools, never crash.
Exit: screenshots of five pages + config round-trip diff (only intended
keys change) + one wifipi credential write verified end-to-end.
M9 Release: Aminet-shape lha (drawer layout, Installer script,
AmigaGuide), GitHub release, honest compat.md. Runs §12.5 joint test.
Exit: clean WinUAE 3.2 + archive + README → working network incl. GUI,
no undocumented step.
M10 Tier-2 `tolunet000`: 68000/OS2.04 build per §1.1. Exit: WinUAE A600
(68000, OS 2.05) DHCP + TolunetGet.
M11 Tier-3 OS1.3 (optional): static IP first. Exit: WinUAE A500 OS1.3
ping both directions; DHCP only if it fits — state which.

# 11. QUESTIONS.md SEEDS
1 Final name; "tolunet.device" vs library-only naming.
2 Roadshow extension tags: honour which, refuse which.
3 ART Baseline catalogue row after M7.
4 OS4/MorphOS/AROS: keep porting hooks clean or ignore.
5 wifipi + WirelessManager credential file paths (VERIFY items; human
  reads driver docs on bench).
6 Tier-3 go/no-go after M10.
7 Turkish .catalog proofread by author.
8 PaulaNET licence answer from RobSmithDev; allowed integration depth.

# 12. ART INTEGRATION CONTRACT
tolunet ships in ART Baseline (PiStorm distro, OS 3.2 + 3.9 bases) and
must work there unattended.
1 `DEVS:tolunet.config` is the integration API: versioned, stable; ART
  pre-seeds it at card build (device/unit from ART hardware matrix,
  hostname from user form). Format changes need a version key + old-read/
  new-write path, same commit.
2 Unattended install: Installer script has silent all-defaults path AND
  flat-copyable layout so ART lays files into images directly. No manual
  Workbench step beyond credentials ART already collected.
3 `$VER` strings accurate in every binary (ART parses them).
4 Publish into ART's amipkg repository (two static files + signed
  archive) for on-Amiga updates.
5 Joint exit test (M9, then every release): ART builds Baseline card
  with tolunet → boots in WinUAE → network up with zero manual steps →
  amiget fetches one package. Logged in ART manifest + STATUS.md.

# 13. ROLES
Implementing model: milestones in order under §4. Claude Code: periodic
audits vs §4 + exits; files TNET-xxx. Human (tolon): QUESTIONS.md
answers, Roadshow probe runs, M7 hardware, releases.

# END — build M0, prove it, return for M1.
