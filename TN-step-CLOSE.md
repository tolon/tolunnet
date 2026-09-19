# tolunnet — step CLOSE: verify everything claimed, finish what is missing, freeze for release. One commit per numbered item; core bench green (both profiles, `dirty: NO`) before each commit; STOP RULES: bench red twice in a row or 3 h without commit → STOP-REPORT.md. No questions — `[auto]` lines in QUESTIONS.md.

## A. Audit first (commit `docs: CLOSE audit`) — no code changes in this commit
1. For each of the 14 shipped commands, list in `docs/commands.md`: binary in `C/`, ReadArgs template, `?` output, RC behaviour, **name of its `tc_cmd_*` conformance test and the bench dir where it last passed**. A command with no passing `tc_cmd_*` gets an ISSUES row `TNET-14x OPEN` and is fixed in §B. Do not mark anything verified without a log dir.
2. Regenerate the compat table (`make python-checks`) and confirm README/STATUS counts (BUILT/STUB, bsdsocktest 126/142, 2 WONTFIX) match the newest bench dir; fix any drift.
3. Re-open **TNET-106**: the SendIO pipelining was reverted (shared IORequest race) — row must say OPEN, not resolved. **TNET-139**: status must read "awaiting owner retest with build <sha>" until the owner reports; never "resolved" without the owner's log.
4. Tree hygiene: `.dbg-shot.png`, `.uae-shot.png`, `_to_delete/`, `Claude outputs/` out of the repo root (`.gitignore` or move); root contains only release docs + `TN-*.md` step files → move finished `TN-*.md` to `docs/history/`.

## B. Missing commands (from TN-step-CMD.md, same rules) — one commit each
5. `route SHOW|ADD DEST [MASK] GW|DELETE DEST|DEFAULT GW` + `AddNetRoute`/`DeleteNetRoute` wrappers; IPC `ROUTECTL`; `src/task/route.c` static table + `LWIP_HOOK_IP4_ROUTE_SRC`; host `test_route` un-SKIPped.
6. `iperf [SERVER/S] [CLIENT HOST] [PORT/N=5201] [SECONDS/N=10]` from `TcpPerf`; numbers into `docs/bench.md` (before/after RX freelist).
7. `AddNetInterface` / `ConfigureNetInterface` / `Online` / `Offline` (Roadshow syntax, `DEVS:Internet/interfaces` reader, IPC `IFCTL`).
8. `CheckNetConfig [FILE]` (RC 0/10, line numbers) and `NetShutdown [FORCE/S]`.
9. Every new command: `tc_cmd_*` in core, `docs/commands.md` row, README.guide entry.

## C. Perf, properly (commit `perf(sana2): TX pipelining with IORequest pool (TNET-106)`)
10. Pool of `TX_QUEUE=` (default 4) separate `IOSana2Req` + buffers, `SendIO`, completion on the RX port; never reuse a request before its reply; `AbortIO`/`WaitIO` all on shutdown. Prove with `iperf` both directions on a1200 + 68000 and 1 h soak (`ping -f` 60 s bursts + `wget` loop) with `AvailMem` drift ≤ 8 KB.

## D. Release gate (commit `release: 1.2.0-rc2`)
11. `NetTrace` is deferred to 1.3 — write it in README "not yet" list; no stub binary.
12. 24 h soak on a1200 profile (`ci/bench.sh soak`): daemon up, `wget` loop every 30 s, `ping` every 10 s, `TolunnetControl STATS` every 10 min into the log; pass = no Guru, no freeze, RAM drift ≤ 8 KB, zero `not ok`.
13. MuForce/MuGuardianAngel pass if the ADF is present under `E:\amiga\Amigatolon\tools\` (owner supplies); else record SKIP honestly.
14. `make package` (lha + adf, SHA256), `CHANGELOG.md` since 1.1.0 by TNET id, `tolunnet.readme` Aminet format, `docs/OWNER-RETEST.md` updated for rc2 (what to run on the A500+PiStorm, where the crash/diag log lands). Version string from one header.

Report: item → commit → bench dir → verbatim `not ok`; list of commands with their `tc_cmd_*` names; soak summary; SHA256 of lha/adf.
