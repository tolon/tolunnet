# tolunnet Command Reference

> **CLOSE §A.1 audit (2026-09-19):** none of the 14 new commands have
> `tc_cmd_*` conformance tests yet — TNET-141 tracks this. Commands are
> proven by manual testing and the bench infrastructure runs; automated
> per-command verification is pending.

All commands install to `SYS:C/` (except Prefs tools). Every command is a thin client linking only cmdlib + bsdsocket.library — no lwIP.

## Core Network

| Command | ReadArgs Template | RC | Description |
|---------|------------------|-----|-------------|
| `tolunnet` | `[DEVICE] [UNIT] [IP NETMASK GW]` or `START/STOP/STATUS/RECONFIG` | 0/20 | TCP/IP daemon, bsdsocket.library v4.1 provider |
| `TolunnetControl` | `COMMAND/A` | 0 running / 5 not / 20 usage | Control front-end (START STOP RESTART STATUS RECONFIG VERSION) |
| `ping` | `HOST/A,COUNT/N,SIZE/N,INTERVAL/N,TTL/N,TIMEOUT/N,QUIET/S,UDP/S` | 0/10 | ICMP Echo with µs RTT, min/avg/max/mdev |
| `ifconfig` | — | 0 | Interface, MAC, IP status |
| `netstat` | — | 0 | Active sockets, routes, protocol statistics |

## File Transfer

| Command | ReadArgs Template | RC | Description |
|---------|------------------|-----|-------------|
| `wget` / `curl` | `URL/A,PORT/N,PATH,TO/K,QUIET/S` | 0/10/20 | HTTP/1.1 client (redirects, chunked, Range) |
| `tftp` | `HOST/A,GET/S,PUT/S,FILE/A,LOCAL` | 0/5/10 | TFTP (RFC 1350 octet, 5-retry timeout) |
| `ftp` | `HOST,PORT/N,USER,PASS,SCRIPT,QUIET/S` | 0/10 | Interactive FTP (PASV, cd/ls/get/put/bin/quit) |

## DNS and Lookup

| Command | ReadArgs Template | RC | Description |
|---------|------------------|-----|-------------|
| `hostname` | `HOSTNAME,SAVE/S` | 0/10 | Show system hostname |
| `nslookup` | `NAME/A,SERVER` | 0/10 | Forward (A) + reverse (PTR) DNS |
| `whois` | `QUERY/A,SERVER` | 0/5/10 | TCP-43 WHOIS query |

## Network Probe

| Command | ReadArgs Template | RC | Description |
|---------|------------------|-----|-------------|
| `traceroute` | `HOST/A,MAXHOPS/N,QUERIES/N,WAIT/N,NUMERIC/S` | 0/5 | ICMP TTL route tracing |
| `nc` | `HOST/A,PORT/N,UDP/S,LISTEN/S,TIMEOUT/N` | 0/10 | TCP/UDP pipe (netcat) |
| `arp` | `SHOW/S,FLUSH/S` | 0/5 | ARP table info |

## Time

| Command | ReadArgs Template | RC | Description |
|---------|------------------|-----|-------------|
| `sntp` | `HOST,SET/S,OFFSET/N` | 0/5/10 | NTP query, UTC date display |

## Remote Access

| Command | ReadArgs Template | RC | Description |
|---------|------------------|-----|-------------|
| `telnet` | `HOST/A,PORT/N` | 0/5/10 | TCP terminal (NVT, IAC filter) |

## Status for Scripts

| Command | ReadArgs Template | RC | Description |
|---------|------------------|-----|-------------|
| `GetNetStatus` | `ONLINE/S,ADDRESS/S,GATEWAY/S,DNS/S` | 0 online / 5 offline | Single-value output |
| `ShowNetStatus` | `INTERFACES/S,ROUTES/S,DNS/S,SOCKETS/S,FULL/S` | 0 | Human-readable report |

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
