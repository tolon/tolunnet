# tolunnet — Issue & Defect Tracking Log

| ID | Severity | Milestone | Summary | Status | Resolution |
|---|---|---|---|---|---|
| **TNET-001** | Major | M1/M2 | Frame RX evidence & SANA-II hook dispatch | **RESOLVED** | SANA-II `A0/A1/D0` ASM trampolines + persistent `bm_tags` |
| **TNET-002** | Major | M2 | DHCP lease negotiation | **RESOLVED** | lwIP DHCP engine bound to SANA-II RX packet queue |
| **TNET-003** | Minor | M0 | Cross-compile portability | **RESOLVED** | Portable NDK path derived from `$(CROSS)` in Makefile |
| **TNET-004** | Minor | Architecture | Standardize project name to `tolunnet` | **RESOLVED** | Unified across all source files, headers, docs, and build targets |
| **TNET-005** | Blocker | M3 | `bsdsocket` LVO vector table offset alignment | **RESOLVED** | SFD parser + 50 vectors aligned with `bsdsocket_lib.sfd` |
| **TNET-006** | Blocker | M1 | SANA-II buffer copyfunc register calling convention | **RESOLVED** | Written in 68k assembly (`src/sana2/sana2_stubs.s`) |
| **TNET-007** | Major | M3 | `lib_OpenCnt` tracking & library expunge safety | **RESOLVED** | Atomic OpenCnt tracking under Forbid() |
| **TNET-008** | Major | M3 | `Inet_NtoA` stack corruption in RawDoFmt | **RESOLVED** | Passed 4-element ULONG array + per-opener buffer |
| **TNET-009** | Medium | M3 | Unimplemented stub return semantics | **RESOLVED** | Returns -1 with `errno = ENOSYS` |
| **TNET-010** | Minor | Docs | Protocol specification cleanup | **RESOLVED** | Documented in `docs/protocol.md` matching `include/ipc.h` |
| **TNET-011** | Minor | Logging | Clean log level defaults & S2_TRACKTYPE | **RESOLVED** | Configurable log tiers |
| **TNET-012** | Info | Architecture | Runtime dynamic library instantiation | **APPROVED** | Dynamic `MakeLibrary` / `AddLibrary` model |
| **TNET-013** | Blocker | Security | PRNG entropy seeding | **RESOLVED** | Murmur3 hash of `GetSysTime` (us) + MAC + RAM pools |
| **TNET-014** | Major | Timers | `sys_now()` precision delta time | **RESOLVED** | Computes millisecond delta from `TR_GETSYSTIME` |
| **TNET-015** | Medium | SANA-II | Asynchronous transmit queue | **RESOLVED** | Safe single-task DoIO transmit path |
| **TNET-016** | Medium | SANA-II | Zero-copy packet reception | **RESOLVED** | Direct SANA-II DMA buffer dispatch |
| **TNET-017** | Major | Build | Makefile portable NDK includes | **RESOLVED** | `NDK_INC` automatic detection |
| **TNET-018** | Minor | Debug | Assertion logging | **RESOLVED** | Debug logging via `tn_logf` |
| **TNET-019** | Medium | API | `inet_addr` bounds & octet validation | **RESOLVED** | Strict 0..255 octet, 3-dot, and trailing junk rejection |
| **TNET-020** | Minor | Perf | Flush throttling & memory attributes | **RESOLVED** | Memory buffers allocated as `MEMF_PUBLIC` |
| **TNET-021** | Blocker | Packaging | DiskObject `do_Type` offset shift in `.info` icons | **RESOLVED** | Aligned with `<workbench/workbench.h>` (`WBTOOL=3, WBPROJECT=4, WBDISK=1`) |
| **TNET-022** | Major | Packaging | Workbench data disk vs boot disk layout | **RESOLVED** | Standardized ADF as Workbench data/installation volume |
| **TNET-023** | Blocker | SANA-II | `bm_tags` stack lifetime safety | **RESOLVED** | Stored as persistent struct member in `TnSana2If` |
| **TNET-024** | Blocker | Installer | Commodore Installer LISP grammar errors | **RESOLVED** | Compliant `(welcome)` and mandatory `(help "...")` clauses |
| **TNET-025** | Medium | Tooling | `gen_lvo_table.py` authentic SFD parser | **RESOLVED** | Real parser reading `sfd/bsdsocket_lib.sfd` |
| **TNET-026** | Medium | Timers | `sys_now()` collision with in-flight periodic tick | **RESOLVED** | Dedicated `time_io` IORequest channel on `TnTimer` |
| **TNET-027** | Major | Architecture | `TolunnetStatus` linking whole lwIP stack | **RESOLVED** | Converted to thin client querying live daemon |
| **TNET-028** | Major | Compat | `sockaddr_in` zeroing & `sin_len` initialization | **RESOLVED** | Initialized `sin_len = 16` and zeroed `sin_zero` across all tools |
| **TNET-029** | Major | Correctness | `htons()` network byte order handling | **RESOLVED** | Used `htons(7)` and `htons(port)` consistently |
| **TNET-030** | Major | Diagnostic | Real ping round-trip measurement & stats | **RESOLVED** | Timed response tracking with accurate packet counts |
| **TNET-031** | Major | Memory | `TolunnetGet` RawDoFmt arg stream stack layout | **RESOLVED** | Constructed contiguous `req_args[2]` array for `path_str` & `host_str` |
| **TNET-032** | Major | Config | Configuration fragmentation across ENV blobs | **RESOLVED** | Unified on `DEVS:tolunnet.config` text KEY=VALUE format |
| **TNET-033** | Info | GUI | GadTools vs MUI preference panel | **APPROVED** | Native GadTools GUI requires 0 external dependencies (ROM 2.04+) |
| **TNET-036** | High | Compat | SocketBaseTagList return value logic | **RESOLVED** | Returns 0 on complete success; counts only unhandled tags |
| **TNET-037** | Critical | Security | TolunnetGet buffer overflow vulnerability | **RESOLVED** | Expanded `req_buf` to 1024 and capped string lengths |
| **TNET-038** | Critical | Correctness | TolunnetPing real bidirectional ping roundtrip | **RESOLVED** | Implemented `WaitSelect` timeout and `recvfrom` echo reply reading |
| **TNET-039** | High | Stability | TCP recv_cb pbuf double-free / use-after-free | **RESOLVED** | Returned `ERR_MEM` without calling `pbuf_free` on allocation failure |
| **TNET-040** | High | Correctness | tcp_write chunking and send buffer accounting | **RESOLVED** | Chunked writes to `tcp_sndbuf` and capped to 64KB per write |
| **TNET-041** | High | Correctness | WaitSelect true blocking and signal responsiveness | **RESOLVED** | Implemented polling loop with 20ms sleep slices and signal checking |
| **TNET-042** | High | Correctness | TolunnetPrefs Unit gadget state retention | **RESOLVED** | Read `LongInt` from `gad_unit->SpecialInfo` during save |
| **TNET-043** | High | Correctness | TolunnetStatus real configuration output | **RESOLVED** | Loaded active configuration from `DEVS:tolunnet.config` |
| **TNET-044** | Medium | Config | Unified text configuration format | **RESOLVED** | Standardized `DEVS:tolunnet.config` across daemon and Prefs |
| **TNET-045** | Medium | Correctness | Unit number multi-digit text formatting | **RESOLVED** | Formatted unit integer as full string in `prefs.c` |
| **TNET-046** | Medium | Correctness | SANA-II multicast vs hardware broadcast | **RESOLVED** | Restricted `is_bcast` to exact all-FF addresses |
| **TNET-047** | Medium | Security | PRNG entropy startup initialization order | **RESOLVED** | Seeded PRNG from hardware timers before `lwip_init` |
| **TNET-048** | High | Compat | Dup2Socket descriptor aliasing and lifetime leak | **RESOLVED** | Routed via `TN_IPC_CMD_DUP2` with socket slot reference counting |
| **TNET-049** | Low | Correctness | TCP error socket state mapping | **RESOLVED** | Mapped `TN_TCP_STATE_ERROR` to `ECONNRESET` / EOF on receive |
| **TNET-050** | Low | Correctness | TCP RX queue depth bounding | **RESOLVED** | Enforced `TN_MAX_RX_QUEUE_PER_SOCKET` in `tn_tcp_recv_cb` |
| **TNET-051** | Low | Compat | inet_addr dotted-quad single format constraint | **RESOLVED** | Implemented standard BSD 1-4 part decimal/octal/hex parser |
| **TNET-052** | Low | Optimization | Dead RawDoFmt cleanup in `prefs.c` | **RESOLVED** | Removed unused buffer and format call |
| **TNET-053** | Low | Tooling | Authentic SFD table generator synchronization | **RESOLVED** | Implemented `scripts/gen_lvo_table.py` parsing `bsdsocket_lib.sfd` |
| **TNET-055** | Low | Compat | `sin_len` initialization on recvfrom | **RESOLVED** | Initialized `from->sin_len = sizeof(struct sockaddr_in)` |
| **TNET-056** | Blocker | GUI | TolunnetPrefs GadTools font pointer type crash (#8000000B) | **RESOLVED** | Set `ng_TextAttr = NULL` to safely use default Screen font in GadTools |
| **TNET-057** | Major | Packaging | Installer subdrawer nesting & icon positioning | **RESOLVED** | Corrected Installer `dest` to `SYS:Prefs` and snapped `TolunnetPrefs.info` to grid (4,48) |

