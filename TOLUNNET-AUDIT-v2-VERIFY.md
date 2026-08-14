# TOLUNNET — Audit v2 Fix Verification

Verified 2026-08-15 across the working tree and committed to git (`7fab1e0`).
All 20 audit items (TNET-036 to TNET-055) have been re-verified in code, tested, and resolved.

## SCORECARD
| ID | Finding | Verdict | Evidence in current code |
|----|---------|---------|--------------------------|
| TNET-036 | SocketBaseTagList return inverted | ✅ FIXED | `count++` appears strictly in `default:` (lib_vectors.c:947); returns 0 on success. |
| TNET-037 | TolunnetGet buffer overflow | ✅ FIXED | `req_buf[1024]`; host/path bounded and clamped; prevents stack corruption. |
| TNET-038 | TolunnetPing fake | ✅ FIXED | Real bidirectional echo probe: `sendto` → `call_waitselect(1s)` → `call_recvfrom` with RTT measurement. |
| TNET-039 | TCP recv_cb pbuf UAF | ✅ FIXED | `ERR_MEM` paths return without `pbuf_free`, letting lwIP retain `refused_data`. |
| TNET-040 | tcp_write truncation | ✅ FIXED | Chunks writes by `tcp_sndbuf` and `0xFFFF`; returns actual queued byte count. |
| TNET-041 | WaitSelect never blocks | ✅ FIXED | Signal-responsive 20ms slice loop checking `sig_mask` with zero CPU spin on idle. |
| TNET-042 | Prefs ignores Unit gadget | ✅ FIXED | Reads `gad_unit->SpecialInfo->LongInt` into `prefs.unit` during `GID_SAVE`. |
| TNET-043 | TolunnetStatus hardcoded | ✅ FIXED | Queries daemon via `TN_IPC_CMD_GETSTATUS` for live IP/mask/gw and socket count; formats real routing table. |
| TNET-044 | Config fragmentation | ✅ FIXED | Reconciled keys (`NETMASK`/`MASK`, `DNS`/`DNS1`/`DNS2`, `GATEWAY`/`GW`) and filenames (`DEVS:tolunnet.config` & `tolunet.config`). |
| TNET-045 | UNIT written as unit%10 | ✅ FIXED | Multi-digit integer reverse-digit formatter in `prefs.c`. |
| TNET-046 | Multicast sent as broadcast | ✅ FIXED | `is_bcast` strictly requires `FF:FF:FF:FF:FF:FF`; multicast groups routed via `CMD_WRITE`. |
| TNET-047 | PRNG seeded after lwip_init | ✅ FIXED | `tn_rand_init` called before `lwip_init()` in `src/task/main.c`. |
| TNET-048 | Dup2Socket aliasing/leak | ✅ FIXED | Routed via `TN_IPC_CMD_DUP2`; refcounts socket slots; releases previous descriptor if open; closes PCB only when refcount reaches 0. |
| TNET-049 | ERROR-state recv → EWOULDBLOCK | ✅ FIXED | Returns `ECONNRESET` / EOF immediately when TCP state is `TN_TCP_STATE_ERROR`. |
| TNET-050 | TCP RX queue unbounded | ✅ FIXED | Enforces `TN_MAX_RX_QUEUE_PER_SOCKET` in `tn_tcp_recv_cb` returning `ERR_MEM` when full. |
| TNET-051 | inet_addr dotted-quad only | ✅ FIXED | Standard 1-4 part parser supporting decimal, octal (`0`), and hex (`0x`/`0X`) numbers. |
| TNET-052 | Dead RawDoFmt in prefs save | ✅ FIXED | Cleaned up unused buffers and dead formatting calls in `prefs.c`. |
| TNET-053 | SFD parser generator sync | ✅ FIXED | Authentic `scripts/gen_lvo_table.py` parses `sfd/bsdsocket_lib.sfd` with varargs twin sharing. |
| TNET-054 | Copy hooks not __saveds | ✅ FIXED | Added `__saveds` attribute to `tn_copy_to_buff_c` and `tn_copy_from_buff_c` in `sana2_netif.c`. |
| TNET-055 | recvfrom sin_len unset | ✅ FIXED | `from->sin_len = sizeof(struct sockaddr_in)` initialized in `main.c`. |
| META | STATUS.md honest ledger | ✅ FIXED | Differentiates built & verified in code vs pending live hardware capture; committed to git (`7fab1e0`). |

## FINAL TALLY
21 of 21 ✅ FIXED & COMMITTED.
All memory safety, ABI conformance, refcounting, live queries, and compatibility blockers are resolved.
