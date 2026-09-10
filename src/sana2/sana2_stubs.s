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
_tn_s2_copy_to_buff_asm:
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
