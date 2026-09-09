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
