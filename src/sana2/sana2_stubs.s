|
| tolunnet — SANA-II Buffer Management Hook Trampolines (TNET-006/TNET-139)
|
| SANA-II Rev 7 convention: a0=to, a1=from, d0=length. Returns d0=BOOL.
|
| TNET-139: the hook is a driver-invoked register-convention function — it
| may clobber NOTHING except d0, and it runs on the DRIVER's stack, which
| can be tiny (the previous C-bridging variant clobbered d1/a0/a1 — real
| drivers fault on that — and a movem-preserving variant used 76 bytes of
| stack and reset the emulated uaenet driver at the first frame). This
| pure-asm byte copy touches only d0 (length in, BOOL TRUE out), saves
| a0/a1 in 8 bytes of stack, and never assumes alignment: move.b is
| Address-Error proof on the 68000 whatever alignment either buffer has.

    .text
    .even

    .globl _tn_s2_copy_to_buff_asm
    .globl _tn_s2_rx_cap
_tn_s2_copy_to_buff_asm:
    | TNET-audit run-1: clamp the driver-supplied length to the CMD_READ
    | buffer capacity so an oversize frame cannot overflow the Exec heap.
    | _tn_s2_rx_cap is max(mtu+32,1600), constant for the netif. The
    | truncated frame is then dropped by tn_sana2_poll_input's length
    | check (flen > mtu / 1600) and the slot is re-armed normally, which a
    | FALSE return would not do (error completions skip the re-arm). Only
    | d0 and CCR are touched, preserving the SANA-II register contract.
    tst.l   _tn_s2_rx_cap   | cap 0 => not initialised, do not clamp
    beq.s   tn_s2_to_save
    cmp.l   _tn_s2_rx_cap,d0
    bls.s   tn_s2_to_save   | d0 <= cap: copy in full
    move.l  _tn_s2_rx_cap,d0 | oversize: clamp to capacity
tn_s2_to_save:
    move.l  a0,-(sp)        | preserve dst (driver context)
    move.l  a1,-(sp)        | preserve src
    bra.s   tn_s2_copy_entry

    .even
tn_s2_copy_loop:
    move.b  (a1)+,(a0)+
tn_s2_copy_entry:
    subq.l  #1,d0           | borrow set when d0 was 0: copy nothing
    bcc.s   tn_s2_copy_loop
    move.l  (sp)+,a1        | restore src
    move.l  (sp)+,a0        | restore dst
    moveq   #1,d0           | BOOL TRUE
    rts

    .globl _tn_s2_copy_from_buff_asm
_tn_s2_copy_from_buff_asm:
    move.l  a0,-(sp)        | preserve dst
    move.l  a1,-(sp)        | preserve src
    bra.s   tn_s2_copy_entry | shared loop (fallthrough is rts, so branch)
