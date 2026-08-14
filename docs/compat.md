# Application & API Compatibility Specification

> Standard compatibility specification for `tolunnet` against Roadshow SDK 1.8, AmiTCP V4, and Miami DX standards.

---

## 1. Verified Tier 1 bsdsocket.library LVOs

| LVO Vector | Bias | Status | Supported Features |
|---|---|---|---|
| `socket` | -30 | **PASS** | `AF_INET`, `SOCK_STREAM`, `SOCK_DGRAM` |
| `bind` | -36 | **PASS** | Local address binding, ephemeral port allocation |
| `listen` | -42 | **PASS** | TCP backlog listening |
| `accept` | -48 | **PASS** | Synchronous connection acceptance |
| `connect` | -54 | **PASS** | TCP connection handshake with timeout |
| `sendto` | -60 | **PASS** | UDP datagram transmission |
| `send` | -66 | **PASS** | TCP stream write with `tcp_sndbuf` chunking |
| `recvfrom` | -72 | **PASS** | UDP datagram reception with `sin_len` BSD4.4 format |
| `recv` | -78 | **PASS** | TCP stream read with EOF / `ECONNRESET` handling |
| `shutdown` | -84 | **PASS** | Full socket shutdown |
| `setsockopt` / `getsockopt` | -90 / -96 | **PASS** | `SO_REUSEADDR`, `SO_KEEPALIVE`, `TCP_NODELAY`, `SO_NONBLOCK` |
| `getsockname` / `getpeername` | -102 / -108 | **PASS** | Address retrieval |
| `IoctlSocket` | -114 | **PASS** | `FIONBIO`, `FIONREAD` non-blocking controls |
| `CloseSocket` | -120 | **PASS** | Graceful descriptor release |
| `WaitSelect` | -126 | **PASS** | Multi-descriptor multiplexing with signal checking |
| `SetSocketSignals` | -132 | **PASS** | Task signal mask configuration |
| `getdtablesize` | -138 | **PASS** | Fixed 32 descriptor capacity reporting |
| `Errno` / `SetErrnoPtr` | -162 / -168 | **PASS** | Task `errno` pointer & width management (1, 2, 4 bytes) |
| `Inet_NtoA` | -174 | **PASS** | Thread-safe dotted-decimal formatting |
| `inet_addr` / `inet_network` | -180 / -204 | **PASS** | Dotted-quad and network byte-order address conversion |
| `Inet_LnaOf` / `Inet_NetOf` | -186 / -192 | **PASS** | Class A/B/C network/host decomposition |
| `Inet_MakeAddr` | -198 | **PASS** | IP address assembly |
| `gethostbyname` | -210 | **PASS** | Dynamic DNS resolution via lwIP DNS engine |
| `getservbyname` / `getservbyport` | -234 / -240 | **PASS** | Standard network service database (HTTP, HTTPS, FTP, SSH, DNS, NTP, etc.) |
| `getprotobyname` / `getprotobynumber` | -246 / -252 | **PASS** | Standard protocol database (`ip`, `icmp`, `tcp`, `udp`) |
| `Dup2Socket` | -264 | **PASS** | Socket descriptor duplication |
| `gethostname` / `gethostid` | -282 / -288 | **PASS** | Host identity and local IP retrieval |
| `SocketBaseTagList` | -294 | **PASS** | Complete tag decoding: `SBTC_ERRNOLONGPTR`, `SBTC_HERRNOLONGPTR`, `SBTC_BREAKMASK`, `SBTC_SIGIOMASK`, `SBTC_SIGURGMASK`, `SBTC_DTABLESIZE`, and `SBTC_HAVE_*_API` capability queries |

---

## 2. Application Compatibility Matrix

| Application | Target Version | Status | Notes |
|---|---|---|---|
| **`ping` / `TolunnetPing`** | 1.1.0 | **PASS** | Real bidirectional UDP/ICMP echo round-trip with ms timing |
| **`wget` / `curl` / `TolunnetGet`** | 1.1.0 | **PASS** | HTTP 1.0 download, URL parsing, header extraction |
| **`ifconfig` / `netstat`** | 1.1.0 | **PASS** | Live configuration and routing table query |
| **`TolunnetPrefs`** | 1.1.0 | **PASS** | Native GadTools GUI configuration and live ping test |
| **AmiSSL** | 5.x | **READY** | Standard `SocketBaseTagList` and `WaitSelect` verified |
| **IBrowse** | 2.5.x | **READY** | Standard TCP stream and DNS resolver verified |
| **smbfs** | 1.x | **READY** | Standard `Dup2Socket`, `setsockopt`, and `WaitSelect` verified |
