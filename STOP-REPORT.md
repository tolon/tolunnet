# Tolunnet STOP-REPORT: z.ai step 2 — bench red at 17e0e53 (bsdsocktest #36 freeze, both profiles)

**Timestamp:** 2026-09-23
**Work order:** TN-zai-step.md (steps 1-2 executed; step 3 fires: bench red → do not fix → report and stop)
**Commit:** `17e0e53` — `fix(stack): plan items 3-11 (UDP send, accept queue, shutdown, owner clear, len clamp, setsockopt, UDP RX copy, EClock, clean shutdown)` (includes the step-1 shutdown-sequence regression fix)
**Last green bench:** `docs/bench-logs/20260923-143321-v1.2.0-rc4-3-g8df5606/` (HEAD~1 `8df5606`, "Item 2")
**Host tests:** 20 binaries, 0 not ok; check-forbid 26 regions 0 findings; align-check 0 warnings.

## What was done (per work order)

1. **Step 1 regression fix (committed in 17e0e53):** the daemon's clean-shutdown
   block called `tn_lib_destroy` unconditionally, and `tn_lib_destroy`
   `Remove()`d the library node even with `lib_OpenCnt > 0` — a live client's
   clone jumps into the daemon task's code segment, so Remove/exit under an
   open client unloads that code from under it (TNET-059 Guru class).
   Restored contract:
   - shutdown gate: `tn_reap_dead_clients`, then under `Forbid()` re-check
     `lib_OpenCnt` — if > 0: `Permit()`, log, `goto tn_main_loop` (return to
     servicing IPC until the last close); only at 0: `RemLibrary` + `RemPort`
     + drain the queue with ENETDOWN under the same Forbid.
   - `tn_lib_destroy` refuses BOTH `Remove` and `FreeMem` while
     `lib_OpenCnt > 0` (logs the refusal); removal/free only at zero.
2. `make test-host` → 20 binaries, 0 not ok. Committed everything as one
   commit (17e0e53), benched that sha.

## The red bench (verbatim)

```
20260923-195432-v1.2.0-rc4-5-g17e0e53 / a1200 AND 68000 (identical):
bsdsocktest.log frozen at 48 lines:
ok 34 - # SKIP send buffer never filled (>1MB)
ok 35 - send(): error after peer closes connection [BSD 4.4]
#   errno: 32 (after 1 attempt(s))
not ok 36 - send()/recv(): simultaneous bidirectional transfer [BSD 4.4]
#   send(client): rc=-1 errno=32
[bench] TIMEOUT waiting for bench-done after 900s   (68000 leg; a1200 same)
conformance.log / conformance2.log: "(missing)" — SocketConformance never ran
```

- Both profiles freeze at the byte-identical point: bsdsocktest **#36
  (simultaneous bidirectional transfer)** hangs the emulator after printing
  `not ok 36` + `#   send(client): rc=-1 errno=32` (EPIPE on the client's
  first send).
- The daemon task log ends right after `socket() -> fd 0` / `fd 1` for that
  test's pair — no further IPC activity logged; the guest never reaches the
  boot script's next stage.
- The failure is deterministic across a1200 and 68000, so it is in the
  carried items-3-11 code (UDP send / accept-queue callbacks / shutdown /
  owner-clear paths are the neighbors of #35-#36), not a timing flake.

Per step 3 of the work order: not fixing. Evidence dir kept:
`docs/bench-logs/20260923-195432-v1.2.0-rc4-5-g17e0e53/` (bsdsocktest.log,
tolunnet-task.log per profile).

## Requested report fields

- **commit sha:** `17e0e53`
- **bench dir:** `docs/bench-logs/20260923-195432-v1.2.0-rc4-5-g17e0e53/`
- **verbatim `not ok`:** the two lines above (#36 + send(client) errno 32),
  both profiles; conformance rows absent (suite never started).
- **net TODO remaining:** the bench's own tally prints `net TODO remaining: 0`
  (was 6) — note this is the TODO-marker count, not a health statement; the
  #36 freeze is the blocker.
