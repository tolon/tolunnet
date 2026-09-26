# STOP-REPORT — z.ai step 9b (2026-09-26)

## Verdict

Items 1, 2 and 4 are complete and green. Item 3 (ping TTL) is code-complete and
half-proven: the explicitly-given `TTL 0` rejection (RC 10) works, but the
`TTL 7 → RC 0 and a reply line` half cannot pass in this bench because ICMP
echo requests to 10.0.2.2 receive no reply through WinUAE slirp — with or
without TTL, with or without this step's changes. Per the order's rule the
work stops here for owner review.

## What landed (HEAD at stop time: `2aab192`, tree clean)

- `edc2e76` fix(log): printf width, zero-pad and left-align — formatter moved
  to pure, host-testable src/common/log_format.c
  (%[-][0][width](s|ld|lu|lx|lX|c|%)); trailing '%' stops cleanly.
  Host test tests/host/test_log_format.c (6/6 green, ASan/UBSan).
- `1661e68` + `f581aa9`(bench staging) + `d9eee89` fix(prefs,hostname):
  tn_prefs_save honest (merge unknown keys + >127-char lines via
  tn_config_merge_preserve, tmp+Rename, FALSE on failure; DEVS canonical,
  ENVARC/ENV best-effort because ENVARC: is not mounted headless);
  hostname NAME validates RFC 1123, persists HOSTNAME= and applies it
  live with IPC RECONFIG (RC 0 only if both worked). Row
  net_cmd_hostname_set green.
- `76837c5` + `2aab192` fix(ping): TTL 1-255 applied via
  setsockopt(IPPROTO_IP, IP_TTL) before the first send (local LVO wrappers
  -6/-83 added); explicit TTL 0 or >255 → usage message, RC 10; the 3.1 KB
  tx/rx buffers moved off the 4 KB CLI stack to AllocVec.
- `74dd2f4` + `6a5ffb0` feat(nslookup): SERVER/PORT with its own UDP DNS
  query for A and PTR (id, RD, one question; PTR = d.c.b.a.in-addr.arpa;
  3 s x 2 tries; compression-safe answer parsing). netsvc gained a UDP DNS
  responder on 15353 (A tolunbench.test → 10.0.2.55; PTR 10.0.2.2 →
  tolunnet-guest.test; NXDOMAIN otherwise) whose reply now echoes the
  question section (e605e00). Rows net_cmd_nslookup_server and
  net_cmd_nslookup_ptr green.

## The one red row

`net_cmd_ping_ttl`: r1 = 5 (RC 5 = "0 received") for
`TolunnetPing 10.0.2.2 COUNT 1 TTL 7`; r2 = 10 for `TTL 0` (correct).
The TTL 0 half is proven green; the reply half fails because ICMP echo
requests never receive replies in this bench — slirp's ICMP echo
passthrough to the Windows host produces no responses, and netsvc has no
ICMP service. Evidence: `3 packets transmitted, 0 packets received` in the
row dump, identical without the TTL option and identical to pre-step
behaviour. The row's `TTL 0 → RC 10` assertion is green.

## Bench (2aab192, both profiles, cycle 1+2)

conformance.log: core: 95 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)
conformance2.log: core: 95 ok / 1 not ok; external: 1 skipped (skip=2 todo=0)
net TODO remaining: 0
Every log ends with `1..97`. Only net_cmd_ping_ttl and the external
MSG_OOB pair are red.

## Last 5 lines of each log (verbatim)

68000/conformance.log:
```
TolunnetControl: STOP...
ok 97 - tc_cmd_stop_start
tolunnet: daemon stopped
1..97
# bench: asking daemon to stop (restart-cycle proof)
```

a1200/conformance2.log:
```
TolunnetControl: STOP...
ok 97 - tc_cmd_stop_start
tolunnet: daemon stopped
1..97
# bench: asking daemon to stop (restart-cycle proof)
```

68000/conformance.log ping rows (the one red):
```
# net_cmd_ping_ttl: ttl=0 zero=0 r1=5 r2=10 out=PING 10.0.2.2 (10.0.2.2): 32 data bytes
Request timeout for seq 1
--- 10.0.2.2 ping statistics ---
3 packets transmitted, 0 packets received
not ok 94 - net_cmd_ping_ttl # TTL handling broken
```

## Suggested resolution (owner decision)

Point the ping row's target at something that answers ICMP in this bench,
or accept the row asserting only the TTL 0 rejection plus TTL-set
side effects until an ICMP-aware host helper exists in netsvc.
