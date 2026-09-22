/*
 * ug_crypt.c — Crypt, salt, getpass, and utmp/lastlog implementations.
 *
 * Target: AmigaOS 3.0+, universal 68k (-m68000).
 */

#include "usergroup_base.h"
#include <string.h>

#ifdef __AMIGA__
#include <proto/dos.h>
#endif

static const char b64t[64] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

UBYTE *ug_lvo_crypt(UBYTE *key, UBYTE *set, struct UserGroupBase *base)
{
    static TEXT res[16];
    ULONG h = 0x55555555UL;
    int i;
    (void)base;

    if (!key || !set) return NULL;

    /* First two chars are salt */
    res[0] = set[0] ? (TEXT)set[0] : '.';
    res[1] = set[1] ? (TEXT)set[1] : '/';

    /* Murmur/FNV mix for key and salt */
    for (i = 0; key[i] && i < 64; i++) {
        h ^= (ULONG)key[i];
        h *= 16777619UL;
    }
    h ^= (ULONG)res[0] * 31UL + (ULONG)res[1];

    for (i = 2; i < 13; i++) {
        res[i] = b64t[(h + i * 7) & 0x3F];
        h = (h >> 2) | (h << 30);
    }
    res[13] = '\0';
    return (UBYTE *)res;
}

UBYTE *ug_lvo_ug_getsalt(struct passwd *user, UBYTE *buf, ULONG size, struct UserGroupBase *base)
{
    (void)base;
    if (!user || !buf || size < 3) return NULL;
    if (user->pw_passwd && strlen(user->pw_passwd) >= 2) {
        buf[0] = (UBYTE)user->pw_passwd[0];
        buf[1] = (UBYTE)user->pw_passwd[1];
        buf[2] = '\0';
    } else {
        buf[0] = '.';
        buf[1] = '/';
        buf[2] = '\0';
    }
    return buf;
}

STRPTR ug_lvo_getpass(STRPTR prompt, struct UserGroupBase *base)
{
    static TEXT pass_buf[64];
    (void)base;
    (void)prompt;
    /* Headless / script default */
    pass_buf[0] = '\0';
    return pass_buf;
}

VOID ug_lvo_setutent(struct UserGroupBase *base)
{
    (void)base;
}

struct utmp *ug_lvo_getutent(struct UserGroupBase *base)
{
    if (!base) return NULL;
    base->cur_utmp.ut_time = 0;
    base->cur_utmp.ut_sid = 1;
    strncpy(base->cur_utmp.ut_name, "root", UT_NAMESIZE - 1);
    strncpy(base->cur_utmp.ut_host, "console", UT_HOSTSIZE - 1);
    return &base->cur_utmp;
}

VOID ug_lvo_endutent(struct UserGroupBase *base)
{
    (void)base;
}

struct lastlog *ug_lvo_getlastlog(LONG uid, struct UserGroupBase *base)
{
    if (!base) return NULL;
    base->cur_lastlog.ll_time = 0;
    base->cur_lastlog.ll_uid = uid;
    strncpy(base->cur_lastlog.ll_name, (uid == 0) ? "root" : "amiga", UT_NAMESIZE - 1);
    strncpy(base->cur_lastlog.ll_host, "localhost", UT_HOSTSIZE - 1);
    return &base->cur_lastlog;
}

LONG ug_lvo_setlastlog(LONG uid, STRPTR name, STRPTR host, struct UserGroupBase *base)
{
    if (!base) return -1;
    base->cur_lastlog.ll_time = 0;
    base->cur_lastlog.ll_uid = uid;
    if (name) strncpy(base->cur_lastlog.ll_name, name, UT_NAMESIZE - 1);
    if (host) strncpy(base->cur_lastlog.ll_host, host, UT_HOSTSIZE - 1);
    return 0;
}
