# Tolunnet STOP-REPORT: z.ai step 7e — tc_usergroup reload leg passes, but the flush freezes the suite tail and cycle 2

**Timestamp:** 2026-09-25
**Work order:** TN-zai-step.md (step 7e item 1). The item-1 change works as
specified (close before flush; LibList empty; bsdsocket survives; reload +
getpwnam green) — but the flush freezes the rest of the suite and cycle 2.
Log ends early → per the rules: STOP-REPORT with the last lines verbatim,
stop.

**Commit:** `3b47584` — `test(usergroup): reload leg closes before flush; bsdsocket survives flush`
**Bench dir:** `docs/bench-logs/20260925-131139-v1.2.0-rc4-72-g3b47584/`
**Host:** 20 binaries, 0 not ok (before bench).

## What works now (verbatim from both profiles' cycle 1)

```
ok 62 - tc_cmd_route
# tc_usergroup: bsdsocket survived flush (socket ok)
ok 63 - tc_usergroup
```

The reload leg proves the full contract: close → flush AllocMem →
usergroup.library gone from LibList → bsdsocket.library still present →
socket()+CloseSocket still work → reopen → getpwnam(root) → close.

## The regression (log ends early — all four logs, identical)

```
a1200/conformance.log : 63 ok rows, ends at "ok 63 - tc_usergroup"; no 1..N plan line
68000/conformance.log : identical
a1200/68000 conformance2.log : "(missing)" / empty — cycle 2 never started
```

The freeze happens at the test immediately following tc_usergroup, in BOTH
profiles and BOTH... cycle 2 never even began. The daemon task log ends on
normal socket activity (fd 0 open/close — the next test's first IPC), so the
client/daemon pair wedges right after the reload leg's flush allocation.

## Analysis

The reload leg's `AllocMem(0x7FFFFFF0, MEMF_PUBLIC)` failure triggers
exec-wide memory purge: expungeable CLOSED libraries get thrown away
system-wide. Something the remaining suite (or the daemon) needs was among
the purged/corrupted state — the wedging is deterministic in both profiles.
The step-7d item-2 wording anticipated exactly this ("reload leg closes
before flush; **bsdsocket survives flush**") — bsdsocket does survive, but
the following test still wedges, so the purge damage is wider than LibList
(likely pool pbufs or another closed library's data the daemon still
references).

## Suggested next steps (owner decision)

1. Replace the flush AllocMem with `RemLibrary`-free approach: make the
   reload leg best-effort — reopen WITHOUT the flush and assert only the
   reopen+getpwnam; move the expunge proof to a dedicated later row that
   runs LAST (after tc_cmd_stop_start can no longer be affected), or
2. Run the reload leg inside a dedicated child process (like dns_resp /
   break_helper) so any purge damage cannot affect the main suite; or
3. Investigate what exactly the purge removes: log LibList before/after the
   flush from the suite.

Evidence kept: `docs/bench-logs/20260925-131139-v1.2.0-rc4-72-g3b47584/`.
Item 1's spec-compliance is proven (close-before-flush, LibList empty,
bsdsocket survives); only the suite-tail freeze blocks an ALL-GREEN run.
