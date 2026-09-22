# QUESTIONS.md

Strategic questions and architectural resolutions for `tolunnet`.

Status: `open` · `answered` · `superseded`.

---

## Resolved & Answered Decisions

1. **[answered] Project Identity & Executable Naming.**
   - Resolved: Strictly **`tolunnet`** (two 'n's). Main background daemon binary is `tolunnet`, with standard CLI utilities (`ping`, `ifconfig`, `netstat`, `wget`, `curl`) installed in `SYS:C/`.

2. **[answered] Toolchain & Architecture.**
   - Resolved: `m68k-amigaos-gcc` (GCC 6.5.0b) in WSL Ubuntu, universal Motorola 68k binary built with `-m68000 -msoft-float -noixemul` — runs on 68000 through 68060 without an FPU coprocessor. (Single source of truth per TNET-074: shipped binaries scanned with `objdump`, no 68020+ opcodes; evidence in ISSUES.md. Older "68020+" statements in historical docs are superseded.)

3. **[answered] Roadshow SDK & Standard Specification.**
   - Resolved: Roadshow SDK 1.8 at `E:\amiga\Amigatolon\roadshow\Roadshow-SDK-1.8` and `sfd/bsdsocket_lib.sfd` serve as the normative bsdsocket API specification.

4. **[answered] SANA-II Driver Specification.**
   - Resolved: SANA-II Rev 7 standard is strictly followed with 68k assembly register trampolines (`A0/A1/D0`) and persistent `bm_tags` in `TnSana2If`.

5. **[answered] lwIP Version & Integration Model.**
   - Resolved: Pinned lwIP 2.2.0 compiled with `NO_SYS=1` in a single Exec task context, driven by a 100ms `timer.device` ticker.

6. **[answered] Dynamic Library Instantiation (TNET-012).**
   - Resolved: `bsdsocket.library` is dynamically created via `MakeLibrary` and registered via `AddLibrary` upon network task startup, and removed on clean stack shutdown.

7. **[answered] Roadshow / Miami DX Compatibility Vectors (M6).**
   - Resolved: Implemented Tier 1 compatibility suite: `SocketBaseTagList` (-294), `getservbyname` (-234), `getservbyport` (-240), `getprotobyname` (-246), `getprotobynumber` (-252), `Inet_LnaOf` (-186), `Inet_NetOf` (-192), `Inet_MakeAddr` (-198), `inet_network` (-204), `gethostname` (-282), `gethostid` (-288), `Dup2Socket` (-264).

8. **[answered] Preferences Panel GUI Framework (TNET-033).**
   - Resolved: `TolunnetPrefs` is built using native `intuition.library` & `gadtools.library` (ROM 2.04+), eliminating external MUI runtime dependencies.

9. **[answered] Unified Configuration Architecture (TNET-032 / TNET-044).**
   - Resolved: Single source of truth is `DEVS:tolunnet.config` (human-readable `KEY=VALUE` text format), mirrored to `ENVARC:tolunnet.prefs`.

---

## Active Testing & Integration Horizon

1. **[active] Hardware Validation:** Live network testing on real Amiga 1200 / Amiga 500 + PiStorm / A2065 ethernet controllers.
2. **[active] Application Suite Gauntlet:** Validating AmiSSL 5.x, IBrowse 2.5.x, smbfs, and Aminet downloaders against the stack.

---

## Open Questions (Round 3)

3. **[open] SFD vector count: 133 or 139?** `TOLUNNET-SCOPE-v4-full-api.md` §0 says the
   SFD declares "133 public functions (LVO −30 … −828)". Parsing
   `sfd/bsdsocket_lib.sfd` as shipped (==varargs twins share slots, ==reserve
   slots counted) yields **139 slots, −30 … −858** (121 named functions +
   18 reserved: 10 + 2 + 6). The C-side parser in
   `tests/host/test_lvo_table.c` and the §D generator follow the SFD (139).
   Please confirm the intended counting convention — if "133" was meant to
   exclude the final `==reserve 6` plus one more slot, the §D table size
   needs an explicit owner decision. Until answered, the SFD is normative.
4. **[open] MuForce/Enforcer for the bench second pass (Round 3 §B.3):** neither tool
   is present on this bench (searched `E:\amiga` and Downloads on
   2026-09-05). Supply an ADF/LhA with MuForce + MuGuardianAngel (68020
   config) or Enforcer + Wipeout and set `MUFORCE_ADF` in `ci/bench.sh` to
   enable the memory-hit pass; it currently prints an explicit SKIP.

## Auto-Decisions (implementation-time, TNET-110 part 3 — 2026-09-09)

1. **[auto] FONT= precedence:** CLI argument > icon ToolType > `DEVS:tolunnet.config` FONT= key > screen font (the step file's stated default). Rationale: more specific launch contexts override the general config; the config key is what TolunnetPrefs "Large text" writes (`FONT=topaz/11`, removed when unchecked).
2. **[auto] DNS 2 in DHCP mode:** written to the config only when the Advanced checkbox "Use DHCP DNS, fall back to DNS 2" is set (Manual mode always writes DNS2=). Keeps wizard output byte-identical to before unless the user opts in.
3. **[auto] Advanced window contents follow the step file** (priority, log file, DATABASE_ORDER, DEVS:Internet checkbox, DNS2-fallback checkbox). The STOP-REPORT punch-list's "NTP/IPv6 placeholder" items are not in the normative page-4 spec and were skipped.
4. **[auto] "Run tests again (default until all pass)":** RETURN on the Test page runs the tests instead of Finish while any of the 5 checks has not passed; ARexx FINISH is never blocked (bench compatibility).
5. **[auto] Screen title uses ASCII `-`** instead of the em-dash in the step text (topaz cannot render U+2014; the dash is decorative).
6. **[auto] tc_wizard_ntsc proof shape:** the wizard self-reports `ENV:TolunnetSetup.geom` (page, window rect, screen size, lowest gadget edge, compact flag) after every page rebuild; the test walks all 5 pages via the port and asserts window<=screen and lowest gadget < screen bottom on both configs (NTSC 68000 leg runs the actual 640×200 case), and dumps IFF screenshots of the wizard screen to WORK: for the bench log.
7. **[auto] Clipboard "Copy report"** (STOP-REPORT punch list) is not in the step-file page-5 spec — skipped; "Save log..." covers reporting.

## Auto-Decisions (implementation-time, ANX-01 — 2026-09-09)

1. **[auto] bsdsocktest runs BEFORE SocketConformance in cycle 1**, on the fresh bench-config daemon — not after the cycles on a third daemon. Evidence: the after-cycles arrangement failed (`20260909-154725-a392d82-dirty/a1200`): the third daemon provided no bsdsocket.library (wizard-rewritten config / daemon stop-restart race) and the leg froze before bench-done. Reordered arrangement proven end-to-end on both profiles (ALL-GREEN, 154 s / 258 s to bench-done).
2. **[auto] LOOPBACK tier only** (`bsdsocktest LOOPBACK NOPAGE LOG WORK:bsdsocktest.log`): the NETWORK tier needs a host helper which the hermetic bench does not provide; its 15 rows SKIP by design ("host helper not connected").
3. **[auto] bsdsocktest failures do not gate the bench** — each failing row becomes its own OPEN ISSUES row (fixes are explicitly out of scope for ANX-01). Only the core 35-test suite must stay green on both profiles.
4. **[auto] bsdsocktest is bench-only, not distributed:** vendored under `vendor/bsdsocktest/` (GPL-3, commit cb08680, notice in THIRD_PARTY_LICENSES.md), built by the `bsdsocktest` Makefile target with the project's `-m68000 -msoft-float -noixemul` flags (upstream Makefile's `-m68020` overridden per TNET-074 universal-binary rule).

## Auto-Decisions (implementation-time, ANX-02 — 2026-09-09)

1. **[auto] Status derivation:** BUILT = `tn_lvo_<name>` body exists in `src/lib/lib_vectors.c` and every `TN_IPC_CMD_*` it marshals has a non-NULL handler in `src/task/ipc_dispatch.c` (or it needs no IPC at all); BROKEN = marshaled command with NULL/missing dispatch entry; STUB = no body (honest stub). SFD names are mixed-case, C handlers lowercase — matching is case-insensitive. Current truth: 66 BUILT / 55 STUB / 0 BROKEN (README's old "61 implemented / 72 stubs" was stale and is now generated).
2. **[auto] PASS column** comes from the newest bench dir's `68000/conformance.log` TAP — only GREEN `tc_*` tests earn the label; the tc→API map lives in `TC_COVERS` in the script.
3. **[auto] `docs/compat.md` §3 ("What the OS knows about the stack") moved verbatim to `TOLUNNET-COMPAT.md` §5** instead of being deleted with the rest of the file — it is interface documentation, not a status table; §1 (LVO table) is superseded by the generated table, §2 (app matrix) was stale and contradicted the README.
4. **[auto] Staleness gate:** `make python-checks` regenerates all LVO outputs and fails on `git diff --exit-code` over them (worktree vs index) — the AmiNetXDuo `check-vector-abi.sh` logic; post-commit this equals worktree vs HEAD.
5. **[auto] The generator refuses to run when `IMPLEMENTED_FUNCS` drifts from `lib_vectors.c`** — the static set stays as the codegen input but source is the asserted truth.
6. **[auto] pistorm-68000 bench profile Kickstart:** the work order asks for KS 3.2; no 3.2 ROM exists in `E:\amiga\Shared\rom` (closest: 3.1 A600). KS 3.2 is a Cloanto ROM update with no SANA-II/DOS interface changes relevant to this path, so the profile uses KS 3.1 — swap the ROM line if a 3.2 image is provided.
7. **[auto] PiStorm CPU mode decision:** PiStorm runs Emu68; Emu68's default core is 68ec020-class, but the work order pins this profile to a plain 68000, which also matches the strictest Address-Error behaviour — the profile follows the order. The owner-side DIAG build is universal `-m68000`, so the same binary covers both.
8. **[auto] a2065 stands in for wifipi.device** in all PiStorm profiles: the real driver talks to the RPi's SDIO wifi chip through Emu68 mailboxes that WinUAE does not emulate. The SANA-II contract our code depends on (register-convention copy hooks, DEVICEQUERY, ONEVENT) was verified against the WiFiPi source directly (TNET-139 notes).

## Auto-Decisions (independent audit, Step A — 2026-09-21)

1. **[auto] Untracked files handling (Step A.5):** User explicitly directed that session markdown files (`TN-*.md`) are for agent guidance and must not be committed to git. Added `TN-*.md`, `.agent`, `.ignore`, `AGENTS.md`, and `graft/` to `.gitignore`. Intermediate soak runs (`docs/bench-logs/*-soak-*/`) and `ci/.soak-tolunnet.config` are `.gitignore`d pending the official 24 h soak run in Step B.7.
2. **[auto] ISSUES.md L3 audit standard:** Strict adherence to Step A.2: every row marked RESOLVED with rung L3 whose cited bench does not exist, does not contain both profiles (`a1200`, `68000`), lacks `dirty: NO`, or contains `not ok` lines outside WONTFIX #27/#64 was flipped to `OPEN (audit)`. This correctly identified 24 ANX-01 bsdsocktest rows (TNET-115..138) citing dirty baseline failure logs, TNET-110 citing incomplete/red logs, TNET-141 without cited logs, and pre-harness rows TNET-073..094 predating the dirty check.
3. **[auto] Audit bench run on HEAD (Step A.3):** Full bench run on commit f55538d (`20260921-163058-f55538d`) passed 68000 (58/58 ×2) and cycle 1 a1200 (58/58), but failed cycle 2 a1200 with `not ok 1 - lib_open` due to daemon restart failure, empirically confirming that TNET-152 (scheduled for Step B.6) remains an open defect.

## Auto-Decisions (TNET-152 relaunch half, Step B.6 — 2026-09-21)

1. **[auto] Orderly teardown before stop reply:** SANA-II `CloseDevice`, DHCP stop, and network interface removal must occur BEFORE Exec `RemPort` and stop message reply. The reply is parked in `d->stop_msg` and sent only after hardware and port cleanup is complete, guaranteeing that `TolunnetControl STOP` returns only when the system is completely free.
2. **[auto] Library deallocation standard:** `bsdsocket.library` created via `MakeLibrary` (Exec `AllocMem`) must be freed using `FreeMem((UBYTE *)lib - lib->lib_NegSize, (ULONG)(lib->lib_NegSize + lib->lib_PosSize))` rather than `FreeVec`. Calling `FreeVec` on `MakeLibrary` memory reads garbage as the allocation size and corrupts Exec's memory list on 68020/Fast RAM.
3. **[auto] Configuration LOG= preservation:** `TolunnetSetup` and `tc_reconfig_rc` preserve the `LOG=` configuration parameter across rewrites so subsequent daemon instances log to `WORK:tolunnet-task.log`.
4. **[auto] Relaunch proof shape:** `tc_cmd_stop_start` tests user-facing `TolunnetControl STOP` (refused while clients open with RC 5 / EBUSY, then succeeds with RC 0), port disappearance verification (waits up to 100 × 5 ticks), `TolunnetControl START` (RC 0), port appearance and UDP socket roundtrip verification, followed by clean final STOP. Both profiles (68000 and A1200) achieved 58/58 ok × 4 in bench `20260921-182756-ccad270` with multiple "lwIP 2.2.0 initialized" banners in `tolunnet-task.log` (lines 1, 1008, 1032 on both profiles).

