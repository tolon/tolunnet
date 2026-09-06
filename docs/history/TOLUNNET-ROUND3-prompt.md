# TOLUNNET — Round 3 Work Order: Verified Bugfix Pass + Full API Skeleton
# Repo: D:\Projeler\tolunnet (WSL: /mnt/d/Projeler/tolunnet). Toolchain: m68k-amigaos-gcc 6.5.0b (WSL).
# Governing documents, in precedence order:
#   TOLUNNET-BUGTRACK-v3-prompt.md (§0 LAW)  >  TOLUNNET-BUGTRACK-v3-round2.md  >  TOLUNNET-SCOPE-v4-full-api.md  >  this file.
# This file is the executable work order for ONE session. Do it in the order given. Do not start §D before §C is green.

---

## A. LAW FOR THIS ROUND (read twice)

1. **Find bugs by testing, not by reading.** Every fix in this round must come with (a) a test that
   FAILED before the fix and PASSES after, or (b) a reproducible bench log showing the failure and
   then the pass. A fix without one of those is not accepted — put it in ISSUES as `FIXED (unverified)`
   and say so in the final report.
2. **Verification ladder** — each item must state which rung it reached:
   `L0 compiles` → `L1 host test passes` → `L2 static/scan clean (gcc -Werror, objdump, Enforcer)` →
   `L3 WinUAE bench log` → `L4 real hardware`. Nothing below L3 may be called "TESTED" anywhere in docs.
3. **No claims without evidence** in ISSUES/STATUS/README: quote the test name + log path + commit hash.
4. Do not modify `vendor/lwip/`. Do not change `-m68000 -msoft-float -noixemul -O2 -fomit-frame-pointer`.
   No MUI/ReAction. Name stays `tolunnet`.
5. Commit per lettered section below (`fix(core): …`, `test(host): …`, `feat(lib): …`). Tree clean at
   the end of every section. `make all && make package` green at every commit.
6. If you must choose between two behaviours and the SDK docs (`Roadshow SDK 1.8 doc/bsdsocket.doc`,
   `netinclude/`) settle it, follow the SDK. If they don't, append the question to `QUESTIONS.md` and pick
   the AmiTCP-V4-compatible behaviour.

---

## B. TEST HARNESS FIRST (build the net before walking the wire) — commit `test: harness`

### B.1 Host-side (native gcc, Linux) — `tests/host/`
Create a tiny framework (`tests/host/tn_test.h`: `TN_TEST(name)`, `TN_ASSERT_EQ`, `TN_ASSERT_STREQ`,
TAP output `ok N - name` / `not ok N - name`) and `make test-host` runs every `tests/host/test_*.c`
compiled with **host** gcc `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined`.
To make daemon/library logic host-compilable, split pure logic out of AmigaOS-dependent files into
`src/common/*.c` units with no NDK includes (this is refactoring, not behaviour change — keep diffs minimal):

| Test file | Unit under test | Cases that must exist |
|---|---|---|
| `test_inet_addr.c` | `inet_addr` parser (move to `src/common/inet_parse.c`) | all 1/2/3/4-part forms, hex/octal/decimal, `256.1.1.1` → INADDR_NONE, trailing junk, empty, `0x`, `255.255.255.255` (valid, not INADDR_NONE confusion), leading spaces |
| `test_config.c` | `prefs.c` text parser/writer (move parse/format into `src/common/config_text.c`) | round-trip of every key, both key dialects, case-insensitivity, CR/LF/CRLF, comments, missing `=`, oversize values truncated safely, `DNS2` does not alias `DNS1`, empty defaults (TNET-078) |
| `test_lvo_table.c` | generated LVO table vs `sfd/bsdsocket_lib.sfd` | count == 133, every offset == −6·(n+5)… per SFD `##bias`, names match, no gaps, table terminator |
| `test_sbtc.c` | `SocketBaseTagList` dispatcher (extract the switch into `src/common/sbtc_dispatch.c` taking a callback table) | return 0 on all-known, count unknown only, GETREF/GETVAL/SETREF/SETVAL matrix, `SBTC_HAVE_*` truthful |
| `test_route.c` (for §D later, stub now) | route table lookup | longest-prefix, default, delete |
| `test_fdset.c` | `WaitSelect` fd_set marshaling | 64-bit set, fd ≥ 32 → EBADF, empty sets + timeout |

CI: fix `.github/workflows/build.yml` — job `host` runs `make test-host`; job `amiga` runs `make all
&& make package`, uploads `build/release` + `.lha` + `.adf`; artifact names `tolunnet-*`; remove every
`tolunet-hello` reference (TNET-073). Add `__pycache__/`, `*.pyc` to `.gitignore`.

### B.2 Amiga-side conformance binary — `tests/amiga/SocketConformance.c`
One program, built by `make all`, output TAP to stdout **and** `WORK:conformance.log`. Structure:
`tc_lib_open_close`, `tc_socket_types` (STREAM/DGRAM/RAW, bad → EPROTONOSUPPORT/ESOCKTNOSUPPORT),
`tc_bind_udp` (INADDR_ANY:0 → getsockname returns port ≠ 0), `tc_bind_reuse` (EADDRINUSE without
SO_REUSEADDR, OK with), `tc_listen_accept_loopback` (needs loopback — skip with `# SKIP` until §D),
`tc_connect_refused` (ECONNREFUSED within 5 s), `tc_nonblock_connect` (FIONBIO → EINPROGRESS →
WaitSelect writable → SO_ERROR 0), `tc_shutdown_wr`, `tc_getpeername`, `tc_dns_a` (gethostbyname
`aminet.net` → h_length 4, h_addr_list[1] NULL or valid), `tc_dns_fail` (h_errno HOST_NOT_FOUND),
`tc_errno_ptr` (byte/word/long widths), `tc_dup2`, `tc_waitselect_timeout` (200 ms ±100),
`tc_sigio` (SetSocketSignals + recv wake-up, skip until TNET-067), `tc_icmp_raw` (SOCK_RAW echo to
gateway, skip until raw), `tc_every_vector_callable` (**call all 133 vectors with harmless args and
verify no Guru + documented return** — this is the test that proves §E).
Each `tc_*` prints `ok`/`not ok`/`ok # SKIP reason`. Exit code = number of `not ok`.

### B.3 Bench automation — `ci/bench.sh` (runs from WSL or Windows)
- Uses the existing `ci/*.uae` configs; mounts `E:\amiga\Amigatolon\work` as `WORK:`; copies
  `build/release/tolunnet/*` into the bench HDF (`xdftool`), installs `ci/User-Startup-Conformance`
  that runs: `Stack 32768`, `Run >NIL: C:tolunnet`, `Wait 8`, `SocketConformance >WORK:conformance.log`,
  `tolunnet STOP`, `Wait 3`, then **repeats start/conformance/stop a second time** (TNET-059/060
  restart proof), then `UAEQuit` (`uae-configuration -s quit_amiga=1` or the `uaectrl`/`uae_reset`
  hook the bench already has).
- Runs WinUAE headless (`winuae64.exe -f cfg -s use_gui=false -s win32.start_minimized=true`), waits for
  `WORK:conformance.log` + `WORK:bench-done` marker (timeout 180 s), copies logs to
  `docs/bench-logs/<date>-<hash>/` and prints the TAP summary. **Two runs are mandatory:** the A1200/020
  config and a **68000 A500+/A600 config** (`ci/tolunnet-68000.uae` — create it: 68000, 2 MB Chip +
  4 MB Fast, WB 3.0, A2065/`uaenet.device`).
- Second pass of each run with **MuForce + MuGuardianAngel** (or Enforcer + Wipeout on 68020 config)
  loaded first, output to `WORK:enforcer.log`. Any hit = a new ISSUES row, this round.
- Add `SnoopDos`-style visibility: daemon logs every `OpenLibrary/OpenDevice` failure with the name.

If the bench cannot be automated end-to-end (e.g. bare HDF geometry problem noted in `ci/README.md`),
fix that first — it is in scope — and record how in `docs/bench.md`.

---

## C. VERIFIED BUGFIX PASS — one commit per item, each with its test/log

Order is by blast radius. For each: (1) write/enable the test that demonstrates the bug → run → record
`not ok`; (2) fix; (3) rerun → `ok`; (4) ISSUES row with rung reached.

| # | Item | Test that must flip | Fix spec |
|---|---|---|---|
| C1 | **TNET-077** bind/listen/accept/shutdown/getsockname/getpeername/gethostbyaddr unhandled in `tn_handle_ipc` | `tc_bind_udp`, `tc_shutdown_wr`, `tc_getpeername`, `tc_listen_accept_loopback` (SKIP→ok after §D loopback), host `test_fdset` | Daemon handlers: `udp_bind/tcp_bind` (INADDR_ANY, port 0 = ephemeral 49152–65535 via own allocator, `SO_REUSEADDR` → `ip_set_option(pcb, SOF_REUSEADDR)`), `tcp_listen_with_backlog` + accept queue per listener (bounded 8, `ECONNABORTED` on overflow), blocking/non-blocking `accept` (`EWOULDBLOCK`), `WaitSelect` readability on listeners, `tcp_shutdown(pcb, rx, tx)` mapping `SHUT_RD/WR/RDWR`, sockname/peername fill (`sin_len 16, sin_family AF_INET`, network order), `gethostbyaddr` via `dns_gethostbyname` on `d.c.b.a.in-addr.arpa` (lwIP `dns` only returns A — implement PTR by hand-building the query on a UDP pcb in the daemon; parse the answer; 5 s timeout → `TRY_AGAIN`). |
| C2 | **TNET-080** daemon stack: 4 KB default when started via `Run`/`Execute` | Bench: start daemon from `Execute` with default stack under MuForce → expect stack-overflow hit before fix; after fix: refusal message or StackSwap and clean run | In `main()`: `pr_StackSize < 32768` → `StackSwap()` onto a 32 KB `AllocMem(MEMF_PUBLIC)` stack for the whole run (swap back before `return`), log the swap. Prefs: launch via `SystemTags(cmd, SYS_Asynch, TRUE, SYS_Input, NULL, SYS_Output, NULL, NP_StackSize, 32768, TAG_END)`. Installer/User-Startup: `Stack 32768` line before `Run`. Also set **`SetTaskPri(5)`** (TNET-066) with `PRIORITY=` key, restore on exit. |
| C3 | **TNET-078** slirp literals + DNS clobber on RECONFIG; `gethostid()` literal | Host `test_config` (defaults empty); bench: DHCP lease → note DNS from `tolunnet STATUS` → press Use in Prefs → DNS unchanged | Defaults empty; `tn_apply_live_config` touches `dns_setserver` only for explicitly configured servers; `gethostid` via `GETSTATUS` IPC; bench values only in `ci/`. |
| C4 | **TNET-069** lwIP pools | `tc_socket_types` extended: open 12 TCP sockets → all succeed; connect 8 concurrently to the bench HTTP server → all succeed | `MEMP_NUM_TCP_PCB 32`, `MEMP_NUM_TCP_PCB_LISTEN 8`, `MEMP_NUM_UDP_PCB 16`, `MEMP_NUM_RAW_PCB 8`, `MEMP_NUM_TCP_SEG 64`, `PBUF_POOL_SIZE 32`, `MEM_SIZE 96*1024`, `DNS_MAX_SERVERS 2`, `DNS_TABLE_SIZE 8`. Report `AvailMem` delta before/after daemon start in `docs/bench.md` (must stay ≤ 400 KB). |
| C5 | **TNET-079/081** Prefs Start/Stop button + blocking Delay loops | Bench: open Prefs, click Start → button text/state changes within 3 s while the window still redraws (screenshot via WinUAE `-s screenshot`?) — at minimum a log line from the GUI; Enforcer clean | Two buttons `Start`/`Stop` with `GT_SetGadgetAttrs(GA_Disabled)`; state polling from a `timer.device` request in the main `Wait()` mask (1 s), no `Delay()` in the event loop. |
| C6 | **TNET-082** IPC message on stack; factor `tn_ipc_oneshot()` | MuForce clean on Prefs Save/Use and `TolunnetStatus`; host test for the marshaling helper | `src/common/ipc_client.c`: `AllocMem(MEMF_PUBLIC|MEMF_CLEAR)`, `CreateMsgPort`, PutMsg/WaitPort/GetMsg, cleanup. Used by Prefs, Status, and the new `tolunnet START/STOP/STATUS/RECONFIG` sub-commands (implement those now — thin, ReadArgs template `START/S,STOP/S,STATUS/S,RECONFIG/S,DEVICE,UNIT/N`). |
| C7 | **TNET-083** config precedence + blob versioning | Host `test_config`: hand-edited DEVS newer than ENV wins; blob with old size still loads; `VERSION=` written | Rule: `DEVS:tolunnet.config` canonical; `ENV:tolunnet.prefs` overrides only if newer (`fib_Date` compare); write ENV as **text** (same parser), drop the binary blob (read-compat for one release). |
| C8 | **TNET-072** explicit library bases + WBStartup + ToolTypes | Bench: launch Prefs from Workbench icon AND from CLI; both open; `SnoopDos` shows explicit opens | `OpenLibrary` intuition/graphics/gadtools/icon v36 with clean failure paths; handle `WBStartup` (libnix does; verify by reading the startup code's map symbols, else implement); read `TOOLPRI`, `PUBSCREEN` tooltypes. |
| C9 | **TNET-070** real ICMP ping (raw sockets) | `tc_icmp_raw` un-SKIPped; bench `ping 10.0.2.2 COUNT 4` → 4 replies with RTT | Daemon: `SOCK_RAW`+`IPPROTO_ICMP` slot type over `raw_pcb`, `IP_HDRINCL` for `IPPROTO_RAW`, RX queue reuse; `ping` rewritten with `ReadArgs` `HOST/A,COUNT/N,SIZE/N,INTERVAL/N,TTL/N,TIMEOUT/N,QUIET/S`, id = task address low 16 bits, seq, checksum, min/avg/max/mdev, loss %, Ctrl-C summary. Keep UDP echo as `UDP/S`. |
| C10 | **TNET-071** fake `lo0`, loopback, netstat rows | `tc_listen_accept_loopback` un-SKIPped; bench `netstat` shows the conformance test's own sockets | `LWIP_NETIF_LOOPBACK 1`, `LWIP_HAVE_LOOPIF 1`, `LWIP_NETIF_LOOPBACK_MULTITHREADING 0`, loop netif polled in the main loop; new IPC `ENUMSOCKETS` (proto, local, remote, state) → `netstat` real rows; `ifconfig` prints only real netifs. |
| C11 | **TNET-067** SIGIO delivery + real `Wait()` in WaitSelect | `tc_sigio`, `tc_waitselect_timeout` | Daemon `Signal(owner_task, sig_io)` on RX/accept/connect-complete/error/close when mask set; `WaitSelect` = build wait mask (`sig_io` if set, caller `signals`, one `timer.device` request per call) → `Wait()` → re-poll readiness via one IPC → return; poll loop only as fallback when `sig_io == 0`. `EINTR` when a caller signal fires. |
| C12 | **TNET-075** wget/curl | Bench: `wget http://aminet.net/recent.txt` (301/302 chain) succeeds; `wget http://<bench>/1MB` shows progress and right size | HTTP/1.1 `Host`, ≤5 redirects same-scheme, `https://` → clear message, `Content-Length` + chunked, `TO`, `QUIET`, resume via `Range` if server allows. |

Also in this pass, from §3 of the v3 contract — audit rows most likely to hide Blockers; do them with
evidence now, the rest of the audit later:
- **1.7 `LIBF_SUMUSED` on clones**: after `tn_lib_open` copies the jump table, either `SumLibrary(clone)`
  or clear `LIBF_SUMUSED|LIBF_CHANGED` on the clone. Test: `tc_lib_open_close` ×100 under Enforcer,
  no `AN_LibChkSum` alert.
- **3.5 68000 alignment / `ETH_PAD_SIZE`**: run the full bench on the **68000 config**; any Address
  Error (Guru `80000003`) → set `ETH_PAD_SIZE 2` and adapt the SANA-II copy hooks; retest.
- **2.5/2.6 shutdown ordering**: `tolunnet STOP` ×10 in a loop under MuForce; no leak reported by
  `Wipeout`/`MuGuardianAngel`; `AvailMem` returns to baseline ±2 KB.

---

## D. FULL 133-VECTOR API SKELETON — commit `feat(lib): full SFD jump table with honest stubs`

1. `scripts/gen_lvo_table.py` becomes the **generator**: emits `src/lib/lib_table.gen.c` (the vector
   array) and `src/lib/lib_stubs.gen.s` (register-marshaling stubs) **from the SFD**, for all 133
   functions, using the SFD register signature (`(a0,d0,d1,…)`) to generate each stub's push sequence.
   The build regenerates and `test_lvo_table` asserts it. Hand-written stubs are deleted.
2. Every function not yet implemented gets a C stub in `src/lib/lib_unimpl.c` with the **exact honest
   return for its signature** (from the SFD return type): `LONG` → `-1` + `errno = ENOSYS`
   (except `bpf_*/ipf_*` → `-1` + `ENXIO` per Roadshow doc), `STRPTR`/`struct *` → `NULL` (+ `h_errno
   = NO_RECOVERY` for resolver ones), `BOOL` → `FALSE`, `VOID` → return, `in_addr_t` → `INADDR_NONE`.
   Stubs log once per name at debug tier (`"tolunnet: <name> not implemented"`), never spam.
3. `SBTC_HAVE_*` answers become a generated bitmask from an `IMPLEMENTED(name)` registry so docs,
   capability tags and `TOLUNNET-COMPAT.md` §2 table cannot drift (generate the md table too).
4. `tc_every_vector_callable` (B.2) must pass on both bench configs: 133 calls, zero Guru, each return
   matches the documented stub or real behaviour.
5. Implement in this round, because they are cheap and widely used: `inet_aton`, `inet_ntop`,
   `inet_pton` (AF_INET), `gethostbyname_r`/`gethostbyaddr_r` (wrappers over the per-opener storage
   with caller buffers), `GetDefaultDomainName`/`SetDefaultDomainName` (from `DOMAIN=` key /
   DHCP option 15), `getnetbyname/addr` + `get*ent` over `DEVS:Internet/{networks,services,protocols}`
   with built-in defaults, `syslog/vsyslog` → daemon log. Everything else in the SFD stays an honest
   stub for the next rounds (Scope v4 tiers T1-remainder/T3/T4).

---

## E. DOCUMENT & REPORT — commit `docs: round 3 ledger`

- `ISSUES.md`: one row per item above with **rung reached** (`L1`…`L4`), test name, log path, commit.
  Add a one-line note explaining the TNET-066…075 reservation and the 076+ numbering.
- `STATUS.md`: replace the two-column ledger with three states per milestone — **BUILT /
  EMULATOR-PROVEN (log link) / IRON-PROVEN** — and nothing else. No "TESTED" wording anywhere.
- `TOLUNNET-COMPAT.md` §2: regenerated implemented/stub table (133 rows).
- `docs/bench.md`: how to run `ci/bench.sh`, both configs, MuForce pass, RAM delta table.
- `README.md`: "133 vectors present; N implemented, M stubbed (list)" + link; command list updated
  (`tolunnet START/STOP/STATUS/RECONFIG`, ICMP `ping`).
- Move all `TOLUNNET-*prompt*.md`, `*AUDIT*.md`, `AMIWIFI-*`, `TOLUNWIFI-*` into `docs/history/`
  (git mv), fix links.
- `make package` regenerates **both** `.lha` and `.adf` (`make adf` target via `xdftool`); version
  stays `1.1.x` this round — bump to `1.2.0-rc1` only if every C-row reached L3 on both bench configs.

**Final report format (paste at the end of the session):**
```
| ID | Rung | Test / log | Commit |
...
Bench summary: A1200/020: N ok / M not ok / K skip · A500/68000: … · MuForce hits: …
Unverified items and why: …
New defects found by the harness (not in this list): TNET-0xx … (each with its ISSUES row)
Open questions appended to QUESTIONS.md: …
```

# END — begin with §B (harness). Do not touch §C until `make test-host` runs and `ci/bench.sh` completes one full cycle on the current code, producing the first `docs/bench-logs/` directory (that baseline log is itself a deliverable).
