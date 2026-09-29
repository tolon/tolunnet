# STOP-REPORT — z.ai step 11n, item 1 (net check rows while live), 2026-09-29

State: HEAD `301b81b` (logs for 08bfe34). The rows are still red, so
per this step's rule I print the four `# ` detail lines and stop —
no second bench, no guessing. Item 2 not started.

## The four detail lines (verbatim, a1200/conformance.log)

```
# tc_net_checks_ok: address 1 - address 10.0.2.15
# tc_net_checks_ok: ping 1 - ping 127.0.0.1 replied
# tc_net_checks_ok: dns 0 - cannot resolve test.tolunnet.lan (rc=10)
# tc_net_checks_ok: tcp 1 - tcp 10.0.2.2:15080 connected
not ok 100 - tc_net_checks_ok # a hermetic check unexpectedly failed
```

`tc_net_checks_fail` passed correctly (address guard green, then
ping/dns/tcp all 0): `ok 101 - tc_net_checks_fail`.

## What the numbers say

The 11n diagnosis was right: with the rows after
`tc_net_cmd_nslookup_ptr` (live stack), address, ping and tcp are
green — 3 of the 4 checks work, on the exact hermetic targets the
suite already proves. The only red is DNS:

- `C:nslookup test.tolunnet.lan` (rc=10) fails in nslookup's plain-A
  branch, which is `gethostbyname` (`src/cmds/nslookup.c:352`,
  `src/cmds/cmdlib.c:211`, LVO -210, SocketBase open — the same
  wrapper the suite uses for green rows).
- In THIS bench, `gethostbyname` never resolves a netsvc DNS name:
  `tc_dns_local` ("A: test.tolunnet.lan -> 10.0.2.2", `ok 20`) plays
  resolver and client with two UDP sockets and its own comment says
  explicitly: "no host, no deferred gethostbyname IPC". The only
  names `gethostbyname` resolves on the bench come from the hosts
  table (`tc_cmd_nslookup`, `10.9.9.7`).
- The proven nslookup DNS path on the bench is the UDP one with
  explicit SERVER/PORT: `net_cmd_nslookup_server ok 98` —
  `C:nslookup tolunbench.test SERVER 10.0.2.2 PORT 15353` ->
  `10.0.2.55`.

So `tn_check_dns` hit a real environment fact, not a code fault:
on a real install `gethostbyname` uses the live resolver and the
check would behave; on the hermetic bench it cannot see the netsvc
DNS zone. My `tc_net_checks_ok` chose `test.tolunnet.lan` — a name
only the netsvc DNS zone knows.

## Decision needed (NOT applied — stopping per the rule)

One of:
1. `tc_net_checks_ok` calls DNS the bench-proven way: keep
   `tn_check_dns` as the real-user check, but the row asserts the
   SERVER-form (`tn_check_dns` gains optional server/port, or the
   row runs `C:nslookup test.tolunnet.lan SERVER 10.0.2.2 PORT 15353`
   — netsvc answers `test.tolunnet.lan -> 10.0.2.2` in the same
   zone as `tolunbench.test`).
2. Drop the DNS check from `tc_net_checks_ok` (address/ping/tcp
   only) and leave DNS to `net_cmd_nslookup_server`, which already
   covers it.
3. Wizard-side (item 2) keeps `gethostbyname`-based `tn_check_dns`
   unchanged — real users have a resolver — and accepts that the
   hermetic bench cannot run that row green; the row then belongs
   in the FAIL-guarded family instead.

Working tree clean at `301b81b`. Item 2 (`fix(setup): Test page
shows real results and advice`) not started.
