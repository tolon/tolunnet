68000: Sun Sep 20 04:46:29 TSS 2026
conformance.log: core: 51 ok / 0 not ok; external: 1 skipped
conformance2.log: core: 51 ok / 0 not ok; external: 1 skipped
bench services: none (suite is loopback-hermetic); DNS_PORT=15353
config: ci/tolunnet-68000.uae (HDF copy staged from the pristine WB3.0 image)
commit: 8f81fd28931440ed0a875f366bed1d5079c984a5
describe: 8f81fd2
dirty: NO
MuForce pass: SKIP (MuForce/Enforcer not present on this bench; MUFORCE_ADF unset)

TNET-150 stop-path note: the Ctrl-C reap/logging printed NOTHING this run -
no holder named, nothing reaped; the log (append mode) shows a single daemon
banner, so cycle 2 ran against the cycle-1 daemon (TNET-152).
