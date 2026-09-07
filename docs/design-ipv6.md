# IPv6-Ready Architecture & Migration Seams

**Document Version:** 1.0 (Round 4b)  
**Target Era:** 2026 Dual-Stack (IPv4 / IPv6) on AmigaOS 68k  
**Reference:** `TOLUNNET-ROUND4b-prompt.md` §L

---

## 1. Architectural Principles

When `LWIP_IPV6=1` is enabled in a future release, the stack must support dual-stack operation without requiring redesign of the task architecture, IPC protocol, or public `bsdsocket.library` API.

To prevent technical debt and breaking changes, all internal structures introduced in Round 4b adhere to the following invariants:
1. **Generic IP Address Storage:** Daemon state and interface structures use `ip_addr_t` rather than `ip4_addr_t`.
2. **Versioned Client Interfacing:** Structs exchanged across IPC boundaries (`GETSTATUS`, `ENUMSOCKETS`, `GETSTATS`) carry a leading `struct_size` field, an explicit `family` field (`AF_INET` or `AF_INET6`), and contiguous 16-byte address storage.
3. **Centralized Sockaddr Marshaling:** All transformations between BSD `struct sockaddr` / `sockaddr_in` / `sockaddr_in6` and lwIP `ip_addr_t` pass through `src/common/sockaddr_util.c`.

---

## 2. Prepared Seams

### 2.1 Interface Manager (`src/task/netif_mgr.h` / `netif_mgr.c`)
- The interface array `TnNetif ifs[TN_MAX_NETIF]` maintains:
  - `uint8_t family;`
  - `uint8_t addr_count;`
  - `ip_addr_t addrs[4];` (supports primary IPv4, IPv6 link-local `fe80::/10`, and global unicast `2000::/3`).
- Netif lookups and packet routing will evaluate destination prefix matches against both address families.

### 2.2 IPC Telemetry & Enumeration (`include/ipc.h`)
- **`TnSocketInfoV2`**: Contains `uint16_t struct_size`, `uint8_t family`, `uint8_t proto`, `uint8_t state`, `uint8_t local_addr[16]`, and `uint8_t remote_addr[16]`.
- **`TnStatusInfoV2`**: Contains `uint16_t struct_size`, `uint8_t family`, `uint8_t ip_addr[16]`, `uint8_t netmask[16]`, `uint8_t gw[16]`, `uint8_t dns1[16]`, `uint8_t dns2[16]`.
- Legacy callers continue using `args[0..3]` and `TnSocketInfo` (IPv4-only), while modern callers negotiate V2 via `imsg->args` buffer size tags.

### 2.3 Address Conversion Utility (`src/common/sockaddr_util.c`)
- `tn_ip_from_sockaddr(const struct sockaddr *sa, socklen_t salen, ip_addr_t *out_ip, uint16_t *out_port)`:
  - Validates `sa->sa_family`. Maps `AF_INET` into `IPADDR_TYPE_V4` and prepares `AF_INET6` into `IPADDR_TYPE_V6`.
- `tn_sockaddr_from_ip(struct sockaddr *sa, socklen_t *salen, const ip_addr_t *ip, uint16_t port)`:
  - Populates `sockaddr_in` or `sockaddr_in6` based on `IP_IS_V6(ip)`.

---

## 3. File Delta Manifest for `LWIP_IPV6=1`

When `LWIP_IPV6` is activated, the following files will be updated:

| File | Planned Modification |
|---|---|
| `lwipopts/lwipopts.h` | Set `#define LWIP_IPV6 1`, `#define LWIP_IPV6_DHCP6 1`, `#define LWIP_IPV6_AUTOCONFIG 1`. |
| `src/common/sockaddr_util.c` | Enable `#if LWIP_IPV6` branches to marshal `struct sockaddr_in6` (`sin6_addr`, `sin6_scope_id`). |
| `src/task/netif_mgr.c` | Enable `netif_create_ip6_linklocal_address()` and DHCPv6 / SLAAC state machines. |
| `src/task/ipc_socket.c` | Allow `AF_INET6` domain in `socket()` and `setsockopt(..., IPV6_V6ONLY, ...)`. |
| `src/task/ipc_tcp.c` | Connect and bind to IPv6 endpoints; handle dual-stack listening sockets. |
| `src/task/ipc_dgram.c` | Handle IPv6 UDP endpoints and ICMPv6 raw sockets (`IPPROTO_ICMPV6`). |
| `src/task/ipc_netdb.c` | Enable AAAA DNS queries via `dns_gethostbyname_addrtype` (`LWIP_DNS_ADDRTYPE_IPV4_IPV6`). |
| `src/lib/lib_vectors.c` | Wire `getaddrinfo` / `getnameinfo` (Roadshow SDK vectors) to parse `sockaddr_in6`. |
