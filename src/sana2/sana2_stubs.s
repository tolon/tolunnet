|
| tolunet — SANA-II Buffer Management Hook Trampolines (TNET-006)
|
| SANA-II Rev 7 convention: a0=to, a1=from, d0=length. Returns d0=BOOL.
| Bridges driver register call convention to C copy implementations.
|

    .text
    .even

    .globl _tn_s2_copy_to_buff_asm
_tn_s2_copy_to_buff_asm:
    move.l  d0,-(sp)        | arg3: len
    move.l  a1,-(sp)        | arg2: src
    move.l  a0,-(sp)        | arg1: dst
    jsr     _tn_copy_to_buff_c
    lea     12(sp),sp
    rts

    .globl _tn_s2_copy_from_buff_asm
_tn_s2_copy_from_buff_asm:
    move.l  d0,-(sp)        | arg3: len
    move.l  a1,-(sp)        | arg2: src
    move.l  a0,-(sp)        | arg1: dst
    jsr     _tn_copy_from_buff_c
    lea     12(sp),sp
    rts
