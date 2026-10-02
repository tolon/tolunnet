/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ug_crypt.c — Crypt, salt, getpass, and utmp/lastlog implementations.
 *
 * Target: AmigaOS 3.0+, universal 68k (-m68000).
 */

#include "usergroup_base.h"
#include <string.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/dos.h>
#endif

/*
 * 5.7: traditional DES crypt(3) - 2-char salt + 11 chars, compatible with
 * the hashes in AmiTCP:db/passwd. Classic bit-per-byte formulation (V7
 * style): slow but tiny, no 64-bit math, ~250 bytes of stack. The key
 * schedule and salted E table are static, so the caller holds base->lock.
 */
static const UBYTE des_ip[64] = {
    58,50,42,34,26,18,10, 2, 60,52,44,36,28,20,12, 4,
    62,54,46,38,30,22,14, 6, 64,56,48,40,32,24,16, 8,
    57,49,41,33,25,17, 9, 1, 59,51,43,35,27,19,11, 3,
    61,53,45,37,29,21,13, 5, 63,55,47,39,31,23,15, 7
};
static const UBYTE des_fp[64] = {
    40, 8,48,16,56,24,64,32, 39, 7,47,15,55,23,63,31,
    38, 6,46,14,54,22,62,30, 37, 5,45,13,53,21,61,29,
    36, 4,44,12,52,20,60,28, 35, 3,43,11,51,19,59,27,
    34, 2,42,10,50,18,58,26, 33, 1,41, 9,49,17,57,25
};
static const UBYTE des_pc1_c[28] = {
    57,49,41,33,25,17, 9, 1,58,50,42,34,26,18,
    10, 2,59,51,43,35,27,19,11, 3,60,52,44,36
};
static const UBYTE des_pc1_d[28] = {
    63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
    14, 6,61,53,45,37,29,21,13, 5,28,20,12, 4
};
static const UBYTE des_shifts[16] = { 1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1 };
static const UBYTE des_pc2_c[24] = {
    14,17,11,24, 1, 5, 3,28,15, 6,21,10,
    23,19,12, 4,26, 8,16, 7,27,20,13, 2
};
static const UBYTE des_pc2_d[24] = {
    41,52,31,37,47,55,30,40,51,45,33,48,
    44,49,39,56,34,53,46,42,50,36,29,32
};
static const UBYTE des_e2[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9,
     8, 9,10,11,12,13,12,13,14,15,16,17,
    16,17,18,19,20,21,20,21,22,23,24,25,
    24,25,26,27,28,29,28,29,30,31,32, 1
};
static const UBYTE des_s[8][64] = {
    {14, 4,13, 1, 2,15,11, 8, 3,10, 6,12, 5, 9, 0, 7,
      0,15, 7, 4,14, 2,13, 1,10, 6,12,11, 9, 5, 3, 8,
      4, 1,14, 8,13, 6, 2,11,15,12, 9, 7, 3,10, 5, 0,
     15,12, 8, 2, 4, 9, 1, 7, 5,11, 3,14,10, 0, 6,13},
    {15, 1, 8,14, 6,11, 3, 4, 9, 7, 2,13,12, 0, 5,10,
      3,13, 4, 7,15, 2, 8,14,12, 0, 1,10, 6, 9,11, 5,
      0,14, 7,11,10, 4,13, 1, 5, 8,12, 6, 9, 3, 2,15,
     13, 8,10, 1, 3,15, 4, 2,11, 6, 7,12, 0, 5,14, 9},
    {10, 0, 9,14, 6, 3,15, 5, 1,13,12, 7,11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6,10, 2, 8, 5,14,12,11,15, 1,
     13, 6, 4, 9, 8,15, 3, 0,11, 1, 2,12, 5,10,14, 7,
      1,10,13, 0, 6, 9, 8, 7, 4,15,14, 3,11, 5, 2,12},
    { 7,13,14, 3, 0, 6, 9,10, 1, 2, 8, 5,11,12, 4,15,
     13, 8,11, 5, 6,15, 0, 3, 4, 7, 2,12, 1,10,14, 9,
     10, 6, 9, 0,12,11, 7,13,15, 1, 3,14, 5, 2, 8, 4,
      3,15, 0, 6,10, 1,13, 8, 9, 4, 5,11,12, 7, 2,14},
    { 2,12, 4, 1, 7,10,11, 6, 8, 5, 3,15,13, 0,14, 9,
     14,11, 2,12, 4, 7,13, 1, 5, 0,15,10, 3, 9, 8, 6,
      4, 2, 1,11,10,13, 7, 8,15, 9,12, 5, 6, 3, 0,14,
     11, 8,12, 7, 1,14, 2,13, 6,15, 0, 9,10, 4, 5, 3},
    {12, 1,10,15, 9, 2, 6, 8, 0,13, 3, 4,14, 7, 5,11,
     10,15, 4, 2, 7,12, 9, 5, 6, 1,13,14, 0,11, 3, 8,
      9,14,15, 5, 2, 8,12, 3, 7, 0, 4,10, 1,13,11, 6,
      4, 3, 2,12, 9, 5,15,10,11,14, 1, 7, 6, 0, 8,13},
    { 4,11, 2,14,15, 0, 8,13, 3,12, 9, 7, 5,10, 6, 1,
     13, 0,11, 7, 4, 9, 1,10,14, 3, 5,12, 2,15, 8, 6,
      1, 4,11,13,12, 3, 7,14,10,15, 6, 8, 0, 5, 9, 2,
      6,11,13, 8, 1, 4,10, 7, 9, 5, 0,15,14, 2, 3,12},
    {13, 2, 8, 4, 6,15,11, 1,10, 9, 3,14, 5, 0,12, 7,
      1,15,13, 8,10, 3, 7, 4,12, 5, 6,11, 0,14, 9, 2,
      7,11, 4, 1, 9,12,14, 2, 0, 6,10,13,15, 3, 5, 8,
      2, 1,14, 7, 4,10, 8,13,15,12, 9, 0, 3, 5, 6,11}
};
static const UBYTE des_p[32] = {
    16, 7,20,21,29,12,28,17, 1,15,23,26, 5,18,31,10,
     2, 8,24,14,32,27, 3, 9,19,13,30, 6,22,11, 4,25
};

static UBYTE des_ks[16][48];   /* key schedule */
static UBYTE des_e[48];        /* salted expansion table */

static void des_setkey(const UBYTE *key)
{
    UBYTE c[28], d[28], t;
    int i, j, k;

    for (i = 0; i < 28; i++) {
        c[i] = key[des_pc1_c[i] - 1];
        d[i] = key[des_pc1_d[i] - 1];
    }
    for (i = 0; i < 16; i++) {
        for (k = 0; k < des_shifts[i]; k++) {
            t = c[0];
            for (j = 0; j < 27; j++) c[j] = c[j + 1];
            c[27] = t;
            t = d[0];
            for (j = 0; j < 27; j++) d[j] = d[j + 1];
            d[27] = t;
        }
        for (j = 0; j < 24; j++) {
            des_ks[i][j]      = c[des_pc2_c[j] - 1];
            des_ks[i][j + 24] = d[des_pc2_d[j] - 28 - 1];
        }
    }
    for (i = 0; i < 48; i++) des_e[i] = des_e2[i];
}

static void des_encrypt(UBYTE *block)
{
    UBYTE lr[64], tmp[32], pre_s[48], f[32];
    UBYTE *l = lr, *r = lr + 32;
    int i, j, k, t;

    for (j = 0; j < 64; j++) lr[j] = block[des_ip[j] - 1];
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 32; j++) tmp[j] = r[j];
        for (j = 0; j < 48; j++) pre_s[j] = (UBYTE)(r[des_e[j] - 1] ^ des_ks[i][j]);
        for (j = 0; j < 8; j++) {
            t = 6 * j;
            k = des_s[j][(pre_s[t + 0] << 5) + (pre_s[t + 1] << 3) +
                         (pre_s[t + 2] << 2) + (pre_s[t + 3] << 1) +
                         (pre_s[t + 4] << 0) + (pre_s[t + 5] << 4)];
            t = 4 * j;
            f[t + 0] = (UBYTE)((k >> 3) & 1);
            f[t + 1] = (UBYTE)((k >> 2) & 1);
            f[t + 2] = (UBYTE)((k >> 1) & 1);
            f[t + 3] = (UBYTE)(k & 1);
        }
        for (j = 0; j < 32; j++) r[j] = (UBYTE)(l[j] ^ f[des_p[j] - 1]);
        for (j = 0; j < 32; j++) l[j] = tmp[j];
    }
    for (j = 0; j < 32; j++) {
        UBYTE s = l[j];
        l[j] = r[j];
        r[j] = s;
    }
    for (j = 0; j < 64; j++) block[j] = lr[des_fp[j] - 1];
}

UBYTE *ug_lvo_crypt(UBYTE *key, UBYTE *set, struct UserGroupBase *base)
{
    static TEXT res[16];
    UBYTE block[66];
    int i, j, c;

    if (!key || !set || !base) return NULL;

    UG_LOCK(base);

    /* 8 key chars, 7 bits each (bit 7 ignored), parity slots left 0 */
    for (i = 0; i < 66; i++) block[i] = 0;
    for (i = 0; *key != 0 && i < 64; key++) {
        for (j = 0; j < 7; j++, i++) block[i] = (UBYTE)((*key >> (6 - j)) & 1);
        i++;
    }
    des_setkey(block);

    /* Salt: 12 bits swap E-table entries. A missing salt char repeats
     * the other one (V7 behaviour); an empty salt is "..". */
    res[0] = set[0] ? (TEXT)set[0] : '.';
    res[1] = (set[0] && set[1]) ? (TEXT)set[1] : res[0];
    for (i = 0; i < 2; i++) {
        c = (UBYTE)res[i];
        if (c > 'Z') c -= 6;
        if (c > '9') c -= 7;
        c -= '.';
        for (j = 0; j < 6; j++) {
            if ((c >> j) & 1) {
                UBYTE t = des_e[6 * i + j];
                des_e[6 * i + j] = des_e[6 * i + j + 24];
                des_e[6 * i + j + 24] = t;
            }
        }
    }

    for (i = 0; i < 66; i++) block[i] = 0;
    for (i = 0; i < 25; i++) des_encrypt(block);

    /* 64 bits -> 11 chars of 6 bits (the last char's low bits are the
     * two zero pad slots block[64..65]) */
    for (i = 0; i < 11; i++) {
        c = 0;
        for (j = 0; j < 6; j++) {
            c <<= 1;
            c |= block[6 * i + j];
        }
        c += '.';
        if (c > '9') c += 7;
        if (c > 'Z') c += 6;
        res[i + 2] = (TEXT)c;
    }
    res[13] = '\0';

    UG_UNLOCK(base);
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
    if (base) base->utmp_served = FALSE;
}

/* 5.6: one synthetic console entry per setutent/endutent cycle, then NULL,
 * so "while ((u = getutent()) != NULL)" (who, finger, w) terminates. */
struct utmp *ug_lvo_getutent(struct UserGroupBase *base)
{
    struct utmp *u = NULL;
    if (!base) return NULL;
    UG_LOCK(base);
    if (!base->utmp_served) {
        base->utmp_served = TRUE;
        base->cur_utmp.ut_time = 0;
        base->cur_utmp.ut_sid = 1;
        strncpy(base->cur_utmp.ut_name, "root", UT_NAMESIZE - 1);
        strncpy(base->cur_utmp.ut_host, "console", UT_HOSTSIZE - 1);
        u = &base->cur_utmp;
    }
    UG_UNLOCK(base);
    return u;
}

VOID ug_lvo_endutent(struct UserGroupBase *base)
{
    if (base) base->utmp_served = FALSE;
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
