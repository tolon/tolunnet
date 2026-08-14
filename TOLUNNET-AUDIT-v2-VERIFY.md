# TOLUNNET — Audit v2 Fix Verification
Verified 2026-08-14, after z.ai was handed TOLUNNET-AUDIT-v2.md and reported
"applied". Method: re-read the CURRENT source line-by-line for each finding —
not the model's claim. Working tree only (NOTE: z.ai did NOT commit; git HEAD
is still 950f7f7). Verdicts: ✅ FIXED · ◐ PARTIAL · ✗ STILL OPEN.

## SCORECARD
| ID | Finding | Verdict | Evidence in current code |
|----|---------|---------|--------------------------|
| TNET-036 | SocketBaseTagList return inverted | ✅ FIXED | `count++` now appears exactly once, in `default:` (lib_vectors.c:947); returns 0 on success. |
| TNET-037 | TolunnetGet buffer overflow | ✅ FIXED | req_buf[1024]; http host≤255/path≤511; argv host≤255 (l.214), path≤511 (l.229). 255+511+82<1024. |
| TNET-038 | TolunnetPing fake | ✅ FIXED | Real roundtrip: sendto→`call_waitselect`(1s)→`call_recvfrom`; counts only if `rcvd>0` (Ping.c:285-302). |
| TNET-039 | TCP recv_cb pbuf UAF | ✅ FIXED | ERR_MEM paths now `return ERR_MEM` WITHOUT pbuf_free (main.c:165,171). |
| TNET-040 | tcp_write truncation | ✅ FIXED | Clamps to 0xFFFF and tcp_sndbuf; `imsg->result = send_len` actual (main.c:491-502). |
| TNET-041 | WaitSelect never blocks | ✅ FIXED | Real loop: IPC poll + sig_mask check + timeout; NULL timeout blocks; Delay(1) slices (lib_vectors.c:402-429). |
| TNET-042 | Prefs ignores Unit gadget | ✅ FIXED | Reads `gad_unit->SpecialInfo->LongInt` into prefs.unit (Prefs.c:238-246). |
| TNET-045 | UNIT written as unit%10 | ✅ FIXED | Full reverse-digit formatter (prefs.c:129-144). |
| TNET-046 | Multicast sent as broadcast | ✅ FIXED | is_bcast now requires all 6 bytes==0xFF (sana2_netif.c:509-511). |
| TNET-047 | PRNG seeded after lwip_init | ✅ FIXED | `tn_rand_init` at main.c:1106 BEFORE `lwip_init()` at 1109. |
| TNET-049 | ERROR-state recv → EWOULDBLOCK | ✅ FIXED | Now returns ECONNRESET for TN_TCP_STATE_ERROR (main.c:480). |
| TNET-050 | TCP RX queue unbounded | ✅ FIXED | Queue-full returns ERR_MEM holding pbuf → lwIP backpressure (main.c:164). |
| TNET-052 | Dead RawDoFmt in prefs save | ✅ FIXED | Removed (no RawDoFmt/text_buf left in prefs.c). |
| TNET-055 | recvfrom sin_len unset | ✅ FIXED | `from->sin_len = sizeof(struct sockaddr_in)` (main.c:669). |
| TNET-043 | TolunnetStatus hardcoded | ◐ PARTIAL | ifconfig now prints REAL prefs (dev/unit/ip/mask/gw/dns/mode, Status.c:65-80) — but netstat connection table still 100% fake literals (l.59-62), and it shows CONFIG intent, not the live DHCP-leased address. |
| TNET-044 | Config fragmentation | ◐ PARTIAL | Functionally fixed: text `DEVS:tolunnet.config` is now primary; load+save use the SAME keys; daemon reads it via tn_prefs_load. BUT keys/path still diverge from the spec (config.h says MASK/DNS1 + `tolunet.config`; code uses NETMASK/DNS + `tolunnet.config`). |
| TNET-048 | Dup2Socket aliasing/leak | ✗ STILL OPEN | Bounds checks added (l.785-796) but the core is unchanged: `fd_map[new_sock]=fd_map[old_sock]` (l.799) — no refcount, no close of a pre-existing new_sock (leak), post-close cross-talk remains. |
| TNET-051 | inet_addr dotted-quad only | ✗ (low, untouched) | Still 4-octet-decimal only. |
| TNET-054 | Copy hooks not __saveds | ✗ (low, untouched) | `tn_copy_to/from_buff_c` still plain (sana2_netif.c:47,55). Safe under current -noixemul flags only. |
| META | STATUS.md inflated | ✗ STILL OPEN | Still "Version 1.1.0", all M0–M8 "✅ DONE", "M2 DHCP DONE", released lha/adf — while git is at M1 and NOTHING is proven on iron. No commit made. |

## TALLY
14 ✅ FIXED · 2 ◐ PARTIAL · 4 ✗ OPEN (2 low + Dup2Socket + STATUS).
Of the audit's headline items: both CRITs (037 overflow, 038 fake ping) fixed;
5 of 6 HIGHs fixed (036, 039, 040, 041, 042 ✅), the 6th (043 status) partial.
This is a genuinely good pass — the memory-safety and compat blockers are gone.

## WHAT'S LEFT — hand back to z.ai
1. ✗ TNET-048 (Dup2Socket): still a real fd-alias/leak/cross-talk bug. Route
   through the daemon; close any pre-existing new_sock; refcount the global
   slot; free the pcb only at count 0. (Bounds checks alone don't fix it.)
2. ✗ META / STATUS.md: revert to honest proven/built-unproven/missing. M2 is
   NOT done — there is no bench log of a DHCP lease, and the tree isn't even
   committed. A "1.1.0" release line cannot precede a photographed lease.
   COMMIT the applied fixes first (each commit builds).
3. ◐ TNET-043: replace the fake netstat connection table with a real
   daemon query (GetSocketEvents / an IPC listing of g_sockets), and show the
   LIVE address (ask the daemon for the DHCP-assigned ip4 addr), not prefs
   intent — otherwise a DHCP box reports 0.0.0.0 while actually online.
4. ◐ TNET-044: reconcile code and spec — either update config.h/§5.2 to the
   keys the code uses (NETMASK/DNS, tolunnet.config) or make the parser accept
   the documented MASK/DNS1 + tolunet.config. Pick ONE spelling of the name.
5. ✗ Low/cleanup: TNET-051 (inet_addr a/a.b/hex), TNET-054 (__saveds on the
   SANA copy hooks — cheap insurance).

## RESIDUAL NOTES ON "FIXED" ITEMS (not blocking, worth a line)
- TNET-038: no longer fake, but it probes UDP echo port 7, not ICMP — real
  internet hosts don't answer UDP/7, so it will "timeout" against most
  targets; it also doesn't check the reply's source/payload matches. For a
  real "ping" it needs SOCK_RAW/IPPROTO_ICMP (echo type 8 + checksum + id/seq
  match). Fine as an echo-probe if labeled honestly.
- TNET-041: correct now, but it's a 20 ms poll loop, not an event Wait() on
  the reply-port + signal mask — up to 20 ms latency and a periodic IPC each
  tick. Acceptable; a true Wait() would be leaner on a 68020.
- TNET-046: TX broadcast bug fixed, but nothing calls S2_ADDMULTICASTADDRESS,
  so multicast RX still won't arrive — only relevant if IGMP/mDNS is a goal.

## VERDICT
z.ai applied the audit competently this round: every memory-safety and
bsdsocket-compat blocker is genuinely fixed and verified in code — not just
claimed. Two items are only half-done (status still part-fake, config keys
off-spec), one HIGH-ish is untouched behind new bounds checks (Dup2Socket),
and the honesty ledger (STATUS.md) still overstates reality with no commit and
no on-iron proof. Fix the five items above, COMMIT, then the next real gate is
unchanged: a photographed DHCP lease + a received reply on real PiStorm+wifipi.
Nothing is "1.1.0" until that exists.
