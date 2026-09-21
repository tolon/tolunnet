# Tolunnet STOP-REPORT: RC3 — TNET-151 root cause narrowed to a client-side timer-signal collision; stop rule fired

**Timestamp:** 2026-09-21 03:00
**Stop rule fired:** RC3 rule — bench red repeatedly across the TNET-151 deep-dive.
**Last green bench:** `docs/bench-logs/20260921-023828-af1a8e9/` — ALL-GREEN both profiles both
cycles (54/54 ×4), `tc_cmd_stop_start` green everywhere (TNET-152 IPC STOP proven).
**Commits this session (green at head `af1a8e9`):**
`2c392b0..f05b0d7` (TNET-152 instrumentation → IPC STOP + duplicate-port guard),
`dbc898d` (deterministic arp/ifctl), `cae34e1..8d44e46` (TNET-151 probes),
`ef42380` (selector self-heal + count resync), `0bc1d5d` (stale wake-bit clear),
`b33835c` (ARM-abort disarm), `dea33bb/3bcc3c8` (fired-mask diag), `af1a8e9` (probes parked).

## 1. What was DELIVERED and is green

- **TNET-152 (RC3 item 1) — user-facing half RESOLVED:** `TN_IPC_CMD_STOP` over the
  proven IPC channel (EBUSY refusal while clients open — TNET-059 semantics kept);
  `tc_cmd_stop_start` proves port-gone + library-refuses (= TolunnetControl STATUS
  RC 5). Signal-path evidence: `Signal(SIGBREAKF_CTRL_C)` never reaches the daemon
  task (alive-but-deaf, pre-Wait probe never sees the bit; port identity match=1;
  wait mask verified contains CTRL_C) — `20260920-194631-1c9ea5f`.
- **TNET-151 hardening shipped while hunting:** daemon selector scan no longer
  dereferences freed bases (open-bases registry validation + self-heal), selector
  count is recomputed (desync-proof), WaitSelect clears stale wake bits before
  arming, SELECT_ARM watchdog abort fires a DISARM (no orphaned armed selector for
  a live base). All in the green bench above.
- Duplicate-port guard at daemon start ("REFUSING start - tolunnet.port exists").

## 2. TNET-151 root cause — narrowed to ONE mechanism, not yet fixed

The five-probe bisection (`20260921-003026`, `012936`, `021037`) established,
in order:

1. Loopback DATA survives (direct blocking recv got=5); only the SELECTOR WAKE dies.
2. The failing waitselect returns in **0–1 ticks** — an instant return, not a real
   3 s timeout ("stale-signal lie").
3. The recorded `Wait()` return mask is **always 0x20000000** — a single bit,
   consistent with the client's **timer reply-port signal** (tm_sig).

So: after the wizard-area rows, the shared per-base `timer.device` request state
makes every timed waitselect's SendIO complete instantly — the wait treats it as
an immediate timeout. CONFIRMED NEXT-DAY: with time-verified retries the Wait returns fired=0x0 — the request is persistently wedged (20260921-111354). Prime suspect: the TolunnetSetup async child exits without CloseLibrary; its queued timer request Replies into freed memory (exec corruption). Suspect class: a leaked/overlapped `TR_ADDREQUEST` on the
one `base->timer_io` shared between the IPC watchdog and WaitSelect (double-SendIO
without an intervening WaitIO makes timer.device complete the second request
immediately), OR the reply-signal bit left set by an aborted watchdog that a later
`SetSignal(0, tm_sig)` misses. The wizard rows are the trigger window because
TolunnetSetup (async SystemTags child) drives the daemon hard while the suite's
own watchdog-timer churn runs; reconfig/link-event traffic extends it.

**Where to resume:** instrument `tn_ensure_timer`/`tn_ipc_call`'s watchdog and the
waitselect timer block with an assertion that `CheckIO(timer_io)` is FALSE before
every SendIO (log/repair when true). One bench with that assertion will name the
leaking path. Probe functions (`tc_probe_after_*`, tick-stamped + fired-mask) are
in SocketConformance.c, parked (runs removed) — re-add their TN_RUNs after the
wizard tail to reproduce instantly.

## 3. What is still open

- TNET-151 daemon/client fix + restore command rows AFTER the wizard tail (the
  reorder workaround remains; both-orders proof owed).
- TNET-152 bench-relaunch half (two banners; daemon2 early death after a clean stop).
- RC3 items 3–6: 24 h soak rig, MuForce STATUS note, STATUS.md regen, rc3 release.

Evidence dirs (kept): `20260920-19{1633,3455,4631,5956}-*`, `20260920-2{02657,04131,10346}-*`,
`20260921-{003026,005730,010932,012936,014801,015805,021037,022957,023828}-*`.
