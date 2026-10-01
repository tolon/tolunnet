# tolunnet — Status

_Last updated: 2026-10-01 · version string `1.2.0-rc5` (`include/version.h`)_

## Release state

| Item | State |
|---|---|
| Latest release candidate | **1.2.0-rc5** |
| Changes in rc5 | CANCEL/break-signal correctness, WaitSelect per 4.4BSD, errno truth (EDESTADDRREQ family), the 68000 shutdown/freeze class closed, command output number fixes, ftp/nc/TolunnetGet rework, nslookup answer-vs-server + PTR default, honest wizard Test page (stack-reported values, SKIPPED states), installer parity between bench and archive (see CHANGELOG) |
| Package LHA | `build/tolunnet-1.2.0-rc5.lha` (`d6cd572af853f5d690b801661f76dbc1a37df9d54c5b26fd6dc4651f3c1e4308`) |
| Package ADF | `build/tolunnet.adf` (`6419f2844c13fa3b9f47feaea8d5a39b6be6fc58ed8fcfb851c44ed8cb94bc47`) — 880 KB FFS, priority-packed from the stripped release tree (must-haves C/ + Libs + TolunnetSetup + LICENSE always; then Disk.info, installer pair, prefs pair, docs until full; skips are printed at build time) |
| TX pipelining | `TX_QUEUE=0` (synchronous `DoIO`) is the release default. The TX pool is proven in the bench with `TX_QUEUE=4`; the real-hardware default is still to be decided. |

## Library coverage

| Library | Coverage |
|---|---|
| `bsdsocket.library` | 139 LVO slots (121 SFD functions + 18 reserved): **70 BUILT**, 51 stubs with Roadshow error semantics, 0 broken |
| `usergroup.library` | 39 public LVOs, all present. Some are partial; see [Known limitations](#known-limitations). |

## Test results

| Layer | Result | Evidence |
|---|---|---|
| Host unit tests (`make test-host`) | 20 programs, ASan/UBSan | run locally before every commit |
| Emulated conformance bench, rc3 tree | **ALL-GREEN**: 58/58 in both cycles on both profiles (a1200 + 68000); bsdsocktest **126/142** | bench `20260921-135938-51196ec` (in git history up to `7f87caf`) |
| Emulated conformance bench, post-rc3 work | **ALL-GREEN**: 60/60 in both cycles on both profiles (a1200 + 68000); bsdsocktest **126/142** | bench `20260923-082158-v1.2.0-rc3-25-gdb29de8` |
| Emulated conformance bench, rc5 tree | **ALL-GREEN**: `110 ok / 0 not ok`, plan `1..111`, four legs (a1200 + 68000, two cycles); bsdsocktest **126/142** | bench `20261001-011416-v1.2.0-rc4-440-gf7c4284` |
| Phase 1b Red Baseline (`c598d6d`) | **ALL-GREEN (7 TODO)**: 60/60 + 9 net tests (7 TODO); bsdsocktest **126/142** | bench `20260923-125745-v1.2.0-rc4-1-gc598d6d-netbaseline` |
| Phase 1b Item 2 | `net_tcp_blocking_recv` active; blocking recv parking + `SO_RCVTIMEO` | host test `test_slot_table` ok 10 |
| Session-profile soak | **PASS**: 12 cycles (3 h 20 m, a1200, `TX_QUEUE=4`), 0 Gurus, 0 `not ok`, Chip RAM drift 0 B, Fast RAM −1200 B (cycle 1 → 11) | bench `20260922-144257-soak-842cc1f` (in git history up to `7f87caf`) |
| MuForce / Enforcer | **SKIP**: the tool image is not part of the bench | `muforce.txt` in each bench run |
| RTG (Picasso96) wizard layout | **SKIP**: no RTG drivers in the bench image. PAL 640×256 and NTSC 640×200 are verified; RTG is retested by the owner on PiStorm. | — |
| Real hardware (A500 + PiStorm + `wifipi.device`) | owner retest, manual | — |

bsdsocktest (142 tests): 126 passed, 2 failed, 14 skipped.
- The 2 failures are the `MSG_OOB` pair (#27 `recv(MSG_OOB)`, #64 `WaitSelect` exceptfds for OOB data). They will not be fixed, because lwIP 2.2.0 has no TCP out-of-band data.
- The 14 skipped tests need bsdsocktest's host-side helper, which the hermetic bench does not run.

## Known limitations

**Stack / commands**
- `ftp`: passive mode only (no `PORT`/`EPRT`).
- `iperf`: simple TCP throughput tool with its own format, not iperf2/iperf3 compatible.
- `telnet`: raw TCP terminal. Telnet options are filtered, not negotiated.
- `ifconfig` / `netstat`: read-only summaries. `netstat`'s route view is built from the configuration, not read from the live route table (use `route SHOW` or `ShowNetStatus ROUTES`).
- `CheckNetConfig`: checks config syntax only, not whether the SANA-II device is present.

**`usergroup.library`**
- `crypt()` is an FNV-style hash and not DES-compatible with Unix password files; existing AmiTCP passwd files are not compatible.
- `getpass()` returns an empty string without prompting.
- `setutent`/`endutent` do nothing, and `getutent` returns a fixed `root`/`console` record.
- `getlastlog`/`setlastlog` keep their data in memory only.
