# QUESTIONS.md

Strategic questions and architectural resolutions for `tolunnet`.

Status: `open` · `answered` · `superseded`.

---

## Resolved & Answered Decisions

1. **[answered] Project Identity & Executable Naming.**
   - Resolved: Strictly **`tolunnet`** (two 'n's). Main background daemon binary is `tolunnet`, with standard CLI utilities (`ping`, `ifconfig`, `netstat`, `wget`, `curl`) installed in `SYS:C/`.

2. **[answered] Toolchain & Architecture.**
   - Resolved: `m68k-amigaos-gcc` (GCC 6.5.0b) in WSL Ubuntu targeting Motorola 68020+ with pure integer emulation (`-msoft-float -noixemul`), running seamlessly without an FPU coprocessor.

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
