# QUESTIONS.md

Open questions for the human (tolon). The implementing model never silently
resolves these; it records them here and stops short. (Master prompt §4.1, §11)

Status: `open` · `answered` · `superseded`.

## From master prompt §11 (seeds)

1. **[open]** Final name; `tolunet.device` vs library-only naming.
2. **[open]** Roadshow extension tags (AddRouteTagList etc.): honour which,
   refuse which loudly?
3. **[open]** ART Baseline catalogue row — coordinate after M7.
4. **[open]** OS4/MorphOS/AROS: keep porting hooks clean or ignore outright?
5. **[open]** wifipi + WirelessManager credential file paths (VERIFY items;
   human reads driver docs on bench).
6. **[open]** Tier-3 (OS 1.3) go/no-go after M10.
7. **[open]** Turkish `.catalog` proofread by author.
8. **[open]** PaulaNET licence answer from RobSmithDev; allowed integration depth.

## Raised during M0

9. **[open]** **Toolchain location.** Master prompt §3 / §3.1 links
   `https://github.com/bebbo/amiga-gcc`, but that repo has moved to
   [AmigaPorts/m68k-amigaos-gcc](https://github.com/AmigaPorts/m68k-amigaos-gcc)
   (prebuilt packages available; AUR upstream now
   [BlitterStudio](https://aur.archlinux.org/packages/m68k-amigaos-gcc)). The CI
   uses the AmigaPorts prebuilt. Confirm this is acceptable, or name the exact
   toolchain/version to pin. Decision does not change the `-m68020 -noixemul`
   build flags.

10. **[open]** **SDI headers staging.** §3.1 names
    `https://aminet.net/dev/c/SDI_headers.lha`. The dev bench
    (`E:\amiga\Amigatolon`) should already have it; please confirm the path so
    the bsdsocket.library skeleton (M3) can `#include <SDI/SDI_lib.h>`. Until
    confirmed, the library skeleton is not written.

11. **[open]** **lwIP pin confirmation.** Vendored `lwip-2.2.0.zip` from
    `https://download.savannah.gnu.org/releases/lwip/`. §3.1 says "pin latest
    stable tag"; 2.2.0 is the pinned release. A newer 2.2.1 exists — confirm we
    stay on 2.2.0 (pinned) or bump.

## Raised during M0 — §3.1 SDK reference additions (master prompt update)

The master prompt §3.1 gained a "Roadshow SDK 1.8 — primary local reference"
block (paths under `E:\amiga\Amigatolon\roadshow\Roadshow-SDK-1.8`). This binds
the later milestones; recorded here so nothing is missed.

12. **[answered]** **Roadshow SDK version conflict (was 1.8 vs 1.5).** §3.1 line
    77 said "SDK 1.8" (primary reference), line 109 said "DevPack (Roadshow SDK
    1.5)". Human resolved: **1.8 is the valid primary reference; the 1.5 wording
    was a stale leftover.** Master prompt edited: line 109 now reads
    "DevPack (MUI 5 SDK), Roadshow SDK 1.8". No further action.

13. **[answered]** **SANA-II revision conflict (was Rev 7 vs r4/r5 files).**
    §3.1 line 66 names "SANA-II Rev 7" (wiki) as normative; lines 81–82 list
    local files `doc/SANA-II.pdf`, `sana2r4.html`, `sana2r5.html`. Human
    resolved: **Rev 7 stays normative; the local r4/r5 HTML files are
    additional reading that shipped with the SDK, not the binding spec.** M1
    code cites Rev 7; local files are cross-reference only.

14. **[open]** **bsdsocket.library jump table from SFD.** §3.1 now says:
    "`sfd/` + `interfaces/bsdsocket.xml` — function definitions; generate the
    library jump table from the SFD, do not hand-write it." This binds M3. Need
    on bench: the SFD + a generator (`fd2inline` / `sfd` toolchain, or a small
    script). Please confirm the SDK ships the SFD at the named path and which
    generator to use. The library skeleton is NOT written until this is settled.

15. **[answered]** **netinclude errno source for §5.1.** §3.1 names
    `netinclude/sys/errno.h` as the errno value source §5.1 requires. Confirmed
    present and BSD-licensed ("Freely Distributable") at
    `E:\amiga\Amigatolon\roadshow\Roadshow-SDK-1.8\netinclude\sys\errno.h`.
    All §5.1 errno values verified there: ENOBUFS=55, ETIMEDOUT=60,
    EHOSTUNREACH=65, EINPROGRESS=36, EINVAL=22, EWOULDBLOCK=EAGAIN=35,
    EADDRINUSE=48, EALREADY=37, EISCONN=56, ENOTCONN=57, ECONNABORTED=53,
    ECONNRESET=54, ENETDOWN=50, EINTR=4. errno.c (M4) builds from this file.

    **Also closed:** `include/devices/sana2.h` verified against the M1 source.
    Every struct field and constant used in src/sana2/sana2_netif.c is present
    and matches (IOSana2Req, Sana2DeviceQuery, S2_CopyToBuff/S2_CopyFromBuff,
    SANA2_MAX_ADDR_BYTES, all S2_* commands, S2ERR_*/S2WERR_* codes). The
    `/* VERIFY */` tags were removed in the verification commit.

16. **[open]** **CI container choice.** §3 names "bebbo amiga-gcc, Docker image
    or cached CI build". The CI workflow uses the
    `sebastianbergmann/amiga-gcc:latest` Docker image (purpose-built Bebbo
    toolchain, provides `m68k-amigaos-gcc`). Verify this is acceptable vs a
    cached self-built image; the exact GCC version is recorded in STATUS.md on
    first green run. Tied to #9 (toolchain relocation).
