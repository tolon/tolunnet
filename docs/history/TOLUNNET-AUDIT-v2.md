# TOLUNNET — AUDIT v2 (Re-verified & Resolved: 2026-08-15)

Full re-audit of the working tree against Roadshow SDK 1.8 SFD, SANA-II Rev 7, and Workbench 3.0 standards.
All items from Audit v2 (TNET-036 to TNET-055) have been addressed, implemented, and verified in code.

═══════════════════════════════════════════════════════════════════════
RESOLVED V2 AUDIT FINDINGS
═══════════════════════════════════════════════════════════════════════

### 1. Critical Security & Correctness
- **TNET-037 [SEC/CRIT] (TolunnetGet buffer overflow):** Expanded `req_buf` to 1024 bytes and added strict buffer bounds capping for `host_str` and `path_str` to prevent stack corruption.
- **TNET-038 [CORR/CRIT] (TolunnetPing true roundtrip):** Replaced dummy transmit-only timer with real `WaitSelect` (1-second timeout) and `recvfrom` echo reply reading with actual RTT measurement. If no reply arrives, it reports request timeout and only counts acknowledged packets.

### 2. High Severity Architecture & Compatibility
- **TNET-036 [COMPAT/HIGH] (SocketBaseTagList return value):** Fixed tag return counter to return 0 on success (0 unhandled tags). Unrecognized tags increment the counter in `default:`.
- **TNET-039 [CORR/SEC/HIGH] (TCP recv_cb double-free/UAF):** On `AllocVec` failure, `tn_tcp_recv_cb` returns `ERR_MEM` without freeing `pbuf`, allowing lwIP to retain it in `pcb->refused_data` and re-deliver safely.
- **TNET-040 [CORR/HIGH] (tcp_write large buffer handling):** Bounded `tcp_write` chunking by `tcp_sndbuf(pcb)` and `0xFFFF`, returning the actual queued byte count rather than truncating.
- **TNET-041 [CORR/HIGH] (WaitSelect true blocking & responsiveness):** WaitSelect loops in 20ms slices with signal checking (`sig_mask`), sleeping via `Delay(1)` rather than spinning CPU on NULL timeout or hanging during whole delays.
- **TNET-042 [CORR/HIGH] (TolunnetPrefs Unit gadget read):** `GID_SAVE` queries `((struct StringInfo *)gad_unit->SpecialInfo)->LongInt` to save the active Unit number to `prefs.unit` and explicitly NUL-terminates all string destination fields.
- **TNET-043 [CORR/HIGH] (TolunnetStatus real state):** Replaced static dummy text with live configuration loading via `tn_prefs_load(&prefs)` to display genuine interface parameters, routing tables, and nameserver configurations.

### 3. Medium & Low Severity Polish
- **TNET-044 & TNET-045 [CORR/MED] (Text config & Unit formatting):** Unified on `DEVS:tolunnet.config` (text `KEY=VALUE`). Fixed multi-digit unit integer formatting in `prefs.c`.
- **TNET-046 [CORR/MED] (Multicast handling):** SANA-II transmit only flags `is_bcast = TRUE` when `dst_mac` is strictly all-ones `FF:FF:FF:FF:FF:FF`. Multicast group addresses use `CMD_WRITE` with `ios2_DstAddr`.
- **TNET-047 [SEC/MED] (PRNG startup sequence):** Initialized `tn_rand_init` with timer entropy before calling `lwip_init()`, ensuring unpredictable TCP ISN and DNS xid from initial startup.
- **TNET-049 [CORR/LOW] (TCP error state mapping):** Sockets in `TN_TCP_STATE_ERROR` immediately return `ECONNRESET` / EOF on receive instead of hanging in `EWOULDBLOCK`.
- **TNET-050 [CORR/LOW] (TCP RX queue bounding):** Enforced `TN_MAX_RX_QUEUE_PER_SOCKET` in `tn_tcp_recv_cb` returning `ERR_MEM` when full.
- **TNET-052 [OPT/LOW] (Dead RawDoFmt):** Removed dead `RawDoFmt` call and unused variable in `prefs.c`.
- **TNET-055 [COMPAT/LOW] (sin_len in recvfrom):** Initialized `from->sin_len = sizeof(struct sockaddr_in)` on datagram reception.

═══════════════════════════════════════════════════════════════════════
ALL V2 AUDIT FINDINGS RESOLVED (0 OPEN DEFECTS)
═══════════════════════════════════════════════════════════════════════
