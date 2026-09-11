# Tolunnet STOP-REPORT: ANX-02 bench — recurring 68000 freeze in bsdsocktest #32 (TNET-115)

**Timestamp:** 2026-09-09 20:38
**Stop rule fired:** bench red twice in a row (ANX-02 rule 4).
**Last green bench:** `docs/bench-logs/20260909-170622-20d951e/` (ANX-01 proof, ALL-GREEN both profiles).
**Commits this session:** `726b33c` (W4 part 3), `a392d82`/`20d951e`/`2beb9ff` (docs),
`989eebf` (ANX-01), `9bca042` (ANX-02), `e4039fa` (TNET-115 evidence), tree clean.

## 1. What was delivered

- **W4 Part 3 / TNET-110 (resolved):** wizard Advanced sibling window, 5-check
  Test page with advice + Save log… (ASL with RAM: fallback), Host/Domain,
  FONT= chain (CLI > ToolType > config key > screen font) with TolunnetPrefs
  "Large text", multi-network Wireless.prefs with priority=, `tc_wizard_ntsc`
  geometry proof + PAL/NTSC IFF screenshots. Clean proofs: 68000
  `20260909-152832-726b33c` (35/35 ×2), a1200 `20260909-152558-726b33c`
  (35/35 ×2 ALL-GREEN).
- **ANX-01 (`989eebf`):** `bsdsocket_emu=false` gated in both .uae configs,
  bsdsocktest vendored (GPL-3, tbdye@cb08680) and built with project flags,
  runs in bench cycle 1 before SocketConformance (after-cycles arrangement
  froze: third daemon had no bsdsocket.library), SUMMARY.txt score lines.
  Clean proof `20260909-170622-20d951e/`: ALL-GREEN, **bsdsocktest 102/142
  on both profiles**, identical 25-row failure set → ISSUES TNET-114…138.
- **ANX-02 (`9bca042`):** compat table generated from three sources
  (SFD offsets/prototypes; lib_vectors.c + ipc_dispatch.c → BUILT/BROKEN/STUB;
  latest bench TAP → PASS column). `docs/compat.md` deleted (unique §3 moved
  to TOLUNNET-COMPAT §5, §2 links the generated table), README lvo-stats
  block now generated (66 BUILT / 55 STUB / 0 BROKEN — old "61/72" was
  stale), `make python-checks` regenerates + git-diff gate, generator
  asserts IMPLEMENTED_FUNCS against lib_vectors.c.

## 2. The two red benches (verbatim)

```
20260909-201118-9bca042 / 68000: TIMEOUT 600 s; conformance.log 0 rows;
    bsdsocktest.log frozen at 41 lines, last row "ok 31 - sendmsg()/recvmsg():
    single iovec" → frozen inside test #32 (scatter-gather multi-iovec)
20260909-202632-e4039fa / 68000: TIMEOUT 600 s; conformance.log 0 rows;
    bsdsocktest.log frozen at the identical point (after ok 31)
(a1200 in both runs: core 35/35 ×2 green)
```

Because bsdsocktest runs first in the boot script (ANX-01 ordering), the
freeze kills the leg before SocketConformance starts, so the core suite
never runs on 68000.

## 3. Analysis

- The freeze is **TNET-115** (bsdsocktest #32 sendmsg/recvmsg scatter-gather):
  3 of 6 full 68000 runs froze at the byte-identical point (`20260909-165110`,
  `20260909-201118`, `20260909-202632`); a1200 always completes (#32 fails
  functionally there but never hangs). It predates ANX-02: first occurrence
  was during the ANX-01 clean bench at commit `989eebf`.
- ANX-02 changed **no Amiga-side code** (generator/docs/Makefile only —
  `git show --stat 9bca042`), so it cannot be the cause; it is the
  bystander whose bench proof is blocked.
- ANX-02 rule 1 forbids fixing another TNET inside this step, and the stop
  rule has now fired → stopping as designed.

## 4. Suggested next steps (owner decision)

1. Run **ANX-04** ("68000 freeze diagnosis: health counters") next — it was
   written for exactly this class of freeze; TNET-115 now has three
   byte-identical repro logs to work from (`tolunnet-task.log` in each dir
   ends mid-socket-creation, daemon silent).
2. After the freeze is fixed or bounded, re-run `ci/bench.sh` once at
   `9bca042`+ to mint the ANX-02 clean-proof bench dir (the ANX-02 commit
   itself needs no changes: acceptance greps pass, `make python-checks`
   green, codegen byte-stable).
3. Optional interim mitigation if a green bench is needed before ANX-04:
   add a tolunnet profile to bsdsocktest's `known_failures.c` marking #32
   KNOWN_CRASH (skips the hang on both profiles; masks the a1200 functional
   failure of #32, which stays documented in TNET-115).

Evidence dirs: `docs/bench-logs/20260909-201118-9bca042/` and
`docs/bench-logs/20260909-202632-e4039fa/` (kept).

---

## 2026-09-11 CLOSURE

**Reopened and closed by:** TNET-139 session (owner-directed).
- The blocked ANX-02 clean-proof bench now exists: `20260911-001150-5f71f10`
  (68000 ALL-GREEN at the TNET-139 fix commit, Fast-RAM profile) plus
  a1200 35/35 ×2 in `20260910-235753-5f71f10` (same commit; its 68000 leg
  froze — TNET-115 occurrence #4, recorded in ISSUES.md).
- TNET-115 remains OPEN (4 occurrences, byte-identical point); diagnosis is
  the ANX-04 next step, now equipped with the WinUAE debugger automation in
  `ci/debugger/`.
- This report is historical; no stop rule is currently active.
