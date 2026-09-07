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

All test suites follow standard TAP (Test Anything Protocol) output format via `tests/host/tn_test.h`.

| Binary | Source | Covers |
|--------|--------|--------|
| `test_addr` | `test_addr.c` | IP address formatting and conversions. |
| `test_config` | `test_config.c` | Config file parser & writer round-tripping both keyword dialects (`NETMASK`/`MASK`, `GATEWAY`/`GW`, `DNS`/`NAMESERVER`). |
| `test_constants` | `test_constants.c` | Protocol constants and flag verification. |
| `test_errstr` | `test_errstr.c` | Error string translations across BSD errnos. |
| `test_fdset` | `test_fdset.c` | `fd_set` bit manipulation and bounds checks. |
| `test_http_url` | `test_http_url.c` | HTTP URL parsing, port extraction, path normalization. |
| `test_inet_addr` | `test_inet_addr.c` | `inet_addr` / `inet_aton` parser (decimal, octal, hex, multi-part, invalid format rejection per TNET-051). |
| `test_ipc_client` | `test_ipc_client.c` | Client-side IPC message construction. |
| `test_ipc_dispatch`| `test_ipc_dispatch.c`| Daemon IPC dispatch table, unknown command handling, argument unpacking. |
| `test_queues` | `test_queues.c` | Chained pbuf partial reads across buffer boundaries, RX queue 32-packet limit, accept queue drain TCP aborts, and event queue coalescing per `bsdsocket.doc`. |
| `test_sbtc` | `test_sbtc.c` | `SocketBaseTagList` tag parsing and tag list dispatch. |
| `test_slot_table` | `test_slot_table.c` | Global socket slot table, 64-slot limit, reference counting, allocation/free lifecycle. |
| `test_sockaddr` | `test_sockaddr.c` | Socket address manipulation and family validation. |
| `test_status` | `test_status.c` | Status reporting string generation and counters. |

## Running Tests

```bash
make test-host
```

Executes all 14 host test binaries with AddressSanitizer and UndefinedBehaviorSanitizer enabled, followed by Python-based tree-wide verifiers (`gen_lvo_table.py`, `verify_icons.py`, `check_md_links.py`).
