# Tolunnet STOP-REPORT: Bench Incident Analysis & Architectural Separation

**Timestamp:** 2026-09-08 19:30
**Base Commit:** a2fd332dee986735dcb57ee174aafeee40e4c5d8 (docs(issues): record TNET-099 for Step W2 resolution)
**Incident Trigger:** Dual-profile bench failure on 68000 (halt after test 5) and A1200 (test 31 failure) under dirty working tree.

---

## 1. What Changed Since a2fd332 (Audit of Working Tree)

Two distinct feature sets were erroneously mixed in the uncommitted working tree:

### Set A: Step W3 Security & Robustness Fixes (Target for Commit 1)
1. **S:User-Startup Atomic File Rewrite (`src/setup/stack_detect.c`)**:
   - Replaced fragile 512-byte line buffer reading with whole-file memory allocation via `AllocDosObject(DOS_FIB, NULL)` and `fib_Size`.
   - Atomic replacement: writes to temporary file `S:User-Startup.tolunnet-new`, verifies byte count on `Close()`, and invokes `Rename()` over original.
2. **Wireless.prefs Injection Defense (`src/setup/wifi_mgr.c`)**:
   - `tn_format_wireless_block`: if SSID contains characters outside `[A-Za-z0-9 ._-]`, emits standard hex format `ssid=<hex>`. Passphrase strictly enforced between 8 and 63 chars with quote/newline/control rejection.
3. **SANA-II WiFi TagItem Parsing (`src/setup/wifi_mgr.c`, `wifi_mgr.h`)**:
   - Replaced improper `S2INFO_BSSID + 8` offset read with standard `S2INFO_SSID` tag and length.
   - Added signal dBm-to-percentage conversion via `S2INFO_Signal`.
4. **Command Injection & Truncation Hardening (`src/setup/wifi_mgr.c`, `net_test.c`, `TolunnetPrefs.c`, `Install_Tolunnet_Launcher.c`)**:
   - Device name validation `[A-Za-z0-9._-]+` capped at 31 chars before running `SystemTags` / `Execute`.
   - `snprintf` return value checks ensuring no truncated commands are ever executed.
5. **Passphrase Hygiene (`src/cmds/TolunnetSetup.c`)**:
   - Added `pass_edit_hook_fn` (`GTST_EditHook`) to mask passphrase with asterisks unless "Show" checkbox is toggled.
   - Memory zeroing via `volatile char *` immediately after writing prefs.
6. **Removal of Hardcoded `WORK:tolunnet-task.log` (`src/task/daemon_main.c`)**:
   - Removed default `WORK:tolunnet-task.log` from release daemon to prevent "Please insert volume WORK:" requesters on real hardware.
   - Wrapped `Open()` in `pr_WindowPtr = (APTR)-1`.
7. **Bench Clean Guard (`ci/bench.sh`)**:
   - Enforced `ALLOW_DIRTY=1` to run on a dirty tree, exiting with code 2 otherwise.

### Set B: Step G Live Reconfiguration Engine (Erroneously Mixed In)
1. **Daemon Struct Mutation (`src/task/task_ctx.h`)**:
   - Converted `TnSelector selectors[TN_MAX_SELECTORS]` array inside `struct TnDaemon` into a heap pointer `TnSelector *selectors`, adding `max_selectors`.
   - Modified `TnSocketSlot` and `TnDaemon` struct boundaries, shifting memory layouts and offsets.
2. **Reconfig IPC & Feedback Protocol (`include/ipc.h`, `src/task/netif_mgr.c`, `src/task/ipc_status.c`, `src/common/prefs.h`, `src/common/config_text.c`, `src/cmds/daemon_main.c`)**:
   - Dynamic selector resizing (`AllocVec`, copy, `FreeVec`).
   - `LOG=` live redirection, `PRIORITY=` task priority update, `STATS` reset.
   - `TnReconfigResponse` bitmasks (`applied`, `needs_restart`, `failed`).
3. **Conformance Additions (`tests/amiga/SocketConformance.c`)**:
   - Added `tc_reconfig_rc` (test 32) and `tc_wifi_scan_parse` (test 33).

---

## 2. Why Each Bench Run Failed

### Run 1: A1200 Test 31 Failure (`not ok 31 - tc_wizard_wired # S:User-Startup.tolunnet-bak not created`)
- **Primary Root Cause (AmigaDOS Script Lock):**
  In `ci/bench.sh`, `ci/User-Startup-Conformance` was staged directly as `S/User-Startup` on the bench HDF. When AmigaOS boots, the initial shell executes `S:User-Startup`. AmigaDOS maintains an active shared read lock on `S:User-Startup` for the entire duration of the script.
  When `SocketConformance` reached test 31 (`tc_wizard_wired`), it called `Open("S:User-Startup", MODE_READWRITE)`. In AmigaDOS, `MODE_READWRITE` requires exclusive write access and fails with `ERROR_OBJECT_IN_USE` (code 202) if another lock is held. Because `Open` returned NULL, the test lines `\nRun MiamiDx\nAmiTCP:bin/startnet\n` were never appended. `TolunnetSetup` therefore had no legacy stack lines to migrate, did not create `S:User-Startup.tolunnet-bak`, and test 31 failed.
- **Secondary Root Cause (ARexx FINISH Reply Race):**
  In `setup_rexx.c:132`, `ReplyMsg(msg)` was called immediately upon receiving `FINISH`, before `apply_wizard_finish()` even started. `SocketConformance` awoke and entered a 30-iteration delay loop (`30 * 100ms = 3s`), while `TolunnetSetup` was executing `tn_stack_request_quit()`, which had an unconditional `Delay(25)` (500ms) delay.

### Run 2: A600/68000 Profile Halt After Test 5 (`tc_sockopt_matrix`)
- **Primary Root Cause (task_ctx.h Monolith Mutation from Step G):**
  Step G modified `task_ctx.h`, replacing `TnSelector selectors[TN_MAX_SELECTORS]` with dynamic pointer `TnSelector *selectors` and adding `max_selectors`. Because `Makefile` did not enforce complete dependency tracking across all C source files and objects (`sana2_netif.o`, `slot_table.o`, `daemon_main.o`), mismatched struct offsets caused memory corruption when packet structures or SANA-II buffers were accessed on the strict 68000 CPU.
- **Secondary Root Cause (Multicast Loopback Pbuf Drain):**
  In `SocketConformance.c:703` (`tc_multicast_join`), socket `s` joins multicast group `224.0.0.251`, and `s_sender` sends a datagram `"mDNS_TEST"` with `IP_MULTICAST_LOOP=1`. The packet was queued into `s->rx_queue`. `tc_multicast_join` closed `s_sender`, dropped group membership, and closed `s` without ever reading the datagram. When `CloseSocket(s)` destroyed the slot, `tn_rx_queue_drain` called `pbuf_free(pkt->p)`. Due to `task_ctx.h` alignment offsets and `pbuf_free` heap interaction under corrupted slot structures, the 68000 halted.

---

## 3. Corrective Action Plan & Next Steps

1. **Step 1 — Baseline Verification at a2fd332:**
   - Working tree is clean at `a2fd332`.
   - Run clean bench `ci/bench.sh` on both `a1200` and `68000`. Both configs must be 31/31 ALL-GREEN.
2. **Step 2 — Strict W3 Commit (Zero G Code):**
   - Pop stash, isolate ONLY W3 security/robustness files.
   - Do NOT include any Step G files (`task_ctx.h` selector pointer changes, `ipc.h` reconfig structs, reconfig CLI).
   - Resolve `S:User-Startup` lock in `ci/bench.sh` by bootstrapping via `Run Execute S:Conformance-Script`.
   - Run bench: both `a1200` and `68000` must be ALL-GREEN.
   - Commit: `fix(setup): security and robustness fixes for User-Startup, wifi prefs, and command execution (TNET-100..105)`.
3. **Step 3 — Step G Hot-Reload Commit:**
   - Apply Section G hot-reload logic in its own dedicated, clean commit with full header rebuild synchronization.
   - Verify host tests and dual-profile bench.