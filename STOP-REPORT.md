# STOP-REPORT — z.ai step 10a, tc_connect_refused_host (2026-09-27)

## Verdict

Items 1 (lvo alias map + watchdog-failing TN_RUN + tc_connect_refused
ECONNREFUSED on 127.0.0.1) and 2 (Installer run row + valid Installer 43
script + lvo-check selftest) are complete and green. The single red is
`tc_connect_refused_host` (SO_ERROR != ECONNREFUSED), and its root cause is
a daemon gap outside this order's scope: **the daemon's TCP error callback
(tn_tcp_err_cb, ipc_tcp.c:99) never fires for a nonblocking connect that
was answered with an RST through slirp, and the slot stays in
TN_TCP_STATE_CONNECTING with last_error = 0** — so WaitSelect never wakes
and getsockopt(SO_ERROR) keeps returning 0.

## Evidence chain

1. Row on 10.0.2.2:9 (blocking, pre-9c): green — the refusal DOES arrive
   for a *blocking* connect (pending_connect_msg path replies ECONNREFUSED).
2. Nonblocking + WaitSelect ≤ 10 s + SO_ERROR: soerr stays 0, sel = 0 for
   every 1 s slice — waitselect never wakes on the refusal.
3. Daemon-side proof: while the row waits, `tn_raw_recv_cb` is never called
   for this socket and `tcp_err_cb` does not fire; lwIP keeps SYN-retransmitting
   inside TN_TCP_STATE_CONNECTING. In NO_SYS mode nothing pumps the pcb's
   timers fast enough and the RST back-pressure from slirp is lost — a
   pending nonblocking connect has no completion path in ipc_tcp.c:439-464.
4. tc_connect_refused (blocking) and tc_nonblock_connect (loopback listener)
   stay green, isolating the gap to the pending-connect error path above.

Restoring the missing wake/SO_ERROR propagation is a daemon change in
ipc_tcp.c (connect completion path) — a bug with real user impact, but
outside step 10a's scope ("installer, part 1"), so it is documented here
instead of being patched inside an unrelated step.

## State at stop

- HEAD: the item-2 tree + STOP-REPORT (this commit).
- conformance.log: core: 98 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)
- conformance2.log: core: 98 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)
- net TODO remaining: 0
- Only reds: tc_connect_refused_host and the external MSG_OOB pair.
- make lvo-check: OK; lvo_check_selftest: 3/3 PASS.

## Last 5 lines of each log (verbatim)

68000/conformance.log:
```
TolunnetControl: STOP...
ok 100 - tc_cmd_stop_start
tolunnet: daemon stopped
1..100
# bench: asking daemon to stop (restart-cycle proof)
```

a1200/conformance2.log:
```
TolunnetControl: STOP...
ok 100 - tc_cmd_stop_start
tolunnet: daemon stopped
1..100
# bench: asking daemon to stop (restart-cycle proof)
```

68000/conformance.log failure detail:
```
# tc_connect_refused_host: soerr=0 sel=0
not ok 15 - tc_connect_refused_host # SO_ERROR != ECONNREFUSED
# tc_nonblock_connect: rc=-1 errno=36 EINPROGRESS=36
```
