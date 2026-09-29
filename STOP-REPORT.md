# STOP-REPORT — z.ai step 11o, item 2 (Test page real results), 2026-09-29

State: HEAD `041c0d3` (logs for 9687593). Item 2 is red twice in a
row (benches `190428`, `192253`) — per the order's rule I stop, with
no third run. Item 1 is DONE and ALL-GREEN.

## Item status

1. `fix(setup): DNS check queries the configured server` — DONE:
   `28f4951` + caller fix `daab94f`. Bench
   `20260929-183733-v1.2.0-rc4-358-gdaab94f`, four legs
   `core: 108 ok / 0 not ok; external: 1 skipped`, **109 `ok`
   lines and `1..109` in all four logs** (bench summary counts the
   one external skip outside "core"). Logs commit `ce1cce6`.
2. `fix(setup): Test page shows real results and advice` — code in
   (`784fc03` + ping budget `9687593`). RED ×2. NOT DONE.

Counted runs: 2 for item 2. One earlier `FATAL: make all failed`
(before any TAP line — net_test.c still called the old
tn_check_dns signature) does not count; it was fixed in `daab94f`
and the rerun (attempt 1) was the first counted run.

## The evidence, precisely

Both red benches show the identical single red, and it moved with
the command name:

```
# tc_net_checks_ok: address 1 - address 10.0.2.15
# tc_net_checks_ok: ping 0 - ping 127.0.0.1 got no reply (rc=5)      <- the only red
# tc_net_checks_ok: dns 1 - resolved tolunbench.test to 10.0.2.2 via 10.0.2.2
# tc_net_checks_ok: tcp 1 - tcp 10.0.2.2:15080 connected
```

- In bench `183733` (item 1) the SAME check with the SAME arguments
  ran as `C:TolunnetPing` and answered **rc=0** ("ping 127.0.0.1
  replied"). In `190428` and `192253` it runs as `C:ping` and
  returns **rc=5**.
- `C:ping` is the same binary in the package (Makefile:416 copies
  build/TolunnetPing to C/ping), and rc=5 means "acknowledged == 0"
  (TolunnetPing.c:451). Attempt 2 raised the TIMEOUT from 5 s to
  the full 10 s budget (`9687593`) and rc stayed 5 — so this is NOT
  a timing miss.
- Root cause, from `ci/bench.sh`: the bench hdf is populated by the
  script's own `xd write` lines, and they write only
  `C/TolunnetPing` (line 406). **`C/ping` is never written**, so on
  the bench hdf `C:ping` is whatever the hdf template carries —
  not our ping. The package argument in the order ("the package
  installs C:ping next to C:TolunnetPing") is true for the package,
  but the bench does not install the package that way.

## One-line fix I did NOT apply (stopping per the rule)

In `ci/bench.sh`, next to line 406, add the mirror line:
`xd write build/TolunnetPing C/ping || die "xdftool write ping
failed"` (both legs' staging paths). Then the row should go green
without any further product change — the wizard wiring itself
(status "Testing...", busy pointer, tolunnet.port, skip/advice
texts) is in `784fc03` and only this one check depended on the
missing file.

Working tree clean at `041c0d3`. No infra FATAL reruns in this
step beyond the one non-counted make failure described above.
