/*
 * ug_context.c — Task context, credentials, and usergroup LVO implementations.
 *
 * Target: AmigaOS 3.0+, universal 68k (-m68000).
 */

#include "usergroup_base.h"
#include <string.h>
#include <stdlib.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#endif

/* Forward declaration from common error table */
extern const char * const g_sys_errlist[];

struct UgTaskContext *ug_get_task_context(struct UserGroupBase *base, APTR task)
{
    struct MinNode *node;
    struct UgTaskContext *ctx;
    if (!base) return NULL;

    if (!task) {
#ifdef __AMIGA__
        task = (APTR)FindTask(NULL);
#else
        task = (APTR)1;
#endif
    }

    for (node = base->contexts.mlh_Head; node->mln_Succ != NULL; node = node->mln_Succ) {
        ctx = (struct UgTaskContext *)node;
        if (ctx->task == task) {
            return ctx;
        }
    }

    /* Allocate new context for task */
#ifdef __AMIGA__
    ctx = (struct UgTaskContext *)AllocMem(sizeof(struct UgTaskContext), MEMF_PUBLIC | MEMF_CLEAR);
#else
    ctx = (struct UgTaskContext *)calloc(1, sizeof(struct UgTaskContext));
#endif
    if (!ctx) return NULL;

    ctx->task = task;
    ctx->creds.cr_ruid = 0;
    ctx->creds.cr_euid = 0;
    ctx->creds.cr_rgid = 0;
    ctx->creds.cr_umask = 022;
    ctx->creds.cr_ngroups = 1;
    ctx->creds.cr_groups[0] = 0;
    ctx->creds.cr_session = 1;
    strncpy(ctx->creds.cr_login, "root", sizeof(ctx->creds.cr_login) - 1);

    /* Add to contexts list */
    {
        struct MinNode *tailpred = base->contexts.mlh_TailPred;
        ctx->node.mln_Succ = (struct MinNode *)&base->contexts.mlh_Tail;
        ctx->node.mln_Pred = tailpred;
        tailpred->mln_Succ = &ctx->node;
        base->contexts.mlh_TailPred = &ctx->node;
    }

    return ctx;
}

void ug_set_task_error(struct UserGroupBase *base, LONG err)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    if (!ctx) return;

    ctx->last_err = err;
    if (ctx->err_lptr) *ctx->err_lptr = err;
    if (ctx->err_wptr) *ctx->err_wptr = (WORD)err;
    if (ctx->err_bptr) *ctx->err_bptr = (BYTE)err;
}

LONG ug_lvo_ug_setupcontexttaglist(STRPTR name, struct TagItem *tags, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx;
    struct TagItem *tstate = tags;
    struct TagItem *tag;
    APTR owner_task = NULL;

    (void)name;
    if (!base) return -1;

    /* First pass: find UGT_OWNER if provided */
    while (tstate && (tag = tstate++) && tag->ti_Tag != TAG_DONE) {
        if (tag->ti_Tag == UGT_OWNER) {
            owner_task = (APTR)(uintptr_t)tag->ti_Data;
            break;
        }
    }

    ctx = ug_get_task_context(base, owner_task);
    if (!ctx) return -1;

    tstate = tags;
    while (tstate && (tag = tstate++) && tag->ti_Tag != TAG_DONE) {
        switch (tag->ti_Tag) {
        case UGT_ERRNOBPTR:
            ctx->err_bptr = (BYTE *)(uintptr_t)tag->ti_Data;
            break;
        case UGT_ERRNOWPTR:
            ctx->err_wptr = (WORD *)(uintptr_t)tag->ti_Data;
            break;
        case UGT_ERRNOLPTR:
            ctx->err_lptr = (LONG *)(uintptr_t)tag->ti_Data;
            break;
        case UGT_INTRMASK:
            ctx->break_mask = (ULONG)tag->ti_Data;
            break;
        default:
            break;
        }
    }
    return 0;
}

LONG ug_lvo_ug_geterr(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? ctx->last_err : 0;
}

STRPTR ug_lvo_ug_strerror(LONG err, struct UserGroupBase *base)
{
    (void)base;
    if (err >= 0 && err <= 92) {
        /* Standard BSD errno range */
        static const char * const err_names[] = {
            "No error", "Operation not permitted", "No such file or directory",
            "No such process", "Interrupted system call", "Input/output error",
            "Device not configured", "Argument list too long", "Exec format error",
            "Bad file descriptor", "No child processes", "Resource deadlock avoided",
            "Cannot allocate memory", "Permission denied", "Bad address",
            "Block device required", "Device busy", "File exists",
            "Cross-device link", "Operation not supported by device", "Not a directory",
            "Is a directory", "Invalid argument", "Too many open files in system",
            "Too many open files", "Inappropriate ioctl for device"
        };
        if (err < (LONG)(sizeof(err_names) / sizeof(err_names[0]))) {
            return (STRPTR)err_names[err];
        }
    }
    return (STRPTR)"Unknown error";
}

LONG ug_lvo_getuid(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? ctx->creds.cr_ruid : 0;
}

LONG ug_lvo_geteuid(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? ctx->creds.cr_euid : 0;
}

LONG ug_lvo_setreuid(LONG real, LONG effective, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    if (!ctx) return -1;
    if (real != -1) ctx->creds.cr_ruid = real;
    if (effective != -1) ctx->creds.cr_euid = effective;
    return 0;
}

LONG ug_lvo_setuid(LONG uid, struct UserGroupBase *base)
{
    return ug_lvo_setreuid(uid, uid, base);
}

LONG ug_lvo_getgid(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? ctx->creds.cr_rgid : 0;
}

LONG ug_lvo_getegid(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? ctx->creds.cr_groups[0] : 0;
}

LONG ug_lvo_setregid(LONG real, LONG effective, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    if (!ctx) return -1;
    if (real != -1) ctx->creds.cr_rgid = real;
    if (effective != -1) {
        ctx->creds.cr_groups[0] = effective;
        if (ctx->creds.cr_ngroups < 1) ctx->creds.cr_ngroups = 1;
    }
    return 0;
}

LONG ug_lvo_setgid(LONG gid, struct UserGroupBase *base)
{
    return ug_lvo_setregid(gid, gid, base);
}

LONG ug_lvo_getgroups(LONG gidsetlen, LONG *gidset, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    LONG i, n;
    if (!ctx) return -1;
    if (gidsetlen <= 0 || !gidset) return ctx->creds.cr_ngroups;
    n = (ctx->creds.cr_ngroups < gidsetlen) ? ctx->creds.cr_ngroups : gidsetlen;
    for (i = 0; i < n; i++) {
        gidset[i] = ctx->creds.cr_groups[i];
    }
    return n;
}

LONG ug_lvo_setgroups(LONG gidsetlen, LONG *gidset, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    LONG i, n;
    if (!ctx || gidsetlen < 0) return -1;
    n = (gidsetlen < NGROUPS) ? gidsetlen : NGROUPS;
    for (i = 0; i < n; i++) {
        ctx->creds.cr_groups[i] = gidset ? gidset[i] : 0;
    }
    ctx->creds.cr_ngroups = (WORD)n;
    return 0;
}

LONG ug_lvo_initgroups(STRPTR name, LONG basegid, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    (void)name;
    if (!ctx) return -1;
    ctx->creds.cr_rgid = basegid;
    ctx->creds.cr_groups[0] = basegid;
    ctx->creds.cr_ngroups = 1;
    return 0;
}

ULONG ug_lvo_umask(ULONG mask, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    UWORD old;
    if (!ctx) return 022;
    old = ctx->creds.cr_umask;
    ctx->creds.cr_umask = (UWORD)(mask & 0777);
    return (ULONG)old;
}

ULONG ug_lvo_getumask(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? (ULONG)ctx->creds.cr_umask : 022;
}

LONG ug_lvo_setsid(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    if (!ctx) return 1;
    ctx->creds.cr_session = (LONG)((uintptr_t)ctx->task ^ 0xA5A50000UL);
    return ctx->creds.cr_session;
}

LONG ug_lvo_getpgrp(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? ctx->creds.cr_session : 1;
}

STRPTR ug_lvo_getlogin(struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    return ctx ? ctx->creds.cr_login : (STRPTR)"root";
}

LONG ug_lvo_setlogin(STRPTR name, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, NULL);
    if (!ctx || !name) return -1;
    strncpy(ctx->creds.cr_login, name, sizeof(ctx->creds.cr_login) - 1);
    ctx->creds.cr_login[sizeof(ctx->creds.cr_login) - 1] = '\0';
    return 0;
}

struct UserGroupCredentials *ug_lvo_getcredentials(struct Task *task, struct UserGroupBase *base)
{
    struct UgTaskContext *ctx = ug_get_task_context(base, (APTR)task);
    return ctx ? &ctx->creds : NULL;
}

/* Database passthroughs */
struct passwd *ug_lvo_getpwnam(STRPTR login, struct UserGroupBase *base)
{
    return ug_db_getpwnam(base, login);
}

struct passwd *ug_lvo_getpwuid(LONG uid, struct UserGroupBase *base)
{
    return ug_db_getpwuid(base, uid);
}

VOID ug_lvo_setpwent(struct UserGroupBase *base)
{
    ug_db_setpwent(base);
}

struct passwd *ug_lvo_getpwent(struct UserGroupBase *base)
{
    return ug_db_getpwent(base);
}

VOID ug_lvo_endpwent(struct UserGroupBase *base)
{
    ug_db_endpwent(base);
}

struct group *ug_lvo_getgrnam(STRPTR name, struct UserGroupBase *base)
{
    return ug_db_getgrnam(base, name);
}

struct group *ug_lvo_getgrgid(LONG gid, struct UserGroupBase *base)
{
    return ug_db_getgrgid(base, gid);
}

VOID ug_lvo_setgrent(struct UserGroupBase *base)
{
    ug_db_setgrent(base);
}

struct group *ug_lvo_getgrent(struct UserGroupBase *base)
{
    return ug_db_getgrent(base);
}

VOID ug_lvo_endgrent(struct UserGroupBase *base)
{
    ug_db_endgrent(base);
}
