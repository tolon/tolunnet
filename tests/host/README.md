# tests/host/

Host-compilable (native gcc/clang) unit tests for TolunNet logic under AddressSanitizer and UndefinedBehaviorSanitizer (`-fsanitize=address,undefined -Wall -Wextra -Werror`).

## Architectural Philosophy & Real Header Isolation

Host tests do not use mocked reimplementations of project data structures. Instead, they compile directly against **real TolunNet and BSD headers**:

| Header | Role & Contents |
|--------|-----------------|
| `include/ipc.h` | Real IPC wire protocol definitions (`TnIpcMsg`, command constants `CMD_*`, result codes). |
| `include/netinclude/sys/socket.h` | BSD socket API definitions (`sockaddr_in`, `socklen_t`, `AF_INET`, `SOCK_STREAM`, etc.). |
| `include/netinclude/sys/errno.h` | Standard BSD / AmigaOS errno constants (`EBADF`, `EWOULDBLOCK`, `ENFILE`, etc.). |
| `include/netinclude/netinet/in.h` | IP protocol numbers (`IPPROTO_TCP`, `IPPROTO_UDP`, etc.) and IP address structures. |
| `src/task/task_ctx.h` | Real daemon context (`TnDaemon`, `TnNetif`, `TnSocketSlot`, `TnSocketBase`, `TnRxPacket`, `TnAcceptEntry`). Conditionally loads real headers on AmigaOS vs `mock_lwip.h` on host. |
| `src/task/slot_table.h` | Real slot table and queue management declarations (`tn_slot_alloc`, `tn_rx_queue_*`, `tn_accept_queue_*`, `tn_record_socket_event`). |

### Mock lwIP & Exec Harness (`mock_lwip.[ch]`)

When compiling for host (`!defined(TN_AMIGA_BUILD)`), `mock_lwip.[ch]` provides:
- Genuine linked pbuf chain allocations, partial-read boundary traversals (`pbuf_copy_partial`), and recursive chain free (`pbuf_free`).
- Call recording ring buffer (`mock_lwip_record`, `mock_lwip_call_count`, `mock_lwip_last_call`) tracking lwIP calls (`tcp_abort`, `tcp_close`, `tcp_write`, `udp_sendto`, etc.).
- AmigaOS memory and task primitives (`AllocVec`, `FreeVec`, `ReplyMsg`, `Signal`).

## Test Suites

All test suites emit TAP (Test Anything Protocol) output, most of them through `tests/host/tn_test.h`.
Several tests `#include` the daemon or library `.c` file under test directly (e.g. `ipc_dispatch.c`,
`ipc_getsockopt.c`, `ug_*.c`). The Makefile picks up every `tests/host/test_*.c` automatically.

| Binary | Source | Covers |
|--------|--------|--------|
| `test_config` | `test_config.c` | Config file parser & writer round-tripping both keyword dialects (`NETMASK`/`MASK`, `GATEWAY`/`GW`, `DNS`/`NAMESERVER`). |
| `test_constants` | `test_constants.c` | Protocol constants and flag verification. |
| `test_dns_pending` | `test_dns_pending.c` | Deferred DNS request tracking and safe cancel on close. |
| `test_errstr` | `test_errstr.c` | Error string translations across BSD errnos. |
| `test_fdset` | `test_fdset.c` | `fd_set` bit manipulation and bounds checks. |
| `test_http` | `test_http.c` | HTTP URL parsing, port extraction, response header parsing. |
| `test_ifreader` | `test_ifreader.c` | Roadshow-style interface file reader. |
| `test_inet_addr` | `test_inet_addr.c` | `inet_addr` / `inet_aton` parser: decimal, octal, hex, multi-part input, and rejection of invalid formats. |
| `test_ipc` | `test_ipc.c` | Client-side IPC message construction. |
| `test_ipc_dispatch`| `test_ipc_dispatch.c`| Daemon IPC dispatch table, unknown command handling, argument unpacking. |
| `test_lvo_table` | `test_lvo_table.c` | Generated LVO table checked against `sfd/bsdsocket_lib.sfd` (139 slots, 121 functions). |
| `test_queues` | `test_queues.c` | Chained pbuf partial reads across buffer boundaries, the RX queue 32-packet limit, TCP aborts when the accept queue is drained, and event queue coalescing per `bsdsocket.doc`. |
| `test_route` | `test_route.c` | Route table lookup, default gateway, mask matching. |
| `test_sbtc` | `test_sbtc.c` | `SocketBaseTagList` tag parsing and tag list dispatch. |
| `test_slot_table` | `test_slot_table.c` | Global socket slot table: 64-slot limit, reference counting, allocation/free lifecycle. |
| `test_sockaddr` | `test_sockaddr.c` | Socket address manipulation and family validation. Uses its own `TAP_TEST` macro. |
| `test_sockopt` | `test_sockopt.c` | `getsockopt` option-length bounds and `EINVAL`. |
| `test_stats` | `test_stats.c` | `TnStats` telemetry counters. |
| `test_usergroup` | `test_usergroup.c` | `usergroup.library` database, credentials, crypt. |
| `test_wizard_config` | `test_wizard_config.c` | `TolunnetSetup` config generation. |

## Running Tests

```bash
make test-host
```

The command runs in two steps:

1. `python-checks`: `gen_lvo_table.py`, `gen_usergroup_table.py`, `verify_icons.py`, `check_md_links.py` and
   `check-forbid.sh`, followed by a `git diff` gate over the generated sources and `README.md`.
2. All 20 host test binaries, run with AddressSanitizer and UndefinedBehaviorSanitizer enabled.
