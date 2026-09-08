# tolunnet IPC Protocol Specification

> Single source of truth for AmigaOS IPC messaging between `bsdsocket.library` openers and the `tolunnet` network daemon.

---

## 1. Overview & Architecture

The `tolunnet` network stack runs in a single dedicated Exec task (`NO_SYS=1`), ensuring thread safety for lwIP. Client tasks accessing the network do so through `bsdsocket.library`.

When a client task calls standard BSD socket LVO vectors (e.g. `socket()`, `bind()`, `sendto()`, `recvfrom()`, `CloseSocket()`, `WaitSelect()`):
1. The 68k assembly dispatch stub (`src/lib/lib_stubs.s`) marshals register arguments into the client task's private `TnSocketBase`.
2. The client task populates `base->ipc_msg` (a zero-allocation embedded `struct TnIpcMsg`).
3. The client calls `PutMsg(base->tolunnet_port, &base->ipc_msg.msg)` and waits synchronously on its private `base->reply_port`.
4. The `tolunnet` network task processes the request within lwIP's core loop, sets `result` and `err_no`, and calls `ReplyMsg()`.
5. The client wakes up, updates its task-local `errno` (or pointer), and returns the result in `d0`/`a0`.

---

## 2. Command Codes (`TnIpcCmd`)

| Command Code | Value | Description |
|---|---|---|
| `TN_IPC_CMD_OPEN` | 0 | Client opens `bsdsocket.library` |
| `TN_IPC_CMD_CLOSE` | 1 | Client closes `bsdsocket.library` (unwinds open fds) |
| `TN_IPC_CMD_SOCKET` | 2 | `socket(domain, type, protocol)` |
| `TN_IPC_CMD_BIND` | 3 | `bind(sock, name, namelen)` |
| `TN_IPC_CMD_LISTEN` | 4 | `listen(sock, backlog)` |
| `TN_IPC_CMD_ACCEPT` | 5 | `accept(sock, addr, addrlen)` |
| `TN_IPC_CMD_CONNECT` | 6 | `connect(sock, name, namelen)` |
| `TN_IPC_CMD_SENDTO` | 7 | `sendto(sock, buf, len, flags, to, tolen)` |
| `TN_IPC_CMD_SEND` | 8 | `send(sock, buf, len, flags)` |
| `TN_IPC_CMD_RECVFROM` | 9 | `recvfrom(sock, buf, len, flags, addr, addrlen)` |
| `TN_IPC_CMD_RECV` | 10 | `recv(sock, buf, len, flags)` |
| `TN_IPC_CMD_SHUTDOWN` | 11 | `shutdown(sock, how)` |
| `TN_IPC_CMD_SETSOCKOPT` | 12 | `setsockopt(...)` |
| `TN_IPC_CMD_GETSOCKOPT` | 13 | `getsockopt(...)` |
| `TN_IPC_CMD_GETSOCKNAME` | 14 | `getsockname(...)` |
| `TN_IPC_CMD_GETPEERNAME` | 15 | `getpeername(...)` |
| `TN_IPC_CMD_IOCTL` | 16 | `IoctlSocket(...)` |
| `TN_IPC_CMD_CLOSESOCKET` | 17 | `CloseSocket(sock)` |
| `TN_IPC_CMD_GETHOSTBYNAME` | 18 | `gethostbyname(name)` |
| `TN_IPC_CMD_GETHOSTBYADDR` | 19 | `gethostbyaddr(addr, len, type)` |
| `TN_IPC_CMD_WAITSELECT` | 20 | `WaitSelect(nfds, read_fds, write_fds, except_fds, timeout, sigmask)` |
| `TN_IPC_CMD_DUP2` | 21 | `Dup2Socket(old_fd, new_fd)` |
| `TN_IPC_CMD_GETSTATUS` | 22 | Query live interface status + active socket count (TNET-043) |
| `TN_IPC_CMD_RECONFIG` | 23 | Reload configuration from prefs stores and apply the live subset (TNET-064, TNET-108). Sent by TolunnetPrefs after Save/Use and by `tolunnet RECONFIG`; `socket_base` may be NULL (not a per-socket operation). Live-applied keys: DNS/DNS2, hostname (DHCP option 12 + future openers), MTU clamp, LOGLEVEL/DEBUG tier, PRIORITY (`SetTaskPri`), LOG (close old file, open new for append), DATABASE_ORDER (netdb reload flag, §D1), SELECTORS (grow-only table resize), STATS (counter reset; NO freezes GETSTATS at zero), SYSLOG (arm/disarm RFC3164 UDP-514 forwarding, §D3). Interface-level keys (DEVICE/UNIT/DHCP/IP/NETMASK/GATEWAY) are reported as needing a stack restart. Reply: masks in `args[0..2]` (`applied`, `needs_restart`, `failed`) and, when the caller passes a buffer in `ptrs[0]` with `args[4] >= sizeof(TnReconfigResponse)`, a versioned `TnReconfigResponse` (see §3). |

---

## 3. Data Structures

```c
typedef struct TnIpcMsg {
    struct Message msg;         /* Standard Exec Message node */
    TnIpcCmd       cmd;         /* Command code */
    struct Task   *client_task; /* Calling client task pointer */
    APTR           socket_base; /* Calling SocketBase instance */
    LONG           args[6];     /* Generic integer/register arguments */
    APTR           ptrs[4];     /* Generic pointer arguments */
    LONG           result;      /* Return code (>=0 success, -1 error) */
    LONG           err_no;      /* POSIX errno if result == -1 */
} TnIpcMsg;
```

### TnReconfigResponse (TNET-108, TN_IPC_CMD_RECONFIG reply)

```c
#define TN_RECFG_VERSION 1

#define TN_RECFG_DEVICE         0x0001u   /* restart-only keys */
#define TN_RECFG_UNIT           0x0002u
#define TN_RECFG_DHCP           0x0004u
#define TN_RECFG_IP             0x0008u
#define TN_RECFG_NETMASK        0x0010u
#define TN_RECFG_GATEWAY        0x0020u
#define TN_RECFG_DNS            0x0040u   /* live-applied keys */
#define TN_RECFG_DNS2           0x0080u
#define TN_RECFG_HOSTNAME       0x0100u
#define TN_RECFG_MTU            0x0200u
#define TN_RECFG_LOGLEVEL       0x0400u
#define TN_RECFG_PRIORITY       0x0800u
#define TN_RECFG_LOG            0x1000u
#define TN_RECFG_DATABASE_ORDER 0x2000u
#define TN_RECFG_SELECTORS      0x4000u
#define TN_RECFG_STATS          0x8000u
#define TN_RECFG_SYSLOG         0x10000u

typedef struct TnReconfigResponse {
    uint16_t struct_size;   /* sizeof(TnReconfigResponse) */
    uint16_t version;       /* TN_RECFG_VERSION */
    uint32_t applied;       /* changed keys whose live re-apply succeeded */
    uint32_t needs_restart; /* changed interface keys: stop/start required */
    uint32_t failed;        /* live keys the daemon could not apply */
} TnReconfigResponse;
```

A key appears in exactly one mask: `failed` wins over `applied` for live keys;
interface keys are never `applied`. Unchanged keys appear in no mask.
