# tolunnet — step CMD: full command suite. Repo D:\Projeler\tolunnet. Rules: one commit per numbered command (build + `make test-host` + `core` bench green before each commit); STOP RULES: bench red twice in a row or 3 h without commit → STOP-REPORT.md; no `-dirty` proofs; no questions — decide by Roadshow SDK 1.8 `doc/` + AmiNetXDuo command syntax (https://github.com/tinic/AmiNetXDuo README), log `[auto]` lines in QUESTIONS.md. Do the groups strictly in order; do not start a group before the previous one's last commit is green.

Common rules for every command: AmigaDOS `ReadArgs` template, `?` prints template + one-line help, RC `RETURN_OK 0 / WARN 5 / ERROR 10 / FAIL 20`, Ctrl-C (`SIGBREAKF_CTRL_C`) → clean `CloseSocket` + RC 5, output via `FPuts/VFPrintf` only, thin client (links `cmdlib` + `bsdsocket.library`, never lwIP), no literal SDK constants, tunables = config keys. Each command gets `tc_cmd_<name>` in `SocketConformance` (core, hermetic — use the lo0 listener/resolver from TNET-111) and an entry in `README.guide` generated from its template by `scripts/gen_cmd_docs.py` → `docs/commands.md`. Archive layout: all commands into `C/`, Roadshow-syntax wrappers too.

## CMD-0 — `src/cmds/cmdlib.c/.h` (commit `feat(cmd): cmdlib`)
`tn_cmd_args(template, &opts)`, `tn_cmd_rc(rc, msg)`, `tn_cmd_resolve(name, &sin)` (numeric → `inet_addr`, else `gethostbyname`; prints "Resolving <name>…" unless `QUIET`), `tn_cmd_open_lib()` (bsdsocket v4 with clear error "tolunnet is not running — start it with TolunnetControl START"), `tn_cmd_ctrlc()` check, `tn_cmd_progress(bytes,total)`. Host test `test_cmdlib.c` (arg/rc/resolve-numeric).

## CMD-1 — easy
1. `hostname [NAME] [SAVE/S]` — show; with NAME set via `SBTC`/RECONFIG and `ENV:HostName`; `SAVE` also `ENVARC:` + `HOSTNAME=` in config.
2. `sntp [HOST] [SET/S] [OFFSET/N]` — daemon-side lwIP `LWIP_SNTP` (`SNTP_SERVER_DNS 1`, server from `NTP=` key, default `pool.ntp.org`); new IPC `SNTP_QUERY` returns UTC seconds + rtt; `SET` writes `battclock.resource` + `timer.device` (`TR_SETSYSTIME`) with `OFFSET` minutes (or `TZ=` key); prints old/new time.
3. `whois QUERY [SERVER] [PORT/N]` — TCP 43, default `whois.iana.org` then follow `refer:`.
4. `TolunnetControl START|STOP|RESTART|STATUS|STATS|RECONFIG|DIAG|VERSION` — the control front (RoadshowControl equivalent); `tolunnet <subcmd>` stays as alias; `STATUS` RC 0 running / 5 not running (scriptable).

## CMD-2 — names, ARP, routes
5. `host NAME|ADDR [TYPE=A|PTR|MX|TXT|CNAME|NS] [SERVER] [PORT/N]` — A/PTR through the library; MX/TXT/CNAME/NS via own UDP-53 query builder + parser (compression pointers, TTL shown); `nslookup` = same binary, second name.
6. `arp [SHOW/S] [DELETE ADDR] [ADD ADDR MAC] [FLUSH/S]` — new IPC `ENUMARP` (walk lwIP `etharp` table) + `ARPCTL` (`etharp_add_static_entry`/`etharp_remove_static_entry`, `etharp_cleanup_netif`).
7. `route [SHOW/S] [ADD DEST [MASK] GW] [DELETE DEST] [DEFAULT GW]` — new IPC `ROUTECTL`; until multi-netif lands, table = default gw + static entries kept in `src/task/route.c` (`LWIP_HOOK_IP4_ROUTE_SRC` consulted); un-SKIP host `test_route`. `AddNetRoute`/`DeleteNetRoute` = Roadshow/AmiNetXDuo syntax wrappers (`AddNetRoute DEFAULT GATEWAY=…`, `AddNetRoute DESTINATION=… GATEWAY=…`).

## CMD-3 — probes
8. `traceroute HOST [MAXHOPS/N=30] [FIRSTHOP/N=1] [QUERIES/N=3] [WAIT/N=3] [ICMP/S|UDP/S] [NUMERIC/S]` — raw ICMP socket (existing) + `IP_TTL`; UDP mode to port 33434+; prints hop, name (unless NUMERIC), 3 RTTs, `*` on timeout; stops at target or MAXHOPS.
9. `nc HOST PORT [UDP/S] [LISTEN/S] [ZERO/S] [TIMEOUT/N] [VERBOSE/S]` — pipe stdin↔socket with `WaitSelect` on socket + `CON:` read via `SetMode(RAW)`; `PORT` accepts range `20-25` with `ZERO` (scan, RC 0 if any open); `LISTEN` accepts one connection.
10. `iperf [SERVER/S] [CLIENT HOST] [PORT/N=5201] [SECONDS/N=10] [REVERSE/S]` — ship existing `TcpPerf` under this name; prints KB/s per second and total; used by `docs/bench.md` throughput table.

## CMD-6 — script parity (before telnet/ftp)
11. `AddNetInterface NAME [DEVICE] [UNIT/N] [DHCP/S] [ADDRESS] [NETMASK] [GATEWAY] [MTU/N] [QUIET/S]` — Roadshow syntax; with no args reads `DEVS:Internet/interfaces` (Roadshow format) or `INTERFACE0=` keys; talks to daemon via `IFCTL` IPC (add/config the netif; with one netif today it configures the primary; multi-netif later fills the array).
12. `ConfigureNetInterface NAME [UP/S|DOWN/S] [DHCP/S] [ADDRESS] [NETMASK] [GATEWAY]`; `Online NAME` / `Offline NAME` = aliases (link up/down via existing S2_ONEVENT path + `netif_set_link_*`).
13. `ShowNetStatus [INTERFACES/S] [ROUTES/S] [DNS/S] [SOCKETS/S] [FULL/S]` — human report; `GetNetStatus [ONLINE/S|ADDRESS/S|GATEWAY/S|DNS/S|LEASE/S]` — prints one value, RC 0/5 for scripts; `CheckNetConfig [FILE]` — validates `DEVS:tolunnet.config` + `DEVS:Internet/*` (syntax, address rules, device exists) RC 0/10 with line numbers; `NetShutdown [FORCE/S]` = `TolunnetControl STOP`.

## CMD-4 — interactive
14. `telnet HOST [PORT/N=23] [ESCAPE=^]]` — NVT negotiation (DO/DONT/WILL/WONT; accept ECHO, SGA; answer TTYPE=`ansi`, NAWS from console window size via `CON:` `ACTION_SCREEN_MODE`/`ConsoleSize`), CR/LF handling, escape prompt `close/quit`; `CON:` in RAW mode; 8-bit clean.
15. `tftp HOST GET|PUT FILE [LOCAL] [BLKSIZE/N]` — UDP 69, RFC 1350 octet, block-number wrap, 5 retries, `blksize` option.

## CMD-5 — ftp
16. `ftp [HOST] [PORT/N=21] [USER] [PASS] [PASSIVE/S] [ACTIVE/S] [SCRIPT file] [QUIET/S]` — interactive: `open close user pwd cd lcd ls dir get put mget mput delete mkdir rmdir bin asc pasv hash prompt size resume quit help`; passive default, active needs `bind/listen/accept` (present); `REST` resume when server advertises; `SCRIPT` runs commands from a file (Aminet mirror batch); wildcards via `MatchPattern`. Conformance: lo0 mini-FTP in `SocketConformance` (LIST/RETR/STOR/PASV) — write it in the same commit.

## CMD-7 — NetTrace (only after CMD-5)
17. `NetTrace FILE [FILTER] [SNAPLEN/N=1600] [COUNT/N] [TEXT/S]` — pcap (magic `0xa1b2c3d4`, v2.4, linktype 1, Amiga epoch +252460800) via a daemon bpf tap on the SANA-II RX/TX path (new IPC `TAP_OPEN/READ/CLOSE`, ring of N frames, config `TAP_RING=`); `FILTER` subset `host net port tcp udp icmp arp and or not`; `TEXT` decodes Ethernet/ARP/IP/ICMP/UDP/TCP one line per frame. Verify pcap in Wireshark (screenshot in `docs/`).

Finish: `fetch` = second name of `wget`; `docs/commands.md` + README.guide regenerated; ISSUES rows TNET-141… one per command with `tc_cmd_*` proof; `make package`; STATUS command table updated. Report per command: commit, bench dir, verbatim `not ok`.
