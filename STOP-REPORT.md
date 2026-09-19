# Tolunnet STOP-REPORT: CLOSE step — bench red three times in a row (TNET-141/§B.5 suite)

**Timestamp:** 2026-09-20 03:30
**Stop rule fired:** CLOSE rule 4 — bench red twice in a row (three reds total:
`20260919-234035-62b298d`, `20260920-000915-d143398`, `20260920-021508-4d55e20`).
**Last green bench:** `docs/bench-logs/20260919-141006-6de710e/` (pre-CLOSE §B, 35/35 ×2 both profiles).
**Commits this session (all host-green, amiga bench pending):**
`62b298d` (§B.1 tc_cmd_* suite + cmdlib LVO fixes + SIOCGARP + host repair),
`d143398` (§B.5 route/ROUTECTL/lwIP hooks + child lifecycle + arp warm-probe fix),
`4d55e20` (IPC reply-identity verification + gethostbyname NULL mapping + nslookup telemetry + TN_DIAG bench collection),
`f6c0167-era follow-up` (iperf + tc_iperf_loopback + route test-call restoration — see git log for exact sha).

## 1. What was delivered and is DONE (host-proven)

- **TNET-141 closed as code:** 13 `tc_cmd_*` conformance rows; three real
  cmdlib LVO bugs found and fixed (gethostname −240→−282, gethostbyname
  −156→−210, gethostbyaddr −150→−216 — hostname/nslookup/ShowNetStatus/
  GetNetStatus were calling unrelated vectors); arp SHOW real via new
  daemon SIOCGARP ioctl (`TN_SIOCGARP`, NetBSD numbering, byte-wise
  arpreq access); GetNetStatus ONLINE de-DNS'd.
- **§B.5 route stack:** host `test_route` un-SKIPped and green
  (longest-prefix / validation / capacity), ROUTECTL IPC + dispatch row,
  lwIP `LWIP_HOOK_IP4_ROUTE_SRC` + `LWIP_HOOK_ETHARP_GET_GW` hooks (empty
  table = stock lwIP behaviour), `route`/`AddNetRoute`/`DeleteNetRoute`
  commands. Host suite 16/16 green, align-check 0, forbid 0, python-checks clean.
- **§B.6 iperf:** command + `tc_iperf_loopback` (number → SUMMARY.txt) +
  docs/bench.md throughput section (numbers pending a green bench).
- **Library hardening:** `tn_ipc_call` now verifies the GetMsg'd reply IS
  the message of the current call (freed-block reuse poisoning vector
  closed); `gethostbyname` maps res≤0 → NULL (was `(hostent *)-1`).
- **Host tests repaired** (broken since 639c903: slot_table's unguarded
  proto/exec.h) and the TNET-115 scatter host test realigned with the
  shipped batch-flush design.

## 2. What is still RED and why (evidence)

Two failure clusters, both novel to the tc_cmd_* additions:

### Cluster A — TCP-loopback pairs go deaf after tc_cmd_nslookup (both profiles, cycle 1+2)
- `tc_cmd_whois/nc/telnet/ftp` fail: their first `waitselect` after
  `tc_cmd_tcp_pair` times out (diag `wait/recv got=… errno=4` — `got` is
  stale stack, errno 4 stale; the actual event is a 3 s readability
  timeout), then everything downstream in the test fails.
- Pattern across three runs: whois+ftp always, nc/telnet on slower
  profiles/cycles; UDP tests (sntp/tftp) always pass; the PRE-EXISTING
  TCP pair test #9 (tc_listen_accept_loopback, early in the suite)
  always passes.
- Everything between #9 and #39 that is novel: **tc_cmd_nslookup's
  child process** — a `SystemTags(SYS_Asynch,TRUE)` shell running
  `C:SocketConformance dns_resp <port>`, which opens bsdsocket.library,
  binds UDP 127.0.0.1:DNS_PORT, answers A/PTR, exits (ready/done file
  handshake, deterministic). The done-file proves the CHILD exited, but
  the ASYNC SHELL wrapper's teardown is not covered by that handshake —
  it lands on the daemon/CLI right when the next test's TCP pair runs.
- a1200 C2 nslookup also fails on the second cycle (watchdog ETIMEDOUT,
  `he=0x0 errno=60`): the daemon never processes child#2's answer —
  consistent with child#1's UDP pcb/slot residue on the same (never
  restarted, see cluster B) daemon intercepting the query.

### Cluster B — 68000 leg dies late / daemon never restarts
- Run 1: 68000 cycle-2 froze inside tc_cmd_nslookup (600 s timeout).
- Run 2: 68000 cycle-1 completed (184 s) but cycle-2's daemon never came
  up (`lib_open` failed; only ONE "lwIP 2.2.0 initialized" banner in the
  task log). a1200's cycle-2 ran against its STILL-RUNNING cycle-1
  daemon (also only one banner) — the TNET-059 stop is not happening or
  is being refused with clients open.
- Run 3 (TN_DIAG=1): the whole 68000 leg produced nothing (no crash log —
  the daemon did not Guru; the leg stalled and the hard emulator kill
  lost the HDF files). NOTE: this run's "syntax error near unexpected
  token" was self-inflicted — bench.sh was edited WHILE bash was reading
  it (bash re-reads scripts incrementally). Never edit ci/bench.sh while
  a bench is running.

## 3. Suggested next steps (owner decision)

1. **Isolate cluster A in one step:** delete the child-process mechanism
   from tc_cmd_nslookup entirely. Replace with (a) gethostbyname of a
   dotted-quad literal (exercises the IPC + hostent packing hermetically,
   no DNS, no child) and (b) rely on tc_dns_local (already green) for the
   wire-level DNS proof. If whois/nc/telnet/ftp go green in the same
   bench, the async child is confirmed as the poison and stays out.
2. **Then chase cluster B** (daemon stop/restart): add a boot-script
   probe after cycle 1 (`WaitPort`-style check / log `tolunnet STATUS`
   output) to see whether the stop was refused (client still open) or the
   relaunch failed (library name still registered). The TN_DIAG trap-log
   collection shipped in 4d55e20 is in place for any real crash.
3. §B.5/§B.6 code is complete and host-green; only the bench proof is
   blocked by the suite-level reds above.
4. Rule for all future benches: do not touch ci/bench.sh while a bench
   run is active.

Evidence dirs (kept): `docs/bench-logs/20260919-234035-62b298d/`,
`docs/bench-logs/20260920-000915-d143398/`, `docs/bench-logs/20260920-021508-4d55e20/`.
