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
