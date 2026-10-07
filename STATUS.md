# tolunnet — Status

_Last updated: 2026-10-07 · version string `1.2.0-rc6` (`include/version.h`)_

## Release state

| Item | State |
|---|---|
| Latest release candidate | **1.2.0-rc6** |
| Changes in rc6 | Security audit run-1 fixes: lwIP `dns_compare_name`/`dhcp_parse_reply` OOB reads, RFC 6528 keyed TCP ISN, keyed SipHash RNG (no leaked DNS id/port), DNS reply name-binding, TCP `PBUF_POOL` clone, UDP/raw `SO_RCVBUF` byte cap, SANA-II CopyToBuff clamp, tftp first-reply source check (see CHANGELOG) |
| Package ADF | `build/tolunnet-1.2.0-rc6-disk1.adf` + `-disk2.adf` — the Gotek two-disk set (FFS DD 880 KB each, explicit manifest `scripts/adf_manifest.txt`); merging both disks into one drawer reproduces the package tree byte for byte; `Install_From_Floppies` on disk 1 bootstraps the install |
| TX pipelining | `TX_QUEUE=0` (synchronous `DoIO`) is the release default. The TX pool is proven in the bench with `TX_QUEUE=4`; the real-hardware default is still to be decided. |

## Library coverage

| Library | Coverage |
|---|---|
| `bsdsocket.library` | 139 LVO slots (121 SFD functions + 18 reserved): **70 BUILT**, 51 stubs with Roadshow error semantics, 0 broken |
| `usergroup.library` | 39 public LVOs, all present. Some are partial; see [Known limitations](#known-limitations). |

## Test results

| Layer | Result | Evidence |
|---|---|---|
| Host unit tests (`make test-host`) | every `tests/host/test_*.c` (24 programs, 229 ok after the 2026-10-02 bug-track fixes), ASan/UBSan | run locally before every commit |
| Emulated conformance bench, rc3 tree | **ALL-GREEN**: 58/58 in both cycles on both profiles (a1200 + 68000); bsdsocktest **126/142** | bench `20260921-135938-51196ec` (in git history up to `7f87caf`) |
| Emulated conformance bench, post-rc3 work | **ALL-GREEN**: 60/60 in both cycles on both profiles (a1200 + 68000); bsdsocktest **126/142** | bench `20260923-082158-v1.2.0-rc3-25-gdb29de8` |
| Emulated conformance bench, rc5 tree (`TX_QUEUE=4` only; the default `TX_QUEUE=0` leg is new in `ci/bench.sh` and not yet run) | **ALL-GREEN**: `112 ok / 0 not ok`, plan `1..112`, four legs (a1200 + 68000, two cycles); `tc_floppy_install` ok (51/51 manifest lines), `tc_installer_pretend` SKIP (real Installer is GUI-bound headlessly); the `112` counts the pretend row's `ok … # SKIP` line, `skip=3` = tc_dns_a (external), tc_link_events, tc_installer_pretend; bsdsocktest **126/142** | bench `20261002-121439-v1.2.0-rc4-511-g80c7455` |
| Emulated conformance bench, 1.2.0-rc6 fix tree | **ALL-GREEN**: `112 ok / 0 not ok` on a1200 (`TX_QUEUE=4`) and 68000 (`TX_QUEUE=0`), both cycles; `tc_floppy_install` ok; bsdsocktest **126/142** (within gate) | headless WinUAE run on the audit-run1 fix tree, 2026-10-07 |
| Emulated conformance bench, 1.2.0-rc5 release build | **ALL-GREEN**: `112 ok / 0 not ok` in both cycles on a1200 (`TX_QUEUE=4`) and 68000 (`TX_QUEUE=0`, the release default); DHCP lease, all `net_*` green, `tc_boot_block` ok; bsdsocktest **126/142** | bench `20261003-125019-v1.2.0-rc4-531-g483f115`, `20261003-130138-v1.2.0-rc4-531-g483f115` |
| Release asset checksums | lha `2b75587c5c4ca6acff2995b1c5660d4393d3229f0a69b9552d4ab3195c933913`, disk1 `e3ed36f4a529fec0e17de50e0d6789538a7221183db178c0ab53ce4dfbcb6814`, disk2 `7ce89cfbf9808ca064bc39be14705320814d79bd7e8059eb196f586ea6a06fcc` | `build/release-assets/` (+`SHA256SUMS.txt`) |
| Phase 1b Red Baseline (`c598d6d`) | **ALL-GREEN (7 TODO)**: 60/60 + 9 net tests (7 TODO); bsdsocktest **126/142** | bench `20260923-125745-v1.2.0-rc4-1-gc598d6d-netbaseline` |
| Phase 1b Item 2 | `net_tcp_blocking_recv` active; blocking recv parking + `SO_RCVTIMEO` | host test `test_slot_table` ok 10 |
| Session-profile soak (pre-9.5 harness: its `not ok` gate could not fire, HTTP went to port 1 - re-run needed) | **PASS**: 12 cycles (3 h 20 m, a1200, `TX_QUEUE=4`), 0 Gurus, 0 `not ok`, Chip RAM drift 0 B, Fast RAM −1200 B (cycle 1 → 11) | bench `20260922-144257-soak-842cc1f` (in git history up to `7f87caf`) |
| MuForce / Enforcer | **SKIP**: the tool image is not part of the bench | `muforce.txt` in each bench run |
| RTG (Picasso96) wizard layout | **SKIP**: no RTG drivers in the bench image. PAL 640×256 and NTSC 640×200 are verified; RTG is retested by the owner on PiStorm. | — |
| Real hardware (A500 + PiStorm + `wifipi.device`) | owner retest, manual | — |

bsdsocktest (142 tests): 126 passed, 2 failed, 14 skipped.
- The 2 failures are the `MSG_OOB` pair (#27 `recv(MSG_OOB)`, #64 `WaitSelect` exceptfds for OOB data). They will not be fixed, because lwIP 2.2.0 has no TCP out-of-band data.
- The 14 skipped tests need bsdsocktest's host-side helper, which the hermetic bench does not run.

## Known limitations

**Package**
- The two-disk ADF set has not been tested on real Gotek/floppy
  hardware (emulator-tested pieces only).

**Stack / commands**
- `ftp`: passive mode only (no `PORT`/`EPRT`).
- `iperf`: simple TCP throughput tool with its own format, not iperf2/iperf3 compatible.
- `telnet`: raw TCP terminal. Telnet options are filtered, not negotiated.
- `ifconfig` / `netstat`: read-only summaries. `netstat`'s route view is built from the configuration, not read from the live route table (use `route SHOW` or `ShowNetStatus ROUTES`).
- `CheckNetConfig`: checks config syntax only, not whether the SANA-II device is present.

**`usergroup.library`**
- `crypt()` is traditional DES crypt(3), so hashes from existing AmiTCP/Roadshow passwd files validate; tolunnet itself never writes password hashes.
- `getpass()` returns an empty string without prompting.
- `setutent`/`endutent` do nothing, and `getutent` returns a fixed `root`/`console` record.
- `getlastlog`/`setlastlog` keep their data in memory only.
