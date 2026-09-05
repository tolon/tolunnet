# tests/host/

Host-compilable (native gcc) unit tests for logic that does not depend on
AmigaOS. Master prompt §8: wrapper logic that can run host-side gets host
unit tests.

## Status (2026-09-05)

**Still empty.** `make test-host` currently runs only the tree-wide
verifiers (`scripts/gen_lvo_table.py`, `scripts/verify_icons.py`) — no real
host unit tests exist yet. The project has zero automated logic tests; this
is tracked as TNET-073 in ISSUES.md.

## Planned scope (TNET-073, v3 bugtrack §2 P1)

| Test | Covers |
|------|--------|
| `inet_addr` parser | all TNET-051 forms: decimal/octal/hex, 1–4 parts, range/trailing-junk rejection |
| config parser/writer round-trip | both key dialects (`NETMASK`\|`MASK`, `GATEWAY`\|`GW`, `DNS`\|`DNS1`\|`NAMESERVER`, …), new `HOSTNAME`/`DNS2`/`MTU`/`DEBUG` keys |
| LVO table vs `sfd/bsdsocket_lib.sfd` | extend `gen_lvo_table.py` to *assert* agreement, not just print |
| `SocketBaseTagList` semantics | SBTM_GET/SET, VAL/REF decoding, return-count convention |

The IPC wire types live in `include/ipc.h` (not the old
`include/tolunet/protocol.h` path early planning assumed). Do not write a
test the host cannot compile — anything touching NDK structs or lwIP stays
in `tests/amiga/` (also empty).
