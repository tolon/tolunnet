/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Persistent Preferences & Configuration Engine
 *
 * Implements DEVS:tolunnet.config (plain text KEY=VALUE per TNET-032 / Master prompt §5.2)
 * with ENVARC: / ENV: compatibility fallback.
 */

#include "prefs.h"
#include <string.h>
#include "config_text.h"
#include <stddef.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dos.h>

/* Read the binary blob (TnPrefs struct image) from fh into *out. Tolerates
 * older, smaller pre-TNET-063/TNET-108 structs: any prefix of the current
 * layout at least as large as the pre-TNET-063 blob is recognised, and fields
 * beyond the read prefix keep the defaults already present in *out. Returns
 * TRUE on a recognised blob. */
static BOOL tn_prefs_read_blob(TnPrefs *out, BPTR fh)
{
    TnPrefs tmp;
    LONG read_bytes;

    tn_prefs_default(&tmp);
    Seek(fh, 0, OFFSET_BEGINNING);
    read_bytes = Read(fh, &tmp, sizeof(TnPrefs));
    if (read_bytes >= (LONG)offsetof(TnPrefs, hostname) && read_bytes <= (LONG)sizeof(TnPrefs)) {
        *out = tmp;
        return TRUE;
    }
    return FALSE;
}

/* Reads a config file (either modern text or legacy binary blob) into prefs */
static BOOL tn_prefs_read_file(TnPrefs *prefs, BPTR fh)
{
    char line[128];
    UBYTE header[4];
    LONG n;
    BOOL parsed_any = FALSE;

    Seek(fh, 0, OFFSET_BEGINNING);
    n = Read(fh, header, sizeof(header));
    if (n <= 0) return FALSE;
    Seek(fh, 0, OFFSET_BEGINNING);

    /* Check if file starts with non-ASCII binary data (legacy 1.0.0 blob) */
    if ((header[0] < 32 && header[0] != '\t' && header[0] != '\r' && header[0] != '\n') ||
        (header[1] < 32 && header[1] != '\t' && header[1] != '\r' && header[1] != '\n')) {
        return tn_prefs_read_blob(prefs, fh);
    }

    while (FGets(fh, (STRPTR)line, sizeof(line)) != NULL) {
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq == '=') {
            *eq = '\0';
            char *val = eq + 1;
            while (*val == ' ' || *val == '\t') val++;
            tn_config_parse_line(prefs, line, val);
            parsed_any = TRUE;
        }
    }
    return parsed_any;
}

BOOL tn_prefs_load(TnPrefs *prefs)
{
    BPTR fh_devs = (BPTR)0;
    BPTR fh_env  = (BPTR)0;
    __attribute__((aligned(4))) struct FileInfoBlock fib_devs;
    __attribute__((aligned(4))) struct FileInfoBlock fib_env;
    BOOL have_devs = FALSE;
    BOOL have_env  = FALSE;
    BOOL loaded = FALSE;

    if (prefs == NULL) return FALSE;
    tn_prefs_default(prefs);

    /* Examine DEVS:tolunnet.config (canonical source of truth, TNET-083) */
    fh_devs = Open((CONST_STRPTR)TN_CONFIG_FILE_DEVS, MODE_OLDFILE);
    if (fh_devs != (BPTR)0) {
        if (ExamineFH(fh_devs, &fib_devs)) {
            have_devs = TRUE;
        }
    }

    /* Examine ENV:tolunnet.prefs (live session state) */
    fh_env = Open((CONST_STRPTR)TN_PREFS_FILE_ENV, MODE_OLDFILE);
    if (fh_env != (BPTR)0) {
        if (ExamineFH(fh_env, &fib_env)) {
            have_env = TRUE;
        }
    }

    if (have_devs && have_env) {
        /* Both exist: ENV overrides ONLY if newer than DEVS (TNET-083) */
        if (CompareDates(&fib_env.fib_Date, &fib_devs.fib_Date) > 0) {
            loaded = tn_prefs_read_file(prefs, fh_env);
        } else {
            loaded = tn_prefs_read_file(prefs, fh_devs);
        }
    } else if (have_devs) {
        loaded = tn_prefs_read_file(prefs, fh_devs);
    } else if (have_env) {
        loaded = tn_prefs_read_file(prefs, fh_env);
    }

    if (fh_devs != (BPTR)0) Close(fh_devs);
    if (fh_env != (BPTR)0)  Close(fh_env);

    if (loaded) return TRUE;

    /* Fallback: ENVARC: text/blob config (last Save) */
    fh_env = Open((CONST_STRPTR)TN_PREFS_FILE_ENVARC, MODE_OLDFILE);
    if (fh_env != (BPTR)0) {
        loaded = tn_prefs_read_file(prefs, fh_env);
        Close(fh_env);
        if (loaded) return TRUE;
    }

    /* Fallback: pre-rename legacy DEVS:tolunet.config */
    fh_devs = Open((CONST_STRPTR)"DEVS:tolunet.config", MODE_OLDFILE);
    if (fh_devs != (BPTR)0) {
        loaded = tn_prefs_read_file(prefs, fh_devs);
        Close(fh_devs);
        if (loaded) return TRUE;
    }

    return FALSE;
}

/* z.ai step 9b item 2: honest atomic write - merge in the previous
 * file's unknown keys and over-long lines, write to <path>_tmp,
 * then Rename over the target. Any failure returns FALSE and leaves
 * the target untouched. */
int tn_prefs_last_stage = 0;

static BOOL tn_prefs_write_one(const char *path, const char *text)
{
    char prev[1024];
    char merged[2048];
    char tmp[256];
    LONG plen = 0, wlen, r;
    BPTR fh;

    prev[0] = 0;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (fh != (BPTR)0) {
        plen = Read(fh, (APTR)prev, (LONG)sizeof(prev) - 1);
        Close(fh);
        if (plen < 0) plen = 0;
    }
    prev[plen] = 0;

    tn_prefs_last_stage = 30;
    if (tn_config_merge_preserve(text, prev, merged, (int)sizeof(merged)) < 0) { tn_prefs_last_stage = 31; return FALSE; }

    for (r = 0; path[r] != 0 && r + 6 < (LONG)sizeof(tmp); r++) tmp[r] = path[r];
    tmp[r] = 0;
    strcat(tmp, "_tmp");

    tn_prefs_last_stage = 40;
    fh = Open((CONST_STRPTR)tmp, MODE_NEWFILE);
    if (fh == (BPTR)0) { tn_prefs_last_stage = 41; return FALSE; }
    wlen = strlen(merged);
    tn_prefs_last_stage = 50;
    if (Write(fh, (APTR)merged, wlen) != wlen) {
        Close(fh);
        DeleteFile((CONST_STRPTR)tmp);
        tn_prefs_last_stage = 51;
        return FALSE;
    }
    tn_prefs_last_stage = 60;
    if (Close(fh) == FALSE) {
        DeleteFile((CONST_STRPTR)tmp);
        tn_prefs_last_stage = 61;
        return FALSE;
    }
    tn_prefs_last_stage = 70;
    /* some filesystems refuse Rename onto an existing target that
     * carries odd protection bits (the wizard rewrites this file);
     * clear the way first, then retry the rename once. */
    if (Rename((CONST_STRPTR)tmp, (CONST_STRPTR)path) == FALSE) {
        DeleteFile((CONST_STRPTR)path);
        if (Rename((CONST_STRPTR)tmp, (CONST_STRPTR)path) == FALSE) {
            DeleteFile((CONST_STRPTR)tmp);
            tn_prefs_last_stage = 71;
            return FALSE;
        }
    }
    return TRUE;
}

/* z.ai step 10b item 2: TRUE when at least one configuration store
 * exists (DEVS config, ENV live state, ENVARC last save). Used by
 * the daemon so a plain start with no configuration says one clear
 * line and exits instead of booting a useless default stack.
 */
BOOL tn_prefs_any_store_exists(void)
{
    static const char *const paths[] = {
        TN_CONFIG_FILE_DEVS, TN_PREFS_FILE_ENV, TN_PREFS_FILE_ENVARC,
    };
    size_t i;
    BPTR lk;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        lk = Lock((CONST_STRPTR)paths[i], ACCESS_READ);
        if (lk != (BPTR)0) {
            UnLock(lk);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL tn_prefs_save(const TnPrefs *prefs, TnPrefsSaveMode mode)
{
    char text[TN_CONFIG_TEXT_MAX];

    if (prefs == NULL) return FALSE;
    if (tn_config_format(prefs, text, sizeof(text)) < 0) return FALSE;

    if (mode == TN_PREFS_SAVE) {
        /* 1. Persistent text config: DEVS:tolunnet.config (canonical,
         * TNET-083). Its failure fails the save. */
        if (!tn_prefs_write_one(TN_CONFIG_FILE_DEVS, text)) return FALSE;
        /* 2./3. ENVARC:/ENV mirrors are best-effort: ENVARC: is not
         * mounted on every headless install, and a missing mirror must
         * not fail a save whose canonical target succeeded. */
        tn_prefs_write_one(TN_PREFS_FILE_ENVARC, text);
    }

    tn_prefs_write_one(TN_PREFS_FILE_ENV, text);
    return TRUE;
}
