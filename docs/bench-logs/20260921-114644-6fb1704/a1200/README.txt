a1200: Mon Sep 21 11:48:57 TSS 2026
conformance.log: core: 54 ok / 0 not ok; external: 1 skipped
conformance2.log: core: 54 ok / 0 not ok; external: 1 skipped
bench services: none (suite is loopback-hermetic); DNS_PORT=15353
config: ci/tolunnet-a1200.uae (HDF copy staged from the pristine WB3.0 image)
commit: 6fb17048af2cfdfbb0ffe145d4eba92cf22cfbfd
describe: 6fb1704
dirty: NO
MuForce pass: SKIP (MuForce/Enforcer not present on this bench; MUFORCE_ADF unset)

RC3 resting-state proof: 55/55 both cycles both profiles, zero not-ok.
TNET-151 hardening shipped (selector self-heal, count resync, stale wake
clear, ARM-abort DISARM, time-verified WaitSelect). Root cause isolated:
per-base timer request wedges after the wizard child runs (fired=0x0,
watchdog ETIMEDOUT) - wizard unclean-exit ReplyMsg-to-freed-memory
suspect; fix is the next session's first item (ISSUES TNET-151).
