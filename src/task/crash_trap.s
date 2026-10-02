|
| tolunnet — DIAG trap entry (TNET-139, TN-bugtrack-2 item 1)
|
| Exec calls tc_TrapCode in supervisor mode with the trap (vector) number
| longword on top of the CPU exception frame: 0(sp)=trap#, 4(sp)=frame.
| Save every register, hand the register block and the trap#/frame pointer
| to the C formatter (stack args: gcc m68k default ABI), restore, then
| chain to the previous handler with the original stack and registers
| (a0 included) so the normal Software Failure still occurs.
|
| Register block (16 longs): d0-d7, a0-a6, then USP (the task's a7).
|
    .text
    .even

    .globl _tn_crash_trap_asm
    .extern _tn_crash_entry
    .extern _tn_crash_old_trap
_tn_crash_trap_asm:
    subq.l  #4,sp            | slot for USP (block long 15)
    movem.l d0-d7/a0-a6,-(sp)
    move.l  usp,a0
    move.l  a0,60(sp)        | block[15] = USP
    move.l  sp,a0            | a0 = register block
    lea     64(sp),a1        | a1 = trap# longword, frame follows
    move.l  a1,-(sp)         | arg 2: trap#/frame
    move.l  a0,-(sp)         | arg 1: register block
    jsr     _tn_crash_entry
    addq.l  #8,sp
    movem.l (sp)+,d0-d7/a0-a6
    addq.l  #4,sp            | drop USP slot; sp = trap# again
    move.l  _tn_crash_old_trap,-(sp)
    rts                      | jump to old handler, all regs intact
