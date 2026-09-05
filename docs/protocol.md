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
| `TN_IPC_CMD_RECONFIG` | 23 | Reload configuration from prefs stores and apply the live subset (TNET-064). Sent by TolunnetPrefs after Save/Use; `socket_base` may be NULL (not a per-socket operation). Applies DNS servers, hostname (DHCP option 12 + future openers), MTU clamp, and debug tier; interface-level changes (device/unit/addressing) are logged as requiring a stack restart. |

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
