# tests/host/

Host-compilable (native gcc) unit tests for logic that does not depend on
AmigaOS. Master prompt §8: "wrapper logic that can run host-side (request
encoding, socket table) gets host unit tests."

## Scope

- Protocol request encode/decode (include/tolunet/protocol.h wire layout).
- bsdsocket.library socket-table bookkeeping (M3+).
- errno mapping table (§5.1) — input/output correctness, host-side.

## Status (M0)

Skeleton only. The CI host job runs `ls -R tests/host` and reports nothing to
run yet. Real tests arrive with the milestones that introduce the logic:

| Test                 | Arrives in |
|----------------------|------------|
| protocol encode      | M3         |
| socket table         | M3         |
| errno map            | M4         |
| select fd-set logic  | M4         |

Do not write a test that the host cannot compile — anything touching NDK
structs or lwIP stays in tests/amiga/.
