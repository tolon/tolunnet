# Decisions for your Socratic Gate — proceed, no further questions.

1. Order: finish **W4 Part 3** (Advanced window, Save log…, `tc_wizard_ntsc`, TNET-110 row) as ONE commit with a clean dual-profile bench (core 100 %). Only then start the AmiNetXDuo series at `01-bench-bsdsocktest.md`, in its numbered order. Do not merge `07a/07b/08` into W4. TNET-112 stays its own row and its own commit.
2. "Advanced…" is a sibling window, not modal: open its own `Window`, add its `UserPort` signal to the main `Wait()` mask; while open, `GT_SetGadgetAttrs(GA_Disabled, TRUE)` on the main window's Back/Next/Cancel, re-enable on close. No `ModifyIDCMP` tricks. Values are written to `TnPrefs` only on OK; Cancel discards.
3. "Save log…": `OpenLibrary("asl.library", 36)`; if it fails, write to `RAM:tolunnet-setup.log` and show the path in an `EasyRequest`. Never crash, never skip silently. `CloseLibrary` in cleanup.
4. STOP RULES unchanged: bench red twice in a row or 3 h without a commit → STOP-REPORT.md.

Report after the W4 commit: hash, bench dir, core/external counts both configs, verbatim `not ok` lines. Then continue with 01.
