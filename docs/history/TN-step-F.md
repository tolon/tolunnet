# tolunnet — step F (stats telemetry). Repo D:\Projeler\tolunnet. Rules: docs/history/TOLUNNET-ROUND3-prompt.md §A (test-or-log proof, no fabricated logs). Toolchain in WSL. Commit per step, tree clean, `make all && make package && make test-host` green.

## R4 (10 min, commit `docs: bench log housekeeping`)
- `git add docs/bench-logs/20260907-200333-ad2d713/` (cited by TNET-096, untracked).
- Delete empty `docs/bench-logs/20260907-135754-7f51c72/`; make `ci/bench.sh` delete its own log dir on failure.
- TNET-096: add one line — what `ad2d713` fixed vs `7f51c72` and how it was found.

## F (commit `feat(status): lwIP stats, pool occupancy, queue depths`)
1. `lwipopts.h`: `LWIP_STATS 1`, `MEMP_STATS 1`, `MEM_STATS 1`, `TCP/UDP/IP/ICMP/ETHARP/LINK_STATS 1` in release; `LWIP_STATS_DISPLAY 0`. Record text-size delta in commit message.
2. New IPC `TN_IPC_CMD_GETSTATS` → versioned `struct TnStats` (size first): lwIP counters; per-pool `used/max/avail` from `lwip_stats.memp[i]`; heap used/max; daemon counters (IPC calls per cmd, deferred replies, SIGIO sent, selector wakeups, main-loop iterations, SANA-II rx/tx frames/bytes/drops, per-slot RX-queue high-water); uptime; DHCP lease remaining. Handler in `ipc_status.c`; counters live in `TnDaemon`.
3. `tolunnet STATS [RAW/S,WATCH/N]` (ReadArgs; `WATCH n` reprints every n s until Ctrl-C). `netstat -s` reuses the same struct.
4. Tests: host `test_stats.c` (struct versioning: old/new size handshake); conformance `tc_stats_counters` (3 UDP sends → `udp.xmit` +3; open/close → pool `used` back to baseline). Bench both configs, link log in ISSUES **TNET-097**, rung L3.

Report: commit hashes, size delta, bench dir, `not ok` lines verbatim (if any).
