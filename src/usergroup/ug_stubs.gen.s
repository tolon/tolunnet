|
| ug_stubs.gen.s — Generated 68k LVO Assembly Dispatch Stubs for usergroup.library
| Generated automatically by scripts/gen_usergroup_table.py from sfd/usergroup_lib.sfd.
|

    .text
    .even

| --- Library Management Vectors ---
    .globl _ug_stub_open
_ug_stub_open:
    move.l  d0,-(sp)
    move.l  a6,-(sp)
    jsr     _ug_lib_open
    addq.l  #8,sp
    move.l  d0,a0
    rts

    .globl _ug_stub_close
_ug_stub_close:
    move.l  a6,-(sp)
    jsr     _ug_lib_close
    addq.l  #4,sp
    move.l  d0,a0
    rts

    .globl _ug_stub_expunge
_ug_stub_expunge:
    move.l  a6,-(sp)
    jsr     _ug_lib_expunge
    addq.l  #4,sp
    move.l  d0,a0
    rts

    .globl _ug_stub_reserved
_ug_stub_reserved:
    moveq   #0,d0
    suba.l  a0,a0
    rts

| -30: ug_SetupContextTagList(STRPTR name,struct TagItem *tags)
    .globl _ug_stub_ug_setupcontexttaglist
_ug_stub_ug_setupcontexttaglist:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _ug_lvo_ug_setupcontexttaglist
    lea     12(sp),sp
    rts

| -36: ug_GetErr()
    .globl _ug_stub_ug_geterr
_ug_stub_ug_geterr:
    move.l  a6,-(sp)
    jsr     _ug_lvo_ug_geterr
    addq.l  #4,sp
    rts

| -42: ug_StrError(LONG err)
    .globl _ug_stub_ug_strerror
_ug_stub_ug_strerror:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    jsr     _ug_lvo_ug_strerror
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -48: getuid()
    .globl _ug_stub_getuid
_ug_stub_getuid:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getuid
    addq.l  #4,sp
    rts

| -54: geteuid()
    .globl _ug_stub_geteuid
_ug_stub_geteuid:
    move.l  a6,-(sp)
    jsr     _ug_lvo_geteuid
    addq.l  #4,sp
    rts

| -60: setreuid(LONG real,LONG effective)
    .globl _ug_stub_setreuid
_ug_stub_setreuid:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_setreuid
    lea     12(sp),sp
    rts

| -66: setuid(LONG uid)
    .globl _ug_stub_setuid
_ug_stub_setuid:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_setuid
    addq.l  #8,sp
    rts

| -72: getgid()
    .globl _ug_stub_getgid
_ug_stub_getgid:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getgid
    addq.l  #4,sp
    rts

| -78: getegid()
    .globl _ug_stub_getegid
_ug_stub_getegid:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getegid
    addq.l  #4,sp
    rts

| -84: setregid(LONG real,LONG effective)
    .globl _ug_stub_setregid
_ug_stub_setregid:
    move.l  a6,-(sp)
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_setregid
    lea     12(sp),sp
    rts

| -90: setgid(LONG gid)
    .globl _ug_stub_setgid
_ug_stub_setgid:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_setgid
    addq.l  #8,sp
    rts

| -96: getgroups(LONG gidsetlen,LONG *gidset)
    .globl _ug_stub_getgroups
_ug_stub_getgroups:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_getgroups
    lea     12(sp),sp
    rts

| -102: setgroups(LONG gidsetlen,LONG *gidset)
    .globl _ug_stub_setgroups
_ug_stub_setgroups:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_setgroups
    lea     12(sp),sp
    rts

| -108: initgroups(STRPTR name,LONG basegid)
    .globl _ug_stub_initgroups
_ug_stub_initgroups:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a1,-(sp)
    jsr     _ug_lvo_initgroups
    lea     12(sp),sp
    rts

| -114: getpwnam(STRPTR login)
    .globl _ug_stub_getpwnam
_ug_stub_getpwnam:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    jsr     _ug_lvo_getpwnam
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -120: getpwuid(LONG uid)
    .globl _ug_stub_getpwuid
_ug_stub_getpwuid:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_getpwuid
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -126: setpwent()
    .globl _ug_stub_setpwent
_ug_stub_setpwent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_setpwent
    addq.l  #4,sp
    rts

| -132: getpwent()
    .globl _ug_stub_getpwent
_ug_stub_getpwent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getpwent
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -138: endpwent()
    .globl _ug_stub_endpwent
_ug_stub_endpwent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_endpwent
    addq.l  #4,sp
    rts

| -144: getgrnam(STRPTR name)
    .globl _ug_stub_getgrnam
_ug_stub_getgrnam:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    jsr     _ug_lvo_getgrnam
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -150: getgrgid(LONG gid)
    .globl _ug_stub_getgrgid
_ug_stub_getgrgid:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_getgrgid
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -156: setgrent()
    .globl _ug_stub_setgrent
_ug_stub_setgrent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_setgrent
    addq.l  #4,sp
    rts

| -162: getgrent()
    .globl _ug_stub_getgrent
_ug_stub_getgrent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getgrent
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -168: endgrent()
    .globl _ug_stub_endgrent
_ug_stub_endgrent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_endgrent
    addq.l  #4,sp
    rts

| -174: crypt(UBYTE *key,UBYTE *set)
    .globl _ug_stub_crypt
_ug_stub_crypt:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _ug_lvo_crypt
    lea     12(sp),sp
    move.l  d0,a0
    rts

| -180: ug_GetSalt(struct passwd *user,UBYTE *buf,ULONG size)
    .globl _ug_stub_ug_getsalt
_ug_stub_ug_getsalt:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    jsr     _ug_lvo_ug_getsalt
    lea     16(sp),sp
    move.l  d0,a0
    rts

| -186: getpass(STRPTR prompt)
    .globl _ug_stub_getpass
_ug_stub_getpass:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    jsr     _ug_lvo_getpass
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -192: umask(UWORD mask)
    .globl _ug_stub_umask
_ug_stub_umask:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_umask
    addq.l  #8,sp
    rts

| -198: getumask()
    .globl _ug_stub_getumask
_ug_stub_getumask:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getumask
    addq.l  #4,sp
    rts

| -204: setsid()
    .globl _ug_stub_setsid
_ug_stub_setsid:
    move.l  a6,-(sp)
    jsr     _ug_lvo_setsid
    addq.l  #4,sp
    rts

| -210: getpgrp()
    .globl _ug_stub_getpgrp
_ug_stub_getpgrp:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getpgrp
    addq.l  #4,sp
    rts

| -216: getlogin()
    .globl _ug_stub_getlogin
_ug_stub_getlogin:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getlogin
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -222: setlogin(STRPTR name)
    .globl _ug_stub_setlogin
_ug_stub_setlogin:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    jsr     _ug_lvo_setlogin
    addq.l  #8,sp
    rts

| -228: setutent()
    .globl _ug_stub_setutent
_ug_stub_setutent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_setutent
    addq.l  #4,sp
    rts

| -234: getutent()
    .globl _ug_stub_getutent
_ug_stub_getutent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_getutent
    addq.l  #4,sp
    move.l  d0,a0
    rts

| -240: endutent()
    .globl _ug_stub_endutent
_ug_stub_endutent:
    move.l  a6,-(sp)
    jsr     _ug_lvo_endutent
    addq.l  #4,sp
    rts

| -246: getlastlog(LONG uid)
    .globl _ug_stub_getlastlog
_ug_stub_getlastlog:
    move.l  a6,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_getlastlog
    addq.l  #8,sp
    move.l  d0,a0
    rts

| -252: setlastlog(LONG uid,STRPTR name,STRPTR host)
    .globl _ug_stub_setlastlog
_ug_stub_setlastlog:
    move.l  a6,-(sp)
    move.l  a1,-(sp)
    move.l  a0,-(sp)
    move.l  d0,-(sp)
    jsr     _ug_lvo_setlastlog
    lea     16(sp),sp
    rts

| -258: getcredentials(struct Task *task)
    .globl _ug_stub_getcredentials
_ug_stub_getcredentials:
    move.l  a6,-(sp)
    move.l  a0,-(sp)
    jsr     _ug_lvo_getcredentials
    addq.l  #8,sp
    move.l  d0,a0
    rts
