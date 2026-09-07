# tolunnet — Round 4 kickoff prompt for Gemini (Antigravity)
# Paste the block below as the first message in a new Antigravity conversation with the tolunnet workspace open.

---

You are the implementation engineer for **tolunnet**, an open-source lwIP-2.2.0-based TCP/IP stack and
`bsdsocket.library` v4.1 for classic AmigaOS (68k). Workspace: `D:\Projeler\tolunnet`. Previous rounds were done by
another model; you inherit their code, tests, bench automation and ledgers. Your job this round is
**`TOLUNNET-ROUND4-prompt.md` in the repo root — read it now, in full. It is the work order.** Then read, in this
order: `docs/history/TOLUNNET-BUGTRACK-v3-prompt.md` §0 (LAW), `docs/history/TOLUNNET-ROUND3-prompt.md` §A
(verification ladder L0–L4) and §B (harness you must reuse), `docs/history/TOLUNNET-SCOPE-v4-full-api.md`,
`ISSUES.md`, `STATUS.md`, `QUESTIONS.md`, `docs/bench.md`, `ci/bench.sh`, `tests/amiga/SocketConformance.c`,
`tests/host/tn_test.h`, `scripts/gen_lvo_table.py`, then all of `src/`, `include/`, `lwipopts/lwipopts.h`,
`Makefile`. Do not analyse anything from memory or assumption; if something looks missing, grep first.

## How to work in this workspace
- **Toolchain runs in WSL**, not Windows: `wsl -d Ubuntu-24.04 -- bash -lc "cd /mnt/d/Projeler/tolunnet && make all"`.
  `make test-host` (native gcc + ASan/UBSan) also in WSL. `ci/bench.sh` runs from Git Bash on Windows and drives
  WinUAE headless (see its header for env vars). `xdftool` lives in WSL at `$HOME/.local/bin/xdftool`.
- Before writing code, produce an **Implementation Plan artifact** that maps every table row of
  `TOLUNNET-ROUND4-prompt.md` §B–§F to concrete files, the conformance test that will flip from SKIP/absent to
  `ok`, and the commit message. I will review the plan before you execute. Then keep a **Task list artifact**
  updated per row; at the end of each lettered section produce a **Walkthrough artifact** with: commit hashes,
  `make test-host` TAP summary, and the `docs/bench-logs/<stamp>/` directories with their `conformance.log` results
  (paste the `not ok` / `# SKIP` lines verbatim, never summarise them as "all green").
- **Verification is the product.** A change without a failing-then-passing test or a bench log is not done —
  mark it `FIXED (unverified)` in ISSUES.md and say so. Never claim a bench run you did not execute; if WinUAE
  cannot be launched from your terminal session, stop and tell me — do not fabricate `docs/bench-logs/` content.
  Rungs: L0 compiles · L1 host test · L2 static/objdump/MuForce · L3 WinUAE log (both `a1200` and `68000` configs)
  · L4 real hardware (owner only).
- Commit per lettered section with conventional-commit messages; tree clean after each. `make all && make package`
  (produces both `.lha` and `.adf`) must be green at every commit.
- Hard limits: never edit `vendor/lwip/`; never change `-m68000 -msoft-float -noixemul -O2 -fomit-frame-pointer`;
  GadTools/Intuition only (no MUI/ReAction); the name is `tolunnet` (two n). **No hard-coded values anywhere** —
  every tunable (pool sizes, timeouts, priorities, paths, database order, log tiers) is a config key or a
  `#define` in one header with the key documented in `README.guide`.
- All SDK numbers (errno, `SBTC_*`, `SIOC*`, `SO_*`, struct layouts) come from Roadshow SDK 1.8
  `netinclude/` and `doc/bsdsocket.doc` at `E:\amiga\Amigatolon\roadshow\Roadshow-SDK-1.8` — quote the header,
  never a literal. Gaps or ambiguities go to `QUESTIONS.md`; pick the AmiTCP-V4-compatible behaviour meanwhile.

## Decisions already taken (apply, do not reopen)
1. **Resolver buffers are per-opener** (`TnSocketBase`), never daemon-side: the daemon copies *data* into the
   caller's base over IPC and never returns daemon pointers. `hostent`: `h_addr_list` 8, `h_aliases` 4, name 256.
   `getaddrinfo` results: `AllocVec(MEMF_PUBLIC)` lists tracked per opener, freed by `freeaddrinfo` and swept by
   `CloseLibrary`. `_r` variants write only to caller buffers.
2. **Database file precedence:** `DEVS:tolunnet.config` / `DEVS:Internet/<file>` → `AmiTCP:db/<file>` (only if the
   assign exists and the Roadshow file is absent — per file, not per directory) → built-in IANA defaults. Order is
   parametric via `DATABASE_ORDER=` config key. First match wins within a file; the effective source of each file
   is logged once at BASIC tier.
3. Ledger numbering: TNET-066…075 were reserved and used; new items continue from **TNET-086** (see ISSUES.md note).

## Start
Begin with `TOLUNNET-ROUND4-prompt.md` **§B** (ledger hygiene) — it is small and lets me check your workflow —
and post the Implementation Plan for §C–§F together with the §B commit. Do not open §E (application gauntlet)
until every conformance test on both bench configs is `ok` with zero `not ok` lines.
