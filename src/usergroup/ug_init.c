/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ug_init.c — Library initialization, resident RomTag, and lifecycle.
 *
 * Target: AmigaOS 3.0+, universal 68k (-m68000).
 */

#include "usergroup_base.h"
#include <string.h>

#ifdef __AMIGA__
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/initializers.h>
#include <proto/exec.h>
#include <proto/dos.h>

extern const APTR g_ug_vectors[];

/* CLI safety entry point and RomTag */
__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _ug_lib_entry\n"
    "_ug_lib_entry:\n"
    "    moveq   #-1, %d0\n"
    "    rts\n"
);

__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _ug_autoinit_stub\n"
    "_ug_autoinit_stub:\n"
    "    move.l  %a0, -(%sp)\n"
    "    move.l  %d0, -(%sp)\n"
    "    jsr     _ug_init_c\n"
    "    addq.l  #8, %sp\n"
    "    rts\n"
);

BPTR ug_lib_expunge(struct UserGroupBase *base);

static const char g_ug_name[] = USERGROUP_LIB_NAME;
static const char g_ug_id[]   = USERGROUP_ID_STR;

/* z.ai step 7d item 1: exec layout is { LibBaseSize, FunctionTable,
 * DataTable (may be NULL), InitRoutine } - step 7c had the last two
 * swapped, so exec never called the routine and executed the stub as
 * data. Exec AUTOINIT calls the routine with D0 = base, A0 = segList;
 * the stack-push stub feeds them to the C function in that order. */
/* z.ai step 7c item 1: exec calls an RTF_AUTOINIT initializer with
 * D0 = freshly made library base, A0 = segList. This tiny asm stub moves
 * the two registers onto the stack and calls the C initializer with the
 * natural (base, seglist) argument order — no register bindings, no
 * prologue assumptions. */

extern void ug_autoinit_stub(void); /* C name -> asm _ug_autoinit_stub */

static const APTR g_ug_init_table[] = {
    (APTR)sizeof(struct UserGroupBase),
    (APTR)g_ug_vectors,
    (APTR)NULL,
    (APTR)ug_autoinit_stub
};

const struct Resident g_ug_romtag = {
    RTC_MATCHWORD,
    (struct Resident *)&g_ug_romtag,
    (APTR)(&g_ug_romtag + 1),
    RTF_AUTOINIT, /* no RTF_AFTERDOS: the init does not need dos.library first */
    USERGROUP_VER_NUM,
    NT_LIBRARY,
    0,
    (char *)g_ug_name,
    (char *)g_ug_id,
    (APTR)g_ug_init_table
};

struct ExecBase   *SysBase;
struct DosLibrary *DOSBase;

/* C initializer called from the stub. Exec already allocated and cleared
 * the base and installed the vector table; InitResident adds the library
 * to LibList itself — do NOT AddLibrary here (a second Add corrupts the
 * list). */
struct UserGroupBase *ug_init_c(struct UserGroupBase *base, BPTR seglist)
{
    SysBase = *(struct ExecBase **)4UL;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36UL);

    /* z.ai step 7d item 1: guard by DOSBase — a NULL base deref in the
     * old branch FreeMem'd through NULL. */
    if (DOSBase == NULL) {
        FreeMem((UBYTE *)base - ((struct Library *)base)->lib_NegSize,
                (ULONG)(((struct Library *)base)->lib_NegSize +
                        ((struct Library *)base)->lib_PosSize));
        return NULL;
    }

    base->libNode.lib_Node.ln_Type = NT_LIBRARY;
    base->libNode.lib_Node.ln_Pri  = 0;
    base->libNode.lib_Node.ln_Name = (char *)g_ug_name;
    base->libNode.lib_Flags        = LIBF_SUMUSED | LIBF_CHANGED;
    base->libNode.lib_Version      = USERGROUP_VER_NUM;
    base->libNode.lib_Revision     = USERGROUP_REV_NUM;
    base->libNode.lib_IdString     = (char *)g_ug_id;
    base->segList                  = seglist;
    base->sysBase                  = *(APTR *)4UL;

    InitSemaphore(&base->lock);
    ug_db_init(base);

    return base;
}



struct Library *ug_lib_open(struct UserGroupBase *base, ULONG version)
{
    (void)version;
    if (!base) return NULL;

    base->libNode.lib_OpenCnt++;
    base->libNode.lib_Flags &= ~LIBF_DELEXP;
    return (struct Library *)base;
}

BPTR ug_lib_close(struct UserGroupBase *base)
{
    if (!base) return 0;

    base->libNode.lib_OpenCnt--;
    if (base->libNode.lib_OpenCnt == 0 && (base->libNode.lib_Flags & LIBF_DELEXP)) {
        return (BPTR)ug_lib_expunge(base);
    }
    return 0;
}

BPTR ug_lib_expunge(struct UserGroupBase *base)
{
    BPTR seg;
    if (!base) return 0;

    if (base->libNode.lib_OpenCnt > 0) {
        base->libNode.lib_Flags |= LIBF_DELEXP;
        return 0;
    }

    Forbid();
    Remove(&base->libNode.lib_Node);
    Permit();

    ug_db_free(base);
    seg = base->segList;

    if (DOSBase) {
        CloseLibrary((struct Library *)DOSBase);
        DOSBase = NULL;
    }

    FreeMem((UBYTE *)base - base->libNode.lib_NegSize,
            (ULONG)(base->libNode.lib_NegSize + base->libNode.lib_PosSize));

    return seg;
}

LONG ug_lib_reserved(struct UserGroupBase *base)
{
    (void)base;
    return 0;
}

#else

/* Host mock stubs for unit testing */
struct Library *ug_lib_open(struct UserGroupBase *base, ULONG version)
{
    (void)version;
    if (base) base->libNode.lib_OpenCnt++;
    return (struct Library *)base;
}

BPTR ug_lib_close(struct UserGroupBase *base)
{
    if (base && base->libNode.lib_OpenCnt > 0) base->libNode.lib_OpenCnt--;
    return 0;
}

BPTR ug_lib_expunge(struct UserGroupBase *base)
{
    if (base) ug_db_free(base);
    return 0;
}

LONG ug_lib_reserved(struct UserGroupBase *base)
{
    (void)base;
    return 0;
}

#endif /* __AMIGA__ */
