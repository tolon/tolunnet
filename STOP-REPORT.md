# Tolunnet STOP-REPORT: z.ai step 7 — item 7 (usergroup RTF_AUTOINIT) still red after two fixes

**Timestamp:** 2026-09-25
**Work order:** TN-zai-step.md. Items 0–6 landed green-benched; item 7 (usergroup
RTF_AUTOINIT) is red on `tc_usergroup` after two fix attempts → per the rules:
STOP-REPORT with verbatim not ok, stop.

**Commits this step (all green-benched except item 7):**
- `fd6915e` — item 0: test honesty (CLEARED branch fails; badf clamp rule; INDEX).
- `a7cc13f` — item 1: SetSignal(bits,bits) ×3 restore sites; watchdog timer
  retired (AbortIO/WaitIO/drain) before CANCEL; CANCEL preallocated in
  tn_lib_open.
- `26c0875` — item 2: check_nfds clamp rule (0..FD_SETSIZE, only nfds<0 EINVAL);
  SELECT_ARM/WAITSELECT dual-word with SEPARATE output accumulators and both
  words written back; retry re-ARMs; infinite wait never returns 0;
  WaitSelect(0,…,NULL) blocks on the break mask; fd_set zero sites clear both
  words; fd40 idle-fd-33 assertion.
- `2fd7a03` — item 4: tcp_output pcb re-checks after drains (ipc_tcp.c,
  ipc_msg.c).
- `4c5a14a` — item 5: LISTENING setsockopt/getsockopt slot-only; slot->opt_mss;
  bench row tc_listen_setsockopt.
- `95e3a6f` — item 6: CLI args copied before FreeArgs; silent reap of parked
  accept/connect/recv; STOP reaps then replies EBUSY; stopping=FALSE on the
  deferred branch; tn_reap_dead_clients_public.
- `98bb0bf` + `0c70c9a` — item 7: RTF_AUTOINIT + register-ABI initializer
  (STILL RED — see below).

**Last all-green bench before item 7:** `docs/bench-logs/20260925-015525-v1.2.0-rc4-60-g4c5a14a/`
(core 74 ok / 8 not ok = the 8 TODO rows only; item-4 rows green).
**Red bench:** `docs/bench-logs/20260925-030243-v1.2.0-rc4-65-g0c70c9a/`
(a1200 == 68000, both cycles).

## The red (verbatim)

```
not ok 63 - tc_usergroup # getpwnam(root) failed or fields mismatch
```
(both profiles, both cycles; every other row green — TODO 8)

## What was attempted for item 7

1. `98bb0bf`: RomTag switched to RTF_AUTOINIT | RTF_AFTERDOS with
   g_ug_init_table {sizeof(UserGroupBase), g_ug_vectors, ug_init_from_table,
   NULL}; ug_init_lib kept as a wrapper.
2. `0c70c9a`: initializer redeclared with explicit exec register bindings
   (d0=segList, a0=libBase, a6=ExecBase).

Both still fail `tc_usergroup # getpwnam(root) failed or fields mismatch` —
so the AUTOINIT initializer either still receives wrong arguments or the DB
init path (ug_db_init reading files via DOSBase) behaves differently when
invoked from exec's AutoInit flow instead of the old direct call.

## Suggested next steps (owner decision)

1. Bench the usergroup library in ISOLATION (minimal CLI: OpenLibrary →
   getpwnam("root") → print → CloseLibrary) to see which part breaks: open
   itself, the DB file read, or the field layout.
2. Consider reverting item 7 to the pre-step-7 RomTag (non-AUTOINIT but with
   an asm stub entry that preserves d0/a0/a6) — usergroup.library was
   functional before this step; the regression is contained to this row.
3. Alternatively keep RTF_AUTOINIT but debug via ROM INFO / seg tracker which
   arguments arrive in the initializer.

All earlier fixes (items 0–6) remain committed and green:
last fully green bench `20260925-015525-v1.2.0-rc4-60-g4c5a14a` (74 ok / 8 TODO).
