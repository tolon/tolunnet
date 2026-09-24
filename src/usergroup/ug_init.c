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
struct UserGroupBase *ug_init_from_table(struct UserGroupBase *base, BPTR seglist, struct ExecBase *sysBase);

/* CLI safety entry point and RomTag */
__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _ug_lib_entry\n"
    "_ug_lib_entry:\n"
    "    moveq   #-1, %d0\n"
    "    rts\n"
);

BPTR ug_lib_expunge(struct UserGroupBase *base);
extern struct UserGroupBase *ug_init_lib(BPTR seglist, struct ExecBase *sysBase);

static const char g_ug_name[] = USERGROUP_LIB_NAME;
static const char g_ug_id[]   = USERGROUP_ID_STR;

/* z.ai step 7 item 7: RTF_AUTOINIT with a proper init table. The old
 * non-AUTOINIT RomTag made exec call ug_init_lib as a raw C function; the
 * compiler-generated prologue read remaining arguments from the stack, so
 * after an expunge+reload the caller's stack frame produced garbage (crash
 * on reopen). With RTF_AUTOINIT, exec allocates the library itself and
 * calls the initializer as (base, seglist, sysBase) — plain C. */
static const APTR g_ug_init_table[] = {
    (APTR)sizeof(struct UserGroupBase),
    (APTR)g_ug_vectors,
    (APTR)ug_init_from_table,
    (APTR)NULL
};

const struct Resident g_ug_romtag = {
    RTC_MATCHWORD,
    (struct Resident *)&g_ug_romtag,
    (APTR)(&g_ug_romtag + 1),
    RTF_AUTOINIT | RTF_AFTERDOS,
    USERGROUP_VER_NUM,
    NT_LIBRARY,
    0,
    (char *)g_ug_name,
    (char *)g_ug_id,
    (APTR)g_ug_init_table
};

struct ExecBase   *SysBase;
struct DosLibrary *DOSBase;

/* RTF_AUTOINIT initializer: exec has already allocated and cleared the
 * library base of the requested size and set up the vector table. */
/* Legacy name kept for the vector table / gen files. */
struct UserGroupBase *ug_init_lib(BPTR seglist, struct ExecBase *sysBase)
{
    return ug_init_from_table(NULL, seglist, sysBase);
}

struct UserGroupBase *ug_init_from_table(struct UserGroupBase *base,
                                         BPTR seglist,
                                         struct ExecBase *sysBase)
{
    (void)sysBase;

    SysBase = *(struct ExecBase **)4UL;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36UL);

    if (!base) {
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
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

    Forbid();
    AddLibrary(&base->libNode);
    Permit();

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

struct Library *ug_daemon_create(void)
{
    struct UserGroupBase *base;

    /* Check if already in LibList */
    Forbid();
    base = (struct UserGroupBase *)FindName(&((struct ExecBase *)*(APTR *)4UL)->LibList,
                                            (STRPTR)g_ug_name);
    Permit();
    if (base) return (struct Library *)base;

    return (struct Library *)ug_init_lib(0, (struct ExecBase *)*(APTR *)4UL);
}

void ug_daemon_destroy(struct Library *lib)
{
    if (!lib) return;
    ug_lib_expunge((struct UserGroupBase *)lib);
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
