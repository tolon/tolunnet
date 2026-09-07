# TOLUNNET — Round 4 Work Order: Release-Ready 1.2.0
# Repo: D:\Projeler\tolunnet (WSL: /mnt/d/Projeler/tolunnet). Governing docs (precedence):
#   docs/history/TOLUNNET-BUGTRACK-v3-prompt.md §0 LAW > docs/history/TOLUNNET-ROUND3-prompt.md §A (verification ladder L0–L4)
#   > docs/history/TOLUNNET-SCOPE-v4-full-api.md > this file.
# Goal of this round: a **public 1.2.0 release** (Aminet + GitHub) that real applications run on. Not a feature
# sprint — a "make it correct, prove it, ship it" round. Order is mandatory. Commit per lettered section.

---

## A. LAW (unchanged, plus two release rules)
1. Every change: test that FAILED before and PASSES after, or a bench log. Rung (L1…L4) stated in ISSUES.
2. `make all && make package` (lha **and** adf) green at every commit. `make test-host` green. `ci/bench.sh`
   both configs green (only explicit `# SKIP` allowed, no `not ok`) before §F.
3. **Release rule 1:** nothing ships that the §E application gauntlet has not exercised at least once.
4. **Release rule 2:** every user-visible string, doc and `.readme` describes the *shipped* behaviour. Grep for
   "pending", "TODO", "not yet", "planned" in shipped docs before tagging.
5. No `vendor/lwip` edits; flags unchanged; GadTools only; name `tolunnet`.

---

## B. LEDGER HYGIENE (30 min, commit `docs: ledger corrections`)
- ISSUES.md: **TNET-079** row text is wrong (it was the Prefs Start/Stop `GA_Text` defect, fixed via two buttons +
  `GT_SetGadgetAttrs`); the S2_TRACKTYPE removal gets its own row (**TNET-086**, with why it is safe per SANA-II
  spec: TRACKTYPE is statistics-only). **TNET-080** row says "CreateTask" — the code does `StackSwap`; fix text.
- STATUS.md M8 link points to `20260905-224422-897281c` which does not exist → `20260905-232628-897281c`.
  Add a link-checker to `make test-host` (script that verifies every `docs/bench-logs/...` link in *.md exists).
- Tree-wide grep for socket-domain/option literals (`!= 2 /* AF_INET */`, `0x8004667eUL`, `0x0004 /* SO_REUSEADDR */`
  …) → replace with `netinclude` constants. Host test `test_constants.c` asserts the values we use equal the SDK's.

---

## C. T1 COMPLETION — core socket API to 100 % (commit per sub-item, conformance test per sub-item)

| # | Work | Conformance test(s) that must go from SKIP/absent → ok |
|---|---|---|
| C1 | `setsockopt/getsockopt` full set: `SO_REUSEADDR SO_KEEPALIVE SO_BROADCAST SO_LINGER SO_SNDBUF SO_RCVBUF SO_ERROR SO_TYPE SO_OOBINLINE SO_RCVTIMEO SO_SNDTIMEO`, `TCP_NODELAY TCP_KEEPIDLE TCP_KEEPINTVL TCP_KEEPCNT`, `IP_TTL IP_TOS IP_HDRINCL IP_MULTICAST_TTL IP_ADD_MEMBERSHIP IP_DROP_MEMBERSHIP` (`LWIP_IGMP 1`, netif `NETIF_FLAG_IGMP`, SANA-II `S2_ADDMULTICASTADDRESS/S2_DELMULTICASTADDRESS` via `igmp_mac_filter`). Unknown → `ENOPROTOOPT`. | `tc_sockopt_matrix` (set→get round-trip for every option), `tc_multicast_join` (join 224.0.0.251, receive an mDNS query sent from the host) |
| C2 | `IoctlSocket` full set: `FIONBIO FIONREAD FIOASYNC SIOCATMARK SIOCGIFADDR SIOCGIFNETMASK SIOCGIFBRDADDR SIOCGIFFLAGS SIOCGIFMTU SIOCGIFCONF SIOCADDRT SIOCDELRT` with the `struct ifreq/ifconf/rtentry` layouts from `netinclude/net/if.h`, `net/route.h`. | `tc_ioctl_ifconf` (lists `lo0` + the SANA-II netif), `tc_ioctl_fionread` |
| C3 | `sendmsg/recvmsg` (`struct msghdr`, `iovec` gather/scatter, `MSG_PEEK`, `MSG_OOB` rejected with `EOPNOTSUPP` unless implemented, `MSG_DONTWAIT`). `recv` flags `MSG_PEEK` on TCP/UDP. | `tc_sendmsg_iov`, `tc_recv_peek` |
| C4 | `GetSocketEvents` + `SBTC_SIGEVENTMASK`: per-base event queue (`FD_CONNECT FD_ACCEPT FD_READ FD_WRITE FD_OOB FD_CLOSE FD_ERROR`) fed from the daemon callbacks; documented semantics from Roadshow `bsdsocket.doc`. | `tc_socket_events` (connect → FD_CONNECT, peer close → FD_CLOSE) |
| C5 | `SocketBaseTagList` — every `SBTC_*` in Roadshow 1.8 `libraries/bsdsocket.h` (`BREAKMASK SIGIOMASK SIGURGMASK SIGEVENTMASK LOGSTAT LOGTAGPTR LOGFACILITY LOGMASK UDPCHECKSUM IPDEFAULTTTL ERRNOSTRPTR HERRNOSTRPTR IOERRNOSTRPTR S2ERRNOSTRPTR S2WERRNOSTRPTR DTABLESIZE FDCALLBACK RELEASESTRPTR HAVE_*`), all four `SBTM_*` forms; `SBTC_*STRPTR` return real error strings (table in `src/common/errstr.c`, host-tested). `SBTC_FDCALLBACK` invoked on every fd create/close/dup. | host `test_sbtc` extended to the full tag list; `tc_sbtc_full` |
| C6 | `ObtainSocket / ReleaseSocket / ReleaseCopyOfSocket / ProcessIsServer / ObtainServerSocket` on the daemon's refcounted slot table (socket "parking" by unique id; `inetd` needs it in §D). | `tc_release_obtain` (task A releases, task B obtains — spawn a child process via `SystemTags`) |
| C7 | `WaitSelect` final: `EINTR` on caller signals, `except_fds` for OOB/error, fd ≥ table → `EBADF`, `nfds` honoured. | `tc_waitselect_eintr`, `tc_waitselect_badf` |
| C8 | `tc_nonblock_connect` un-SKIP: bench gains a **live TCP target** — `ci/bench.sh` starts a tiny host-side HTTP server (`python3 -m http.server` on the WinUAE slirp host, reachable as 10.0.2.2:8000) and a DNS forwarder is enabled in the `.uae` config (slirp `10.0.2.3`) so `tc_dns_a` resolves. No test may stay `not ok` on the bench after this. | `tc_nonblock_connect`, `tc_dns_a` → ok |

---

## D. T2 RESOLVER + T3 MINIMUM FOR RELEASE (commit per sub-item)

| # | Work | Tests |
|---|---|---|
| D1 | Database files: read `DEVS:Internet/hosts`, `networks`, `services`, `protocols` (Roadshow layout) and AmiTCP `AmiTCP:db/*`; ship IANA defaults in the archive (`Devs/Internet/` drawer); `get*by*`/`get*ent`/`set*ent`/`end*ent` file-backed with built-in fallback. `gethostbyname`: hosts file first, then DNS; `h_aliases`; up to 8 A records; `h_errno` values from `netinclude/netdb.h`. | host `test_netdb.c` (parser over sample files), `tc_hosts_file`, `tc_gethostbyname_multi` |
| D2 | `getaddrinfo / freeaddrinfo / getnameinfo / gai_strerror` (AF_INET; `AI_PASSIVE AI_CANONNAME AI_NUMERICHOST AI_NUMERICSERV`, `NI_NUMERICHOST NI_NUMERICSERV NI_DGRAM`), per-opener allocations. | host `test_gai.c` (flag matrix), `tc_getaddrinfo` |
| D3 | `syslog/vsyslog` → daemon log + optional UDP 514 (`SYSLOG=host` key), `SBTC_LOG*` wired. | `tc_syslog` |
| D4 | Multi-interface groundwork the release needs: daemon owns `lo0` + N SANA-II netifs (`INTERFACE0=device,unit,dhcp|ip/mask/gw` keys, Prefs handles the first only in this release), small static route table (`src/task/route.c`, `LWIP_HOOK_IP4_ROUTE_SRC`), `QueryInterfaceTagList / ObtainInterfaceList / ReleaseInterfaceList / GetRouteInfo / FreeRouteInfo / AddRouteTagList / DeleteRouteTagList / GetNetworkStatistics` implemented; `SBTC_HAVE_ROUTING_API / INTERFACE_API / STATUS_API` = 1 when done. `AddInterfaceTagList / ConfigureInterfaceTagList / RemoveInterface` implemented for the SANA-II netif type. Remaining T3/T4 vectors stay honest stubs (regenerated COMPAT table). | host `test_route` (real), `tc_interface_query`, `tc_route_add_del` |
| D5 | Commands (all `ReadArgs`, `?` help, return codes, Ctrl-C): `ifconfig` (multi-if, `up/down/mtu/netmask`), `route` (`add/delete/show/flush`), `arp` (`-a/-d/-s` over a new `ENUMARP` IPC), `netstat` (`-a -n -r -s -p tcp|udp|icmp|ip`, real counters from lwIP stats — enable `LWIP_STATS 1` in release too, it is cheap), `traceroute` (UDP + ICMP mode), `nslookup` (A/PTR/MX/TXT over own UDP-53 query builder), `ntpdate` (lwIP SNTP, sets clock via `timer.device` + `battclock.resource`), `ftp` (interactive + scripted, active/passive), `telnet` (NVT), `whois`, `finger`, `inetd` (`DEVS:Internet/inetd.conf`, hands sockets via `ObtainServerSocket`), Roadshow-parity wrappers `AddNetInterface`, `ConfigureNetInterface`, `ShowNetStatus`, `NetShutdown`. `wget/curl`: `https://` through AmiSSL 5 when `amisslmaster.library` is present (dynamic, optional). | `tests/amiga/CmdGauntlet` script: every command run with `?` and one real invocation, output snippets asserted, TAP |
| D6 | `TolunnetPrefs` v2: §4 of v3 prompt fully — `GA_Disabled` static fields under DHCP, group titles, live status line (timer-driven `GETSTATUS`), validation via shared `inet_parse`, menus (`Project/Edit/Help`), `GT_Underscore` + `GA_TabCycle`, `SIMPLE_REFRESH` decision, Ping child window; **`TolunnetStatus` Workbench window** (interfaces, lease, socket count, RX/TX counters refreshed 1 s). | bench screenshots (`winuae -s screenshot`) archived in the log dir; Prefs Save/Use/Start/Stop cycle scripted via ARexx port added to Prefs (`TOLUNNETPREFS` port: `SAVE USE START STOP QUIT`) |

---

## E. APPLICATION GAUNTLET (the release gate) — commit `docs(gauntlet): results <date>`
`ci/bench.sh gauntlet`: a third bench profile (A1200/030, 8 MB Fast, WB 3.1, slirp + host http server) where these
are installed and driven by script or, where impossible, run manually with the log/screenshot captured. Each row in
`docs/gauntlet.md`: app · version · action · expected · result · log · build hash. **All must PASS or have a filed
TNET row with a fix in this round;** a WONTFIX needs a written reason (e.g. app needs `ipf_*`).

Mandatory rows: AmiSSL 5.x (`https://` fetch via its `curl`/`HTTPS` example), IBrowse 2.5 (http page, https page,
form POST, 6 parallel image loads), AWeb-APL, YAM 2.9 (POP3/IMAP/SMTP login + one mail each way against the host's
Python `aiosmtpd`/`dovecot` container or public test accounts), AmiFTP + `ftp` (active & passive list/get/put),
smbfs (mount host share, copy 1 MB both ways), Amiget/`wget`, NTP sync, IRC client (AmIRC or WookieChat: connect,
join, message), telnet to host, `ping`/`traceroute` to an Internet host, Roadshow's own `ShowNetStatus` binary run
against **our** library (SDK's tools are the strictest API consumers), and a 24-hour soak: `wget` loop + `ping -f`
60 s bursts, no leak (`AvailMem` drift ≤ 8 KB) and no Guru, on A1200 and 68000 configs.
Also: MuForce/MuGuardianAngel pass on the 030 profile — the ADF must be obtained (Aminet `util/moni/MuForce.lha`,
`MuGuardianAngel.lha`; place under `E:\amiga\Amigatolon\tools\` and set `MUFORCE_ADF`). Zero hits required.

---

## F. RELEASE 1.2.0 (only after E is green) — commit `release: 1.2.0`, tag `v1.2.0`
- `CHANGELOG.md` (Keep-a-Changelog; sections since 1.1.0 by TNET ids), `tolunnet.readme` in Aminet format
  (`Short:` ≤ 40 chars, `Uploader:`, `Author:`, `Type: comm/net`, `Version: 1.2.0`, `Requires: OS 2.04+, SANA-II
  driver`, `Architecture: m68k-amigaos >= 2.0.4`), `README.guide` regenerated from the actual commands (`?` output
  embedded), `README.md` feature list = shipped list, compatibility table = generated COMPAT table (implemented /
  stub counts).
- Installer: import wizard for an existing Roadshow/AmiTCP configuration (`DEVS:Internet/interfaces`, `hosts`,
  `name_resolution`), `(exists)` guards, uninstall script, `Stack 32768` in User-Startup, no forced deletes outside
  our own drawers.
- Archive layout Aminet-standard: `tolunnet/` drawer with `C/`, `Devs/Internet/`, `Prefs/`, `Docs/`, `Installer`,
  `Install_tolunnet`, icons; `.lha` and `.adf`; `SHA256SUMS`; sizes in `STATUS.md`.
- STATUS.md: every milestone **EMULATOR-PROVEN** with links; hardware column honest (**IRON-PROVEN** only for what
  the owner tests on the A1200/A500 — provide him a one-page `docs/iron-test.md` checklist to run).
- GitHub: CI green on the tag (host tests + amiga build + artefacts attached to the Release), issue templates,
  `SECURITY.md`, `CONTRIBUTING.md`, license headers on every source file (GPL-3.0-or-later + lwIP BSD notice kept).
- Version bump in one place (`include/tolunnet/version.h`) used by the library id string, `SBTC_RELEASESTRPTR`,
  `tolunnet STATUS`, Prefs About, readme generator.

**Final report:** table `| TNET | rung | test/log | commit |` for all C/D rows, `docs/gauntlet.md` summary line
(N pass / M wontfix with reasons), soak results, MuForce result, release artefact sizes + SHA256, and the exact list
of SFD vectors still stubbed (from the generated table) — that list goes verbatim into the readme's "Not yet
implemented" section so users are never surprised.

# END — start with §B, then §C in order. Do not open §E until every conformance test on both configs is `ok` with no `not ok` lines.
