# tolunnet — Status

_Last updated: 2026-09-22 · HEAD `4dff8ef` · version string `1.2.0-rc3` (`include/version.h`)_

## Release state

| Item | State |
|---|---|
| Latest release candidate | **1.2.0-rc3** (commit `7f87caf`) |
| Changes after rc3 (not yet in a release) | `usergroup.library` (`d016809`), AutoIP + mDNS responder (`8b767c5`) |
| Package | `make package` → `build/tolunnet-<version>.lha` + `build/tolunnet.adf` |
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
| Emulated conformance bench, post-rc3 work | **ALL-GREEN**: 60/60 in both cycles on both profiles (a1200 + 68000); bsdsocktest **126/142** | bench `20260923-004535-v1.2.0-rc3-18-gbe27163` |
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
- `arp FLUSH`: not implemented.
- `iperf`: simple TCP throughput tool with its own format, not iperf2/iperf3 compatible.
- `telnet`: raw TCP terminal. Telnet options are filtered, not negotiated.
- `ifconfig` / `netstat`: read-only summaries. `netstat`'s route view is built from the configuration, not read from the live route table (use `route SHOW` or `ShowNetStatus ROUTES`).
- `CheckNetConfig`: checks config syntax only, not whether the SANA-II device is present.

**`usergroup.library`**
- `crypt()` is an FNV-style hash and not DES-compatible with Unix password files.
- `getpass()` returns an empty string without prompting.
- `setutent`/`endutent` do nothing, and `getutent` returns a fixed `root`/`console` record.
- `getlastlog`/`setlastlog` keep their data in memory only.

**Tooling**
- The soak audit prints its numbers but does not fail the run by itself. PASS/FAIL is decided by reviewing the summary.
- The local compatibility table generator (`scripts/gen_lvo_table.py`) takes its PASS column from the newest local bench logs, including dirty ones.
