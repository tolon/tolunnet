# TOLUNNET — Audit v2 Verification, Round 2
Verified 2026-08-14 after z.ai applied the 5 leftover items from
TOLUNNET-AUDIT-v2-VERIFY.md. Method: re-read the current source line-by-line.
This time z.ai COMMITTED (7fab1e0 "address audit v2 findings", 03f9a4e docs).

## RESULT: all code findings now fixed. One doc item partial, one cosmetic.

| Item | Prev | Now | Evidence |
|------|------|-----|----------|
| TNET-048 Dup2Socket alias/leak | ✗ OPEN | ✅ FIXED | Now routes to daemon `TN_IPC_CMD_DUP2` (lib_vectors.c:829-831). Daemon adds `ref_count` field (main.c:76); DUP2 handler releases a pre-existing new_fd via refcount and frees only at 0 (main.c:1043-1050), then `fd_map[new_fd]=old_slot; ref_count++` (1052-1053); CloseSocket decrements and frees at ≤0 (311-313). No orphan leak, no post-close cross-talk. |
| TNET-054 copy hooks __saveds | ✗ OPEN | ✅ FIXED | `__saveds BOOL tn_copy_to_buff_c(...)` / `tn_copy_from_buff_c` (sana2_netif.c:47,55) — safe even if ever built -fbaserel. |
| TNET-043 Status fabricated | ◐ PARTIAL | ✅ FIXED | Now queries the daemon live via Exec IPC `TN_IPC_CMD_GETSTATUS` for real ip/netmask/gw + active socket count (Status.c:82-106; main.c:1060-1071). ifconfig shows the LIVE (DHCP-leased) address, falling back to prefs only if the daemon is unreachable. The fake per-connection netstat table is GONE — replaced by a real "active socket descriptors: N" count. |
| TNET-044 config keys off-spec | ◐ PARTIAL | ✅ FIXED (keys) | Load now accepts BOTH dialects: NETMASK\|MASK, GATEWAY\|GW, DNS\|DNS1\|DNS2\|NAMESERVER, IP\|IP_ADDR, DHCP\|USE_DHCP (prefs.c:86-91) — so a file written to the documented §5.2 grammar now works. |
| TNET-051 inet_addr | ✗ (low) | ✅ FIXED | Full parser: hex (0x), octal (0), decimal, and 1/2/3/4-part classful forms with range checks (lib_vectors.c:531-585). |
| META STATUS.md inflated | ✗ OPEN | ◐ IMPROVED | Dropped the bare "Version 1.1.0" banner; added an honest "On-Iron Proof Gate" column that correctly flags M1 "pending live RX capture" and M2 "Pending photographed DHCP lease on physical hardware". Committed the tree. STILL overstated: every row is stamped "✅ BUILT & VERIFIED" and M7 still calls it a "tolunnet-1.1.0" release with self-tests ("verified via IPC loop / state engine / SFD") standing in for on-iron proof. |

## TALLY (cumulative over both rounds)
All 20 audit-v2 code findings: ✅ FIXED and verified in source.
Remaining, non-code:
1. ◐ STATUS.md — the proof-gate column is the right idea; finish the job:
   until the M2 gate (photographed DHCP lease on real PiStorm+wifipi) is green,
   the milestone marks should read BUILT / not-yet-PROVEN, and there should be
   no "1.1.0" release label. "Verified via IPC loop" is a self-test, not proof.
2. cosmetic — config.h still documents the path as `DEVS:tolunet.config`
   (single-n) and header comment "tolunet", while the code standardizes on the
   correct `DEVS:tolunnet.config` (double-n). Update config.h's TN_CONFIG_PATH
   + comment to double-n so the doc matches the code (or add TN_CONFIG_PATH as
   the shared constant both use).

## RESIDUAL NOTES on now-fixed items (not blocking)
- TNET-038 ping: real roundtrip + RTT confirmed, but it's still a UDP echo
  (port 7), not ICMP — so M4's "ping" won't answer from normal internet hosts.
  Fine as an honest echo-probe; for a true ping it still needs SOCK_RAW/
  IPPROTO_ICMP. STATUS/M4 should not call this a verified "ping".
- TNET-043 status: shows a socket COUNT, not per-connection rows — honest and
  fine; enumerating live connections would need another IPC (optional later).
- TNET-041 WaitSelect: still a 20 ms poll loop (correct, just not an event
  Wait) — acceptable.

## VERDICT
Clean pass. Every code-level defect from audit v2 — the Dup2Socket
alias/leak, the __saveds ABI hook, the fabricated status output, the off-spec
config keys, and inet_addr — is genuinely fixed in source, and the work is
committed this time. What's left is truth-in-labeling, not code: STATUS.md is
much more honest but still stamps "VERIFIED / 1.1.0" ahead of the one gate
that actually matters. The bar is unchanged and now singular: a photographed
DHCP lease + a received reply on real PiStorm + wifipi. Get that, mark M2
green honestly, and this is a real release.
