# Tolunnet STOP-REPORT: z.ai step 7 — item 2 bench red (EINF leak from the break-mask wait), 20 not ok

**Timestamp:** 2026-09-25
**Work order:** TN-zai-step.md (step 7). Items 0 and 1 landed green-benched; item 2's bench is red outside TODO rows → per the rules: STOP-REPORT, stop.
**Commits:**
- `fd6915e` — item 0: test honesty (CLEARED branch now fails; badf reverted to the clamp rule; INDEX corrections).
- (item 1, rode into the same push) `b4231da`-lineage: SetSignal(bits,bits) at the three restore sites, watchdog timer retired (AbortIO/WaitIO/drain) before CANCEL, CANCEL message preallocated in tn_lib_open.
- `26c0875` — item 2: check_nfds clamp rule, dual-word SELECT_ARM/WAITSELECT with separate output accumulators, retry re-ARM, infinite-wait fix, fd40 idle assertion, badf → WaitSelect(FD_SETSIZE) succeeds.
**Bench dir:** `docs/bench-logs/20260924-234434-v1.2.0-rc4-49-g26c0875/` (a1200 == 68000, both cycles: core 61 ok / 20 not ok)
**Host:** 20 binaries, 0 not ok (before bench).

## The red (verbatim, a1200 C1; identical in all four logs)

```
not ok 10 - tc_ioctl_ifconf # SIOCADDRT expected ENOSYS
not ok 14 - tc_nonblock_connect # connect did not return EINPROGRESS
not ok 24 - tc_waitselect_badf # unopened fd in read_fds did not return EBADF
not ok 64 - net_tcp_blocking_recv # recv failed or content mismatch
not ok 65 - net_udp_connected_send # send failed or content mismatch
not ok 66 - net_udp_unconnected_send # expected -1 ENOTCONN
not ok 67 - net_udp_connected_sendto_eisconn # expected -1 EISCONN
not ok 68 - net_tcp_unconnected_recvfrom # expected -1 ENOTCONN
not ok 69 - net_tcp_connected_recvfrom # connect failed
not ok 70 - net_tcp_unconnected_sendto # expected -1 ENOTCONN
not ok 71 - net_tcp_connected_sendto_ignored_to # connect failed
not ok 72 - net_tcp_shutdown_sendto_epipe # connect failed
(not ok 73-81 = the standing TODO red-baseline rows, unchanged)
```

## Root cause (evidence-backed, not fixed per rules)

The library-side break-mask wait introduced in step 7 item 1/2 leaks EINTR
into unrelated blocking calls:

- `tn_ipc_call` now Waits on `reply_sig | (tm_sig) | break_mask` on EVERY
  blocking call (both the WaitPort-replacement branch and the watchdog
  branch), with `break_mask = base->sig_int ? sig_int : SIGBREAKF_CTRL_C`.
- Once ANY CTRL_C-class bit reaches the client task, every subsequent
  blocking call returns instantly -1/EINTR via the break path (each such
  path also CANCELs and destroys the in-flight daemon work — parked
  connect/recv die with EINTR, which is why `connect did not return
  EINPROGRESS`, `net_tcp_connected_recvfrom # connect failed`, and the
  whole net_tcp/net_udp block fell over).
- Bench evidence of the leak: `tc_nonblock_connect: rc=-1 errno=4`,
  `unopened fd res=-1 errno=4`, `net_udp_unconnected_send: n=-1 errno=4`.
  Rows 4-61 pass because they are non-blocking or their Ctrl-C bit was
  consumed by an earlier test; the failures cluster exactly where blocking
  calls meet a freshly-set CTRL_C bit (the daemon RECONFIG/wizard rows and
  the SYS_Asynch children set SIGBREAKF_CTRL_C on the parent as a side
  effect of their startup/exit).
- The step-5/6 design flaw is now clear: the break wait must distinguish
  "this task's own break mask" from stray CTRL_C-class bits set by child
  processes, OR must consume-and-ignore the break when the call is not a
  park-capable call. Roadshow solves this by checking the break mask ONLY
  in park-capable waits and by NOT leaving the bit set for library-internal
  callers.

## Suggested fix direction for the next step (not attempted per rules)

1. Only the park-capable calls (recv/recvfrom/recvmsg/accept/connect) may
   honor the break mask in their wait; everything else waits on
   `reply_sig (| tm_sig)` exactly as before.
2. In park-capable waits, on break: consume the signal (SetSignal(0, bits))
   and reply EINTR — do NOT re-assert (the application asked to interrupt;
   its CheckSignal would already have been made by the same code path that
   raised it, or it intentionally leaves it — decide per Roadshow autodoc
   and document).
3. tc_waitselect_badf's WaitSelect(FD_SETSIZE) acceptance and the fd40 row
   are correct as written and must survive the fix.
4. Re-verify rows 62-72 with a bench; then the remaining items 3-7 of this
   work order resume in order.

Evidence kept: `docs/bench-logs/20260924-234434-v1.2.0-rc4-49-g26c0875/`.
