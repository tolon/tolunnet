/*
 * tolunnet — Persistent Preferences & Configuration Engine
 *
 * Implements DEVS:tolunnet.config (plain text KEY=VALUE per TNET-032 / Master prompt §5.2)
 * with ENVARC: / ENV: compatibility fallback.
 */

#include "prefs.h"
#include "config_text.h"
#include <stddef.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dos.h>

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
                tn_config_parse_line(prefs, line, val);
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

BOOL tn_prefs_save(const TnPrefs *prefs, TnPrefsSaveMode mode)
{
    BPTR fh;

    if (prefs == NULL) return FALSE;

    if (mode == TN_PREFS_SAVE) {
        /* 1. Persistent text config: DEVS:tolunnet.config (TNET-032 / TNET-044 /
         *    TNET-063). Text formatting lives in config_text.c (host-tested). */
        fh = Open((CONST_STRPTR)TN_CONFIG_FILE_DEVS, MODE_NEWFILE);
        if (fh != (BPTR)0) {
            char text[TN_CONFIG_TEXT_MAX];
            if (tn_config_format(prefs, text, sizeof(text)) >= 0) {
                FPuts(fh, (CONST_STRPTR)text);
            }
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
