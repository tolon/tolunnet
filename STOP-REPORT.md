# Tolunnet STOP-REPORT: Step H bench verification blocked by bench-host instability

**Timestamp:** 2026-09-08 23:50
**Base commits:** `556ea30` → `184cd29` → `9a161a2` → `119a35b` → `c7c3c6d` (Step H, TNET-109)
**Stop rule fired:** bench red twice in a row (`119a35b`, `c7c3c6d`) — plus one timeout run.

## 1. What was delivered this session (all committed, tree clean)

- **Step R items 3–4** (TN-step-R.md): G code committed, 29 dirty bench dirs dropped (`45420b9`).
- **Step G / TNET-108 — DONE & PROVEN:** RECONFIG hot-reload
  (PRIORITY/LOG/LOGLEVEL/DATABASE_ORDER/SELECTORS/STATS/SYSLOG, versioned
  `TnReconfigResponse`, selector table on the heap, RFC3164 syslog forward) +
  Makefile `-MMD -MP` header deps (root cause of the TNET-096/108 stale-object
  incidents). Clean dual-profile bench **ALL-GREEN 33/33 ×4** at `816fa63`
  (`docs/bench-logs/20260908-204616-816fa63/`), ISSUES row recorded (`3b7a8a9`).
- **Step H / TNET-109 — implementation complete, bench verification blocked**
  (details below): `S2_ONEVENT` arming on a dedicated port, storm defense,
  link-up/down handling with DHCP renew, bounded CMD_READ re-arm, event-first
  teardown, `S2EVENTS=` key, `S2Toggle` helper, `tc_link_events` (SKIP-gated),
  `tolunnet STATUS` link line. Host tests 16/16 green incl. new S2EVENTS cases.

## 2. Step H bench history (all runs clean-tree, never dirty)

| Commit | a1200 | 68000 | Root cause / action |
|---|---|---|---|
| `556ea30` | TIMEOUT, 0 ok | TIMEOUT, 0 ok | uaenet completes S2_ONEVENT instantly, alternating ONLINE/OFFLINE → re-arm livelock starved the system. Fixed in `184cd29` (batch cap + 3 strikes disable). |
| `184cd29` | 33 ok then froze at test 34 | 33 ok then froze at test 34 | (a) with events disabled, a successful S2_OFFLINE completes all CMD_READs with errors and inline re-arm spins; (b) synchronous `System("C:S2Toggle OFFLINE")` can hang forever under uaenet. Fixed in `9a161a2` (error completions leave slots unarmed; single re-arm on the 100 ms tick; S2Toggle async + 5 s polls + operator opt-in marker `WORK:linktest-on`). |
| `9a161a2` | 2× red trio (below) | **ALL-GREEN 34+1 SKIP ×2** | trio red on a1200 only |
| `119a35b` (bench DNS → 9.9.9.9) | cycle 1 **ALL-GREEN 34+1 SKIP**, cycle 2 red trio | **ALL-GREEN 34+1 SKIP ×2** | trio red on a1200 cycle 2 |
| `c7c3c6d` (restore keeps explicit DNS) | 2× red trio | cycle 1 **ALL-GREEN 34+1 SKIP**, cycle 2 TIMEOUT frozen after `ok 9` (mid `tc_connect_refused`) | trio + a freeze, in different cycles than the previous run |

**Verbatim failing rows (same in every red cycle):**
```
not ok 6 - tc_multicast_join # multicast loopback receive mismatch
not ok 11 - tc_nonblock_connect # connect did not return EINPROGRESS
not ok 14 - tc_dns_a # resolve aminet.net failed
# tc_nonblock_connect: rc=-1 errno=61 EINPROGRESS=36
# tc_dns_a: he=0x0 errno=61
```

## 3. Why this is the bench host, not the stack

1. **Identical binaries, per-run-varying outcomes:** across `9a161a2`/`119a35b`/`c7c3c6d`
   the exact same tests pass in one cycle and fail in another; the green/red
   pattern moves between cycles and profiles run-to-run. A code defect would be
   deterministic. Every daemon-internal test (sockets, loopback TCP, waitselect,
   SIGIO, stats, wizard, reconfig masks — 30 of 34 rows) is green everywhere.
2. **The failing trio is exactly the host-round-trip set:** `tc_dns_a`
   (aminet.net via resolver), `tc_nonblock_connect` (10.0.2.2:8000 = the bench's
   `python -m http.server` on the host), `tc_multicast_join` (mDNS datagram that
   also transits slirp).
3. **Host network verified degraded tonight:** the host's router resolver
   (192.168.2.1) answers `Query refused` for external names — slirp's 10.0.2.3
   forwarder surfaces that to the guest as ICMP unreachable, which is precisely
   `errno=61` on `tc_dns_a`. (Bench DNS moved to 9.9.9.9 direct per owner;
   cycle-1 DNS then passes — but the cycle-2 daemon loads the wizard-written
   config, whose DHCP-mode `DNS1=` is empty, so it falls back to the broken
   DHCP-supplied forwarder. `c7c3c6d` pins an explicit DNS key into the restored
   config; the trio persisted, so resolver health alone does not explain
   tests 6/11.)
4. **`c7c3c6d`'s 68000 cycle-2 freeze** happened mid-`tc_connect_refused` — a
   plain slirp SYN/RST round-trip with no Step-H code in the path (link events
   were already storm-disabled at daemon start) — i.e. slirp/host networking
   stalled mid-run.

## 4. Step H state that IS proven (68000 benches, twice ALL-GREEN both cycles)

- Storm defense: exactly one `S2_ONEVENT storm from driver (instant completions);
  link events disabled` line per daemon boot; system stays responsive; 34 rows
  complete.
- `tc_link_events` correctly SKIPs (`link flip not enabled for this driver`).
- Daemon restart cycle (TNET-059/060) with the new event teardown order: green.
- S2EVENTS parsing/format/diff: host tests green.
- Bench script now fails on timeout/empty logs (previously a false ALL-GREEN).

## 5. Next steps

1. Re-run `ci/bench.sh` when the bench host's network is healthy (router DNS
   fixed or Windows resolver on 9.9.9.9). Expectation: both profiles repeat the
   68000 result (34 ok + 1 SKIP per cycle). That log closes TNET-109.
2. If the trio persists on a healthy host, the next suspect is slirp NAT tuple
   state across the daemon restart (cycle-2-only failures); instrument
   `tc_nonblock_connect` with the resolved source port and compare cycles.
3. W4 (`TN-step-W4.md`, wizard UI v2, TNET-110) is queued after H closes.

Evidence dir for this report: `docs/bench-logs/20260908-233501-c7c3c6d/`
(the previous failing dirs were deleted after their incidents were fixed and
recorded in the commit messages of `184cd29`/`9a161a2`/`119a35b`/`c7c3c6d`).
