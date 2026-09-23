/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ug_db.c — In-memory user and group database with file reading fallback.
 *
 * Target: AmigaOS 3.0+, universal 68k (-m68000).
 */

#include "usergroup_base.h"
#include <string.h>
#include <stdlib.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/dos.h>
#else
#include <stdio.h>
#endif

static void minlist_init(struct MinList *list)
{
    list->mlh_Head = (struct MinNode *)&list->mlh_Tail;
    list->mlh_Tail = NULL;
    list->mlh_TailPred = (struct MinNode *)&list->mlh_Head;
}

static void minlist_add_tail(struct MinList *list, struct MinNode *node)
{
    struct MinNode *tailpred = list->mlh_TailPred;
    node->mln_Succ = (struct MinNode *)&list->mlh_Tail;
    node->mln_Pred = tailpred;
    tailpred->mln_Succ = node;
    list->mlh_TailPred = node;
}

BOOL ug_db_add_user(struct UserGroupBase *base, CONST_STRPTR name, CONST_STRPTR passwd,
                    LONG uid, LONG gid, CONST_STRPTR gecos, CONST_STRPTR dir, CONST_STRPTR shell)
{
    struct UgUser *u;
    if (!base || !name || !name[0]) return FALSE;

#ifdef __AMIGA__
    u = (struct UgUser *)AllocMem(sizeof(struct UgUser), MEMF_PUBLIC | MEMF_CLEAR);
#else
    u = (struct UgUser *)calloc(1, sizeof(struct UgUser));
#endif
    if (!u) return FALSE;

    strncpy(u->name, name, sizeof(u->name) - 1);
    strncpy(u->passwd, passwd ? passwd : "*", sizeof(u->passwd) - 1);
    strncpy(u->gecos, gecos ? gecos : "", sizeof(u->gecos) - 1);
    strncpy(u->dir, dir ? dir : "SYS:", sizeof(u->dir) - 1);
    strncpy(u->shell, shell ? shell : "", sizeof(u->shell) - 1);

    u->pwd.pw_name   = u->name;
    u->pwd.pw_passwd = u->passwd;
    u->pwd.pw_uid    = uid;
    u->pwd.pw_gid    = gid;
    u->pwd.pw_gecos  = u->gecos;
    u->pwd.pw_dir    = u->dir;
    u->pwd.pw_shell  = u->shell;

    minlist_add_tail(&base->users, &u->node);
    return TRUE;
}

BOOL ug_db_add_group(struct UserGroupBase *base, CONST_STRPTR name, CONST_STRPTR passwd,
                     LONG gid, CONST_STRPTR members_csv)
{
    struct UgGroup *g;
    int idx = 0;
    const char *p;
    if (!base || !name || !name[0]) return FALSE;

#ifdef __AMIGA__
    g = (struct UgGroup *)AllocMem(sizeof(struct UgGroup), MEMF_PUBLIC | MEMF_CLEAR);
#else
    g = (struct UgGroup *)calloc(1, sizeof(struct UgGroup));
#endif
    if (!g) return FALSE;

    strncpy(g->name, name, sizeof(g->name) - 1);
    strncpy(g->passwd, passwd ? passwd : "*", sizeof(g->passwd) - 1);
    g->grp.gr_name   = g->name;
    g->grp.gr_passwd = g->passwd;
    g->grp.gr_gid    = gid;

    p = members_csv;
    while (p && *p && idx < 15) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        if (len > 0) {
            if (len >= sizeof(g->mem_names[idx])) len = sizeof(g->mem_names[idx]) - 1;
            memcpy(g->mem_names[idx], p, len);
            g->mem_names[idx][len] = '\0';
            g->members[idx] = g->mem_names[idx];
            idx++;
        }
        if (!comma) break;
        p = comma + 1;
    }
    g->members[idx] = NULL;
    g->grp.gr_mem = g->members;

    minlist_add_tail(&base->groups, &g->node);
    return TRUE;
}

void ug_db_init(struct UserGroupBase *base)
{
    if (!base) return;
    minlist_init(&base->users);
    minlist_init(&base->groups);
    minlist_init(&base->contexts);
    base->cur_user_node = NULL;
    base->cur_grp_node = NULL;

    /* Built-in default user entries */
    ug_db_add_user(base, "root", "*", 0, 0, "System Administrator", "SYS:", "");
    ug_db_add_user(base, "amiga", "*", 1000, 1000, "Amiga User", "SYS:", "");
    ug_db_add_user(base, "nobody", "*", 65534, 65534, "Nobody", "SYS:", "");

    /* Built-in default group entries */
    ug_db_add_group(base, "wheel", "*", 0, "root,amiga");
    ug_db_add_group(base, "staff", "*", 1000, "amiga");
    ug_db_add_group(base, "nobody", "*", 65534, "");

    ug_db_load_files(base);
}

void ug_db_free(struct UserGroupBase *base)
{
    struct MinNode *cur, *nxt;
    if (!base) return;

    cur = base->users.mlh_Head;
    while ((nxt = cur->mln_Succ) != NULL) {
#ifdef __AMIGA__
        FreeMem(cur, sizeof(struct UgUser));
#else
        free(cur);
#endif
        cur = nxt;
    }
    minlist_init(&base->users);

    cur = base->groups.mlh_Head;
    while ((nxt = cur->mln_Succ) != NULL) {
#ifdef __AMIGA__
        FreeMem(cur, sizeof(struct UgGroup));
#else
        free(cur);
#endif
        cur = nxt;
    }
    minlist_init(&base->groups);

    cur = base->contexts.mlh_Head;
    while ((nxt = cur->mln_Succ) != NULL) {
#ifdef __AMIGA__
        FreeMem(cur, sizeof(struct UgTaskContext));
#else
        free(cur);
#endif
        cur = nxt;
    }
    minlist_init(&base->contexts);
}

struct passwd *ug_db_getpwnam(struct UserGroupBase *base, CONST_STRPTR name)
{
    struct MinNode *node;
    if (!base || !name) return NULL;

    for (node = base->users.mlh_Head; node->mln_Succ != NULL; node = node->mln_Succ) {
        struct UgUser *u = (struct UgUser *)node;
        if (strcmp(u->pwd.pw_name, name) == 0) {
            return &u->pwd;
        }
    }
    return NULL;
}

struct passwd *ug_db_getpwuid(struct UserGroupBase *base, LONG uid)
{
    struct MinNode *node;
    if (!base) return NULL;

    for (node = base->users.mlh_Head; node->mln_Succ != NULL; node = node->mln_Succ) {
        struct UgUser *u = (struct UgUser *)node;
        if (u->pwd.pw_uid == uid) {
            return &u->pwd;
        }
    }
    return NULL;
}

void ug_db_setpwent(struct UserGroupBase *base)
{
    if (base) {
        base->cur_user_node = base->users.mlh_Head;
    }
}

struct passwd *ug_db_getpwent(struct UserGroupBase *base)
{
    struct UgUser *u;
    if (!base || !base->cur_user_node || base->cur_user_node->mln_Succ == NULL) {
        return NULL;
    }
    u = (struct UgUser *)base->cur_user_node;
    base->cur_user_node = base->cur_user_node->mln_Succ;
    return &u->pwd;
}

void ug_db_endpwent(struct UserGroupBase *base)
{
    if (base) {
        base->cur_user_node = NULL;
    }
}

struct group *ug_db_getgrnam(struct UserGroupBase *base, CONST_STRPTR name)
{
    struct MinNode *node;
    if (!base || !name) return NULL;

    for (node = base->groups.mlh_Head; node->mln_Succ != NULL; node = node->mln_Succ) {
        struct UgGroup *g = (struct UgGroup *)node;
        if (strcmp(g->grp.gr_name, name) == 0) {
            return &g->grp;
        }
    }
    return NULL;
}

struct group *ug_db_getgrgid(struct UserGroupBase *base, LONG gid)
{
    struct MinNode *node;
    if (!base) return NULL;

    for (node = base->groups.mlh_Head; node->mln_Succ != NULL; node = node->mln_Succ) {
        struct UgGroup *g = (struct UgGroup *)node;
        if (g->grp.gr_gid == gid) {
            return &g->grp;
        }
    }
    return NULL;
}

void ug_db_setgrent(struct UserGroupBase *base)
{
    if (base) {
        base->cur_grp_node = base->groups.mlh_Head;
    }
}

struct group *ug_db_getgrent(struct UserGroupBase *base)
{
    struct UgGroup *g;
    if (!base || !base->cur_grp_node || base->cur_grp_node->mln_Succ == NULL) {
        return NULL;
    }
    g = (struct UgGroup *)base->cur_grp_node;
    base->cur_grp_node = base->cur_grp_node->mln_Succ;
    return &g->grp;
}

void ug_db_endgrent(struct UserGroupBase *base)
{
    if (base) {
        base->cur_grp_node = NULL;
    }
}

#ifdef __AMIGA__
static BPTR safe_open(CONST_STRPTR path)
{
    struct Process *pr = (struct Process *)FindTask(NULL);
    APTR old = NULL;
    BPTR fh;

    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        old = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;
    }
    fh = Open(path, MODE_OLDFILE);
    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        pr->pr_WindowPtr = old;
    }
    return fh;
}

static void parse_passwd_file(struct UserGroupBase *base, CONST_STRPTR path)
{
    BPTR fh = safe_open(path);
    char buf[256];
    if (!fh) return;

    while (FGets(fh, (STRPTR)buf, sizeof(buf))) {
        char *p = buf;
        char *f[7];
        int i = 0;
        /* Strip newline */
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
            buf[--len] = '\0';
        }
        if (buf[0] == '#' || buf[0] == '\0') continue;

        f[0] = p;
        for (i = 1; i < 7; i++) {
            char *colon = strchr(p, ':');
            if (!colon) break;
            *colon = '\0';
            p = colon + 1;
            f[i] = p;
        }
        if (i >= 4) {
            LONG uid = (LONG)atoi(f[2]);
            LONG gid = (LONG)atoi(f[3]);
            CONST_STRPTR gecos = (i >= 5) ? f[4] : "";
            CONST_STRPTR dir   = (i >= 6) ? f[5] : "SYS:";
            CONST_STRPTR shell = (i >= 7) ? f[6] : "";
            /* Avoid duplicate builtin overwrites */
            if (!ug_db_getpwnam(base, f[0])) {
                ug_db_add_user(base, f[0], f[1], uid, gid, gecos, dir, shell);
            }
        }
    }
    Close(fh);
}
#endif

void ug_db_load_files(struct UserGroupBase *base)
{
#ifdef __AMIGA__
    static const char * const pwd_paths[] = {
        "AmiTCP:db/passwd",
        "DEVS:Internet/passwd",
        "DEVS:tolunnet/passwd",
        NULL
    };
    int i;
    for (i = 0; pwd_paths[i]; i++) {
        parse_passwd_file(base, pwd_paths[i]);
    }
#else
    (void)base;
#endif
}
