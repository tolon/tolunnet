# tolunnet Command Reference

> **CLOSE §B (TNET-141):** every shipped command has a `tc_cmd_*` conformance
> test in `tests/amiga/SocketConformance.c` driving its exact library calls
> hermetically on loopback. Writing them found and fixed three wrong LVOs in
> cmdlib (gethostname/-282, gethostbyname/-210, gethostbyaddr/-216) — hostname,
> nslookup, ShowNetStatus and GetNetStatus were calling unrelated vectors and
> printing garbage — plus made `arp SHOW` real via a new daemon SIOCGARP
> ioctl, and replaced GetNetStatus ONLINE's DNS dependency (every DNS-less
> site was "offline") with a library-liveness check.

All commands install to `SYS:C/` (except Prefs tools). Every command is a thin client linking only cmdlib + bsdsocket.library — no lwIP.

## Core Network

| Command | ReadArgs Template | RC | Description | Conformance |
|---------|------------------|-----|-------------|-------------|
| `tolunnet` | `[DEVICE] [UNIT] [IP NETMASK GW]` or `START/STOP/STATUS/RECONFIG` | 0/20 | TCP/IP daemon, bsdsocket.library v4.1 provider |
| `TolunnetControl` | `COMMAND/A` | 0 running / 5 not / 20 usage | Control front-end (START STOP RESTART STATUS RECONFIG VERSION) | `tc_cmd_tolunnetcontrol` |
| `ping` | `HOST/A,COUNT/N,SIZE/N,INTERVAL/N,TTL/N,TIMEOUT/N,QUIET/S,UDP/S` | 0/10 | ICMP Echo with µs RTT, min/avg/max/mdev |
| `ifconfig` | — | 0 | Interface, MAC, IP status |
| `netstat` | — | 0 | Active sockets, routes, protocol statistics |

## File Transfer

| Command | ReadArgs Template | RC | Description | Conformance |
|---------|------------------|-----|-------------|-------------|
| `wget` / `curl` | `URL/A,PORT/N,PATH,TO/K,QUIET/S` | 0/10/20 | HTTP/1.1 client (redirects, chunked, Range) |
| `tftp` | `HOST/A,GET/S,PUT/S,FILE/A,LOCAL` | 0/5/10 | TFTP (RFC 1350 octet, 5-retry timeout) | `tc_cmd_tftp` |
| `ftp` | `HOST,PORT/N,USER,PASS,SCRIPT,QUIET/S` | 0/10 | Interactive FTP (PASV, cd/ls/get/put/bin/quit) | `tc_cmd_ftp` |

## DNS and Lookup

| Command | ReadArgs Template | RC | Description | Conformance |
|---------|------------------|-----|-------------|-------------|
| `hostname` | `HOSTNAME,SAVE/S` | 0/10 | Show system hostname | `tc_cmd_hostname` |
| `nslookup` | `NAME/A,SERVER` | 0/10 | Forward (A) + reverse (PTR) DNS (reverse via gethostbyaddr: STUB) | `tc_cmd_nslookup` |
| `whois` | `QUERY/A,SERVER` | 0/5/10 | TCP-43 WHOIS query | `tc_cmd_whois` |

## Network Probe

| Command | ReadArgs Template | RC | Description | Conformance |
|---------|------------------|-----|-------------|-------------|
| `traceroute` | `HOST/A,MAXHOPS/N,QUERIES/N,WAIT/N,NUMERIC/S` | 0/5 | ICMP TTL route tracing | `tc_cmd_traceroute` |
| `nc` | `HOST/A,PORT/N,UDP/S,LISTEN/S,TIMEOUT/N` | 0/10 | TCP/UDP pipe (netcat) | `tc_cmd_nc` |
| `arp` | `SHOW/S,FLUSH/S` | 0/5 | ARP table via SIOCGARP /24 scan (FLUSH RC 5) | `tc_cmd_arp` |

## Time

| Command | ReadArgs Template | RC | Description | Conformance |
|---------|------------------|-----|-------------|-------------|
| `sntp` | `HOST,SET/S,OFFSET/N` | 0/5/10 | NTP query, UTC date display | `tc_cmd_sntp` |

## Remote Access

| Command | ReadArgs Template | RC | Description | Conformance |
|---------|------------------|-----|-------------|-------------|
| `telnet` | `HOST/A,PORT/N` | 0/5/10 | TCP terminal (NVT, IAC filter) | `tc_cmd_telnet` |

## Status for Scripts

| Command | ReadArgs Template | RC | Description | Conformance |
|---------|------------------|-----|-------------|-------------|
| `GetNetStatus` | `ONLINE/S,ADDRESS/S,GATEWAY/S,DNS/S` | 0 online / 5 offline | Single-value output (ONLINE = liveness, no DNS) | `tc_cmd_getnetstatus` |
| `ShowNetStatus` | `INTERFACES/S,ROUTES/S,DNS/S,SOCKETS/S,FULL/S` | 0 | Human-readable report | `tc_cmd_shownetstatus` |

## GUI Tools (SYS:Prefs/)

| Tool | Description |
|------|-------------|
| `TolunnetPrefs` | GadTools config panel (non-blocking Start/Stop, ToolTypes) |
| `TolunnetSetup` | First-run wizard (hardware → WiFi → address → test) |
| `Install_Tolunnet` | Installer script |

## API (library)

| Function | LVO | Status |
|----------|-----|--------|
| `getaddrinfo` / `freeaddrinfo` | -810/-804 | BUILT (POSIX resolve) |
| `getnameinfo` / `gai_strerror` | -816/-822 | BUILT |
| 70 total BUILT / 51 STUB / 0 BROKEN | | See `src/lib/lib_compat_table.gen.md` |
