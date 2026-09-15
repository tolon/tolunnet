|
| tolunnet — DIAG trap entry (TNET-139, TN-bugtrack-2 item 1)
|
| Exec calls tc_TrapCode with the CPU exception frame on the stack.
| Save every register we touch, hand the register block and the frame to
| the C writer, restore, then chain to the previous handler with the
| original stack state so the normal Software Failure still occurs.
|
    .text
    .even

    .globl _tn_crash_trap_asm
    .extern _tn_crash_entry
    .extern _tn_crash_old_trap
_tn_crash_trap_asm:
    movem.l d0-d7/a0-a6,-(sp)
    move.l  sp,a0            | a0 = saved register block (d0..a6, 15 longs)
    lea     60(sp),a1        | a1 = original SP = exception frame
    jsr     _tn_crash_entry
    movem.l (sp)+,d0-d7/a0-a6
    move.l  _tn_crash_old_trap,a0
    jmp     (a0)

    .even
    .data
_tn_crash_old_trap:
    .long   0
