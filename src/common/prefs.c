/*
 * tolunnet — Persistent Preferences & Configuration Engine
 *
 * Implements DEVS:tolunnet.config (plain text KEY=VALUE per TNET-032 / Master prompt §5.2)
 * with ENVARC: / ENV: compatibility fallback.
 */

#include "prefs.h"
#include <stddef.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dos.h>

static int str_equal_nocase(const char *s1, const char *s2)
{
    if (!s1 || !s2) return 0;
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return 0;
        s1++; s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

static void str_copy_clean(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (src && *src && *src != '\r' && *src != '\n' && *src != ' ' && *src != '\t' && i < max_len - 1) {
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

void tn_prefs_default(TnPrefs *prefs)
{
    if (prefs == NULL) return;

    /* Safe stock defaults */
    str_copy_clean(prefs->device, "ethernet.device", sizeof(prefs->device));
    prefs->unit = 0;
    prefs->use_dhcp = TRUE;
    str_copy_clean(prefs->ip_addr, "10.0.2.15", sizeof(prefs->ip_addr));
    str_copy_clean(prefs->netmask, "255.255.255.0", sizeof(prefs->netmask));
    str_copy_clean(prefs->gateway, "10.0.2.2", sizeof(prefs->gateway));
    str_copy_clean(prefs->dns_server, "10.0.2.3", sizeof(prefs->dns_server));
    /* TNET-063 */
    str_copy_clean(prefs->hostname, "amiga", sizeof(prefs->hostname));
    prefs->dns2[0] = '\0';
    prefs->mtu  = 0;   /* 0 = use driver-reported MTU */
    prefs->debug = 0;  /* TN_LOG_BASIC */
}

/* TNET-063: parse the full §5.2 grammar from a KEY=VALUE text line */
static void tn_prefs_parse_line(TnPrefs *prefs, char *key, char *val)
{
    if (str_equal_nocase(key, "DEVICE")) {
        str_copy_clean(prefs->device, val, sizeof(prefs->device));
    } else if (str_equal_nocase(key, "UNIT")) {
        LONG u = 0;
        if (StrToLong((CONST_STRPTR)val, &u) > 0) prefs->unit = (ULONG)u;
    } else if (str_equal_nocase(key, "DHCP") || str_equal_nocase(key, "USE_DHCP")) {
        if (str_equal_nocase(val, "YES") || str_equal_nocase(val, "1") || str_equal_nocase(val, "TRUE")) {
            prefs->use_dhcp = TRUE;
        } else {
            prefs->use_dhcp = FALSE;
        }
    } else if (str_equal_nocase(key, "IP") || str_equal_nocase(key, "IP_ADDR")) {
        str_copy_clean(prefs->ip_addr, val, sizeof(prefs->ip_addr));
    } else if (str_equal_nocase(key, "NETMASK") || str_equal_nocase(key, "MASK")) {
        str_copy_clean(prefs->netmask, val, sizeof(prefs->netmask));
    } else if (str_equal_nocase(key, "GATEWAY") || str_equal_nocase(key, "GW")) {
        str_copy_clean(prefs->gateway, val, sizeof(prefs->gateway));
    } else if (str_equal_nocase(key, "DNS2")) {
        /* TNET-063: secondary nameserver is its own key, no longer aliases DNS1 */
        str_copy_clean(prefs->dns2, val, sizeof(prefs->dns2));
    } else if (str_equal_nocase(key, "DNS") || str_equal_nocase(key, "DNS1") ||
               str_equal_nocase(key, "NAMESERVER")) {
        str_copy_clean(prefs->dns_server, val, sizeof(prefs->dns_server));
    } else if (str_equal_nocase(key, "HOSTNAME")) {
        str_copy_clean(prefs->hostname, val, sizeof(prefs->hostname));
    } else if (str_equal_nocase(key, "MTU")) {
        LONG m = 0;
        if (StrToLong((CONST_STRPTR)val, &m) > 0) prefs->mtu = (ULONG)m;
    } else if (str_equal_nocase(key, "DEBUG")) {
        LONG d = 0;
        if (StrToLong((CONST_STRPTR)val, &d) > 0 && d >= 0 && d <= 2) prefs->debug = (ULONG)d;
    }
}

/* Read the binary blob (TnPrefs struct image) from fh into *out. Tolerates the
 * older, smaller pre-TNET-063 struct: fields beyond the read prefix keep the
 * defaults already present in *out. Returns TRUE on a recognised blob. */
static BOOL tn_prefs_read_blob(TnPrefs *out, BPTR fh)
{
    TnPrefs tmp;
    LONG read_bytes;

    tn_prefs_default(&tmp);
    read_bytes = Read(fh, &tmp, sizeof(TnPrefs));
    if (read_bytes == (LONG)sizeof(TnPrefs) ||
        read_bytes == (LONG)offsetof(TnPrefs, hostname)) {
        *out = tmp;
        return TRUE;
    }
    return FALSE;
}

BOOL tn_prefs_load(TnPrefs *prefs)
{
    BPTR fh;
    char line[128];

    if (prefs == NULL) return FALSE;
    tn_prefs_default(prefs);

    /* 1. ENV: blob — live session state ("Use", or ENVARC: copy at boot) (TNET-064) */
    fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENV, MODE_OLDFILE);
    if (fh != (BPTR)0) {
        BOOL ok = tn_prefs_read_blob(prefs, fh);
        Close(fh);
        if (ok) return TRUE;
    }

    /* 2. DEVS:tolunnet.config text (canonical grammar, master prompt §5.2) */
    fh = Open((CONST_STRPTR)TN_CONFIG_FILE_DEVS, MODE_OLDFILE);
    if (fh == (BPTR)0) {
        /* 3. Pre-rename legacy path — read-compat only, never written */
        fh = Open((CONST_STRPTR)"DEVS:tolunet.config", MODE_OLDFILE);
    }

    if (fh != (BPTR)0) {
        while (FGets(fh, (STRPTR)line, sizeof(line)) != NULL) {
            char *eq = line;
            while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                *eq = '\0';
                char *val = eq + 1;
                while (*val == ' ' || *val == '\t') val++;
                tn_prefs_parse_line(prefs, line, val);
            }
        }
        Close(fh);
        return TRUE;
    }

    /* 4. ENVARC: blob — last "Save" */
    fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENVARC, MODE_OLDFILE);
    if (fh != (BPTR)0) {
        BOOL ok = tn_prefs_read_blob(prefs, fh);
        Close(fh);
        if (ok) return TRUE;
    }

    return FALSE;
}

/* TNET-063: write a ULONG as decimal text (no libc) */
static void tn_ultostr(ULONG v, char *buf)
{
    char tmp[16];
    int i = 0, j = 0;

    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (v > 0 && i < 15) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

BOOL tn_prefs_save(const TnPrefs *prefs, TnPrefsSaveMode mode)
{
    BPTR fh;
    char num_str[16];

    if (prefs == NULL) return FALSE;

    if (mode == TN_PREFS_SAVE) {
        /* 1. Persistent text config: DEVS:tolunnet.config (TNET-032 / TNET-044 / TNET-063) */
        fh = Open((CONST_STRPTR)TN_CONFIG_FILE_DEVS, MODE_NEWFILE);
        if (fh != (BPTR)0) {
            FPuts(fh, (CONST_STRPTR)"# tolunnet configuration file\n");
            FPuts(fh, (CONST_STRPTR)"DEVICE="); FPuts(fh, (CONST_STRPTR)prefs->device); FPuts(fh, (CONST_STRPTR)"\n");

            tn_ultostr(prefs->unit, num_str);
            FPuts(fh, (CONST_STRPTR)"UNIT="); FPuts(fh, (CONST_STRPTR)num_str); FPuts(fh, (CONST_STRPTR)"\n");
            FPuts(fh, (CONST_STRPTR)"DHCP="); FPuts(fh, prefs->use_dhcp ? (CONST_STRPTR)"YES\n" : (CONST_STRPTR)"NO\n");
            FPuts(fh, (CONST_STRPTR)"IP="); FPuts(fh, (CONST_STRPTR)prefs->ip_addr); FPuts(fh, (CONST_STRPTR)"\n");
            FPuts(fh, (CONST_STRPTR)"NETMASK="); FPuts(fh, (CONST_STRPTR)prefs->netmask); FPuts(fh, (CONST_STRPTR)"\n");
            FPuts(fh, (CONST_STRPTR)"GATEWAY="); FPuts(fh, (CONST_STRPTR)prefs->gateway); FPuts(fh, (CONST_STRPTR)"\n");
            FPuts(fh, (CONST_STRPTR)"DNS="); FPuts(fh, (CONST_STRPTR)prefs->dns_server); FPuts(fh, (CONST_STRPTR)"\n");
            FPuts(fh, (CONST_STRPTR)"DNS2="); FPuts(fh, (CONST_STRPTR)prefs->dns2); FPuts(fh, (CONST_STRPTR)"\n");
            FPuts(fh, (CONST_STRPTR)"HOSTNAME="); FPuts(fh, (CONST_STRPTR)prefs->hostname); FPuts(fh, (CONST_STRPTR)"\n");
            tn_ultostr(prefs->mtu, num_str);
            FPuts(fh, (CONST_STRPTR)"MTU="); FPuts(fh, (CONST_STRPTR)num_str); FPuts(fh, (CONST_STRPTR)"\n");
            tn_ultostr(prefs->debug, num_str);
            FPuts(fh, (CONST_STRPTR)"DEBUG="); FPuts(fh, (CONST_STRPTR)num_str); FPuts(fh, (CONST_STRPTR)"\n");
            Close(fh);
        }
    }

    /* 2. ENV: blob — always written (Save keeps it in sync with ENVARC:/DEVS:,
     *    Use makes the values live for this session only) */
    fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENV, MODE_NEWFILE);
    if (fh != (BPTR)0) {
        Write(fh, (CONST APTR)prefs, sizeof(TnPrefs));
        Close(fh);
    }

    if (mode == TN_PREFS_SAVE) {
        /* 3. ENVARC: blob — persistent across reboots */
        fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENVARC, MODE_NEWFILE);
        if (fh != (BPTR)0) {
            Write(fh, (CONST APTR)prefs, sizeof(TnPrefs));
            Close(fh);
        }
    }

    return TRUE;
}
