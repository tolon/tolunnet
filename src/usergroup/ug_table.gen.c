/*
 * ug_table.gen.c — Generated usergroup.library jump table.
 * Generated automatically by scripts/gen_usergroup_table.py from sfd/usergroup_lib.sfd.
 * DO NOT EDIT MANUALLY.
 */

#include <exec/types.h>

/* Standard Library Management Stubs */
extern void ug_stub_open(void);
extern void ug_stub_close(void);
extern void ug_stub_expunge(void);
extern void ug_stub_reserved(void);

extern void ug_stub_ug_setupcontexttaglist(void);
extern void ug_stub_ug_geterr(void);
extern void ug_stub_ug_strerror(void);
extern void ug_stub_getuid(void);
extern void ug_stub_geteuid(void);
extern void ug_stub_setreuid(void);
extern void ug_stub_setuid(void);
extern void ug_stub_getgid(void);
extern void ug_stub_getegid(void);
extern void ug_stub_setregid(void);
extern void ug_stub_setgid(void);
extern void ug_stub_getgroups(void);
extern void ug_stub_setgroups(void);
extern void ug_stub_initgroups(void);
extern void ug_stub_getpwnam(void);
extern void ug_stub_getpwuid(void);
extern void ug_stub_setpwent(void);
extern void ug_stub_getpwent(void);
extern void ug_stub_endpwent(void);
extern void ug_stub_getgrnam(void);
extern void ug_stub_getgrgid(void);
extern void ug_stub_setgrent(void);
extern void ug_stub_getgrent(void);
extern void ug_stub_endgrent(void);
extern void ug_stub_crypt(void);
extern void ug_stub_ug_getsalt(void);
extern void ug_stub_getpass(void);
extern void ug_stub_umask(void);
extern void ug_stub_getumask(void);
extern void ug_stub_setsid(void);
extern void ug_stub_getpgrp(void);
extern void ug_stub_getlogin(void);
extern void ug_stub_setlogin(void);
extern void ug_stub_setutent(void);
extern void ug_stub_getutent(void);
extern void ug_stub_endutent(void);
extern void ug_stub_getlastlog(void);
extern void ug_stub_setlastlog(void);
extern void ug_stub_getcredentials(void);

const APTR g_ug_vectors[] = {
    (APTR)ug_stub_open,                  /* -6   LIB_OPEN */
    (APTR)ug_stub_close,                 /* -12  LIB_CLOSE */
    (APTR)ug_stub_expunge,               /* -18  LIB_EXPUNGE */
    (APTR)ug_stub_reserved,              /* -24  LIB_RESERVED */

    (APTR)ug_stub_ug_setupcontexttaglist  , /* -30 ug_SetupContextTagList */
    (APTR)ug_stub_ug_geterr               , /* -36 ug_GetErr */
    (APTR)ug_stub_ug_strerror             , /* -42 ug_StrError */
    (APTR)ug_stub_getuid                  , /* -48 getuid */
    (APTR)ug_stub_geteuid                 , /* -54 geteuid */
    (APTR)ug_stub_setreuid                , /* -60 setreuid */
    (APTR)ug_stub_setuid                  , /* -66 setuid */
    (APTR)ug_stub_getgid                  , /* -72 getgid */
    (APTR)ug_stub_getegid                 , /* -78 getegid */
    (APTR)ug_stub_setregid                , /* -84 setregid */
    (APTR)ug_stub_setgid                  , /* -90 setgid */
    (APTR)ug_stub_getgroups               , /* -96 getgroups */
    (APTR)ug_stub_setgroups               , /* -102 setgroups */
    (APTR)ug_stub_initgroups              , /* -108 initgroups */
    (APTR)ug_stub_getpwnam                , /* -114 getpwnam */
    (APTR)ug_stub_getpwuid                , /* -120 getpwuid */
    (APTR)ug_stub_setpwent                , /* -126 setpwent */
    (APTR)ug_stub_getpwent                , /* -132 getpwent */
    (APTR)ug_stub_endpwent                , /* -138 endpwent */
    (APTR)ug_stub_getgrnam                , /* -144 getgrnam */
    (APTR)ug_stub_getgrgid                , /* -150 getgrgid */
    (APTR)ug_stub_setgrent                , /* -156 setgrent */
    (APTR)ug_stub_getgrent                , /* -162 getgrent */
    (APTR)ug_stub_endgrent                , /* -168 endgrent */
    (APTR)ug_stub_crypt                   , /* -174 crypt */
    (APTR)ug_stub_ug_getsalt              , /* -180 ug_GetSalt */
    (APTR)ug_stub_getpass                 , /* -186 getpass */
    (APTR)ug_stub_umask                   , /* -192 umask */
    (APTR)ug_stub_getumask                , /* -198 getumask */
    (APTR)ug_stub_setsid                  , /* -204 setsid */
    (APTR)ug_stub_getpgrp                 , /* -210 getpgrp */
    (APTR)ug_stub_getlogin                , /* -216 getlogin */
    (APTR)ug_stub_setlogin                , /* -222 setlogin */
    (APTR)ug_stub_setutent                , /* -228 setutent */
    (APTR)ug_stub_getutent                , /* -234 getutent */
    (APTR)ug_stub_endutent                , /* -240 endutent */
    (APTR)ug_stub_getlastlog              , /* -246 getlastlog */
    (APTR)ug_stub_setlastlog              , /* -252 setlastlog */
    (APTR)ug_stub_getcredentials          , /* -258 getcredentials */
    (APTR)-1                             /* End of table marker */
};
