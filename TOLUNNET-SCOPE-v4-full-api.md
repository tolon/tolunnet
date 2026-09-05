# TOLUNNET — Scope v4: Full bsdsocket API + Complete Command Suite
# Directive (owner): tolunnet must expose EVERYTHING an AmigaOS TCP/IP stack is expected to expose —
# every bsdsocket.library vector in the Roadshow SDK SFD, ICMP/raw sockets, and the full command set —
# be 100 % compatible with Roadshow / AmiTCP V4 applications, and be measurably better than Roadshow.
# This document extends the v3 contract (TOLUNNET-BUGTRACK-v3-prompt.md) and its Round 2 file.
# Precedence: v3 §0 LAW → Round 2 → this file. Round 2 R2-A fixes ship BEFORE any work from here.

---

## 0. THE HARD FACT THAT DRIVES THIS SCOPE

`sfd/bsdsocket_lib.sfd` (Roadshow SDK 1.8) declares **133 public functions** (LVO −30 … −828).
`src/lib/lib_init.c` builds a jump table of **50** and ends the table at −300.

Any application that calls a vector beyond −300 (`GetSocketEvents`, `inet_aton`, `inet_pton`,
`getaddrinfo`, `QueryInterfaceTagList`, `GetDefaultDomainName`, `gethostbyname_r`, …) jumps into
whatever memory lies below the base → Guru. Modern ports (AmiSSL-linked curl, Amelinium, newer
IBrowse builds, Roadshow's own `C:` tools) do exactly that. **"100 % compatible" is therefore
impossible with a 50-entry table, regardless of how good those 50 are.**

Rule from now on: **the jump table is always the full SFD** — 133 vectors, generated from the SFD
by `scripts/gen_lvo_table.py` (which must fail the build if the table and the SFD disagree). Every
vector is either implemented or an *honest stub* with the correct return convention for its
signature (−1 + errno, NULL + h_errno, FALSE, or VOID) — **never missing, never `(APTR)-1`
truncation.** `SocketBaseTagList` capability tags (`SBTC_HAVE_*_API`) must report exactly what is
implemented at that build.

---

## 1. API IMPLEMENTATION MATRIX (all 133 — grouped in delivery tiers)

Legend for the "target" column: **IMPL** = fully implemented and conformance-tested;
**STUB** = honest stub, documented in `TOLUNNET-COMPAT.md` with the exact return.

### T1 — Core socket API (all IMPL; this tier closes TNET-077)
`socket bind listen accept connect sendto send recvfrom recv shutdown setsockopt getsockopt
getsockname getpeername IoctlSocket CloseSocket WaitSelect SetSocketSignals getdtablesize
Errno SetErrnoPtr Dup2Socket sendmsg recvmsg GetSocketEvents SocketBaseTagList SocketBaseTags`

Requirements inside T1:
- `socket()`: `SOCK_STREAM`, `SOCK_DGRAM`, **`SOCK_RAW` with `IPPROTO_ICMP` / `IPPROTO_RAW`**
  (lwIP `raw_pcb`; `IP_HDRINCL` for `IPPROTO_RAW`). Raw sockets are the basis of `ping`/`traceroute`.
- `setsockopt/getsockopt`: `SO_REUSEADDR SO_KEEPALIVE SO_BROADCAST SO_LINGER SO_SNDBUF SO_RCVBUF
  SO_ERROR SO_TYPE SO_OOBINLINE SO_RCVTIMEO SO_SNDTIMEO`, `TCP_NODELAY TCP_KEEPIDLE TCP_KEEPINTVL
  TCP_KEEPCNT`, `IP_TTL IP_TOS IP_HDRINCL IP_MULTICAST_TTL IP_ADD_MEMBERSHIP IP_DROP_MEMBERSHIP`
  (needs `LWIP_IGMP 1` + SANA-II `S2_ADDMULTICASTADDRESS`). Unknown option → `ENOPROTOOPT`, not ENOSYS.
- `IoctlSocket`: `FIONBIO FIONREAD FIOASYNC SIOCATMARK SIOCGIFADDR SIOCGIFNETMASK SIOCGIFBRDADDR
  SIOCGIFFLAGS SIOCGIFMTU SIOCGIFCONF SIOCADDRT SIOCDELRT` (values from `netinclude/sys/ioctl.h`
  / `net/if.h` — never literals; the current `0x8004667eUL` style must go).
- `sendmsg/recvmsg` with `struct msghdr` + `iovec` (gather/scatter over the existing send/recv path).
- `WaitSelect`: real `Wait()` on `sig_io | signals | timer` (TNET-067), 64-bit `fd_set` per NDK.
- `GetSocketEvents`: per-base event queue fed by the daemon (`FD_CONNECT FD_ACCEPT FD_READ FD_WRITE
  FD_OOB FD_CLOSE FD_ERROR` per Roadshow `bsdsocket.doc`), driven by `SBTC_SIGEVENTMASK`.
- `SocketBaseTagList`: **every** `SBTC_*` in `libraries/bsdsocket.h` (Roadshow 1.8) handled —
  `SBTC_BREAKMASK SBTC_SIGIOMASK SBTC_SIGURGMASK SBTC_SIGEVENTMASK SBTC_LOGSTAT SBTC_LOGTAGPTR
  SBTC_LOGFACILITY SBTC_LOGMASK SBTC_UDPCHECKSUM SBTC_IPDEFAULTTTL SBTC_ERRNOSTRPTR
  SBTC_HERRNOSTRPTR SBTC_IOERRNOSTRPTR SBTC_S2ERRNOSTRPTR SBTC_S2WERRNOSTRPTR SBTC_DTABLESIZE
  SBTC_FDCALLBACK SBTC_RELEASESTRPTR SBTC_HAVE_*` and the `SBTM_GETREF/GETVAL/SETREF/SETVAL` forms.
- Descriptor semantics: `ObtainSocket / ReleaseSocket / ReleaseCopyOfSocket / ProcessIsServer /
  ObtainServerSocket` (T3 below) share the daemon's socket-slot refcount already used by `Dup2Socket`.

### T2 — Resolver & address database (all IMPL)
`gethostbyname gethostbyaddr gethostbyname_r gethostbyaddr_r getnetbyname getnetbyaddr getnetent
setnetent endnetent getservbyname getservbyport getservent setservent endservent getprotobyname
getprotobynumber getprotoent setprotoent endprotoent getaddrinfo freeaddrinfo getnameinfo
gai_strerror inet_addr inet_aton inet_ntop inet_pton Inet_NtoA Inet_LnaOf Inet_NetOf Inet_MakeAddr
inet_network gethostname gethostid GetDefaultDomainName SetDefaultDomainName AddDomainNameServer
RemoveDomainNameServer ObtainDomainNameServerList ReleaseDomainNameServerList vsyslog syslog`

Requirements:
- Database files: read Roadshow/AmiTCP-compatible **`DEVS:Internet/hosts`, `networks`, `services`,
  `protocols`, `name_resolution`** (and AmiTCP `AmiTCP:db/*`) when present; ship defaults
  (IANA services/protocols) in the archive. `getservby*` / `getprotoby*` today are static tables —
  replace with the file-backed database + built-in fallback.
- `gethostbyname` must consult `hosts` first, then DNS; support `h_aliases` and multiple A records
  (`h_addr_list` up to 8); `h_errno` = `HOST_NOT_FOUND / TRY_AGAIN / NO_RECOVERY / NO_DATA` per netinclude.
- `getaddrinfo/getnameinfo`: `AF_INET` (+ `AF_INET6` when §3 IPv6 is on), `AI_PASSIVE AI_CANONNAME
  AI_NUMERICHOST NI_NUMERICHOST NI_NUMERICSERV`, allocations per-opener and freed by `freeaddrinfo`.
- `gethostid()` = live primary interface address (never a literal — TNET-078).
- `syslog/vsyslog`: to `tolunnet` log + optional UDP 514 forwarding (`LOG=` / `SYSLOG=` config keys).

### T3 — Roadshow control API (IMPL — this is what makes Roadshow's own tools and ours run)
`AddInterfaceTagList/Tags ConfigureInterfaceTagList/Tags ObtainInterfaceList ReleaseInterfaceList
QueryInterfaceTagList/Tags RemoveInterface BeginInterfaceConfig AbortInterfaceConfig
CreateAddrAllocMessage(A) DeleteAddrAllocMessage AddRouteTagList/Tags DeleteRouteTagList/Tags
ChangeRouteTagList/Tags GetRouteInfo FreeRouteInfo GetNetworkStatistics ObtainRoadshowData
ReleaseRoadshowData ChangeRoadshowData In_LocalAddr In_CanForward ObtainSocket ReleaseSocket
ReleaseCopyOfSocket ProcessIsServer ObtainServerSocket`

Requirements:
- **Multi-interface**: the daemon must own N `netif`s (lwIP supports it), each bound to a SANA-II
  device/unit, with per-interface DHCP/static, and a real routing table (lwIP has default-gw only —
  implement a small static route table in `src/task/route.c` consulted via `LWIP_HOOK_IP4_ROUTE_SRC`).
  This is what `AddRouteTagList`, `SIOCADDRT`, `route` and `netstat -r` need.
- Interface tags per `libraries/bsdsocket.h` (`IFQ_*` / `IFC_*` / `IFA_*`), `struct rt_msghdr` for
  `GetRouteInfo`, `struct NetworkStatistics` layout verbatim from the SDK.
- `ProcessIsServer/ObtainServerSocket`: inetd-style hand-off (a server launched by tolunnet's own
  `inetd` — see §2) receives the accepted socket through `ObtainSocket(id)`.

### T4 — Monitoring & filtering (IMPL where SANA-II allows, STUB otherwise — decide per row, document)
`AddNetMonitorHookTagList/Tags RemoveNetMonitorHook bpf_open bpf_close bpf_read bpf_write bpf_ioctl
bpf_data_waiting bpf_set_notify_mask bpf_set_interrupt_mask ipf_open ipf_close ipf_ioctl ipf_log_read
ipf_log_data_waiting ipf_set_notify_mask ipf_set_interrupt_mask mbuf_copym mbuf_copyback
mbuf_copydata mbuf_free mbuf_freem mbuf_get mbuf_gethdr mbuf_prepend mbuf_cat mbuf_adj mbuf_pullup`

- **bpf_***: implementable — a tap on the SANA-II RX/TX path delivering raw frames with BPF-style
  filtering (implement the classic BPF VM, ~300 lines; it is what a `tcpdump` port needs).
  `SBTC_HAVE_MONITORING_API = 1` only when this works.
- **NetMonitorHook**: hook called per packet from the daemon context — straightforward once bpf tap exists.
- **ipf_*** (IP filter/firewall) and **mbuf_***: Roadshow-internal (its BSD mbuf chain). lwIP uses
  `pbuf`. Provide a minimal `mbuf` façade over `pbuf` **only if** a real consumer is found; otherwise
  honest STUB (`-1 / ENOSYS`, `NULL`) and `SBTC_HAVE_*` = 0. Record the decision in `QUESTIONS.md`.

---

## 2. COMMAND SUITE (`SYS:C/` — must match what a Roadshow / AmiTCP user expects, then exceed it)

Each command: AmigaDOS `ReadArgs` template (no ad-hoc argv parsing), `?` help, proper return codes
(`RETURN_OK/WARN/ERROR/FAIL`), Ctrl-C abort, works from CLI and from ARexx `Address Command`.
Everything is a **thin client** of the daemon over IPC or the library — nothing links lwIP twice (TNET-027).

| Command | Purpose / parity target | Notes |
|---|---|---|
| `tolunnet` | daemon; `tolunnet START|STOP|STATUS|RECONFIG|RESTART` sub-commands | replaces `Break` usage; `STATUS` prints interfaces, routes, sockets, lwIP stats |
| `AddNetInterface` | Roadshow parity (`DEVS:Internet/interfaces` syntax + our config) | uses T3 `AddInterfaceTagList` |
| `ConfigureNetInterface` | Roadshow parity (DHCP/static/up/down per interface) | T3 |
| `ShowNetStatus` | Roadshow parity (interfaces, routes, DNS, sockets) | superset of `netstat` |
| `NetShutdown` | Roadshow parity | = `tolunnet STOP` |
| `ifconfig` | BSD-style incl. `up/down/mtu/netmask`, multi-interface | T3 |
| `netstat` | `-a -n -r -s -p tcp|udp|icmp|ip` real rows via `ENUMSOCKETS`, real routing table, real counters | TNET-071 |
| `route` | `add/delete/show/change/flush` | T3 |
| `arp` | `-a`, `-d`, `-s` (lwIP `etharp` table) | new IPC |
| `ping` | **ICMP echo**: `COUNT SIZE INTERVAL TTL TIMEOUT FLOOD QUIET` + min/avg/max/mdev, loss % | raw socket (T1) |
| `traceroute` | UDP-probe and ICMP modes, `MAXHOPS FIRSTHOP QUERIES WAIT` | raw socket |
| `nslookup` / `GetHostByName` | A / PTR / MX / TXT queries, server override | own resolver over UDP 53 (lwIP `dns` only does A) |
| `dig`-lite | optional alias of nslookup with raw record dump | |
| `wget` / `curl` | HTTP/1.1, redirects, `Content-Length`, chunked, resume, progress; `https://` when AmiSSL 5 is installed (dynamic `amisslmaster.library`) | TNET-075 + AmiSSL |
| `ftp` | classic interactive + scripted FTP client (active & passive) | needs bind/getsockname (T1) |
| `telnet` | basic NVT telnet client (line + char mode) | |
| `ntpdate` / `SetClockNTP` | SNTP, sets the Amiga clock (`battclock` + `timer.device`) | lwIP `sntp` app |
| `whois`, `finger`, `echo`/`discard` clients | small, cheap, parity | |
| `inetd` | starts servers on demand (`DEVS:Internet/inetd.conf`), hands sockets via `ObtainServerSocket` | T3 |
| `tcpdump`-lite (`NetSniff`) | bpf-based capture with text decode of Ethernet/ARP/IP/ICMP/UDP/TCP, optional pcap file output | T4 |
| `TolunnetPrefs` | full Prefs editor per v3 §4, now multi-interface, DNS list, routes tab (cycle-gadget "pages" in GadTools) | |
| `TolunnetStatus` | Workbench status window (GadTools): interfaces, lease, throughput graph (simple bars) | new |

Config compatibility: read `DEVS:Internet/interfaces`, `hosts`, `networks`, `services`, `protocols`,
`name_resolution`, `routes` if present (Roadshow layout), and `ENV:HostName`. `DEVS:tolunnet.config`
stays canonical for our own keys. Installer offers to import an existing Roadshow/AmiTCP configuration.

---

## 3. "BETTER THAN ROADSHOW" — measurable deltas (each must be demonstrable, not claimed)

1. **Open source, GPL-3**, builds in CI from a clean checkout (Roadshow is closed).
2. **IPv6 dual stack** (`LWIP_IPV6 1`: SLAAC, DHCPv6-lite via lwIP, `AF_INET6` through
   `getaddrinfo`/`socket`; SANA-II ARPv6 = NDP over Ethernet). Roadshow has none. Ship behind
   `IPV6=YES` in config; default ON only after the gauntlet passes with it on.
3. **Modern TCP**: SACK-out (`LWIP_TCP_SACK_OUT 1`), window scaling (`LWIP_WND_SCALE 1`,
   `TCP_RCV_SCALE 2`), timestamps (`LWIP_TCP_TIMESTAMPS 1`), keepalive — measured throughput on
   WinUAE A1200/030 vs Roadshow on the same bench, table in `docs/bench.md`.
4. **Built-in DHCP hostname, SNTP time sync, mDNS responder** (`LWIP_MDNS_RESPONDER` — the Amiga
   shows up as `amiga.local` on the LAN), loopback interface, multi-interface + routing.
5. **Security hardening already in `lwipopts.h`** + ISN/DNS-xid randomisation + optional
   ingress filter (drop RFC1918 from WAN interface) — documented.
6. **Native GadTools Prefs + status window**, zero MUI dependency; works on Kickstart 2.04+.
7. **Footprint**: daemon RAM budget ≤ 400 KB with default pools (report actual `AvailMem` delta),
   runs on 68000 + 2 MB Chip / 4 MB Fast; universal binary (Roadshow ships per-CPU builds).
8. **Full 133-vector API with honest capability reporting** — apps never Guru on a missing vector.

---

## 4. CONFORMANCE TEST SUITE (mandatory; grows with each tier)

- `tests/host/`: pure-logic tests (parsers, tables, route lookup, BPF VM, `getaddrinfo` flag logic).
- `tests/amiga/SocketConformance`: one Amiga binary that exercises **every** implemented vector
  against expected errno/return semantics (from Roadshow `bsdsocket.doc`), prints a TAP-style
  report to `WORK:conformance.log`. Runs on the WinUAE bench in CI-like fashion via `ci/User-Startup-Conformance`.
- `tests/amiga/Gauntlet`: scripted runs of the §2 commands + AmiSSL/IBrowse/smbfs/AWeb/YAM/AmiFTP,
  each with expected output snippets; results table in `docs/gauntlet.md` with date + build hash.
- No tier is "done" until its conformance rows PASS on the emulator bench.

---

## 5. DELIVERY ORDER (after Round 2 R2-A is merged)

1. **Full 133-vector table** with honest stubs + `gen_lvo_table.py` build assertion + capability tags
   truthful. (Small, unblocks everything, removes the Guru class.)
2. T1 complete (incl. raw ICMP, sockopts/ioctls, sendmsg/recvmsg, GetSocketEvents, real WaitSelect).
   `ping` + `traceroute` ship here.
3. T2 (resolver + database files + getaddrinfo). `nslookup`, `ntpdate`, `ftp`, `telnet`, `wget` v2.
4. Loopback + multi-netif + route table → T3 (interface/route API) → `ifconfig/route/arp/netstat/
   ShowNetStatus/AddNetInterface/ConfigureNetInterface/inetd`.
5. T4 bpf tap + `NetSniff`; decide ipf/mbuf.
6. Prefs v2 (pages), TolunnetStatus window.
7. §3 deltas: TCP options → IPv6 → mDNS/SNTP → bench numbers.
8. Each step: ISSUES rows, conformance rows, STATUS three-state ledger, `make package` (lha **and** adf).

Version plan: 1.2 = steps 1–3; 1.5 = steps 4–6; 2.0 = step 7 with IPv6 on by default.

# END — confirm Round 2 R2-A is merged, then start at §5 step 1.
