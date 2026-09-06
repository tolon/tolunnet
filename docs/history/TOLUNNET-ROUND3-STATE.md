# TOLUNNET — Round 3 Session State (checkpoint 2026-09-05 ~18:00)

**TL;DR (TR):** §B test altyapısı bitti ve a1200 bankında uçtan uca çalışıyor. 68000
bankında daemon `#80000003` (Address Error) ile çöküyor — aktif debugging. Çözüm
emri: `TOLUNNET-FIX-guru-80000003.md` (Adım 1: WinUAE debugger'dan PC al). Ağaçta
büyük commit edilmemiş §B değişikliği var — kayıp riski yok (diskte), ama commit
planı aşağıda.

## Governing docs (precedence)
`TOLUNNET-BUGTRACK-v3-prompt.md` §0 LAW > `TOLUNNET-BUGTRACK-v3-round2.md` >
`TOLUNNET-SCOPE-v4-full-api.md` > `TOLUNNET-ROUND3-prompt.md` (this round) >
`TOLUNNET-FIX-guru-80000003.md` (active fix order for TNET-085).

## DONE this session (all UNCOMMITTED — see commit plan)

### B.1 host harness — GREEN: `make test-host` = 6 binaries, 0 failed
- `tests/host/tn_test.h` — TAP framework (ok/not ok/SKIP/TODO; TODO failures don't fail suite)
- Pure units extracted: `src/common/inet_parse.[ch]` (TNET-051 parser),
  `config_text.[ch]` (KEY=VALUE parse/format + shared `tn_prefs_default`),
  `sbtc_dispatch.[ch]` (SocketBaseTagList classifier, PURE — no derefs; ops applied
  in lib_vectors), `fdset_util.[ch]` (64-bit fd_set, EBADF nfds check, timeout ms)
- Tests: `test_inet_addr.c`, `test_config.c` (incl. TODO-marked TNET-078 red),
  `test_sbtc.c`, `test_fdset.c`, `test_lvo_table.c` (C-side SFD parser: **139 slots,
  −30…−858**, 121 named + 18 reserved; SCOPE-v4's "133" discrepancy → QUESTIONS.md #3),
  `test_route.c` (SKIP stubs)
- `lib_vectors.c` rewired to inet_parse + sbtc_dispatch (with _Static_asserts vs SDK header)
- CI: `.github/workflows/build.yml` rewritten (host job `make test-host`, amiga job
  uploads `tolunnet-lha`+`tolunnet-release`) — **first GitHub run pending push**
- `.gitignore`: `__pycache__/`, `*.pyc`; tracked pyc untracked

### B.2 `tests/amiga/SocketConformance.c` — built by `make all` (target CONF_BIN)
17 TAP cases; deliberate reds document TNET-077; skips carry milestone IDs;
`tc_icmp_raw` inverted to honest semantics (fd granted without validation = not ok).

### B.3 bench — WORKS on a1200, blocked on 68000 by TNET-085
- `ci/bench.sh` (Git Bash; stages HDF copy via WSL xdftool; headless WinUAE; kills
  by image name — refuses if winuae64 already running; MuForce hook = explicit SKIP,
  tool absent → QUESTIONS.md #4)
- `ci/tolunnet-a1200.uae`, `ci/tolunnet-68000.uae` (A600 KS3.1, NTSC, chip2+bogo4),
  `ci/User-Startup-Conformance` (2 start/conformance/stop cycles, `Stack 32768`,
  Wait 15 for DHCP)
- xdftool lessons: **delete needs slash paths** (`S/User-Startup`), write fails on
  existing name → always delete-first
- **a1200 baseline (L3 mechanics proven):** both cycles ran, clean shutdown sequence
  logged, cycle-2 restart needed `S2_CONFIGINTERFACE: already configured (ok)` =
  TNET-060 proof. Logs: `docs/bench-logs/20260905-170525-3082b59/` (a1200 only) and
  `20260905-170923-3082b59/` (a1200 ok + 68000 empty). Tally per log: ok=12/11,
  not_ok=5/6 (TNET-077 ×3, TNET-084 ×2, tc_dns_a — DHCP never leases, see below)

## RESOLVED bug — TNET-085 (Proven on 68000 & A1200 bench)
- **Root Cause:**
  1. Motorola 68000 raises Address Error Vector 3 (#80000003) on odd addresses.
  2. Ethernet header is 14 bytes; without `ETH_PAD_SIZE 2`, IP header in lwIP starts at offset 14 (misaligned u32/u16 accesses).
  3. `ETH_PAD_SIZE 2` required proper framing in SANA-II: `pbuf_add_header(2)` in RX before `netif->input` so `ethernet_input` strips 16 bytes (14+2) leaving payload 4-byte aligned, and `pbuf_remove_header(2)` in `tn_sana2_linkoutput` before building Ethernet frame.
  4. lwIP memory pools lacked 4-byte alignment declaration: added `LWIP_DECLARE_MEMORY_ALIGNED` in `include/arch/cc.h`.
  5. `Makefile clean` was missing `build/vendor/`, leaving stale unaligned objects. Fixed to `rm -rf $(BUILD)`.
- **Bench Proof (`docs/bench-logs/20260905-183850-3082b59/`):**
  - A600/68000 bench ran 2 consecutive daemon start/stop cycles cleanly. Zero Gurus.
  - Real DHCP lease acquired on slirp: `10.0.2.15`, netmask `255.255.255.0`, gateway `10.0.2.2`.
  - TAP results identical across A1200 and 68000 (`ok=11, not_ok=6, skip=6`).

## Next Phase: §C Implementation (TNET-084 & TNET-077)
- **TNET-084:** `socket()` type/protocol validation (reject unsupported domains/types with `ESOCKTNOSUPPORT`/`EPROTONOSUPPORT`; reject `SOCK_RAW` until implemented; will turn `tc_socket_types` and `tc_icmp_raw` green).
- **TNET-077:** Daemon IPC handlers for `bind`, `listen`, `accept`, `shutdown`, `getsockname`, `getpeername` (will turn `tc_bind_udp`, `tc_shutdown_wr`, `tc_getpeername` green).

## Commit plan (tree is intentionally uncommitted mid-investigation)
1. `test: harness` — §B files: tests/, src/common/*.{c,h} (new units), lib_vectors.c,
   prefs.*, Makefile, CI, ci/bench.sh + uae + User-Startup, QUESTIONS/ISSUES,
   docs/bench-logs/a1200 baselines. (`make all && make package` before commit.)
2. Alignment fix (lwipopts.h + sana2_netif.c) — separate commit ONLY after Step 1/3
   evidence (or revert if it turns out wrong); label honestly per LAW A.1.
3. Version stays 1.1.x; SCOPE-v4/§E untouched this session.
