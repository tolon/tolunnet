/*
 * tolunnet — Persistent Preferences & Configuration Engine
 *
 * Implements DEVS:tolunnet.config (plain text KEY=VALUE per TNET-032 / Master prompt §5.2)
 * with ENVARC: / ENV: compatibility fallback.
 */

#include "prefs.h"
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
}

BOOL tn_prefs_load(TnPrefs *prefs)
{
    BPTR fh;
    char line[128];

    if (prefs == NULL) return FALSE;
    tn_prefs_default(prefs);

    /* 1. Try text config file DEVS:tolunnet.config or DEVS:tolunet.config (TNET-032 / TNET-044) */
    fh = Open((CONST_STRPTR)TN_CONFIG_FILE_DEVS, MODE_OLDFILE);
    if (fh == (BPTR)0) {
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

                if (str_equal_nocase(line, "DEVICE")) {
                    str_copy_clean(prefs->device, val, sizeof(prefs->device));
                } else if (str_equal_nocase(line, "UNIT")) {
                    LONG u = 0;
                    if (StrToLong((CONST_STRPTR)val, &u) > 0) prefs->unit = (ULONG)u;
                } else if (str_equal_nocase(line, "DHCP") || str_equal_nocase(line, "USE_DHCP")) {
                    if (str_equal_nocase(val, "YES") || str_equal_nocase(val, "1") || str_equal_nocase(val, "TRUE")) {
                        prefs->use_dhcp = TRUE;
                    } else {
                        prefs->use_dhcp = FALSE;
                    }
                } else if (str_equal_nocase(line, "IP") || str_equal_nocase(line, "IP_ADDR")) {
                    str_copy_clean(prefs->ip_addr, val, sizeof(prefs->ip_addr));
                } else if (str_equal_nocase(line, "NETMASK") || str_equal_nocase(line, "MASK")) {
                    str_copy_clean(prefs->netmask, val, sizeof(prefs->netmask));
                } else if (str_equal_nocase(line, "GATEWAY") || str_equal_nocase(line, "GW")) {
                    str_copy_clean(prefs->gateway, val, sizeof(prefs->gateway));
                } else if (str_equal_nocase(line, "DNS") || str_equal_nocase(line, "DNS1") ||
                           str_equal_nocase(line, "DNS2") || str_equal_nocase(line, "NAMESERVER")) {
                    str_copy_clean(prefs->dns_server, val, sizeof(prefs->dns_server));
                }
            }
        }
        Close(fh);
        return TRUE;
    }

    /* 2. Fallback to ENVARC: / ENV: binary blob */
    fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENVARC, MODE_OLDFILE);
    if (fh == (BPTR)0) {
        fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENV, MODE_OLDFILE);
    }

    if (fh != (BPTR)0) {
        LONG read_bytes = Read(fh, prefs, sizeof(TnPrefs));
        Close(fh);
        if (read_bytes == (LONG)sizeof(TnPrefs)) {
            prefs->device[sizeof(prefs->device) - 1] = '\0';
            prefs->ip_addr[sizeof(prefs->ip_addr) - 1] = '\0';
            prefs->netmask[sizeof(prefs->netmask) - 1] = '\0';
            prefs->gateway[sizeof(prefs->gateway) - 1] = '\0';
            prefs->dns_server[sizeof(prefs->dns_server) - 1] = '\0';
            return TRUE;
        }
    }

    return FALSE;
}

BOOL tn_prefs_save(const TnPrefs *prefs)
{
    BPTR fh;

    if (prefs == NULL) return FALSE;

    /* 1. Save text config to DEVS:tolunnet.config (TNET-032 / TNET-044) */
    fh = Open((CONST_STRPTR)TN_CONFIG_FILE_DEVS, MODE_NEWFILE);
    if (fh != (BPTR)0) {
        FPuts(fh, (CONST_STRPTR)"# tolunnet configuration file\n");
        FPuts(fh, (CONST_STRPTR)"DEVICE="); FPuts(fh, (CONST_STRPTR)prefs->device); FPuts(fh, (CONST_STRPTR)"\n");
        
        char unit_str[16];
        char tmp[16];
        ULONG u = prefs->unit;
        int idx = 0;
        if (u == 0) {
            unit_str[0] = '0';
            unit_str[1] = '\0';
        } else {
            while (u > 0 && idx < 15) {
                tmp[idx++] = (char)('0' + (u % 10));
                u /= 10;
            }
            for (int i = 0; i < idx; i++) {
                unit_str[i] = tmp[idx - 1 - i];
            }
            unit_str[idx] = '\0';
        }
        FPuts(fh, (CONST_STRPTR)"UNIT="); FPuts(fh, (CONST_STRPTR)unit_str); FPuts(fh, (CONST_STRPTR)"\n");
        FPuts(fh, (CONST_STRPTR)"DHCP="); FPuts(fh, prefs->use_dhcp ? (CONST_STRPTR)"YES\n" : (CONST_STRPTR)"NO\n");
        FPuts(fh, (CONST_STRPTR)"IP="); FPuts(fh, (CONST_STRPTR)prefs->ip_addr); FPuts(fh, (CONST_STRPTR)"\n");
        FPuts(fh, (CONST_STRPTR)"NETMASK="); FPuts(fh, (CONST_STRPTR)prefs->netmask); FPuts(fh, (CONST_STRPTR)"\n");
        FPuts(fh, (CONST_STRPTR)"GATEWAY="); FPuts(fh, (CONST_STRPTR)prefs->gateway); FPuts(fh, (CONST_STRPTR)"\n");
        FPuts(fh, (CONST_STRPTR)"DNS="); FPuts(fh, (CONST_STRPTR)prefs->dns_server); FPuts(fh, (CONST_STRPTR)"\n");
        Close(fh);
    }

    /* 2. Save binary fallback to ENVARC: and ENV: */
    fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENVARC, MODE_NEWFILE);
    if (fh != (BPTR)0) {
        Write(fh, (CONST APTR)prefs, sizeof(TnPrefs));
        Close(fh);
    }

    fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENV, MODE_NEWFILE);
    if (fh != (BPTR)0) {
        Write(fh, (CONST APTR)prefs, sizeof(TnPrefs));
        Close(fh);
    }

    return TRUE;
}
