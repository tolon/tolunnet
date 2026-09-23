/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * usergroup_base.h — Internal structures and definitions for usergroup.library.
 *
 * Target: AmigaOS 3.0+, universal 68k (-m68000).
 */

#ifndef USERGROUP_BASE_H
#define USERGROUP_BASE_H

#ifdef __AMIGA__
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <libraries/usergroup.h>
#include <pwd.h>
#include <grp.h>
#include <utmp.h>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int32_t   LONG;
typedef uint32_t  ULONG;
typedef int16_t   WORD;
typedef uint16_t  UWORD;
typedef int8_t    BYTE;
typedef uint8_t   UBYTE;
typedef char      TEXT;
typedef char     *STRPTR;
typedef const char *CONST_STRPTR;
typedef void     *APTR;
typedef int32_t   BOOL;
typedef int32_t   BPTR;
#define TRUE      1
#define FALSE     0
#define VOID      void

struct Task;

struct Node {
    struct Node *ln_Succ;
    struct Node *ln_Pred;
    UBYTE        ln_Type;
    BYTE         ln_Pri;
    char        *ln_Name;
};

struct MinNode {
    struct MinNode *mln_Succ;
    struct MinNode *mln_Pred;
};

struct MinList {
    struct MinNode *mlh_Head;
    struct MinNode *mlh_Tail;
    struct MinNode *mlh_TailPred;
};

struct Library {
    struct Node lib_Node;
    UBYTE       lib_Flags;
    UBYTE       lib_pad;
    UWORD       lib_NegSize;
    UWORD       lib_PosSize;
    UWORD       lib_Version;
    UWORD       lib_Revision;
    char       *lib_IdString;
    ULONG       lib_Sum;
    UWORD       lib_OpenCnt;
};

#define LIBF_DELEXP (1 << 3)

struct TagItem {
    ULONG     ti_Tag;
    uintptr_t ti_Data;
};

#define TAG_DONE 0UL
#define TAG_END  0UL

#define MAXLOGNAME 32
#define NGROUPS    32

#define UGT_ERRNOBPTR 0x80000001
#define UGT_ERRNOWPTR 0x80000002
#define UGT_ERRNOLPTR 0x80000004
#define UGT_INTRMASK  0x80000010
#define UGT_OWNER     0x80000011

struct UserGroupCredentials {
    LONG  cr_ruid;
    LONG  cr_rgid;
    UWORD cr_umask;
    LONG  cr_euid;
    WORD  cr_ngroups;
    LONG  cr_groups[NGROUPS];
    LONG  cr_session;
    TEXT  cr_login[MAXLOGNAME];
};

struct passwd {
    char *pw_name;
    char *pw_passwd;
    LONG  pw_uid;
    LONG  pw_gid;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct group {
    char  *gr_name;
    char  *gr_passwd;
    LONG   gr_gid;
    char **gr_mem;
};

#define UT_NAMESIZE 32
#define UT_LINESIZE 32
#define UT_HOSTSIZE 64

struct utmp {
    LONG ut_time;
    LONG ut_sid;
    TEXT ut_name[UT_NAMESIZE];
    TEXT ut_host[UT_HOSTSIZE];
};

struct lastlog {
    LONG ll_time;
    LONG ll_uid;
    TEXT ll_name[UT_NAMESIZE];
    TEXT ll_host[UT_HOSTSIZE];
};

#endif /* !__AMIGA__ */

#define USERGROUP_VER_NUM  4
#define USERGROUP_REV_NUM  1
#define USERGROUP_LIB_NAME "usergroup.library"
#define USERGROUP_ID_STR   "usergroup.library 4.1 (tolunnet)"

/* Per-task context record */
struct UgTaskContext {
    struct MinNode              node;
    APTR                        task;
    LONG                       *err_lptr;
    WORD                       *err_wptr;
    BYTE                       *err_bptr;
    ULONG                       break_mask;
    LONG                        last_err;
    struct UserGroupCredentials creds;
};

/* In-memory user record */
struct UgUser {
    struct MinNode node;
    struct passwd  pwd;
    TEXT           name[32];
    TEXT           passwd[64];
    TEXT           gecos[64];
    TEXT           dir[64];
    TEXT           shell[32];
};

/* In-memory group record */
struct UgGroup {
    struct MinNode node;
    struct group   grp;
    TEXT           name[32];
    TEXT           passwd[32];
    STRPTR         members[16];
    TEXT           mem_names[16][32];
};

struct UserGroupBase {
    struct Library      libNode;
    UWORD               pad;
    BPTR                segList;
    APTR                sysBase;
    APTR                dosBase;
#ifdef __AMIGA__
    struct SignalSemaphore lock;
#endif
    struct MinList      contexts;
    struct MinList      users;
    struct MinList      groups;
    struct MinNode     *cur_user_node;
    struct MinNode     *cur_grp_node;
    BOOL                initialized;
    struct utmp         cur_utmp;
    struct lastlog      cur_lastlog;
};

/* Internal function prototypes */
void ug_db_init(struct UserGroupBase *base);
void ug_db_free(struct UserGroupBase *base);
struct passwd *ug_db_getpwnam(struct UserGroupBase *base, CONST_STRPTR name);
struct passwd *ug_db_getpwuid(struct UserGroupBase *base, LONG uid);
void ug_db_setpwent(struct UserGroupBase *base);
struct passwd *ug_db_getpwent(struct UserGroupBase *base);
void ug_db_endpwent(struct UserGroupBase *base);

struct group *ug_db_getgrnam(struct UserGroupBase *base, CONST_STRPTR name);
struct group *ug_db_getgrgid(struct UserGroupBase *base, LONG gid);
void ug_db_setgrent(struct UserGroupBase *base);
struct group *ug_db_getgrent(struct UserGroupBase *base);
void ug_db_endgrent(struct UserGroupBase *base);

BOOL ug_db_add_user(struct UserGroupBase *base, CONST_STRPTR name, CONST_STRPTR passwd,
                    LONG uid, LONG gid, CONST_STRPTR gecos, CONST_STRPTR dir, CONST_STRPTR shell);
BOOL ug_db_add_group(struct UserGroupBase *base, CONST_STRPTR name, CONST_STRPTR passwd,
                     LONG gid, CONST_STRPTR members_csv);
void ug_db_load_files(struct UserGroupBase *base);

struct UgTaskContext *ug_get_task_context(struct UserGroupBase *base, APTR task);
void ug_set_task_error(struct UserGroupBase *base, LONG err);

#endif /* USERGROUP_BASE_H */
