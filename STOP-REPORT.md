# Tolunnet STOP-REPORT: z.ai step 7c — usergroup stub still red + 68000 cycle-2 hang

**Timestamp:** 2026-09-25
**Work order:** TN-zai-step.md (step 7c). Item 1 attempted three stub/init
variants; `tc_usergroup` still fails AND the 68000 leg now hangs in cycle 2 →
per the rules: STOP-REPORT with the last lines of each log verbatim, stop.

**Commits this round:**
- `bc002e9` — item 1 (work-order stub): push-a0/push-d0 stack-arg stub,
  ug_init_c(base, seglist) plain C, no AddLibrary, RTF_AUTOINIT only,
  dead functions removed, reload leg in tc_usergroup.
- follow-up — register-ABI stub (a0→d1 tail-call) after the first bench
  showed arg corruption.

**Bench dir:** `docs/bench-logs/20260925-115313-v1.2.0-rc4-68-gac6d6b4/`

## The red + hang (verbatim, last lines of each log)

```
a1200/conformance.log (cycle 1 — suite COMPLETED):
  not ok 63 - tc_usergroup # getpwnam(root) failed or fields mismatch
  ...
  ok 83 - tc_cmd_stop_start
  1..83
  (net TODO remaining: 8)

a1200/conformance2.log (cycle 2 — hung after test 1):
  # tolunnet SocketConformance (Round 3 §B.2)
  ok 1 - tc_lib_open_close
  (log ends — 900 s timeout)

68000/conformance.log (cycle 1 — hung at the END, after test 62):
  ok 60 - tc_cmd_ifctl
  ok 61 - tc_cmd_netshutdown
  ok 62 - tc_cmd_route
  (log ends before tc_cmd_stop_start could run)

68000/conformance2.log: (missing)
```

## Analysis

- Cycle 1 a1200: the full suite ran and completed; ONLY tc_usergroup failed
  (getpwnam(root) mismatch) — so the stub/init now receives workable
  arguments but something in the DB init still mismatches (fields vs the
  test's uid/gid/name expectations).
- 68000: cycle 1 hung at the very END (after tc_cmd_route, before stop_start
  could run) and cycle 2 hung after test 1 — the repeated AUTOINIT
  open/expunge cycles appear to destabilize the emulator leg (seg-list or
  memory-poisoning aftereffect of the earlier broken stubs persisting in the
  HDF? Each bench stages a fresh HDF, so more likely the AUTOINIT reload
  path leaves memory in a state the 68000 tolerates worse).
- The reload leg I added runs open/getpwnam/AllocMem-flush/FindName/reopen —
  the flush-class AllocMem + immediate reopen is the new element in both
  hangs. This must be treated as a suspect too: the leg may be tripping an
  exec/WinUAE edge rather than proving the fix.

## Suggested next steps (owner decision)

1. Revert the usergroup RomTag to the pre-step-7 non-AUTOINIT form
   (git: before 98bb0bf) which passed tc_usergroup for many benches, and
   drop the reload leg — treat the expunge/reload crash as a separate,
   lower-priority investigation.
2. If AUTOINIT is still desired: bench the reload leg alone, minus the
   flush AllocMem, to isolate whether the AllocMem(0x7FFFFFF0) flush is
   what destabilizes WinUAE's 68000 emulation.
3. Investigate why tc_usergroup fails even in cycle 1 a1200 with the new
   stubs: add a debug print of base/seglist/DOSBase inside ug_init_c to
   verify arguments before touching the DB.

Evidence kept: `docs/bench-logs/20260925-115313-v1.2.0-rc4-68-gac6d6b4/`.
All other fixes (step 7 items 0–6, step 7b) remain committed and were
green-benched.
