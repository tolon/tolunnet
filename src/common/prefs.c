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

BOOL tn_prefs_save(const TnPrefs *prefs, TnPrefsSaveMode mode)
{
    BPTR fh;
    char text[TN_CONFIG_TEXT_MAX];

    if (prefs == NULL) return FALSE;
    if (tn_config_format(prefs, text, sizeof(text)) < 0) return FALSE;

    if (mode == TN_PREFS_SAVE) {
        /* 1. Persistent text config: DEVS:tolunnet.config (canonical, TNET-083) */
        fh = Open((CONST_STRPTR)TN_CONFIG_FILE_DEVS, MODE_NEWFILE);
        if (fh != (BPTR)0) {
            FPuts(fh, (CONST_STRPTR)text);
            Close(fh);
        }

        /* 2. ENVARC: text config — persistent across reboots */
        fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENVARC, MODE_NEWFILE);
        if (fh != (BPTR)0) {
            FPuts(fh, (CONST_STRPTR)text);
            Close(fh);
        }
    }

    /* 3. ENV: text config — always written (live for session, TNET-083) */
    fh = Open((CONST_STRPTR)TN_PREFS_FILE_ENV, MODE_NEWFILE);
    if (fh != (BPTR)0) {
        FPuts(fh, (CONST_STRPTR)text);
        Close(fh);
    }

    return TRUE;
}
