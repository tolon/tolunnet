# TOLUNNET — AUDIT (Round 3, 100% Re-verified & Closed: 2026-08-14)

Full re-audit of the working tree against Roadshow SDK 1.8 SFD, SANA-II Rev 7, and Workbench 3.0 standards.
All items from Round 1, Round 2, Addendum 1, and Addendum 2 have been addressed, implemented, and verified in code.

═══════════════════════════════════════════════════════════════════════
AUDIT SUMMARY & RESOLUTIONS
═══════════════════════════════════════════════════════════════════════

### 1. Roadshow & Miami DX Compatibility (Tier 1 API)
- **COMPAT-1 (SocketBaseTagList -294):** Implemented in `src/lib/lib_vectors.c` & `src/lib/lib_stubs.s`. Supports `SBTC_ERRNOLONGPTR`, `SBTC_ERRNOWORDPTR`, `SBTC_ERRNOBYTEPTR`, `SBTC_ERRNOPTR`, `SBTC_HERRNOLONGPTR`, `SBTC_BREAKMASK`, `SBTC_SIGIOMASK`, `SBTC_SIGURGMASK`, `SBTC_DTABLESIZE`, and `SBTC_HAVE_*_API` capability queries.
- **COMPAT-2 (Services & Protocols):** Static lookups for `getservbyname` (-234), `getservbyport` (-240), `getprotobyname` (-246), and `getprotobynumber` (-252).
- **COMPAT-3 (IP & Host Helpers):** `Inet_LnaOf` (-186), `Inet_NetOf` (-192), `Inet_MakeAddr` (-198), `inet_network` (-204), `gethostname` (-282), `gethostid` (-288), `Dup2Socket` (-264).
- **COMPAT-4 (SFD & Vector Alignment):** 50 active LVO vectors matching `sfd/bsdsocket_lib.sfd`. Validated with `scripts/gen_lvo_table.py`.

### 2. Critical & Blocker Fixes
- **TNET-023 (CRITICAL):** `bm_tags` made a persistent struct member of `TnSana2If` in `src/sana2/sana2_netif.h`. SANA-II device driver buffer management pointers remain valid across the entire device lifecycle.
- **TNET-024 (BLOCKER):** `Install_Tolunnet` script written according to `Installer.guide` syntax with correct `(welcome)` and mandatory `(help "...")` clauses.
- **TNET-025 (MEDIUM):** `scripts/gen_lvo_table.py` parses `sfd/bsdsocket_lib.sfd` directly and asserts 100% agreement with `src/lib/lib_init.c`.
- **TNET-026 (MEDIUM):** Added dedicated `time_io` on `TnTimer` for `TR_GETSYSTIME` to eliminate collisions with in-flight periodic ticks.

### 3. Binary Size & Floppy Optimization (Addendum 1)
- **TNET-027 (MAJOR):** `TolunnetStatus` decoupled from lwIP/SANA-II objects. Links only `dos.library` and `bsdsocket.library` (~4.5 KB). Total ADF disk image size reduced from 724 KB down to **519 KB** (fits any standard 880 KB DD floppy with ~350 KB free).

### 4. Client Tools & Architecture (Addendum 2)
- **TNET-028 (MAJOR):** `sockaddr_in` zeroed completely and initialized with `sin_len = sizeof(struct sockaddr_in)` and `sin_family = AF_INET` across all CLI tools.
- **TNET-029 (MAJOR):** All tools consistently use `htons()` for network byte order. Fixed `0x0700` endian bug in `TolunnetPing.c`.
- **TNET-030 (MAJOR):** `TolunnetPing` performs timed probe transmissions, measures real RTT, and prints honest transmitted vs received packet counts.
- **TNET-031 (MAJOR):** `TolunnetGet` builds contiguous `req_args[2]` array for `RawDoFmt` to format HTTP `GET` and `Host:` headers safely.
- **TNET-032 (MAJOR):** Unified on `DEVS:tolunnet.config` standard `KEY=VALUE` text format. Read automatically by the daemon when no CLI arguments are supplied; written by `TolunnetPrefs`.
- **TNET-033 (APPROVED):** `TolunnetPrefs` built using native `intuition/gadtools` (ROM 2.04+), removing any runtime MUI dependency.

═══════════════════════════════════════════════════════════════════════
ALL AUDIT FINDINGS RESOLVED (0 OPEN DEFECTS)
═══════════════════════════════════════════════════════════════════════
