# TOLUNNET — Bugtrack, Hardening & GUI Refresh Prompt v3
# Target: a new, genuinely solid release build of tolunnet (AmigaOS 3.x / 68k).
# This document is the contract for this session. Read it fully before touching code.

---

## 0. ROLE & LAW

You are the implementation engineer for **tolunnet**, an lwIP-2.2.0-based TCP/IP stack and
`bsdsocket.library` v4.1 for classic AmigaOS (68k). The project is at `D:\Projeler\tolunnet`
(WSL: `/mnt/d/Projeler/tolunnet`). Toolchain: `m68k-amigaos-gcc` (Bebbo GCC 6.5.0b) in WSL.

Rules (re-read each session — same as the master prompt §4):

1. **Never claim untested behaviour.** "Compiles" ≠ "works". Say exactly what you verified and how.
2. **Every finding gets an ISSUES.md row** (`TNET-059` onward, continue the numbering), with severity,
   file:line, root cause, fix, and how it was verified.
3. **Commit per logical group**, conventional-commit style (`fix(prefs): …`, `fix(lib): …`). Do not leave
   the tree dirty. Untracked leftovers `Install` and `Install_Tolunnet-fixed` in the repo root must be
   either merged into the canonical `Install_Tolunnet` or deleted — decide and record why.
4. **Do not modify** `vendor/lwip/` (pinned, unmodified 2.2.0). Config goes in `lwipopts/lwipopts.h` only.
5. **Do not change** the universal-68k flag set: `-m68000 -msoft-float -noixemul -fomit-frame-pointer -O2`.
   No MUI, no ReAction, no ixemul — GadTools/Intuition only for the GUI.
6. **Do not** rename anything (`tolunnet`, two n's, everywhere). Do not bump the version until §7.
7. `make all` **and** `make package` must be green at the end of every commit group.
8. STATUS.md stays honest: "BUILT" vs "PROVEN ON IRON" are different columns. Do not upgrade a proof gate
   you did not personally observe (WinUAE run with log evidence in `docs/` counts as *emulator-proven*,
   not hardware-proven).

---

## 1. READ FIRST (in this order, end to end)

`README.md` → `TOLUNNET-master-prompt.md` (§4 LAW, §5 PROTOCOL, §7 bsdsocket, §9 BUDGETS) →
`TOLUNNET-COMPAT.md` → `ISSUES.md` → `STATUS.md` → `QUESTIONS.md` → `TOLUNNET-AUDIT-v2-VERIFY-round2.md`
→ `docs/architecture.md`, `docs/protocol.md`, `docs/bench.md` → then **all** of `src/`, `include/`,
`lwipopts/lwipopts.h`, `Makefile`, `Install_Tolunnet`, `.github/workflows/build.yml`, `ci/`, `tests/`.

Do not analyse a component from memory or assumption. If something looks missing, grep before saying so.

---

## 2. PRE-AUDIT: CONFIRMED DEFECTS (fix all — these were found by reading the current tree)

### P0 — crashes, data loss, cannot-restart

| ID | Where | Defect | Required fix |
|---|---|---|---|
| **TNET-059** | `src/task/main.c` shutdown (~L1355) + `src/lib/lib_init.c::tn_lib_destroy` | On Ctrl-C with `lib_OpenCnt > 0` the daemon only logs a warning, `tn_lib_destroy` refuses to `Remove()` the node, and **`main()` returns anyway** → the code segment is unloaded while per-opener clones (whose jump tables point into that segment) stay alive → next client call = Guru. | Daemon must **refuse to exit** while `lib_OpenCnt > 0`: log "N clients still open, retrying", keep servicing IPC, and re-check on every Ctrl-C (or add a `FORCE`-style break that only fires when OpenCnt==0). Alternatively, implement the standard Exec pattern: set `LIBF_DELEXP` and let the last `CloseLibrary` finish the expunge. Either way: **no return from `main()` with open bases.** |
| **TNET-060** | `src/sana2/sana2_netif.c:229-235` | `S2_CONFIGINTERFACE` failure is fatal. On a *restart* (daemon stopped and started again without reboot, which is exactly what the Prefs "Start Stack" button does) most SANA-II drivers answer `S2ERR_BAD_STATE / S2WERR_IS_CONFIGURED`. Result: tolunnet cannot be restarted. `S2_ONLINE` already handles the analogous case (L252-255); `CONFIGINTERFACE` does not. | Treat `S2ERR_BAD_STATE` + `S2WERR_IS_CONFIGURED` as success; read the station address back with `S2_GETSTATIONADDRESS` (already done) and continue. Log at BASIC tier. |
| **TNET-061** | `lwipopts/lwipopts.h` L70 vs `Makefile` L17 | Makefile passes `-DTOLUNNET_DEBUG`; lwipopts tests `TOLUNET_DEBUG` (single n). Debug builds never enable `LWIP_STATS`. Same header still says "tolunet" in its banner. | Unify on `TOLUNNET_DEBUG`. Grep the whole tree for remaining single-n `tolunet` identifiers/strings (`sana2_netif.c:254` log string, `ipc.h` field `tolunet_port`, `ci/*`, workflow) and fix the user-visible ones; internal field renames are optional but must be consistent. |
| **TNET-062** | `src/cmds/TolunnetPrefs.c` `OpenWindowTags` (WA_Height 272, WA_Top 25) | Window is 272 px high at fixed Top=25. On an **NTSC 640×200** Workbench (or any screen < 297 px) `OpenWindow` fails → the Prefs program silently exits with rc 0. PAL 640×256 also cannot show a 297-px-tall placement. | Layout must be derived from the screen: read `scr->Height`, `scr->Font`, `scr->WBorTop + scr->Font->ta_YSize + 1`; compute a compact layout that fits 640×200 (two-column form, ≤ 190 px total), center it on the visible portion, and never hard-code `WA_Top`. Fall back to `topaz/8` only when the screen font is proportional and gadgets would overflow. |
| **TNET-063** | `src/cmds/TolunnetPrefs.c` (`hostname_buf`, `dns2_buf`) + `src/common/prefs.h` (`TnPrefs`) | Hostname and Secondary DNS gadgets are **cosmetic**: their values are local buffers, never written to `TnPrefs`, never saved, never read by the daemon. `include/tolunnet/config.h` defines `HOSTNAME`, `DNS2`, `MTU`, `DEBUG` keys that `prefs.c` never parses or writes. | Extend `TnPrefs` with `hostname[64]`, `dns2[20]`, `mtu`, `debug`; parse/write them in `prefs.c`; daemon must honour `HOSTNAME` (→ `gethostname`, DHCP option 12 via `LWIP_NETIF_HOSTNAME`), `DNS2` (→ `dns_setserver(1, …)`), `MTU`, `DEBUG` tier. Document the final grammar in `docs/` and `README.guide`. |
| **TNET-064** | `TolunnetPrefs.c` `GID_SAVE` / `GID_USE` | "Save" and "Use" do the same thing (`tn_prefs_save` → both ENVARC: and DEVS:). Amiga Prefs convention: **Use** = ENV: only (until reboot), **Save** = ENV: + ENVARC:/DEVS:. Also neither notifies a running daemon. | Implement the convention. After Save/Use, if the daemon is running, send a new IPC `TN_IPC_CMD_RECONFIG` (or signal it) so it reloads config; if not running, offer to start it. |
| **TNET-065** | `TolunnetPrefs.c` `GID_DAEMON` | "Start Stack" blindly runs `Run >NIL: C:tolunnet` → a second daemon instance every click (second instance fails on `AddPort`/`AddLibrary` at best, corrupts state at worst). | Query `FindPort("tolunnet.port")` first. Button becomes **Start / Stop** (Stop = `Signal(task, SIGBREAKF_CTRL_C)` on the daemon task found via the port's `mp_SigTask`, then poll until the port is gone). Show state in a status line (see §4). |

### P1 — correctness & AmigaOS conformance

| ID | Where | Defect | Required fix |
|---|---|---|---|
| **TNET-066** | `src/task/main.c` (no `SetTaskPri`) | Network task runs at priority 0. Any busy CLI/WB task at 0 starves the stack (packet drops, DHCP timeouts). Roadshow/AmiTCP run the stack at pri ≥ 1. | `SetTaskPri(FindTask(NULL), 5)` at startup, restore on exit; make it configurable (`PRIORITY=` key, default 5). |
| **TNET-067** | `lib_vectors.c` `SetSocketSignals` + `main.c` (no `Signal()` anywhere) | `SIGIO/SIGURG/SIGINT` masks are stored but **never delivered**. Apps that rely on SIGIO wake-ups (AmiTCP-style daemons, some IBrowse paths) hang. `WaitSelect` is a 20 ms poll loop (TNET-041) — acceptable, but the daemon should at least `Signal(owner_task, sig_io)` on RX/accept/connect-complete/error when the mask is non-zero. | Implement signal delivery from the daemon side (it already knows `owner_base->owner_task`). Then convert `WaitSelect` to a real `Wait()` on `sig_io | signals | timer` with a per-call `timer.device` request; keep the poll loop only as fallback when `sig_io == 0`. |
| **TNET-068** | `lib_init.c` vector table `-216` | `gethostbyaddr` returns NULL even though `TN_IPC_CMD_GETHOSTBYADDR` exists in `ipc.h`. Reverse lookup is used by ftp/irc clients and `netstat -r`. | Implement via lwIP `dns_gethostbyname` on the `in-addr.arpa` name; fill per-task `hostent` storage. Keep `getnetby*` as NULL (document). |
| **TNET-069** | `lwipopts/lwipopts.h` | `MEMP_NUM_TCP_PCB` is not set (lwIP default **5**), `MEMP_NUM_UDP_PCB` default 4, `MEMP_NUM_TCP_PCB_LISTEN` 8, `TCP_SND_QUEUELEN` = 16 and `MEMP_NUM_TCP_SEG` = 16 shared by *all* connections. `main.c` allows 64 socket slots / 32 fds per task. A browser opening 6 connections hits `ERR_MEM` on the 6th. | Size the pools to the slot table (e.g. `MEMP_NUM_TCP_PCB 32`, `UDP 16`, `TCP_SEG ≥ 64`, `PBUF_POOL_SIZE 32`) and **document the resulting memory footprint** against the §9 budget (2 MB Chip + 4 MB Fast reference machine). Add `LWIP_NETIF_HOSTNAME 1`, `LWIP_DNS_SECURE` defaults, `DNS_MAX_SERVERS 2`. |
| **TNET-070** | `src/cmds/TolunnetPing.c`, `lwipopts.h` (`LWIP_RAW 1`), `main.c` (no `raw_pcb`) | `ping` is a **UDP echo (port 7)** probe. It does not answer from normal Internet hosts, so users will conclude the stack is broken. `LWIP_RAW` is on but `SOCK_RAW/IPPROTO_ICMP` is not exposed through the library. | Add `SOCK_RAW` + `IPPROTO_ICMP` support in the daemon (`raw_new`/`raw_recv`/`raw_sendto`, one slot type) and rewrite `ping` as real ICMP echo with id/seq/RTT/min-avg-max/loss %. Keep the UDP echo behind a `UDP` switch. Update STATUS M4 wording accordingly. |
| **TNET-071** | `src/cmds/TolunnetStatus.c:141-142` | `ifconfig` prints a hard-coded `lo0 … 127.0.0.1` interface that does not exist (`LWIP_NETIF_LOOPBACK` is off). Fabricated output violates §4. | Either enable `LWIP_NETIF_LOOPBACK 1` + `LWIP_HAVE_LOOPIF 1` (recommended — many apps connect to 127.0.0.1) or remove the line. `netstat` should get real per-socket rows via a new `TN_IPC_CMD_ENUMSOCKETS` (proto, local, remote, state) instead of only a count. |
| **TNET-072** | `TolunnetPrefs.c` `main()` | No Workbench startup handling: when launched from an icon `argc == 0` and the `WBStartup` message must be handled (libnix does this for you **only** if the startup code is linked — verify with a WB launch that `argv` is not dereferenced). Also `IntuitionBase`/`GfxBase` are relied on via libnix auto-open; verify the binary actually opens them (check with `Snoopy`/`SnoopDos` or by grepping the map file), and open them explicitly if not. ToolTypes are ignored. | Explicit `OpenLibrary` for intuition/graphics/gadtools/icon (v36+), explicit `WBStartup` handling, honour standard Prefs ToolTypes where sensible. |
| **TNET-073** | `.github/workflows/build.yml`, `ci/README.md`, `tests/host/` | CI still refers to `build/tolunet-hello` and artifact `tolunet-amiga-build`; host-tests job runs `ls` and nothing else. `tests/host` and `tests/amiga` are empty. The project has **zero automated tests**. | Fix the workflow (artifact names, `make all`, `make package`, `make test-host`). Write real host tests (native gcc) for: `inet_addr` parser (all TNET-051 forms), config parser/writer round-trip (both key dialects), LVO table vs `sfd/bsdsocket_lib.sfd` (extend `gen_lvo_table.py` to *assert*), `SocketBaseTagList` return semantics. Make `make test-host` run them. |
| **TNET-074** | `README.md` ("-m68000"), `Makefile` header comment ("68020+"), `QUESTIONS.md` #2 ("68020+"), `STATUS.md` ("68000–68060") | Three different statements of the CPU target. | Single truth: universal `-m68000` binary, runs on 68000–68060. Fix the docs. Verify with `m68k-amigaos-objdump -d` that **no** 68020+ opcodes are present in any shipped binary (scan for `extb`, `bfext*`, `link.l`, scaled-index `(…,Dn.l*4)`, `mul*.l`, `div*.l`, `rtd`, `cmp2`, `pack`). Record the command and result in ISSUES. |
| **TNET-075** | `src/cmds/TolunnetGet.c:296` | Speaks `HTTP/1.0`, ignores `3xx Location`, cannot follow redirects, no `Content-Length`/chunked awareness, no progress indication. Nearly every modern host redirects `http://` → this is the second thing a user tries. | Follow up to 5 redirects (same-scheme only; print a clear message when the target is `https://`), parse `Content-Length`, show bytes/KB-s progress on CLI, keep the 1024-byte bounded buffers (TNET-037), support `-o file` and `-q`. |

### P2 — polish (do after P0/P1 are green)

- `lib_IdString` should follow the Exec convention `"bsdsocket 4.1 (dd.mm.yyyy) tolunnet\r\n"`; `SBTC_RELEASESTRPTR` must return the same string.
- `include/ipc.h`: `TnSocketBase.tolunet_port` naming; `errno_ptr` handling when the client sets a byte-wide errno (write only the low byte — already done, keep) .
- `docs/protocol.md` must match `ipc.h` after the new commands (`RECONFIG`, `ENUMSOCKETS`, raw sockets).
- `scripts/__pycache__` should be git-ignored.

---

## 3. AmigaOS 100 % COMPATIBILITY AUDIT (do this as a pass over the whole tree, record every check)

Produce `TOLUNNET-AUDIT-v3-AMIGAOS.md` with one row per item: **item · file:line · verdict (PASS / FIXED / N-A) · evidence**.
"Evidence" means a quote of the code, an objdump line, or a WinUAE log — not an opinion.

### 3.1 Exec library semantics (`src/lib/`)
- Each `OpenLibrary("bsdsocket.library", 4)` returns a **unique** base (AmiTCP/Roadshow rule) — yes today; ensure `CloseLibrary` on a clone never touches the root's jump table and that `lib_OpenCnt` on the clone is 1.
- Register-based LVO stubs: every stub in `lib_stubs.s` preserves `d2–d7/a2–a6` per the Amiga ABI; verify the C callee (`-fomit-frame-pointer`) does not clobber `a5/a6` before the stub restores them. `a6` on entry = **the clone base** — every C function must use the base it was given, never a global.
- `LIB_EXPUNGE` returns 0 (seglist NULL) because the library is created dynamically — confirm and document; confirm `LIBF_DELEXP` handling (TNET-059).
- `lib_Flags = LIBF_SUMUSED|LIBF_CHANGED`: `AddLibrary` sums the table; clones must be summed too or must clear `LIBF_SUMUSED` (Exec re-checks the sum on `CloseLibrary` when `SUMUSED` is set → a mismatching clone sum triggers alert `AN_LibChkSum`). Verify and fix.
- `Errno()` / `SetErrnoPtr()` / `SBTC_ERRNO*PTR` widths, `h_errno`, `SBTC_DTABLESIZE` — consistent with `netinclude/sys/errno.h` values (no literals).
- `FD_SETSIZE`: NDK netinclude uses 64-bit `fd_set` for bsdsocket; `WaitSelect` must not read past `TN_MAX_FDS_PER_TASK`, and must return `EBADF` for fds ≥ table size.
- `SocketBaseTagList`: verify against Roadshow SDK `libraries/bsdsocket.h` tag numbering; `SBTC_BREAKMASK`, `SBTC_LOGSTAT`, `SBTC_SIGIOMASK/SIGURGMASK` should be handled (they map onto `SetSocketSignals`).

### 3.2 Task / IPC / memory
- All memory shared between client and daemon is `MEMF_PUBLIC`; IPC message lives inside the client's base (fine) — confirm the daemon never dereferences client pointers after `ReplyMsg`.
- No `Forbid()` held across `Wait()`/`DoIO()`; no `Disable()` at all (grep).
- The daemon **must not call into lwIP from the client's task** — only via IPC (grep `lib_vectors.c` for any lwIP symbol).
- Stack sizes: daemon started via `Run` inherits the CLI stack (default 4 KB!) — lwIP + DHCP + DNS in 4 KB will overflow. Either check `((struct Process*)FindTask(NULL))->pr_StackSize` and re-launch/`StackSwap` to ≥ 32 KB, or document `Stack 32768` in `Install_Tolunnet` / User-Startup **and** enforce with a runtime check that refuses to start below 16 KB. Prefs icon `do_StackSize` is 16 KB — fine.
- `timer.device`: two IORequests on `TnTimer` (TNET-026) — confirm both are `AbortIO`'d/`WaitIO`'d on shutdown, `CloseDevice`, `DeleteIORequest`, `DeleteMsgPort` in that order; same for SANA-II reads (`TN_S2_NREADS` outstanding `CMD_READ` must be aborted before `CloseDevice`).

### 3.3 SANA-II Rev 7 (`src/sana2/`)
- Restart tolerance (TNET-060).
- `S2_ONEVENT` for `S2EVENT_ONLINE|OFFLINE|ERROR` — subscribe and react (netif link down/up → `netif_set_link_down/up`, DHCP renew).
- `S2_BROADCAST` vs `CMD_WRITE` (TNET-046 ok), `S2_ADDMULTICASTADDRESS` if `LWIP_IGMP` is ever enabled (currently off — document).
- Buffer-management hooks: `S2_CopyToBuff`/`S2_CopyFromBuff` are `__saveds` (TNET-054 ok); add `S2_PacketFilter = NULL` explicitly; verify `bm_tags` are terminated by `TAG_DONE`.
- `ios2_Req.io_Flags = SANA2IOF_RAW` never set (we want cooked frames) — confirm.
- `ios2_DataLength` for RX: driver reports actual length; we must cap to MTU and drop oversize — check.

### 3.4 Workbench / Prefs / DOS conventions (`src/cmds/TolunnetPrefs.c`, icons, Installer)
- Standard Prefs button order and semantics: **Save · Use · Cancel** (+ our extras); ESC = Cancel; menu `Project → Save/Use/Quit`, `Edit → Reset to Defaults / Last Saved / Restore` like the OS Prefs programs.
- Window must open on the **default public screen** with the screen font (§2 TNET-062), be `WFLG_SIMPLE_REFRESH` or handle `IDCMP_REFRESHWINDOW` correctly (currently `SMART_REFRESH` + manual `render_gui_frames` on refresh — bevel boxes are not redrawn on depth-arrange with SMART; decide and test).
- `IDCMP_VANILLAKEY` for keyboard shortcuts (underscore in labels + `GT_Underscore`).
- Icons: Depth-2 4-colour `.info` verified by `scripts/verify_icons.py` — keep, and add the check to `make test-host`.
- `Install_Tolunnet` (Commodore Installer): review the LISP for `(complete)`, `(startup …)` User-Startup modification with `;BEGIN tolunnet / ;END tolunnet` markers, `DEVS:tolunnet.config` creation from a template **without** overwriting an existing one (`(exists …)` guard), and an uninstall path.

### 3.5 CPU / ABI
- `objdump` scan for 68020+ opcodes (TNET-074) on **every** shipped binary.
- No unaligned long/word access to packet data on 68000 (address error!): lwIP is built with `MEM_ALIGNMENT 4`; but `ETH_PAD_SIZE` is not set → IP header lands at offset 14 → **word-aligned, not long-aligned**. lwIP handles this with `PACK_STRUCT` + `lwip_htonl` on `u16` pairs *only if* `cc.h` declares packed structs correctly for this GCC. Verify `include/arch/cc.h` (`PACK_STRUCT_BEGIN/END/FIELD`, `__attribute__((packed))`) and that `inet_chksum` uses the 16-bit path. Consider `ETH_PAD_SIZE 2` and adjust the SANA-II copy hooks accordingly — test on a **68000** WinUAE config (A500), not only 68020+.
- `-msoft-float`: grep the map file for `__mulsf3`/`__addsf3` etc.; the stack must not link float code at all (lwIP `LWIP_PERF`/stats off).

### 3.6 Application gauntlet (emulator, record logs in `docs/`)
WinUAE with `uaenet.device`/A2065 + host NAT, per `docs/bench.md`. For each: start, run, capture `WORK:` log, note PASS/FAIL:
`ping <gw>`, `ping aminet.net` (ICMP), `wget http://aminet.net/recent.txt`, `TestSocket`, `netstat`, `ifconfig`,
`TolunnetPrefs` (Save/Use/Start/Stop cycle **twice** — this exercises TNET-059/060), AmiSSL 5 `https` via
`HTTPResume`/`AmiSSL curl` if available on the bench, IBrowse 2.5 (or AWeb) loading a plain-http page, `smbfs` mount attempt.
Any failure becomes a new ISSUES row; **do not** mark the gate green with a failing entry.

---

## 4. GUI: TolunnetPrefs redesign (GadTools only)

Goal: it should look and behave like an OS 3.x system Prefs editor, not a demo panel.

1. **Layout engine**: compute all positions from `scr->Font->ta_YSize` and `TextLength()` of the longest label;
   rows = `font_h + 6`; groups in `DrawBevelBox` frames with a `TEXT_KIND` title on the frame edge
   (like *Serial* / *ScreenMode* prefs). Must fit 640×200 with topaz/8, scale up with bigger fonts.
   Centre on the visible screen; remember position in `ENV:tolunnet.prefs` `WINX/WINY` (optional).
2. **Groups**: *Interface* (Device, Unit, MTU), *Addressing* (Mode cycle; IP, Netmask, Gateway, DNS1, DNS2 —
   **disabled via `GA_Disabled` when DHCP**), *Identity* (Hostname), *Stack* (Priority, Debug tier cycle),
   *Status* (read-only TEXT_KIND line: `Stopped` / `Running — 192.168.1.23 via DHCP, 3 sockets` refreshed every 2 s
   via `TN_IPC_CMD_GETSTATUS` using a `timer.device` request in the same `Wait()` mask).
3. **Buttons**: `Save`, `Use`, `Cancel` (Prefs standard) + `Start`/`Stop` (state-aware, TNET-065) + `Ping…`
   (opens a small child window/console with live ICMP output, not `Execute` into a CON: only) — keep the CON: fallback.
4. **Validation** on Save/Use: every address field must pass the TNET-051 `inet_addr` parser (share the code via
   `src/common/`), netmask must be contiguous, unit 0–15, MTU 576–1500, hostname RFC-952 charset. Show errors with
   `EasyRequest`, keep the window open.
5. **Menus**: `Project: Save · Use · Quit`, `Edit: Reset to Defaults · Last Saved · Restore`, `Help: About` (version,
   lwIP 2.2.0, GPL).
6. **Keyboard**: `GT_Underscore` shortcuts, ESC cancels, Return in a string gadget tabs to the next (GadTools does
   this via `STRINGA_…`/`GTST_…` tab-cycle flags — use `GA_TabCycle`).
7. Refresh correctness: `WFLG_SIMPLE_REFRESH` + `IDCMP_REFRESHWINDOW` → `GT_BeginRefresh` / redraw frames & titles /
   `GT_EndRefresh`. Test by depth-arranging another window over it.
8. Icon: keep 4-colour Depth-2, add a distinct "tolunnet" glyph if the current one is generic (optional, keep `verify_icons.py` green).

---

## 5. FEATURE ENRICHMENT (only after §2 P0/P1 and §3 are green; stop here if the budget is tight)

- Real ICMP `ping` (TNET-070) and `traceroute` (TTL-stepped ICMP/UDP, uses the same raw path).
- `netstat` per-connection rows (TNET-071) and `netstat -r` routing view from lwIP `netif` (default gw only — honest).
- Daemon control CLI: `tolunnet STATUS | STOP | RECONFIG` sub-commands (via `FindPort` + IPC) so users don't need `Break`.
- `wget`/`curl` redirects + progress (TNET-075).
- Loopback interface (TNET-071) so `127.0.0.1` works for local clients.
- DHCP hostname option and `HOSTNAME=` (TNET-063).
- Optional `LOG=path` config key to route `tn_logf` to a file when started from User-Startup (`Run >NIL:`).

Nothing beyond this list without asking in `QUESTIONS.md`.

---

## 6. ORDER OF WORK & DELIVERABLES

1. §1 read → write `TOLUNNET-AUDIT-v3-AMIGAOS.md` skeleton (all §3 rows, verdict TBD).
2. §2 P0 fixes → `make all && make package` → commit `fix(core): P0 …`.
3. §2 P1 fixes → tests (TNET-073) → commit(s).
4. §3 audit pass, fill every verdict with evidence → commit `docs(audit): AmigaOS conformance v3`.
5. §4 GUI → commit `feat(prefs): …`.
6. §5 features as budget allows → one commit each.
7. Emulator gauntlet (§3.6) → logs into `docs/`, STATUS.md proof column updated **honestly** → commit.
8. Bump to `1.2.0` **only** if every P0/P1 row is FIXED and the gauntlet has no failing row; otherwise `1.1.1-rc1`.
   Rebuild `build/tolunnet-<ver>.lha` and `build/tolunnet.adf`, refresh `README.md`, `tolunnet.readme`, `README.guide`,
   `CHANGELOG.md` (new), `ISSUES.md`, `STATUS.md`.

**Final report** (paste at the end, keep it factual):
- Table of all TNET-059…N: status, commit hash, how verified.
- Any item you could **not** verify and why.
- Exact sizes of the new `.lha` / `.adf`, and free space on the ADF.
- Open questions appended to `QUESTIONS.md`.

# END — start with §1, then §2 P0. Return with the P0 commit before continuing.
