# STOP-REPORT — z.ai step 11m, item 1 (Test-page checks), 2026-09-29

State: HEAD `36ab316` (logs for 5ad7ef3). Item 1 is red twice in a row
(benches `164439`, `170403`) — per the order's rule I stop, with no
third run. Item 2 not started.

## Item status

1. `refactor(setup): each Test-page check is a function returning a
   result` — code in (`e3541b8` + `56d0643` include fix +
   `5ad7ef3` RunCommand switch), rows `tc_net_checks_ok` /
   `tc_net_checks_fail` registered. RED ×2. NOT DONE as a verified
   item.
2. `fix(setup): Test page shows real results and advice` — NOT
   STARTED (no third run allowed).

Counted runs: 2 (a `FATAL: make all failed` before any TAP line —
missing dos/dostags.h — does not count per the step rule; it was
fixed in `56d0643`).

## The evidence, precisely

Both red benches show an IDENTICAL failure signature, across two
different spawn mechanisms:

```
# tc_net_checks_ok: address 0 - no address: stack reports offline
# tc_net_checks_ok: ping 0 - ping 127.0.0.1 got no reply (rc=20)
# tc_net_checks_ok: dns 0 - cannot resolve test.tolunnet.lan (rc=10)
# tc_net_checks_ok: tcp 0 - tcp 10.0.2.2:15080 refused or failed (rc=10)
not ok 108 - tc_net_checks_ok
```
(`tc_net_checks_fail` passes in both — unreachable targets are
correctly reported as failures.)

In the SAME bench, the suite's own rows drive the SAME C: commands
green seconds earlier, from the same task, with the same
LoadSeg+RunCommand pattern:

```
ok 48 - tc_cmd_nslookup          (run_cmd C:nslookup)
ok 51 - tc_cmd_nc                (run_cmd C:nc)
ok 59 - tc_cmd_getnetstatus      (run_cmd C:GetNetStatus)
ok 89 - net_cmd_status_live      (run_cmd C:GetNetStatus ADDRESS -> 10.0.2.15)
ok 95 - net_cmd_ping_ttl         (run_cmd C:TolunnetPing 127.0.0.1 COUNT 3)
ok 97 - net_cmd_ping_gw
```

Attempt 1 used System() (benches died rc=20/10/10/10); I blamed the
spawn mechanism, switched net_checks.c to the suite's exact
LoadSeg+RunCommand pattern (`5ad7ef3`), and got the identical
signature (`170403`). Two mechanisms, one signature: the difference
is NOT how the command is spawned. What differs between the suite's
run_cmd and my copy is the INPUT side: run_cmd also opens T:cmd.in
and SelectInput()s it before RunCommand, mine leaves the suite's own
Input selected. ReadArgs falls back to reading Input when the
RunCommand buffer is exhausted, and a console-backed Input can make
every /N keyword scan misbehave - which matches rc=20 (usage) on
ping exactly.

## Next step I did NOT take (no third run)

Make run_cmd_capture/run_cmd_silent byte-identical to the suite's
run_cmd: open T:cmd.in, SelectInput it (and SelectInput back after),
keep SelectOutput, newline-terminated command line. That is the one
untested variable and it is visible in the diff against
`SocketConformance.c:7391`.

No further benches were run for item 1 after the second consecutive
red, per the order's rule. The working tree is clean at `36ab316`.
