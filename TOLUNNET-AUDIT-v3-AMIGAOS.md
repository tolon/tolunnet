# TOLUNNET — AmigaOS 100% Compatibility Audit v3

Contract: `TOLUNNET-BUGTRACK-v3-prompt.md` §3. One row per item: **item · file:line ·
verdict (PASS / FIXED / N-A) · evidence**. Evidence = quoted code, objdump line, or
WinUAE log — never an opinion.

Status legend: `TBD` = not yet audited this pass · `PASS` = verified as-is ·
`FIXED` = defect found and fixed in this pass (see ISSUES.md row) · `N-A` = not
applicable to the current scope (with reason).

---

## 3.1 Exec library semantics (`src/lib/`)

| # | Item | Where | Verdict | Evidence |
|---|---|---|---|---|
| 1.1 | Each `OpenLibrary("bsdsocket.library",4)` returns a unique clone base (AmiTCP/Roadshow rule); clone `lib_OpenCnt`==1 | `src/lib/lib_vectors.c` `tn_lib_open` | TBD | |
| 1.2 | `CloseLibrary` on a clone never touches the root's jump table | `src/lib/lib_vectors.c` `tn_lib_close` | TBD | |
| 1.3 | Register stubs preserve `d2–d7/a2–a6` per Amiga ABI (C callee clobbers at most d0/d1/a0/a1) | `src/lib/lib_stubs.s` | TBD | |
| 1.4 | `a6` on entry = the clone base; every C function uses its `base` argument, never a global | `src/lib/lib_vectors.c` | TBD | |
| 1.5 | `LIB_EXPUNGE` returns 0 (seglist NULL; library created dynamically) | `src/lib/lib_init.c` `tn_lib_expunge` | TBD | |
| 1.6 | `LIBF_DELEXP` handling — daemon does not exit with open bases (TNET-059) | `src/task/main.c` shutdown path | TBD | |
| 1.7 | Root `lib_Flags = LIBF_SUMUSED\|LIBF_CHANGED`; clone checksum semantics — clone must not trigger `AN_LibChkSum` on `CloseLibrary` | `src/lib/lib_init.c` `tn_lib_create`, `lib_vectors.c` `tn_lib_close` | TBD | |
| 1.8 | `Errno()/SetErrnoPtr()/SBTC_ERRNO*PTR` widths; `h_errno`; `SBTC_DTABLESIZE` consistent with netinclude values (no literals) | `src/lib/lib_vectors.c` | TBD | |
| 1.9 | `FD_SETSIZE` 64-bit fd_set; `WaitSelect` must not read past `TN_MAX_FDS_PER_TASK`; `EBADF` for fd ≥ table | `src/lib/lib_vectors.c` `tn_lvo_waitselect`, `src/task/main.c` WAITSELECT | TBD | |
| 1.10 | `SocketBaseTagList` tag numbering vs Roadshow `libraries/bsdsocket.h`; `SBTC_BREAKMASK/SIGIOMASK/SIGURGMASK/SIGEVENTMASK` handled | `src/lib/lib_vectors.c` `tn_lvo_socketbasetaglist` | TBD | |

## 3.2 Task / IPC / memory

| # | Item | Where | Verdict | Evidence |
|---|---|---|---|---|
| 2.1 | All client/daemon shared memory is `MEMF_PUBLIC`; daemon never dereferences client pointers after `ReplyMsg` | `src/task/main.c`, `src/lib/lib_vectors.c` | TBD | |
| 2.2 | No `Forbid()` across `Wait()`/`DoIO()`; no `Disable()` anywhere (grep) | tree-wide | TBD | |
| 2.3 | Daemon never calls lwIP from the client's task — only via IPC (grep `lib_vectors.c` for lwIP symbols) | `src/lib/lib_vectors.c` | TBD | |
| 2.4 | Daemon stack ≥ 16 KB: runtime `pr_StackSize` check (refuse < 16 KB) and `Stack 32768` documented in Installer/User-Startup | `src/task/main.c`, `ci/User-Startup*`, `Install_Tolunnet` | TBD | |
| 2.5 | `timer.device`: both `TnTimer` IORequests `AbortIO`/`WaitIO`'d, `CloseDevice`, `DeleteIORequest`, `DeleteMsgPort` in order (TNET-026) | `src/task/timers.c` `tn_timer_fini` | TBD | |
| 2.6 | SANA-II outstanding `CMD_READ`s aborted before `CloseDevice` | `src/sana2/sana2_netif.c` `tn_s2_offline_close` | TBD | |

## 3.3 SANA-II Rev 7 (`src/sana2/`)

| # | Item | Where | Verdict | Evidence |
|---|---|---|---|---|
| 3.1 | Restart tolerance: `S2_CONFIGINTERFACE` treats `S2ERR_BAD_STATE/S2WERR_IS_CONFIGURED` as success (TNET-060) | `src/sana2/sana2_netif.c` `tn_s2_online` | TBD | |
| 3.2 | `S2_ONLINE` treats `S2ERR_BAD_STATE/S2WERR_UNIT_ONLINE` as success | `src/sana2/sana2_netif.c` `tn_s2_online` | TBD | |
| 3.3 | `S2_ONEVENT` subscription for `S2EVENT_ONLINE/OFFLINE/ERROR` → netif link up/down + DHCP renew | `src/sana2/sana2_netif.c` | TBD | |
| 3.4 | `S2_BROADCAST` vs `CMD_WRITE` selection (TNET-046); `S2_ADDMULTICASTADDRESS` only if `LWIP_IGMP` (currently off — document) | `src/sana2/sana2_netif.c` `tn_s2_send` | TBD | |
| 3.5 | `S2_CopyToBuff`/`S2_CopyFromBuff` hooks `__saveds` (TNET-054); `S2_PacketFilter = NULL` explicit; `bm_tags` terminated by `TAG_DONE` | `src/sana2/sana2_netif.c`, `src/sana2/sana2_stubs.s` | TBD | |
| 3.6 | `ios2_Req.io_Flags = SANA2IOF_RAW` never set (cooked frames wanted) | `src/sana2/sana2_netif.c` | TBD | |
| 3.7 | RX `ios2_DataLength` capped to MTU, oversize dropped | `src/sana2/sana2_netif.c` `tn_sana2_poll_input` | TBD | |

## 3.4 Workbench / Prefs / DOS conventions (`src/cmds/TolunnetPrefs.c`, icons, Installer)

| # | Item | Where | Verdict | Evidence |
|---|---|---|---|---|
| 4.1 | Standard Prefs button order/semantics: Save · Use · Cancel; ESC = Cancel; menus Project/Edit/Help | `src/cmds/TolunnetPrefs.c` | TBD | |
| 4.2 | Use = ENV: only; Save = ENV: + ENVARC:/DEVS: (TNET-064); both notify a running daemon (RECONFIG) | `src/cmds/TolunnetPrefs.c`, `src/common/prefs.c` | TBD | |
| 4.3 | Window opens on default public screen with screen font; layout fits 640×200 NTSC; no hard-coded `WA_Top` (TNET-062) | `src/cmds/TolunnetPrefs.c` | TBD | |
| 4.4 | Refresh correctness: SIMPLE_REFRESH + `IDCMP_REFRESHWINDOW` → `GT_BeginRefresh`/redraw/`GT_EndRefresh` (bevels survive depth-arrange) | `src/cmds/TolunnetPrefs.c` | TBD | |
| 4.5 | `IDCMP_VANILLAKEY` keyboard shortcuts; `GT_Underscore`; tab-cycle string gadgets | `src/cmds/TolunnetPrefs.c` | TBD | |
| 4.6 | WB startup (`argc==0`, `WBStartup` message), explicit `OpenLibrary` intuition/graphics/gadtools, ToolTypes honoured (TNET-072) | `src/cmds/TolunnetPrefs.c` | TBD | |
| 4.7 | Icons: Depth-2 4-colour `.info` verified by `scripts/verify_icons.py`, wired into `make test-host` | `scripts/verify_icons.py`, `Makefile` | TBD | |
| 4.8 | Installer: `(complete)`, User-Startup `;BEGIN/;END tolunnet` markers, `DEVS:tolunnet.config` from template with `(exists…)` guard, uninstall path | `Install_Tolunnet` | TBD | |
| 4.9 | Start/Stop stack control via `FindPort`, no duplicate daemon spawn (TNET-065) | `src/cmds/TolunnetPrefs.c` | TBD | |

## 3.5 CPU / ABI

| # | Item | Where | Verdict | Evidence |
|---|---|---|---|---|
| 5.1 | `objdump` scan: no 68020+ opcodes in any shipped binary (`extb`, `bfext*`, `link.l`, scaled index, `mul*.l`, `div*.l`, `rtd`, `cmp2`, `pack`) — TNET-074 | `build/release/…` binaries | TBD | |
| 5.2 | No unaligned long/word access to packet data on 68000: `ETH_PAD_SIZE`/`MEM_ALIGNMENT` analysis; `cc.h` PACK_STRUCT correctness; chksum 16-bit path; A500 (68000) emulator test | `include/arch/cc.h`, `lwipopts/lwipopts.h`, `vendor/lwip/src/core/inet_chksum.c` | TBD | |
| 5.3 | `-msoft-float`: map file grep shows no `__mulsf3`/`__addsf3` float runtime linked | `build/tolunnet` map | TBD | |
| 5.4 | Single CPU-target truth in docs: universal `-m68000`, runs 68000–68060 (TNET-074) | `README.md`, `Makefile`, `STATUS.md`, `QUESTIONS.md` | TBD | |

## 3.6 Application gauntlet (WinUAE, logs archived in `docs/`)

Bench per `docs/bench.md`: WinUAE, `uaenet.device`/A2065 + host NAT, clean OS 3.x,
`WORK:` host dir for logs. Status of each entry this pass:

| # | Entry | Verdict | Evidence log |
|---|---|---|---|
| 6.1 | `ping <gw>` (4 probes, RTT + loss) | TBD | |
| 6.2 | `ping aminet.net` (ICMP — pending TNET-070 raw sockets) | TBD | |
| 6.3 | `wget http://aminet.net/recent.txt` | TBD | |
| 6.4 | `TestSocket` (M6 suite) | TBD | |
| 6.5 | `netstat` / `ifconfig` | TBD | |
| 6.6 | `TolunnetPrefs` Save/Use/Start/Stop cycle **twice** (exercises TNET-059/060) | TBD | |
| 6.7 | AmiSSL 5 https via HTTPResume/AmiSSL curl (if available on bench) | TBD | |
| 6.8 | IBrowse 2.5 / AWeb plain-http page load | TBD | |
| 6.9 | `smbfs` mount attempt | TBD | |

---

## Verdict summary

Section totals — PASS: 0 · FIXED: 0 · N-A: 0 · TBD: 42 (all rows).

This skeleton was committed before any §2 P0 code changes, per §6.1 of the
bugtrack contract. Verdicts are filled only with evidence during the §3/§6.7
passes; emulator-proof items require `WORK:` logs in `docs/` and never count as
hardware proof (§0 rule 8).
