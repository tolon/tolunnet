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
---

## 2026-09-15 STOP (TN-bugtrack-2 item 1): pistorm-68000 bench red twice (TNET-115)

**Stop rule fired:** bench red twice in a row.
**Last green bench:** `docs/bench-logs/20260912-002745-5f1086c/` (a1200 + 68000, 35/35 x2 both).
**Commits this session:** `35f803e` (item 1: DIAG crash capture), tree clean.

### Delivered (item 1 - TNET-139 DIAG)

- `DIAG=YES` config key: task-local CPU trap handler (tc_TrapCode) for the
  bus/address/illegal vectors writing PC, SR, fault address (68000 and
  68010+ frame readings), raw frame, registers, hunk bases and the last 16
  log lines to `RAM:tolunnet-crash.log` BEFORE the Guru, then chaining to
  the previous handler. Startup step logging end-to-end with per-line Flush.
- **Proven functional inside the red runs themselves**: task logs show
  `s2: OpenDevice... / err=0 / DEVICEQUERY err=0 MTU=1500 addr_bits=48 /
  GETSTATIONADDRESS... / CONFIGINTERFACE... / ONLINE...` (both dirs).
- `ci/tolunnet-pistorm-68000.uae` (68000, chip-only, a2065 stand-in; KS 3.1 -
  no 3.2 ROM exists, QUESTIONS.md [auto] 6-8) + `TN_DIAG=1` bench hook.
- `docs/OWNER-RETEST.md`: 3-command owner procedure.
- Host tests green (16 binaries, new DIAG parse case), `-Werror=cast-align`
  clean. Commit `35f803e`.

### The two red benches (verbatim)

```
20260915-184929-35f803e / pistorm-68000: TIMEOUT 600 s; conformance 0 rows;
    bsdsocktest.log 41 lines, last "ok 31 - sendmsg()/recvmsg(): single iovec"
20260915-190106-35f803e / pistorm-68000: TIMEOUT 600 s; conformance 0 rows;
    bsdsocktest.log 41 lines, identical last line
```

Both are the **TNET-115** signature (freeze inside bsdsocktest #32,
scatter-gather). The new chip-only 68000 profile hit it 2 of 2 runs -
overall tally now 8 occurrences. The DIAG build is a bystander: the daemon
completed cycle 1 normally in both (task log ends at the usual RECONFIG
restart request; no CPU exception fired - the freeze is not a daemon Guru).

### Analysis / handoff

- Item 1's deliverable is code-complete and functional; missing is a green
  bench at `35f803e` - blocked by TNET-115 on the new profile. Default
  profiles were not run after the stop rule fired; DIAG is config-gated OFF
  by default, so default-profile risk is the two new object files plus
  additive ring/step logging.
- The new profile is now the **best TNET-115 repro rig**: 2/2, slowest CPU
  config; item 2 (root cause: `il 8`/`fi` live capture, `Tt` task dump,
  ipc_msg invariants) should start from it.
- Owner package rebuilt from `35f803e` after the stop for the promised
  retest (no bench claim attached; default paths config-gated unchanged).

### Next steps (owner decision)

1. Item 2 (TNET-115) first with the pistorm-68000 rig; then the standard
   dual bench mints the green for `35f803e`+fix.
2. Owner retest with the DIAG build per `docs/OWNER-RETEST.md` - the crash
   log closes TNET-139 independently of bench state.
3. Items 3-10 of TN-bugtrack-2 unchanged.
