# TolunnetPrefs — crash #8000000B root cause + fix

## Symptom
Double-clicking TolunnetPrefs on Workbench → Software Failure "TolunnetPrefs
Program failed (error #8000000B)" before the preferences window appears.
Persisted across 3 fix attempts.

## What #8000000B means
68k exception vector 11 = **Line-F emulator trap**. On a no-FPU 68k it fires
either on a real F-line (0xFxxx) instruction OR — the case here — when the CPU
**jumps into garbage** (a corrupted return address / bad pointer whose bytes
decode as an F-line opcode). The build is `-msoft-float`, so there are no real
FPU instructions → it's a jump-to-garbage inside the GUI setup.

## What was NOT the cause (already correct — don't touch)
- Icon type: `TolunnetPrefs.info` do_Type = 3 (WBTOOL). ✓
- Icon stack: do_StackSize = 16384 bytes. ✓ (16 KB is ample; not a stack
  overflow.)
- Logic: the GadTools/event/save code is a clean, textbook panel.
- Flags: `-msoft-float` everywhere → no FPU code emitted.
These are why editing the logic 3× never helped — the bug wasn't in the logic.

## ROOT CAUSE
`src/cmds/TolunnetPrefs.c` set `ng.ng_TextAttr = NULL` for every gadget, with
the comment "Strictly NULL -> uses default Screen font". **That belief is
wrong.** GadTools requires a valid `struct TextAttr *` in `NewGadget.ng_TextAttr`
— it dereferences it to open the label font and to size each gadget. A NULL
here makes GadTools follow a NULL/garbage font pointer during `CreateGadget`
(and label rendering), which crashes before the window opens — exactly the
observed timing. The RKRM GadTools example always sets a real TextAttr
(topaz.font/8) for this reason.

## FIX (applied to src/cmds/TolunnetPrefs.c)
1. Added `#include <graphics/text.h>` (for struct TextAttr).
2. Added a file-scope real font:
   `static struct TextAttr g_gui_font = { (STRPTR)"topaz.font", 8, 0, 0 };`
   (topaz.font is always in ROM, so opening it never fails.)
3. Changed the shared NewGadget line to `ng.ng_TextAttr = &g_gui_font;`
   — one assignment, reused by all 14 gadgets.

## To verify
Rebuild TolunnetPrefs (`make`) on the amiga-gcc toolchain, copy the new binary
to the bench, double-click the icon. The panel should now open. NOTE: this
container has no m68k toolchain, so the fix is source-correct but was NOT
compiled/run here — build and test on the bench to confirm.

## Optional, unrelated (not the crash)
- Makefile CFLAGS say `-m68000` while the header comment / STATUS target is
  "68020+". PiStorm is 68020+, so `-m68020` is valid and a bit faster; harmless
  either way. Change only if you want to match the stated target.
