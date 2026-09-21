# tolunnet — Independent Antigravity Audit (Step A)

Date: 2026-09-21  
Auditor: Antigravity (Architect)  
Commit target: `docs: AG audit`  

## Summary Checklist (One line per check: PASS/FAIL + evidence path)

- **Check 1 (Clean build & test in temp clone):** PASS — `/tmp/tolunnet-audit` (`m68k-amigaos-gcc` 6.5.0b 20260731, host gcc 13.3.0, python 3.12.3, make 4.3; `make` 0 errors, 18 host test binaries 0 failed under ASan/UBSan, `python-checks` pass; unused parameter/var warnings in CLI commands recorded).
- **Check 2 (ISSUES.md L3 RESOLVED rows verification):** PASS — `ISSUES.md` (54 rows flipped to `OPEN (audit)`: 24 ANX-01 bsdsocktest rows TNET-115..138 citing dirty baseline logs with 25 failing rows, TNET-110 citing incomplete/red logs, TNET-141 without cited bench log, and pre-harness rows TNET-073..094 predating the `dirty: NO` check).
- **Check 3 (Full bench on HEAD vs newest ALL-GREEN dir):** FAIL — `docs/bench-logs/20260921-163058-f55538d/` (68000 passed 58/58 ×2, bsdsocktest 126/142; a1200 cycle 1 passed 58/58, but cycle 2 failed with `not ok 1 - lib_open # bsdsocket.library not found` confirming open TNET-152 relaunch defect scheduled for Step B.6).
- **Check 4 (Package & count synchronization against rc2):** PASS — `build/tolunnet-1.2.0-rc2.lha` (SHA256 `449e351f805ab0f0a953e9943e0674fe871be46967261079c1fc4291b4019cb8`), `build/tolunnet.adf` (SHA256 `7451f1c4eb52771ce7e2d871653c83d36a49c186843ee152d84fb41782082794`); `README.md` packaging reference updated to rc2.lha; `src/lib/lib_compat_table.gen.md` drift updated; counts verified at 70 BUILT / 51 STUB / 0 BROKEN.
- **Check 5 (Tree cleanliness & untracked items decision):** PASS — `.gitignore` updated per user directive (`TN-*.md`, `.agent`, `.ignore`, `AGENTS.md`, `graft/`, `ci/.soak-tolunnet.config`, `docs/bench-logs/*-soak-*/`); decisions logged in `QUESTIONS.md`; `git status` clean.
