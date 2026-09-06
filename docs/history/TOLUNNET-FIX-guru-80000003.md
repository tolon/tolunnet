# FIX ORDER — `C:tolunnet` Guru `#80000003` on the 68000 bench (tolunnet-68000.uae)

## What the number means (do not guess around it)
`8000 0003` = 68000 exception vector 3 = **Address Error**: a word/long read or write at an ODD address.
Only the 68000/68010 raise it; 68020+ silently allow unaligned access. That is why the same binary ran
on the A1200/020 config and dies here. The bug is real on hardware (A500/A600/A2000 without accelerator).
It is NOT a stack, library or icon problem — do not touch those.

## Step 1 — get the faulting PC (mandatory, 5 minutes, no code changes)
1. Start WinUAE with `tolunnet-68000.uae`, before running the daemon press **Shift+F12** (debugger).
2. Type `il 8` (exception breakpoint mask, bit 3 = Address Error), then `g` to continue.
3. Run `C:tolunnet` from the shell. The debugger breaks at the fault. Type `r` — note **PC**, the
   **SR/access address** line (WinUAE prints the fault address), and D0–A7.
4. Find the load address of the code hunk: `s "tolunnet: network task" 0 8000000` (or any unique string
   from the daemon) or `Tt`/`Th` to list tasks/segments; `segment_start = string_addr - string_offset`
   where `string_offset` comes from `m68k-amigaos-objdump -s build/tolunnet | grep -B1 "tolunnet: net"`.
5. `m68k-amigaos-objdump -d -l build/tolunnet` (rebuild once with `DEBUG=1` for `-g` line info) and
   look up `PC - segment_start` → the exact C line. Paste PC, fault address, and that line into
   `ISSUES.md` as **TNET-084** before fixing anything.

## Step 2 — fix by class (the line from Step 1 tells you which; do only that one, then re-run Step 1)
A. **Odd pointer cast to a struct/word/long** (`(ULONG *)buf`, `(struct sockaddr_in *)ptr`,
   `*(UWORD *)p`, `CopyMem` into a `char[]` then read as struct, `RawDoFmt` args array that is not
   `ULONG[]`): make the destination a properly typed/aligned object (`ULONG args[]`,
   `__attribute__((aligned(2)))`), or copy byte-wise. Grep the daemon for every cast of a `char*`/`UBYTE*`
   to a wider type — fix all of them, not just the one that fired.
B. **lwIP packed structs**: `include/arch/cc.h` MUST define
   `PACK_STRUCT_BEGIN`, `PACK_STRUCT_END`, `PACK_STRUCT_STRUCT __attribute__((packed))`,
   `PACK_STRUCT_FIELD(x) x`, `PACK_STRUCT_FLD_8(x) x`, `PACK_STRUCT_FLD_S(x) x`. If any is missing,
   lwIP headers (`ip.h`, `tcp.h`, `dhcp.h`, `dns.h`) get natural alignment and GCC emits `move.w/.l` on
   possibly odd pointers. With packed, GCC on `-m68000` emits byte accesses — correct.
C. **Frame/pbuf alignment**: if the fault is inside `ethernet_input`/`ip4_input`/`etharp`:
   set `#define ETH_PAD_SIZE 2` in `lwipopts.h` and make the SANA-II copy hooks / `tn_sana2_poll_input`
   deliver the Ethernet header at `p->payload + ETH_PAD_SIZE` (drop the pad with `pbuf_remove_header`
   before `netif->input`, add it back with `pbuf_add_header` before `tn_s2_send`). Verify every RX
   buffer handed to lwIP starts at an EVEN address (`AllocMem` is 8-byte aligned; check any `+1`/`+3`
   offsets in `buffers.c`).
D. **Stack/StackSwap (only if PC is in startup code)**: stack size and `stk_Pointer` must be even;
   `stk_Upper = base + size`, `stk_Pointer = stk_Upper` (size a multiple of 4).

## Step 3 — prove it
- Re-run Step 1 with `il 8` still armed: daemon must reach "network task running" with no break, then
  run `SocketConformance` (or `TestSocket` + `ping`) on the 68000 config; log to `WORK:`.
- Add a **68000 alignment guard** so this class cannot return: in `tests/host/`, a test compiled with
  `-Wcast-align=strict` over `src/` (host gcc) must produce zero warnings; add `-Wcast-align` to the
  Amiga `CFLAGS` and fix every warning.
- ISSUES TNET-084 row: PC, fault address, C line, root-cause class (A/B/C/D), commit, log path. Rung L3.

Do not report this fixed on the basis of "compiles" or "works on the 020 config".
