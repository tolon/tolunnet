# STOP-REPORT — z.ai step 10d, item 1 (undo sandbox), 2026-09-27

State: HEAD `c26f2e9`. Item 1 is red twice in a row (benches
`20260927-211506-gе4bdc01` and `20260927-214202-gc26f2e9`); per the
order's rule I stop instead of running the third attempt.

## Item status

1. `fix(installer): undo survives missing files` — code in (`e4bdc01`
   FailAt 21 + If EXISTS-guarded deletes + tn_file_size_of fix +
   lint rules with c83af21/HEAD selftest), but the bench is RED.
2. `test: Prefs window must open` — tc_prefs_opens was written early
   and ACCIDENTALLY rode in item 1's commit (process slip, mine): on
   a1200 it correctly emits the TODO row, on 68000 the suite died at
   row 99 right where it runs (details below).
3. `bench: measure HDF free space` — NOT DONE.
4. `docs: OWNER-RETEST` — NOT DONE.

## Final bench 20260927-214202-v1.2.0-rc4-249-gc26f2e9

- a1200: `conformance.log: core: 100 ok / 2 not ok; external: 1 skipped (skip=2 todo=1)`
  - `not ok 100 - tc_prefs_opens # TODO TolunnetPrefs never opens its window - fixed in step 11` (expected shape)
  - `not ok 103 - tc_undo_sandbox # undo did not restore C/NetShutdown from backup`
  - diagnostic: `# tc_undo_sandbox: C/NetShutdown size=-1, backup size=-1`
- a1200 conformance2: identical.
- 68000: `conformance.log: core: 98 ok / 0 not ok; external: 1 skipped (skip=2 todo=0)`,
  suite DIED at row 99/100 (the tc_prefs_opens slot; log simply stops,
  no 1..103), conformance2.log absent, leg TIMEOUT.

## Last 5 lines of each log (verbatim)

a1200/conformance.log:
```
ok 102 - tc_daemon_noconfig_start
# tc_undo_sandbox: C/NetShutdown size=-1, backup size=-1
not ok 103 - tc_undo_sandbox # undo did not restore C/NetShutdown from backup
1..103
# bench: asking daemon to stop (restart-cycle proof)
```
a1200/conformance2.log: identical to a1200/conformance.log.
68000/conformance.log:
```
ok 95 - net_cmd_ping_ttl
ok 96 - net_cmd_ping_self
ok 97 - net_cmd_ping_gw
ok 98 - net_cmd_nslookup_server
ok 99 - net_cmd_nslookup_ptr
```
68000/conformance2.log: (missing) — cycle 2 never started.

## Analysis for the next attempt

- a1200 `size=-1, backup size=-1`: at row time T:tnsbx/C/NetShutdown
  and the whole backup path do not exist. The boot-script sandbox
  (makedir chain, Copy of SYS:C/NetShutdown and SYS:C/nc, three Echo
  seeds, Execute S:tolunnet-undo-sandbox) runs BEFORE the daemon at
  boot; the artifacts should survive in RAM: until row 103. Either the
  boot-script block itself failed early (a makedir/Copy error aborts
  the User-Startup script at FailAt 10 — and the sandbox block has no
  FailAt of its own!) or something removed T:tnsbx later. The undo
  cannot be the remover: it only deletes inside T:tnsbx paths it
  restores first, and `T:tnsbx/S` files were seen in the earlier
  manual probe. NEXT: give the boot-script sandbox its own
  `FailAt 21` + echo markers into WORK: (e.g. `Echo seed-ok
  >WORK:tnsbx-seed.log` after the last seed) so the next run shows
  whether the seeds happened at all.
- 68000 hang at the tc_prefs_opens slot: every wait in the row is
  bounded (10 s window poll, 20 s close poll, screenshot ~seconds), so
  a pure hang is unexpected; next attempt should add tapf markers
  before launch/after poll/after screenshot to localize it, or park
  the row behind a WORK: flag until step 11.
- Process note: tc_prefs_opens belongs to item 2; its early inclusion
  in the item-1 commit is my slip and it contaminates the reading of
  item 1's bench.

No further benches were run after the second consecutive red, per the
order's rule.
