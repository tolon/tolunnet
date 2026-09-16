# TNET-115 Live Diagnosis — 2026-09-16

## Freeze captured live, twice, fully symbolized

Two independent freezes captured on the `ci/.repro115.uae` rig (chip-only
68000, DIAG build, FreezeWatch resident). Evidence:
`docs/bench-logs/tnet115-live-capture-1/` and `tnet115-live-capture-2/`
(debugger console+conlog dumps, bsdsocktest.log frozen at 41 lines).

## What the freeze IS

**Not an exception.** `il 8` armed in capture-1: never fired. No Guru ever
appears. The freeze is a **priority-5 livelock**:

- `Tt` at freeze: `ThisTask = [5, 'C:tolunnet']`; `Workbench` sits **READY
  but never scheduled**; `bsdsocktest` [4] waits on its reply-port signal
  (0x100) forever; `FreezeWatch` [3] waits on 0x100 forever — **its own
  45 s stall-dump never fires and a host-dropped `WORK:dumptasks` trigger
  file is never consumed** — proof that zero task switches occur.
- CPU samples (6 alternating break/resume + register reads): ~2/3 of
  samples inside the daemon's checksum loop, remainder in the ROM VERTB
  chain — interrupts run, scheduler never dispatches anything else,
  because the running task (daemon, priority 5) never blocks.

## Symbolized stack (frozen daemon, base 0x00C4D378)

```
tn_handle_ipc+0x76
  tn_ipc_cmd_sendmsg+0x424          <- bsdsocktest #32 sendmsg dispatch
    tcp_write
      tcp_output+0x2ec
        inet_chksum_pseudo+0x64
          lwip_standard_chksum      <- PC spins here
```

Base solved by matching the live `moveq #0,d1; move.w (a0)+,d1; add.l d1,d2;
dbf` loop to file offset 0x161b2 of `build/tolunnet` (objdump).

## The loop itself

Consecutive register samples inside the checksum:

```
s1: D0=0x0D D1=0x4CF5 A0=0x0000EC7A
s2: D0=0x18 D1=0xBE78 A0=0x0000EC96
```

Both walks end inside the **same ~66-byte buffer at 0x0000EC54–0x0000EC96**
(memp segment-pool region), each checksum short (~dozens of bytes), D0
always near loop end — the daemon has been checksumming the *same tiny
segment(s)* for **20+ minutes**. Conclusion: **tcp_output is walking an
unsent-segment queue that is circular** (a few pool entries cycling), so
`tcp_output` never terminates, `tn_ipc_cmd_sendmsg` never replies, and the
priority-5 daemon starves the entire machine.

## Why the two profiles differ

- **68000 (slow)**: the livelock never breaks inside the bench window →
  "freeze" (occurrences 1-8).
- **a1200 (020, fast)**: the cycle eventually breaks via a different
  memory/timing path, but the segment chain is already corrupted → #32
  completes with **wrong data** — the long-standing a1200 functional
  failure. One root cause, two symptoms.

## Trigger window

bsdsocktest #32 sendmsg/recvmsg scatter-gather (3 iovecs 50+30+20) on a
TCP loopback socket. Our handler performs **one tcp_write per iovec chunk
(3 writes) followed by a single tcp_output** — the 2nd/3rd write's segment
insertion at specific `snd_buf`/`snd_queuelen` boundaries is the prime
suspect for creating the cycle in the unsent list.

## Next step (fix session)

1. Host test: with the mock, replicate 3 consecutive `tcp_write` calls at
   exact snd_buf/snd_queuelen boundaries + `tcp_output`; assert the mock
   segment queue terminates (walk with a step cap). Add the failing
   boundary sizes found by bisecting snd_buf values.
2. Inspect lwIP 2.2.0 `tcp_write` unsent/unsent_tail insertion for the
   multi-write-single-output pattern against our lwipopts (TCP_SND_QUEUELEN
   / TCP_SND_BUF / segment pool geometry) — the cycle is a list-link bug
   or a pool reuse race our config exposes.
3. Interim hardening candidates if the exact boundary isn't found fast:
   a step cap in our sendmsg handler between per-iovec writes (flush via
   tcp_output per chunk instead of batched) — changes the pattern that
   creates the cycle and is host-testable both ways.
4. Proof bar (unchanged, TN-bugtrack-2 item 2): #32 `ok` ×2 both profiles
   + 4 consecutive freeze-free full runs.
