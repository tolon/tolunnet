# Application & API Compatibility Ledger

> Honest status of `bsdsocket.library` API coverage and application support
> for `tolunnet`, against Roadshow SDK 1.8 / AmiTCP V4 / Miami DX standards.
>
> Legend: **BUILT** = implemented and wired end-to-end (library vector → IPC →
> daemon handler), build-verified only. **BROKEN** = marshaled but returns
> `ENOSYS` from the daemon's `default:` arm — the call fails at runtime.
> **STUB** = library vector exists but intentionally unimplemented.
> Nothing below is probe-verified against a Roadshow oracle yet (see
> `TOLUNNET-COMPAT.md` §4); **PASS** labels are earned only after that.
>
> Audited 2026-09-05 against `src/lib/lib_vectors.c` + `src/task/main.c`.

---

## 1. bsdsocket.library LVO coverage

| LVO Vector | Bias | Status | Notes |
|---|---|---|---|
| `socket` | -30 | **BUILT** | `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM` |
| `bind` | -36 | **BROKEN (TNET-077)** | no daemon handler → `ENOSYS` |
| `listen` | -42 | **BROKEN (TNET-077)** | no daemon handler → `ENOSYS` |
| `accept` | -48 | **BROKEN (TNET-077)** | no daemon handler → `ENOSYS` |
| `connect` | -54 | **BUILT** | blocking TCP connect via async IPC reply |
| `sendto` | -60 | **BUILT** | UDP datagram transmission |
| `send` | -66 | **BUILT** | TCP write chunked by `tcp_sndbuf` |
| `recvfrom` | -72 | **BUILT** | UDP reception, BSD4.4 `sin_len` fill |
| `recv` | -78 | **BUILT** | TCP read with EOF / `ECONNRESET` |
| `shutdown` | -84 | **BROKEN (TNET-077)** | no daemon handler → `ENOSYS` |
| `setsockopt` / `getsockopt` | -90 / -96 | **BUILT (subset)** | `SO_REUSEADDR`, `SO_KEEPALIVE`, `TCP_NODELAY`, `SO_ERROR`; other options silently return 0 |
| `getsockname` | -102 | **BROKEN (TNET-077)** | no daemon handler → `ENOSYS` |
| `getpeername` | -108 | **BROKEN (TNET-077)** | no daemon handler → `ENOSYS` |
| `IoctlSocket` | -114 | **BUILT** | `FIONBIO`, `FIONREAD`; `FIOASYNC` accepted as no-op |
| `CloseSocket` | -120 | **BUILT** | graceful release, refcount-aware |
| `WaitSelect` | -126 | **BUILT** | 20 ms poll loop (TNET-041); SIGIO event delivery is TNET-067 |
| `SetSocketSignals` | -132 | **BUILT** | masks stored; delivery pending TNET-067 |
| `getdtablesize` | -138 | **BUILT** | fixed 32 descriptors per opener |
| `ObtainSocket` / `ReleaseSocket` / `ReleaseCopyOfSocket` | -144/-150/-156 | **STUB** | `-1`/`ENOSYS` |
| `Errno` / `SetErrnoPtr` | -162 / -168 | **BUILT** | errno pointer & width management (1/2/4 bytes) |
| `Inet_NtoA` | -174 | **BUILT** | per-opener buffer (TNET-008) |
| `inet_addr` | -180 | **BUILT** | full BSD 1–4-part parser (TNET-051) |
| `Inet_LnaOf` / `Inet_NetOf` | -186 / -192 | **BUILT** | class A/B/C decomposition |
| `Inet_MakeAddr` | -198 | **BUILT** | |
| `inet_network` | -204 | **BUILT** | |
| `gethostbyname` | -210 | **BUILT** | lwIP DNS + dotted-quad fast path |
| `gethostbyaddr` | -216 | **STUB** | returns NULL (implementation tracked as TNET-068) |
| `getnetbyname` / `getnetbyaddr` | -222 / -228 | **STUB** | NULL by design (documented) |
| `getservbyname` / `getservbyport` | -234 / -240 | **BUILT** | built-in services table |
| `getprotobyname` / `getprotobynumber` | -246 / -252 | **BUILT** | built-in protocols table |
| `vsyslog` | -258 | **STUB** | no-op |
| `Dup2Socket` | -264 | **BUILT** | refcounted descriptor aliasing (TNET-048) |
| `sendmsg` / `recvmsg` | -270 / -276 | **STUB** | `-1`/`ENOSYS` |
| `gethostname` / `gethostid` | -282 / -288 | **BUILT** | hostname from `HOSTNAME=` config since TNET-063 |
| `SocketBaseTagList` | -294 | **BUILT** | errno/herrno ptrs, signal masks, `SBTC_DTABLESIZE`, `SBTC_HAVE_*` capability queries |
| `GetSocketEvents` | -300 | **STUB** | `-1`/`ENOSYS` |

---

## 2. Application support matrix

Nothing in this table has been proven against real third-party applications
on the emulator or iron — that is the §3.6 gauntlet / `TOLUNNET-COMPAT.md` §4
proof matrix, still pending.

| Application | Status | Notes |
|---|---|---|
| `ping` / `TolunnetPing` | **BUILT** | UDP echo probe with real RTT/loss stats; ICMP echo needs `SOCK_RAW` (TNET-070) — most Internet hosts will not answer the UDP probe |
| `wget` / `curl` / `TolunnetGet` | **BUILT (limited)** | HTTP/1.0, no redirects, no chunked/Content-Length handling (TNET-075) |
| `ifconfig` / `netstat` | **BUILT (limited)** | live IP/mask/gw + socket count via IPC; `netstat` shows a fabricated `lo0` row (TNET-071) and no per-connection rows |
| `TolunnetPrefs` | **BUILT** | v3 rework (TNET-062/064/065) build-verified only; emulator re-proof pending |
| `TestSocket` | **BUILT** | M6 self-test suite (runs against the live daemon) |
| AmiSSL 5.x / IBrowse 2.5 / AWeb / smbfs | **NOT TESTED** | no gauntlet run yet — do not read earlier "READY" claims as proof |
| Server-style apps (ircd, ftpd, AmiTCP `listen()` users) | **CANNOT RUN** | `bind`/`listen`/`accept` return `ENOSYS` (TNET-077) |

---

## 3. What the OS knows about the stack

When `tolunnet` is running and configured by `TolunnetSetup` (or `TolunnetPrefs`), the AmigaOS system environment reflects the stack through standard system interfaces:

1. **`bsdsocket.library` in Exec `LibList`**:
   - Registered dynamically in RAM via `AddLibrary()` with standard 139-vector LVO table.
   - Applications open it via standard `OpenLibrary("bsdsocket.library", 4)`.

2. **`HostName` & `Domain` Environment Variables**:
   - `ENV:HostName` and `ENVARC:HostName`: plain hostname string without newline. Many Amiga applications (e.g. AmiTCP utilities, mail clients, IRC clients) read this before or instead of calling `gethostname()`.
   - `ENV:Domain` and `ENVARC:Domain`: search domain string if configured.

3. **`S:User-Startup` Boot Block**:
   - Clean, isolated block delimited by `; BEGIN tolunnet` and `; END tolunnet`:
     ```amiga
     ; BEGIN tolunnet
     Run <NIL: >NIL: C:tolunnet
     ; END tolunnet
     ```
   - Legacy stack lines (Miami, AmiTCP, Genesis, Roadshow) are safely backed up to `S:User-Startup.tolunnet-bak` and disabled with `; tolunnet-disabled: <line>`.

4. **Optional `DEVS:Internet/*` and `DEVS:NetInterfaces/*` Mirror**:
   - If enabled in setup, `DEVS:NetInterfaces/Ethernet` (or `WiFiPi`) is written in Roadshow-compatible KEY=VALUE syntax for interoperability with third-party tools expecting Roadshow configuration files.

