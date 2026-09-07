# TOLUNNET — Round 4b Work Order: Consolidation before §D (refactor, host-testable core, telemetry, TCP options, link events, ARexx)
# Repo: D:\Projeler\tolunnet. Precedence: v3 §0 LAW > ROUND3 §A ladder > ROUND4 prompt > this file.
# Position (rev 3, 2026-09-07 20:30): §A, §L, §B(+R1/R2), §C(+R3), §D are merged (a168481 … 746a0f8). Last bench: ad2d713, 29/29, both configs, dirty: NO.
# Sections marked [DONE] below are kept for reference; do the [RESIDUAL] items first, then continue from §D.
# This round sits BETWEEN ROUND4 §C and §D. It exists because §D4 (multi-netif, routing) and §D5 (12 commands)
# will double `src/task/main.c` (3905 lines, one 2600-line `switch` with 28 cases). Consolidate first, then resume §D.
# Verification rules unchanged: failing-then-passing test or bench log per item; rung stated in ISSUES; no fabricated logs.

---


## STATUS OF THIS FILE (rev 3)
| § | State | Residual work (must be done before §D) |
|---|---|---|
| A | [DONE] a168481 | none — TNET-087…094 rows, C8 log 20260907-120042-6fb3c6e, Roadshow header licence section all present; `wip-netdb-d1` is in `git stash` (pop it in §D1). |
| L | [DONE] 302a425 | none — `sockaddr_util`, versioned IPC structs, `docs/design-ipv6.md`. |
| B | [DONE] 22c7af3 + 7772dbd | R1 (bench dirty guard + TNET-095 proof) and R2 (netif array seam) done. |
| C | [DONE] d06afb6 + 7772dbd | R3 (`test_queues.c`, host README header list) done. |
| D | [DONE] 7f51c72 + ad2d713 | Event-driven WaitSelect, `sig_select` per opener, `TnSelector[16]`, `SELECT_ARM/DISARM`, zero `Delay()` left; `tc_waitselect_no_sigio` ok. **R4 [DONE]:** (a) committed `20260907-200333-ad2d713`; (b) deleted stale failed run and added failure trap cleanup to `bench.sh`; (c) detailed stale-object incident and synchronization resolution recorded in TNET-096. |
| F–K | open | unchanged below; §E after §H as ordered. |

---

## A. LEDGER CATCH-UP (commit `docs: round 4 §C ledger`) — do first, 30 min
1. ISSUES.md has no rows for ROUND4 §C1–C8. Add **TNET-087…TNET-094** (one per C-row, in order C1→C8): defect/feature,
   files, conformance test names, bench log dir, commit hash, rung. C8 has **no bench log** (last log is 20638e4 = C7):
   run `ci/bench.sh` on `6fb3c6e` now and link it.
2. `include/netinclude/` (Roadshow SDK 1.8 headers, "Freely Distributable", © Olaf Barthel) is vendored since 6245917
   but absent from `THIRD_PARTY_LICENSES.md`. Add the section with the header's own notice verbatim and the SDK version.
3. Untracked `src/common/netdb.h`, `netdb_defaults.c` (§D1 started): either finish into a clean commit or `git stash`
   them — the tree must be clean before §B.

---

## B. `src/task/main.c` REFACTOR (commit `refactor(task): split daemon into modules, table-driven IPC dispatch`)
Pure refactor: **zero behaviour change; the 28 conformance tests and all host tests stay green with no edits to the
tests** — that is the proof. Do it in this order, building after each step.

### B.1 Module map (new files under `src/task/`, all with `tn_` prefix, no cross-includes of `.c`)
| File | Contents moved from main.c |
|---|---|
| `task_ctx.h` | `TnSocketSlot`, `TnRxPacket`, `TnAcceptEntry`, `TnTcpState`, `TnEventEntry` (GetSocketEvents queue), the `g_*` globals turned into one `TnDaemon` struct (`g_daemon`) — netif(s), timer, ipc port, bsd lib, prefs, socket table, counters. Every module gets `TnDaemon *d` as first arg, no file-scope globals except `g_daemon` itself. |
| `slot_table.c/.h` | `tn_slot_alloc / tn_slot_free / tn_slot_lookup(base, fd) / tn_fd_alloc(base) / tn_slot_ref / tn_slot_unref`, RX queue push/pop/drain, accept-queue push/pop, event-queue push/pop. **NDK-free** (see §C): only `<stdint.h>`, own types; lwIP pcb pointers stored as `void *`. |
| `ipc_dispatch.c/.h` | `tn_handle_ipc()` becomes `static const TnIpcHandler g_ipc_table[TN_IPC_CMD_COUNT]` — `{cmd, handler, needs_base, needs_fd, flags}`; the dispatcher does the common prologue once (validate `base`, resolve `fd → slot` with `EBADF`, `in_use`, owner check) and calls `handler(d, imsg, slot)`. Handlers return `TN_IPC_REPLY_NOW` / `TN_IPC_DEFER` (for blocking connect/accept/recv). Add `tn_ipc_cmd_name(cmd)` for logs. |
| `ipc_socket.c` | OPEN, CLOSE, SOCKET, CLOSESOCKET, DUP2, RELEASESOCKET, OBTAINSOCKET, SETSOCKOPT, GETSOCKOPT, IOCTL, GETSOCKNAME, GETPEERNAME |
| `ipc_tcp.c` | BIND(tcp part), LISTEN, ACCEPT, CONNECT, SEND/RECV (stream), SHUTDOWN + the lwIP TCP callbacks (`tn_tcp_*_cb`) |
| `ipc_dgram.c` | UDP + RAW: BIND(udp/raw), SENDTO, RECVFROM, `tn_udp_recv_cb`, `tn_raw_recv_cb`, multicast join/leave |
| `ipc_msg.c` | SENDMSG, RECVMSG (iovec gather/scatter over the tcp/dgram primitives — no duplicated send/recv logic) |
| `ipc_select.c` | WAITSELECT readiness evaluation, SIGIO/event signalling (`tn_signal_socket`, `tn_record_socket_event`) |
| `ipc_netdb.c` | GETHOSTBYNAME, GETHOSTBYADDR, `tn_dns_found_cb`, PTR query builder; later §D1/D2 land here |
| `ipc_status.c` | GETSTATUS, ENUMSOCKETS, RECONFIG, (new) GETSTATS §F, (new) ENUMARP |
| `netif_mgr.c/.h` | netif bring-up/teardown, DHCP/static, loopback drain, `tn_apply_live_config`; designed for N netifs from day one (array `TnNetif ifs[TN_MAX_NETIF]`, `TN_MAX_NETIF` config-defined, default 4) even though this round still uses one — §D4 fills it |
| `daemon_main.c` | `main()`, StackSwap, `SetTaskPri`, ReadArgs sub-commands, main `Wait()` loop, shutdown ordering |
`main.c` is deleted. `Makefile` object lists updated; `TASK_OBJS` becomes a wildcard over `src/task/*.c`.

### B.2 Invariants to keep (write them as comments at the top of `ipc_dispatch.c` and assert in debug builds)
- A slot is touched only from the daemon task. Client pointers in `imsg->ptrs[]` are read/written **only before**
  `ReplyMsg`; deferred handlers keep the `TnIpcMsg *` in the slot (`pending_*_msg`) and reply from a callback.
- `ref_count` semantics: fd_map entry = +1, parked socket = +1, accept-queue entry = +1; free only at 0.
- Every lwIP callback that touches a slot checks `slot->in_use && slot->tcp_pcb == pcb` first (pcb reuse after abort).
- Reply always sets both `result` and `err_no`; `err_no` values only from `include/netinclude/sys/errno.h`.

### B.3 Proof
`make all` warning-free with `-Wall -Wextra -Wcast-align -Wshadow` (add `-Wshadow` now); `make test-host` green;
`ci/bench.sh` both configs 28/28; `m68k-amigaos-size build/tolunnet` before/after in the commit message (must not
grow > 2 %). `git diff --stat` must show main.c deleted, no test file changed.

---

## C. HOST-TESTABLE CORE (commit `test(host): slot table, queues, dispatch under ASan`)
The bench is 20+ minutes per run; the cheapest bugs to find are logic bugs in the tables and queues. Make them
run natively:
- `slot_table.c`, the RX/accept/event queues, and `ipc_dispatch.c` prologue compile on host gcc with
  `-DTN_HOST_TEST` (an `arch/tn_port.h` shim maps `AllocVec/FreeVec/CopyMem` to `malloc/free/memcpy`, `Signal()` to a
  recording stub, `struct Task *` to `void *`). No `#ifdef` soup in the units themselves — only in the shim.
- `tests/host/test_slot_table.c`: alloc/free exhaustion (`ENFILE` at 64, `EMFILE` at 32 per base), fd reuse order,
  refcount matrix (dup2 → close → close), parked-socket obtain/release across two fake bases, leak check
  (every test ends with `tn_slot_live_count() == 0`, ASan reports no leak).
- `tests/host/test_queues.c`: RX queue bound (`TN_MAX_RX_QUEUE_PER_SOCKET` → `ERR_MEM` path), partial-read offset
  arithmetic across pbuf chains (use a fake pbuf with `tot_len/len/next`), accept queue overflow → abort callback
  called, event queue coalescing (`FD_READ` twice = one entry with count 2, per Roadshow semantics — verify in
  `bsdsocket.doc`; if the doc says no coalescing, don't coalesce and test that instead).
- `tests/host/test_dispatch.c`: table completeness (every `TN_IPC_CMD_*` has a handler, `TN_IPC_CMD_COUNT` matches
  `ipc.h`), prologue rejects bad fd/base/owner with the right errno, `DEFER` path leaves the message pending.
- `test_route.c` stays SKIP until §D4 but the SKIP text is updated to point at this round's `netif_mgr` layout.
- Fuzz seam: `tests/host/fuzz_ipc.c` (optional, `make fuzz` with libFuzzer if clang present): random `TnIpcMsg`
  streams against the dispatcher with a fake lwIP — must never crash; findings become TNET rows.

---

## D. `WaitSelect` — remove the 20 ms poll fallback (commit `fix(lib): event-driven WaitSelect for sig_io == 0`)
`src/lib/lib_vectors.c` ~L560–640: when `base->sig_io == 0` the library still spins `Delay(1)` + IPC poll.
Fix: the **daemon** owns readiness; the library never polls.
- New IPC `TN_IPC_CMD_SELECT_ARM` (fd masks + timeout) → daemon registers a per-base **selector** (`TnSelector` in
  `slot_table`: read/write/except bitmaps + `struct Task *` + a signal bit the daemon allocated for the base at OPEN
  via `AllocSignal` on the client's task — impossible from another task; so instead the client allocates one signal
  bit at `OpenLibrary` (`base->sig_select = 1UL << AllocSignal(-1)`) and hands it to the daemon in OPEN).
- Daemon: whenever a slot changes readiness (`tn_signal_socket` site) it checks all selectors referencing that slot
  and `Signal(task, sig_select)`. Library: `Wait(sig_select | caller_signals | timer_sig)`, then one
  `TN_IPC_CMD_SELECT_POLL` to fetch the final fd sets, then `TN_IPC_CMD_SELECT_DISARM` (also on `EINTR`/timeout).
- `SetSocketSignals(sig_io)` keeps working as before (additive). The selector limit is `TN_MAX_SELECTORS` (config,
  default 16); overflow → old poll path is **not** allowed — return `ENOBUFS` and log.
- Tests: `tc_waitselect_timeout` precision tightens to ±20 ms; new `tc_waitselect_no_sigio` (base with
  `sig_io == 0`, recv readiness wakes within 50 ms, CPU idle — measure with `tolunnet STATS` loop counter not rising).

---

## E. TCP OPTIONS + THROUGHPUT BENCH (commit `perf(tcp): SACK, window scaling, timestamps; bench vs Roadshow`)
`lwipopts.h` (all values become documented constants in one block, with the RAM cost per line):
```
#define LWIP_TCP_SACK_OUT     1
#define LWIP_WND_SCALE        1
#define TCP_RCV_SCALE         2          /* 4x → TCP_WND 8*MSS*4 = 46 KB advertised */
#define TCP_WND               (8 * TCP_MSS)
#define LWIP_TCP_TIMESTAMPS   1
#define TCP_QUEUE_OOSEQ       1
#define TCP_OOSEQ_MAX_BYTES   (4 * TCP_MSS)
#define LWIP_TCP_KEEPALIVE    1
#define TCP_SND_BUF           (8 * TCP_MSS)
```
Rule: `TCP_WND << TCP_RCV_SCALE` must stay ≤ what `PBUF_POOL_SIZE * PBUF_POOL_BUFSIZE` can hold for 2 concurrent
streams; compute it in a `#if` static assert with a comment. Keep a `TN_PROFILE_SMALL` (`-DTN_PROFILE_SMALL`) that
sets the pre-round values for 512 KB-Fast machines; `make package` builds both binaries (`C/tolunnet` and
`C/tolunnet-small`), installer picks by `AvailMem`.
Bench (`ci/bench.sh perf`): host `iperf3`-like plain TCP sink/source in Python on the slirp host (10.0.2.2:5201);
Amiga side `tests/amiga/TcpPerf` (`ReadArgs HOST/A,PORT/N,SECONDS/N,DIR/K`) reports KB/s both directions on
A1200/020, A1200/030-8MB and 68000 configs; then the **same test binary against Roadshow** on the same bench
(Roadshow demo/installed at `E:\amiga\Amigatolon\roadshow`) — table in `docs/bench.md` "Throughput" with build hash,
options on/off, RAM delta (`AvailMem` before/after daemon start, must be ≤ 400 KB default, ≤ 160 KB small).

---

## F. TELEMETRY: STATS IPC + `tolunnet STATS` (commit `feat(status): lwIP stats, pool occupancy, queue depths`)
- `LWIP_STATS 1`, `MEMP_STATS 1`, `MEM_STATS 1`, `TCP_STATS/UDP_STATS/IP_STATS/ICMP_STATS/ETHARP_STATS/LINK_STATS 1`
  in **release** builds (`LWIP_STATS_DISPLAY 0`; cost is a few hundred bytes — measure and record).
- New IPC `TN_IPC_CMD_GETSTATS` copying `struct TnStats` into caller memory: `lwip_stats.*` counters, per-pool
  `used/max/avail` from `lwip_stats.memp[i]` (`MEMP_STATS`), heap `used/max` (`MEM_STATS`), daemon counters
  (IPC calls per cmd, deferred replies, SIGIO signals sent, selector wakeups, main-loop iterations, SANA-II RX/TX
  frames/bytes/drops, RX-queue high-water per slot), uptime, lease remaining (`dhcp->t1/t2` derived).
- `tolunnet STATS [RAW/S] [WATCH/N]` prints it (`WATCH n` re-prints every n s until Ctrl-C); `netstat -s` reuses it.
- Conformance: `tc_stats_counters` (send 3 UDP datagrams → `udp.xmit` +3; open/close → pool used returns to
  baseline). Prefs/Status window (§D6) will read the same struct.

---

## G. RECONFIG HOT-RELOAD EXTENSION (commit `feat(config): live-apply priority, log target, database order, stats`)
`tn_apply_live_config` currently applies DNS/hostname/MTU/debug. Extend, with one host test per key
(`test_config` round-trip) and one bench assertion each:
`PRIORITY=` (`SetTaskPri` live), `LOG=` path (`Close` old, `Open` new, `MODE_READWRITE` + seek to end),
`LOGLEVEL=`/`DEBUG=` (already), `DATABASE_ORDER=` (netdb reload flag), `SELECTORS=` (`TN_MAX_SELECTORS` if raised at
runtime → realloc table), `STATS=YES|NO` (counter reset), `SYSLOG=` host (§D3). Interface keys still require
restart — the RECONFIG reply now returns a bitmask `applied | needs_restart` and `tolunnet RECONFIG` prints both lists.

---

## H. SANA-II LINK EVENTS (commit `feat(sana2): S2_ONEVENT online/offline/error tracking`)
`sana2_netif.c` has no `S2_ONEVENT` (audit 3.3 open). Implement per SANA-II Rev 7:
- A dedicated `IOSana2Req` (`event_io`, own `MsgPort` or the RX port with a marker) sent async with
  `io_Command = S2_ONEVENT`, `ios2_WireError = S2EVENT_ONLINE | S2EVENT_OFFLINE | S2EVENT_ERROR | S2EVENT_TX |
  S2EVENT_RX | S2EVENT_BUFF | S2EVENT_HARDWARE | S2EVENT_SOFTWARE` (mask from config `S2EVENTS=`, default
  ONLINE|OFFLINE|ERROR). On reply, read `ios2_WireError` for the triggered events, re-arm immediately.
- OFFLINE → `netif_set_link_down` (+ `dhcp` keeps state), log; ONLINE → `netif_set_link_up`, if DHCP:
  `dhcp_renew` (or `dhcp_start` if no lease), if static: `netif_set_up`; ERROR → counter + log at BASIC tier.
  `LWIP_NETIF_LINK_CALLBACK 1` so `netif_mgr` gets the callback and updates `GETSTATUS` (`link_up` field — Prefs
  status line shows "link down").
- Shutdown ordering: `AbortIO(event_io)` + `WaitIO` **before** aborting the CMD_READs and `CloseDevice`.
- Bench: WinUAE has no cable-pull; emulate with `S2_OFFLINE`/`S2_ONLINE` via a tiny `tests/amiga/S2Toggle` tool that
  opens the same unit and issues the commands → `tc_link_events` sees `link_up` flip in `GETSTATUS` and a DHCP renew
  in the log. `uaenet.device` vs `a2065.device`: run on both if the bench has both; note which supports ONEVENT.

---

## I. ARexx PORT (commit `feat(rexx): daemon and Prefs ARexx ports`)
- Daemon: if `rexxsyslib.library` opens (v36, optional — absent = no port, log once), create a public `MsgPort`
  named `TOLUNNET` (config `REXXPORT=` to rename; refuse to start with a duplicate name → `PORT_EXISTS` error).
  Add its signal to the main `Wait()`. Messages are `struct RexxMsg`: command in `ARG0(msg)`; parse with `ReadArgs`
  templates; set `rm_Result1` (0 ok, 5 warn, 10 error, 20 fail) and, when `RXFF_RESULT` is set, `rm_Result2 =
  CreateArgstring(text)`; reply with `ReplyMsg`. Commands (mirroring the CLI, same code paths):
  `STATUS`, `STATS`, `RECONFIG`, `IFUP name`, `IFDOWN name`, `ROUTE ADD/DEL dest gw` (real after §D4; until then
  returns 10 "not implemented"), `DNS ADD/DEL ip`, `RESOLVE host` (returns dotted quad), `VERSION`, `QUIT` (same
  refusal rule as Ctrl-C when clients are open → RC 5 + count).
- Prefs: port `TOLUNNETPREFS` with `SAVE USE START STOP QUIT LOAD file SET key value GET key` — this is also how
  the bench scripts drive the GUI (ROUND4 §D6).
- Tests: `tests/amiga/RexxGauntlet.rexx` run by `rx` from `User-Startup-Conformance` (bench HDF needs `C:RX` from the
  WB 3.0 image — it is there under `SYS:System/RexxMast` + `C:RX`; start `RexxMast` first), TAP lines written to
  `WORK:rexx.log`.

---

## J. NETSNIFF SPEC ADDENDUM (no code this round; goes into ROUND4 §D5 when the bpf tap exists)
`NetSniff FILE/K` writes **pcap** (not pcapng): global header magic `0xa1b2c3d4`, version 2.4, thiszone 0,
sigfigs 0, snaplen from `SNAPLEN/N` (default 1600), linktype 1 (`LINKTYPE_ETHERNET`); per record `ts_sec`
(`GetSysTime` seconds + 252460800 to convert the Amiga 1978 epoch to Unix), `ts_usec`, `incl_len`, `orig_len`.
Little-endian magic written **as native big-endian bytes** is fine — readers detect byte order from the magic.
Verify by opening in Wireshark on the host (`docs/bench.md` screenshot). `FILTER/K` BPF expression subset:
`host`, `net`, `port`, `tcp|udp|icmp|arp`, `and|or|not`.

---

## K. BENCH SPLIT (commit `ci(bench): fast and nightly profiles`)
`ci/bench.sh` gains `PROFILE=fast|full|perf|gauntlet`: `fast` = a1200 only, single cycle, no MuForce (≤ 6 min) —
the pre-commit gate; `full` = both configs, dual cycle, MuForce if present — required before every ROUND section
commit and nightly (`scripts/nightly.ps1` Windows Task Scheduler entry, results into `docs/bench-logs/nightly/`);
`perf` = §E; `gauntlet` = ROUND4 §E. `make bench-fast` / `make bench-full` wrappers. Log dirs gain a `profile.txt`.

---

## L. IPv6-READY NETIF DESIGN NOTE (commit with §B; no IPv6 code yet)
Because `LWIP_IPV6` will be enabled in a later round, `netif_mgr` and `TnNetif` must not hard-wire IPv4:
addresses stored as `ip_addr_t` (not `ip4_addr_t`), per-netif `addr_count`, `GETSTATUS`/`ENUMSOCKETS`/`GETSTATS`
structs carry `family` + 16-byte address fields **now** (versioned struct with `size` first member so old clients
keep working), `sockaddr` marshaling goes through one `tn_sockaddr_from_ip / tn_ip_from_sockaddr` pair in
`src/common/sockaddr_util.c` (host-tested) that will grow `AF_INET6`. Write `docs/design-ipv6.md` (1 page) stating
these seams and the list of files that will change when `LWIP_IPV6=1` is flipped.

---

## ORDER & EXIT CRITERIA (rev 3)
R4 → F → G → H → E → I → K (J is documentation only; A/B/C/D/L done). Then `git stash pop` `wip-netdb-d1` and resume ROUND4 §D1 on the new module layout.
Exit: `make test-host` (now ≥ 14 binaries) green under ASan/UBSan; `bench full` 28 + new
conformance tests all `ok` on both configs; `docs/bench.md` has the throughput table and RAM deltas; ISSUES rows
TNET-087…TNET-1xx with rungs; `daemon` binary size delta documented; tree clean.

# END — start with R4, then §F. Post an Implementation Plan for §F–§K (files, tests, commit messages) before writing code.
