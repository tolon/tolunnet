# architecture.md

> Master prompt §6 asks for the threading model to be documented here. This is
> the M0 skeleton; it is filled in as the relevant code lands.

## Layout (master prompt §2)

```
src/task/      network task: lwIP init, timers, netif mgmt, request dispatch
src/bsdsocket/ bsdsocket.library: per-opener base, socket table, marshalling
src/sana2/     lwIP netif ↔ SANA-II driver (CMD_READ pump, copyfuncs)
src/cmds/      shell tools (TolunetStatus, TolunetPing, TolunetGet)
src/common/    log, mem
include/tolunet/  protocol.h, config.h
lwipopts/      lwipopts.h (NO_SYS=1 build config)
vendor/lwip/   pinned, unmodified
```

## Threading model (target)

- **One** Amiga task owns lwIP. lwIP is built `NO_SYS=1` (raw/callback API); it
  has no threads, mutexes, or semaphores of its own.
- The task owns a single public `MsgPort`. The library `PutMsg`s requests to it
  (protocol: `include/tolunet/protocol.h`).
- lwIP runs only on the task's context: the task's mainloop drains the port and
  calls `sys_check_timeouts()` on each 100 ms timer tick.
- **Blocking happens on the caller's task, not the network task.** A blocking
  call in the library `PutMsg`s a request and `Wait`s on the reply signal
  (OR'd with `SIGBREAKF_CTRL_C` and the opener's break mask). The network task
  never blocks on a socket.
- The library never calls lwIP directly (§7.0); it only marshals requests.
- Per-opener base clones hold: errno pointer, signal masks, `h_errno`, fd table.
  A global semaphore-protected registry backs `ObtainSocket`/`ReleaseSocket`.

> Status: this is the design. None of it is built yet beyond the protocol
> header and the hello-task. See STATUS.md.

## Memory

- lwIP pools come from `MEM_SIZE` / `PBUF_POOL_SIZE` (lwipopts.h start values,
  §6). The task allocates the lwIP heap once at init.
- Resident budget: ≤ 250 KB task+library+pools (§9). Measured value lands in
  STATUS.md once `TolunetStatus MEM` exists.
- Task stack ≥ 16 KB explicit (the 4 KB default overflows lwIP paths, §9).

## Big-endian

68k is big-endian = network order. `htons`/`htonl` are identity, but the macros
are kept (§3) so the code reads correctly and stays portable in intent.
