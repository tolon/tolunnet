# Tolunnet STOP-REPORT: z.ai step 2 — #36 freeze FIXED; bench still red on two NEW rows (#68/#69 connect failed, both profiles)

**Timestamp:** 2026-09-23
**Work order:** TN-zai-step_1.md. The diagnosed fix was applied and works; the bench is still red for a DIFFERENT reason, so per the rules this report is updated and work stops.
**Fix commit:** `88d459b` — `fix(stack): reset shut_wr/shut_rd on slot reuse; allow send in CLOSE_WAIT`
**Bench dir:** `docs/bench-logs/20260923-221506-v1.2.0-rc4-7-g88d459b/` (RESULT: HAS-FAILURES)
**Previous red:** `17e0e53` — bsdsocktest #36 froze both profiles (that bench's report is in git history of this file).

## What the fix delivered (verified)

- **bsdsocktest #36 freeze: GONE.** Both profiles complete: 126/142 (failed 2,
  the historical MSG_OOB WONTFIX pair). The 17e0e53 red bench never got past
  bsdsocktest; now the full boot script runs to the end on both profiles.
- Root cause exactly as diagnosed: slot reuse did not reset `shut_wr`/`shut_rd`
  → #35's `shutdown()` poisoned #36's slot → first send EPIPE → both test
  tasks blocked in recv() → hang. Both send gates (send + sendmsg) also now
  implement 4.4BSD CLOSE_WAIT semantics (EPIPE only for OUR SHUT_WR or hard
  error). Host suite: 20 binaries, 0 not ok (new slot-reuse test + CLOSE_WAIT
  sendmsg test; the contradictory old case C updated with a C2 for our-side
  shutdown).
- Two previously-TODO rows turned green vs the last green bench (8df5606):
  `net_udp_connected_send`, `net_cmd_traceroute`. Bench's own tally:
  **net TODO remaining: 4** (was 6).

## The remaining red (verbatim, identical in all four logs)

```
# net_tcp_connected_sendto_ignored_to: connect failed errno=60
not ok 68 - net_tcp_connected_sendto_ignored_to # connect failed
# TIMEOUT: ipc watchdog fired during tc_net_tcp_connected_sendto_ignored_to
# net_tcp_shutdown_sendto_epipe: connect failed errno=60
not ok 69 - net_tcp_shutdown_sendto_epipe # connect failed
not ok 70 - net_cmd_nc # TODO red-baseline 18
not ok 71 - net_cmd_whois # TODO red-baseline 19
not ok 74 - net_cmd_sntp # TODO red-baseline 18
not ok 76 - net_cmd_tftp # TODO red-baseline 20
```

- Rows #70/#74/#76 etc. are pre-existing `TODO red-baseline` markers (the
  standing 4), not regressions.
- **#68/#69 are NEW conformance rows shipped in the carried items-3-11
  batch** (first time they ever ran in a bench — 8df5606 has neither name).
  Both fail identically on a1200 and 68000, both cycles: their setup
  `connect()` times out (errno 60 = ETIMEDOUT after the client watchdog),
  i.e. the test's own TCP connect to its loopback listener never completes.
  Neighbors #66 (net_tcp_connected_recvfrom) and #67
  (net_tcp_unconnected_sendto) pass, so plain connect/recvto works — the
  common factor of the two failures is pending investigation (their listener
  setup or a connect-with-destination interaction), which per the work order
  is NOT attempted here.

## Report fields (work order)

- commit sha: `88d459b`
- bench dir: `docs/bench-logs/20260923-221506-v1.2.0-rc4-7-g88d459b/`
- verbatim not ok: the six lines above (per log, identical ×4)
- net TODO remaining: **4**
