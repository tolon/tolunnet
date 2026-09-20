# TNET-150 — loopback degradation after deferred DNS: root cause from code review + fix. One commit + host test + bench. STOP RULES unchanged. Keep `tc_cmd_nslookup` as-is: it is the proof.

## Root cause (verified in source)
`src/task/ipc_netdb.c`: when `dns_gethostbyname()` returns `ERR_INPROGRESS`, the handler returns DEFER and the only reference to the client's `TnIpcMsg` is the lwIP DNS table (`callback_arg`). Nothing in the daemon tracks it: `TnSocketSlot` has `pending_connect_msg`/`pending_accept_msg` but no pending-DNS record, and `tn_ipc_cmd_close()` (`ipc_socket.c:41`) only unrefs fds and disarms selectors.
Sequence that corrupts memory:
1. Client (`lib_vectors.c` watchdog path, `ipc_timeout_ms=5000`) gives up after 5 s, parks the heap message in `base->ipc_orphan`, **frees it on the next call** (lib_vectors.c ~89-94).
2. lwIP DNS gives up later (`DNS_MAX_RETRIES`×timeout ≈ 4-8 s) or the responder child answers late → `tn_dns_found_cb(name, ipaddr, imsg)` writes `imsg->result/err_no` into **freed memory** and `ReplyMsg()`s it. Heap corruption lands in pool neighbours (`TnRxPacket` freelist / pbuf) → loopback data delivery dies while IPC keeps working. Matches every symptom, including cycle-2 `ETIMEDOUT` (stale pending entry for the same name in the small DNS table).
3. Same callback also writes `hostent_*` into `imsg->socket_base` without checking that base still exists (child's `CloseLibrary` frees it).
`tn_drain_loopback` re-entrancy is NOT the cause (called only from the 4 main-loop sites).

## Fix (all in one commit `fix(netdb): track deferred DNS requests; safe cancel on CLOSE (TNET-150)`)
1. Daemon: `TnDaemon.dns_pending[TN_DNS_PENDING_MAX]` (config `DNS_PENDING=`, default 8): `{TnIpcMsg *imsg; TnSocketBase *base; char name[64]; ULONG tick;}`. `tn_ipc_cmd_gethostbyname` DEFER → add entry (table full → reply `EAGAIN` now). `tn_dns_found_cb`: look up `callback_arg` in the table; **not found → return without touching anything**; found → remove entry, fill `hostent` on the recorded base, `ReplyMsg`.
2. `tn_ipc_cmd_close`: before unref/disarm, for every `dns_pending` entry with this base: remove entry, `imsg->result=0; err_no=ECONNABORTED; ReplyMsg(imsg)`. Also `dns_removeentry`-style invalidation is not available in lwIP 2.2 — leave the DNS table entry; the callback will simply find no pending record.
3. Client (`lib_vectors.c`): never `FreeVec(base->ipc_orphan)` during a later call. Free it only in `tn_lib_close` **after** the CLOSE IPC has been replied (daemon guarantees no further access). If a second watchdog fires while an orphan exists, chain it (`ipc_orphan_next`) — same rule.
4. Ordering guard: watchdog `ipc_timeout_ms` must be > lwIP `DNS_MAX_RETRIES * DNS_TMR_INTERVAL`; expose `DNS_RETRIES=` config key (default 4) and make `SocketConformance` set `ipc_timeout_ms` = that + 2 s. Log at BASIC tier when a DNS reply arrives for an unknown/abandoned request (counter in `GETSTATS`: `dns_late_replies`).
5. Host test `tests/host/test_ipc_dispatch.c`: gethostbyname DEFER → CLOSE for that base → late `tn_dns_found_cb` → assert no write, reply count == 1 (the ECONNABORTED one), ASan clean. Second case: late callback after client freed the message (mock) → no access.
6. Stop refusal: in the Ctrl-C/STOP path log `owner_task` name of every base still counted in `lib_OpenCnt`; if the task no longer exists in Exec's task lists, run the CLOSE path for it and decrement. Record what it printed in the bench README.

Proof: both profiles green with `tc_cmd_nslookup` **enabled** and everything after it (`whois nc telnet ftp sntp tftp`) `ok`; restart cycle works (two "lwIP initialized" lines per profile); ISSUES TNET-150 row with the three evidence dirs + fix commit.
