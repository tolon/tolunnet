/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Stack Detection & Migration Engine
 *
 * Scans for Miami, Roadshow, AmiTCP, and Genesis.
 * Provides safe, non-destructive migration and undo.
 */

#include "stack_detect.h"
#include "../common/safe_replace.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <exec/types.h>
#include <exec/execbase.h>
#include "../../include/ipc.h"
#include <dos/dos.h>
#include <dos/dostags.h>
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
#endif


/* Next blank-separated token of [*pp, end); a leading '"' runs to the
 * closing quote. Returns its length (0 = none), *tok = its start. */
static size_t next_token(const char **pp, const char *end, const char **tok)
{
    const char *p = *pp;
    const char *s;

    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p < end && *p == '"') {
        s = ++p;
        while (p < end && *p != '"') p++;
        *tok = s;
        *pp = (p < end) ? p + 1 : p;
        return (size_t)(p - s);
    }
    s = p;
    while (p < end && *p != ' ' && *p != '\t') p++;
    *tok = s;
    *pp = p;
    return (size_t)(p - s);
}

static int token_is(const char *tok, size_t len, const char *word)
{
    size_t i;
    if (strlen(word) != len) return 0;
    for (i = 0; i < len; i++) {
        if (tolower((unsigned char)tok[i]) != word[i]) return 0;
    }
    return 1;
}

/* 1.6: a line is a stack line only when the COMMAND it runs (first
 * token, or the program after Run/Execute and redirections) has one
 * of these exact names - "Assign Genesis: ..." or "Path .../AmiTCP-
 * utils" are left alone. */
static int is_stack_keyword_slice(const char *line, size_t len)
{
    static const char *const names[] = {
        "miami", "miamidx", "miamiinit", "amitcp", "addnetinterface",
        "configurenetinterface", "netshutdown", "genesis", "startnet",
        "stopnet", NULL
    };
    const char *p = line;
    const char *end = line + len;
    const char *tok;
    size_t tlen;
    int i;

    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p < end && (*p == ';' || *p == '#')) {
        return 0;
    }

    tlen = next_token(&p, end, &tok);
    {
        /* "C:Run" / "C:Execute" count as Run / Execute */
        const char *b = tok;
        size_t blen = tlen;
        for (i = (int)tlen - 1; i >= 0; i--) {
            if (tok[i] == ':' || tok[i] == '/') {
                b = tok + i + 1;
                blen = tlen - (size_t)(i + 1);
                break;
            }
        }
        if (token_is(b, blen, "run") || token_is(b, blen, "execute")) {
            tlen = 0;   /* program follows */
        }
    }
    if (tlen == 0) {
        do {
            tlen = next_token(&p, end, &tok);
        } while (tlen > 0 && (tok[0] == '<' || tok[0] == '>'));
    }
    if (tlen == 0) return 0;

    /* basename: after the last ':' or '/' */
    for (i = (int)tlen - 1; i >= 0; i--) {
        if (tok[i] == ':' || tok[i] == '/') break;
    }
    tok += i + 1;
    tlen -= (size_t)(i + 1);

    for (i = 0; names[i] != NULL; i++) {
        if (token_is(tok, tlen, names[i])) return 1;
    }
    return 0;
}

int tn_parse_startup_script(const char *content, char *out_buf, int out_max, int *disabled_count)
{
    if (!content || !out_buf || out_max <= 0) return 0;

    int dis_count = 0;
    int out_len = 0;
    const char *p = content;

    out_buf[0] = '\0';

    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        const char *line_end = p;
        int has_lf = (*p == '\n');
        if (has_lf) p++;

        int has_cr = 0;
        if (line_end > line_start && *(line_end - 1) == '\r') {
            has_cr = 1;
            line_end--;
        }
        size_t line_len = (size_t)(line_end - line_start);

        if (is_stack_keyword_slice(line_start, line_len)) {
            dis_count++;
            const char *prefix = "; tolunnet-disabled: ";
            size_t plen = strlen(prefix);
            if (out_len + (int)plen < out_max) {
                memcpy(out_buf + out_len, prefix, plen);
                out_len += (int)plen;
            }
            if (out_len + (int)line_len < out_max) {
                memcpy(out_buf + out_len, line_start, line_len);
                out_len += (int)line_len;
            }
        } else {
            if (out_len + (int)line_len < out_max) {
                memcpy(out_buf + out_len, line_start, line_len);
                out_len += (int)line_len;
            }
        }

        if (has_cr && out_len < out_max - 1) {
            out_buf[out_len++] = '\r';
        }
        if (has_lf && out_len < out_max - 1) {
            out_buf[out_len++] = '\n';
        }
    }

    out_buf[out_len] = '\0';
    if (disabled_count) *disabled_count = dis_count;
    return out_len;
}

int tn_uncomment_startup_script(const char *content, char *out_buf, int out_max, int *restored_count)
{
    if (!content || !out_buf || out_max <= 0) return 0;

    int res_count = 0;
    int out_len = 0;
    const char *p = content;

    out_buf[0] = '\0';

    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        const char *line_end = p;
        int has_lf = (*p == '\n');
        if (has_lf) p++;

        int has_cr = 0;
        if (line_end > line_start && *(line_end - 1) == '\r') {
            has_cr = 1;
            line_end--;
        }
        size_t line_len = (size_t)(line_end - line_start);

        const char *prefix = "; tolunnet-disabled: ";
        size_t plen = strlen(prefix);
        if (line_len >= plen && memcmp(line_start, prefix, plen) == 0) {
            res_count++;
            const char *act_start = line_start + plen;
            size_t act_len = line_len - plen;
            if (out_len + (int)act_len < out_max) {
                memcpy(out_buf + out_len, act_start, act_len);
                out_len += (int)act_len;
            }
        } else {
            if (out_len + (int)line_len < out_max) {
                memcpy(out_buf + out_len, line_start, line_len);
                out_len += (int)line_len;
            }
        }

        if (has_cr && out_len < out_max - 1) {
            out_buf[out_len++] = '\r';
        }
        if (has_lf && out_len < out_max - 1) {
            out_buf[out_len++] = '\n';
        }
    }

    out_buf[out_len] = '\0';
    if (restored_count) *restored_count = res_count;
    return out_len;
}

#ifdef __AMIGA__

/* Transient park name while swapping LIBS:bsdsocket.library. OFS/FFS
 * names stop at 30 chars: the old ".tolunnet-prev" made it 31 and
 * every Rename to it failed. "bsdsocket.library.tn-prev" = 25. */
#define TN_LIB_PARK "LIBS:bsdsocket.library.tn-prev"

static int case_slice_contains(const char *haystack, size_t hlen, const char *needle)
{
    if (!haystack || !needle) return 0;
    size_t nlen = strlen(needle);
    if (nlen > hlen) return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            char c1 = tolower((unsigned char)haystack[i + j]);
            char c2 = tolower((unsigned char)needle[j]);
            if (c1 != c2) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static BOOL file_exists(const char *path)
{
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return TRUE;
    }
    return FALSE;
}

/* TNET-112: requester-proof volume/assign check via Lock() with requester suppression.
 * Appends ':' to name if not present. DOS Lock() with pr_WindowPtr = -1 will safely
 * return NULL if the volume/assign does not exist, without raising any insert disk requester. */
static BOOL assign_exists(const char *name)
{
    if (name == NULL || name[0] == '\0') return FALSE;

#ifdef __AMIGA__
    struct Process *pr = (struct Process *)FindTask(NULL);
    APTR old = NULL;
    char path[64];
    size_t len = strlen(name);
    BPTR lock;

    if (len >= sizeof(path) - 2) return FALSE;
    strncpy(path, name, sizeof(path) - 2);
    path[sizeof(path) - 2] = '\0';
    if (path[len - 1] != ':') {
        path[len] = ':';
        path[len + 1] = '\0';
    }

    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        old = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;
    }

    lock = Lock((CONST_STRPTR)path, ACCESS_READ);

    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        pr->pr_WindowPtr = old;
    }

    if (lock != (BPTR)0) {
        UnLock(lock);
        return TRUE;
    }
    return FALSE;
#else
    (void)name;
    return FALSE;
#endif
}


static BOOL scan_script_file(const char *path)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return FALSE;

    char buf[1024];
    LONG bytes;
    BOOL found = FALSE;

    while ((bytes = Read(fh, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        if (case_slice_contains(buf, (size_t)bytes, "miami") ||
            case_slice_contains(buf, (size_t)bytes, "amitcp") ||
            case_slice_contains(buf, (size_t)bytes, "addnetinterface") ||
            case_slice_contains(buf, (size_t)bytes, "genesis")) {
            found = TRUE;
            break;
        }
    }
    Close(fh);
    return found;
}

void tn_stack_detect_all(WizardState *ws)
{
    if (!ws) return;
    ws->stack_count = 0;

    struct Process *pr = (struct Process *)FindTask(NULL);
    APTR old_win = NULL;
    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        old_win = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;
    }

    /* 1. Check Exec LibList for bsdsocket.library */
    Forbid();
    struct Library *lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)"bsdsocket.library");
    Permit();

    if (lib) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Active bsdsocket.library", sizeof(st->name) - 1);
        snprintf(st->details, sizeof(st->details), "v%d.%d running in RAM (ID: %s)",
                 lib->lib_Version, lib->lib_Revision,
                 lib->lib_IdString ? (char *)lib->lib_IdString : "unknown");
        st->is_running = TRUE;
    }

    /* 2. Check Miami / MiamiDx (do not import binary prefs; detect & disable only) */
    BOOL miami_found = FALSE;
    if ((assign_exists("Miami") && file_exists("Miami:")) ||
        file_exists("ENVARC:MiamiDx") || file_exists("ENVARC:Miami") ||
        file_exists("SYS:WBStartup/Miami.info") ||
        scan_script_file("S:User-Startup") || scan_script_file("S:Startup-Sequence")) {
        miami_found = TRUE;
    }
    if (miami_found && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Miami / MiamiDx", sizeof(st->name) - 1);
        strncpy(st->details, "Installed (Miami:, ENVARC:Miami*, WBStartup)", sizeof(st->details) - 1);
        st->has_startup = scan_script_file("S:User-Startup") || scan_script_file("S:Startup-Sequence");
    }

    /* 3. Check Roadshow */
    BOOL roadshow_found = FALSE;
    if (file_exists("DEVS:NetInterfaces") || file_exists("DEVS:Internet")) {
        roadshow_found = TRUE;
    }
    if (roadshow_found && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Roadshow", sizeof(st->name) - 1);
        strncpy(st->details, "Installed (DEVS:NetInterfaces, DEVS:Internet)", sizeof(st->details) - 1);
        st->has_startup = scan_script_file("S:User-Startup") || scan_script_file("S:Network-Startup");
    }

    /* 4. Check AmiTCP / Genesis */
    BOOL amitcp_found = FALSE;
    if ((assign_exists("AmiTCP") && (file_exists("AmiTCP:") || file_exists("AmiTCP:db/"))) ||
        file_exists("SYS:WBStartup/Genesis.info")) {
        amitcp_found = TRUE;
    }
    if (amitcp_found && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "AmiTCP / Genesis", sizeof(st->name) - 1);
        strncpy(st->details, "Installed on disk (AmiTCP:, AmiTCP:db/)", sizeof(st->details) - 1);
        st->has_startup = scan_script_file("S:User-Startup") || scan_script_file("S:Startup-Sequence");
    }

    /* 5. Check LIBS:bsdsocket.library */
    if (file_exists("LIBS:bsdsocket.library") && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Disk bsdsocket.library", sizeof(st->name) - 1);
        strncpy(st->details, "File present in LIBS:", sizeof(st->details) - 1);
        st->has_disk_lib = TRUE;
    }

    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        pr->pr_WindowPtr = old_win;
    }
}

/* 7.8: WB launches have no Output() - log only when there is one */
static void sd_log(const char *msg)
{
    BPTR out = Output();
    if (out != (BPTR)0) {
        Write(out, (APTR)msg, (LONG)strlen(msg));
    }
}

/* 1.13: copy src to dst; FALSE (and no dst left behind) on any
 * open/read/write error, so a partial backup is never kept. */
static BOOL copy_file_checked(const char *src, const char *dst)
{
    BPTR in = Open((CONST_STRPTR)src, MODE_OLDFILE);
    BPTR out;
    char chunk[512];
    LONG n;
    BOOL ok = TRUE;

    if (!in) return FALSE;
    out = Open((CONST_STRPTR)dst, MODE_NEWFILE);
    if (!out) {
        Close(in);
        return FALSE;
    }
    while ((n = Read(in, chunk, sizeof(chunk))) > 0) {
        if (Write(out, chunk, n) != n) {
            ok = FALSE;
            break;
        }
    }
    if (n < 0) ok = FALSE;
    Close(in);
    Close(out);
    if (!ok) DeleteFile((CONST_STRPTR)dst);
    return ok;
}

/* 1.9: a missing or empty file has nothing to rewrite - that is
 * success, not failure. */
static BOOL rewrite_file_with_parser(const char *filepath, int (*parser)(const char *, char *, int, int *))
{
    BPTR fh = Open((CONST_STRPTR)filepath, MODE_OLDFILE);
    if (!fh) return file_exists(filepath) ? FALSE : TRUE;

    struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    LONG file_size = -1;
    if (fib) {
        if (ExamineFH(fh, fib)) {
            file_size = fib->fib_Size;
        }
        FreeDosObject(DOS_FIB, fib);
    }
    if (file_size < 0) {
        Close(fh);
        return FALSE;
    }
    if (file_size == 0) {
        Close(fh);
        return TRUE;
    }

    /* Allocate buffer for reading file plus null terminator */
    char *in_buf = (char *)AllocVec(file_size + 1, MEMF_PUBLIC | MEMF_CLEAR);
    if (!in_buf) {
        Close(fh);
        return FALSE;
    }

    /* 1.13: loop - one short Read() must not install a cut script */
    LONG r = 0;
    while (r < file_size) {
        LONG got = Read(fh, in_buf + r, file_size - r);
        if (got <= 0) break;
        r += got;
    }
    Close(fh);
    if (r != file_size) {
        FreeVec(in_buf);
        return FALSE;
    }
    in_buf[r] = '\0';

    /* Max expansion: every line could gain prefix length ~22 bytes */
    LONG out_cap = file_size * 2 + 1024;
    char *out_buf = (char *)AllocVec(out_cap, MEMF_PUBLIC | MEMF_CLEAR);
    if (!out_buf) {
        FreeVec(in_buf);
        return FALSE;
    }

    int count = 0;
    int out_len = parser(in_buf, out_buf, out_cap - 1, &count);
    FreeVec(in_buf);

    if (count > 0 && out_len > 0) {
        char temp_path[256];
        char prev_path[256];
        snprintf(temp_path, sizeof(temp_path), "%s.tolunnet-new", filepath);
        snprintf(prev_path, sizeof(prev_path), "%s.tolunnet-prev", filepath);

        BPTR out_fh = Open((CONST_STRPTR)temp_path, MODE_NEWFILE);
        if (!out_fh) {
            FreeVec(out_buf);
            return FALSE;
        }

        LONG written = Write(out_fh, out_buf, out_len);
        Close(out_fh);

        if (written == out_len) {
            /* 11k item 2: the shared safe replace keeps the original
             * in <file>.tolunnet-prev (one generation) and never
             * deletes the live file. */
            if (!tn_safe_replace(temp_path, filepath, prev_path)) {
                FreeVec(out_buf);
                return FALSE;
            }
        } else {
            DeleteFile((CONST_STRPTR)temp_path);
            FreeVec(out_buf);
            return FALSE;
        }
    }

    FreeVec(out_buf);
    return TRUE;
}

/* 1.1/1.8: standalone undo for everything tn_stack_apply_replacement
 * changes. The line-level restore of the "; tolunnet-disabled: "
 * lines (no whole-file copy over later edits, see 1.3) cannot be
 * done in AmigaDOS script, so the script calls back into
 * TolunnetSetup (tn_stack_undo_replacement); when that binary is
 * gone it restores what plain Rename can and says what is left. */
static const char s_undo_stacks_text[] =
    "; tolunnet-undo-stacks - undoes what TolunnetSetup changed for other\n"
    "; TCP/IP stacks (startup lines, LIBS:bsdsocket.library, WBStartup)\n"
    "; and removes the tolunnet boot block from S:User-Startup.\n"
    "; Execute it alone or from S:tolunnet-undo. It deletes itself.\n"
    "FailAt 21\n"
    "IF EXISTS SYS:Prefs/TolunnetSetup\n"
    "  SYS:Prefs/TolunnetSetup " TN_UNDO_STACKS_ARG "\n"
    "ENDIF\n"
    "IF EXISTS LIBS:bsdsocket.library.roadshow\n"
    "  IF NOT EXISTS LIBS:bsdsocket.library\n"
    "    Rename >NIL: LIBS:bsdsocket.library.roadshow LIBS:bsdsocket.library\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS LIBS:bsdsocket.library.miami\n"
    "  IF NOT EXISTS LIBS:bsdsocket.library\n"
    "    Rename >NIL: LIBS:bsdsocket.library.miami LIBS:bsdsocket.library\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS LIBS:bsdsocket.library.amitcp\n"
    "  IF NOT EXISTS LIBS:bsdsocket.library\n"
    "    Rename >NIL: LIBS:bsdsocket.library.amitcp LIBS:bsdsocket.library\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS LIBS:bsdsocket.library.genesis\n"
    "  IF NOT EXISTS LIBS:bsdsocket.library\n"
    "    Rename >NIL: LIBS:bsdsocket.library.genesis LIBS:bsdsocket.library\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS " TN_LIB_PARK "\n"
    "  IF NOT EXISTS LIBS:bsdsocket.library\n"
    "    Rename >NIL: " TN_LIB_PARK " LIBS:bsdsocket.library\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS SYS:WBStartup/Miami.info.pre-tolunnet\n"
    "  IF NOT EXISTS SYS:WBStartup/Miami.info\n"
    "    Rename >NIL: SYS:WBStartup/Miami.info.pre-tolunnet SYS:WBStartup/Miami.info\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS SYS:WBStartup/Genesis.info.pre-tolunnet\n"
    "  IF NOT EXISTS SYS:WBStartup/Genesis.info\n"
    "    Rename >NIL: SYS:WBStartup/Genesis.info.pre-tolunnet SYS:WBStartup/Genesis.info\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS S:User-Startup\n"
    "  Search >NIL: S:User-Startup \"; tolunnet-disabled: \" QUIET\n"
    "  IF NOT WARN\n"
    "    Echo \"S:User-Startup: remove the '; tolunnet-disabled: ' prefixes by hand.\"\n"
    "  ENDIF\n"
    "ENDIF\n"
    "IF EXISTS S:Network-Startup\n"
    "  Search >NIL: S:Network-Startup \"; tolunnet-disabled: \" QUIET\n"
    "  IF NOT WARN\n"
    "    Echo \"S:Network-Startup: remove the '; tolunnet-disabled: ' prefixes by hand.\"\n"
    "  ENDIF\n"
    "ENDIF\n"
    "Echo \"Other TCP/IP stack settings restored.\"\n"
    "Run >NIL: Delete >NIL: " TN_UNDO_STACKS_SCRIPT " QUIET\n";

static BOOL write_undo_stacks_script(void)
{
    const char *tmp = TN_UNDO_STACKS_SCRIPT ".new";
    LONG len = (LONG)sizeof(s_undo_stacks_text) - 1;
    BPTR fh;
    LONG w;

    if (file_exists(TN_UNDO_STACKS_SCRIPT)) return TRUE;
    fh = Open((CONST_STRPTR)tmp, MODE_NEWFILE);
    if (!fh) return FALSE;
    w = Write(fh, (APTR)s_undo_stacks_text, len);
    Close(fh);
    if (w != len || !Rename((CONST_STRPTR)tmp, (CONST_STRPTR)TN_UNDO_STACKS_SCRIPT)) {
        DeleteFile((CONST_STRPTR)tmp);
        return FALSE;
    }
    SetProtection((CONST_STRPTR)TN_UNDO_STACKS_SCRIPT, FIBF_SCRIPT);
    return TRUE;
}

BOOL tn_stack_apply_replacement(WizardState *ws)
{
    if (!ws || !ws->replace_stacks) return TRUE;

    /* 1. Backup S:User-Startup once. 1.13: checked copy - a failed
     * or partial backup is deleted and nothing is changed. */
    if (file_exists("S:User-Startup") && !file_exists("S:User-Startup.tolunnet-bak")) {
        if (!copy_file_checked("S:User-Startup", "S:User-Startup.tolunnet-bak")) {
            sd_log("tolunnet: cannot back up S:User-Startup - nothing changed\n");
            return FALSE;
        }
    } else if (file_exists("S:User-Startup.tolunnet-bak")) {
        /* 11k item 2: an older backup wins - say so in the log */
        sd_log("tolunnet: keeping existing S:User-Startup.tolunnet-bak\n");
    }

    /* 1b. 1.1/1.8: the wizard's own undo goes to S:tolunnet-undo-stacks
     * (S:tolunnet-undo belongs to the installer, which Executes this
     * one first). Written before anything changes; an existing one
     * from an earlier run is kept - it already undoes to the state
     * before the FIRST run. */
    if (!write_undo_stacks_script()) {
        sd_log("tolunnet: cannot write S:tolunnet-undo-stacks - nothing changed\n");
        return FALSE;
    }

    /* 2. Comment out stack lines in S:User-Startup and S:Network-Startup.
     * 11k item 2: a failed rewrite must not report success - the
     * original file is still in place when tn_safe_replace bailed. */
    if (!rewrite_file_with_parser("S:User-Startup", tn_parse_startup_script)) {
        return FALSE;
    }
    if (file_exists("S:Network-Startup")) {
        if (!rewrite_file_with_parser("S:Network-Startup", tn_parse_startup_script)) {
            return FALSE;
        }
    }

    /* 3. Rename LIBS:bsdsocket.library -> LIBS:bsdsocket.library.<stack> */
    const char *stack_suffix = "pre-tolunnet";
    for (int i = 0; i < ws->stack_count; i++) {
        if (strstr(ws->stacks[i].name, "Roadshow") != NULL) {
            stack_suffix = "roadshow";
            break;
        } else if (strstr(ws->stacks[i].name, "Miami") != NULL) {
            stack_suffix = "miami";
            break;
        } else if (strstr(ws->stacks[i].name, "AmiTCP") != NULL) {
            stack_suffix = "amitcp";
            break;
        } else if (strstr(ws->stacks[i].name, "Genesis") != NULL) {
            stack_suffix = "genesis";
            break;
        }
    }
    if (strcmp(stack_suffix, "pre-tolunnet") == 0) {
        if (file_exists("DEVS:NetInterfaces") || file_exists("DEVS:Internet")) {
            stack_suffix = "roadshow";
        } else if (assign_exists("Miami") || file_exists("ENVARC:MiamiDx") || file_exists("ENVARC:Miami")) {
            stack_suffix = "miami";
        } else if (assign_exists("AmiTCP") || file_exists("AmiTCP:")) {
            stack_suffix = "amitcp";
        }
    }

    if (file_exists("LIBS:bsdsocket.library")) {
        char backup_path[64];
        char park_path[80];
        snprintf(backup_path, sizeof(backup_path), "LIBS:bsdsocket.library.%s", stack_suffix);
        snprintf(park_path, sizeof(park_path), TN_LIB_PARK);
        DeleteFile((CONST_STRPTR)park_path);
        /* 11k item 2: park the live library first; only after the
         * parked copy sits under the backup name is the swap done.
         * If that rename fails, the park goes straight back - the
         * library itself is never Deleted, and a stale backup from
         * an earlier run is kept, not overwritten. */
        if (Rename((CONST_STRPTR)"LIBS:bsdsocket.library", (CONST_STRPTR)park_path)) {
            if (Rename((CONST_STRPTR)park_path, (CONST_STRPTR)backup_path) == FALSE) {
                Rename((CONST_STRPTR)park_path, (CONST_STRPTR)"LIBS:bsdsocket.library");
            }
        }
    }

    /* 4. Disable WBStartup icons */
    if (file_exists("SYS:WBStartup/Miami.info")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Miami.info",
               (CONST_STRPTR)"SYS:WBStartup/Miami.info.pre-tolunnet");
    }
    if (file_exists("SYS:WBStartup/Genesis.info")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Genesis.info",
               (CONST_STRPTR)"SYS:WBStartup/Genesis.info.pre-tolunnet");
    }

    return TRUE;
}

BOOL tn_stack_undo_replacement(void)
{
    /* 1. Restore commented lines in S:User-Startup.
     * 11k item 2: do not report success when a rewrite failed. */
    if (file_exists("S:User-Startup")) {
        if (!rewrite_file_with_parser("S:User-Startup", tn_uncomment_startup_script)) {
            return FALSE;
        }
    }
    if (file_exists("S:Network-Startup")) {
        if (!rewrite_file_with_parser("S:Network-Startup", tn_uncomment_startup_script)) {
            return FALSE;
        }
    }

    /* 2. Restore LIBS:bsdsocket.library.<stack>.
     * 11k item 2: park the replacement first, put the original back,
     * and delete the park only after the original is in place. */
    const char *suffixes[] = { "roadshow", "miami", "amitcp", "genesis", "pre-tolunnet", NULL };
    for (int i = 0; suffixes[i] != NULL; i++) {
        char path[64];
        char park_path[80];
        snprintf(path, sizeof(path), "LIBS:bsdsocket.library.%s", suffixes[i]);
        if (file_exists(path)) {
            snprintf(park_path, sizeof(park_path), TN_LIB_PARK);
            if (file_exists("LIBS:bsdsocket.library")) {
                /* park the replacement, put the original back, and
                 * delete the park only after the original is in
                 * place. */
                DeleteFile((CONST_STRPTR)park_path);
                if (Rename((CONST_STRPTR)"LIBS:bsdsocket.library", (CONST_STRPTR)park_path)) {
                    if (Rename((CONST_STRPTR)path, (CONST_STRPTR)"LIBS:bsdsocket.library")) {
                        DeleteFile((CONST_STRPTR)park_path);
                    } else {
                        Rename((CONST_STRPTR)park_path, (CONST_STRPTR)"LIBS:bsdsocket.library");
                    }
                }
            } else {
                /* nothing to protect: the original goes straight back
                 * (the plain undo case - apply already moved it). */
                Rename((CONST_STRPTR)path, (CONST_STRPTR)"LIBS:bsdsocket.library");
            }
            break;
        }
    }

    /* 3. Restore WBStartup icons */
    if (file_exists("SYS:WBStartup/Miami.info.pre-tolunnet")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Miami.info.pre-tolunnet",
               (CONST_STRPTR)"SYS:WBStartup/Miami.info");
    }
    if (file_exists("SYS:WBStartup/Genesis.info.pre-tolunnet")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Genesis.info.pre-tolunnet",
               (CONST_STRPTR)"SYS:WBStartup/Genesis.info");
    }

    /* 1.1/1.8: everything is back - the wizard's undo script has
     * nothing left to do. (Run from inside that script the delete
     * fails "in use"; the script removes itself at its end.) */
    DeleteFile((CONST_STRPTR)TN_UNDO_STACKS_SCRIPT);

    return TRUE;
}

void tn_stack_request_quit(WizardState *ws)
{
    if (!ws) return;

    /* 1. Ask Miami / MiamiDx to quit via ARexx port */
    Forbid();
    struct MsgPort *miami_port = FindPort((CONST_STRPTR)"MIAMI");
    struct MsgPort *miamidx_port = FindPort((CONST_STRPTR)"MIAMIDX");
    Permit();

    if (miami_port) {
        Execute((CONST_STRPTR)"rx \"address MIAMI 'QUIT'\" >NIL: <NIL:", 0, 0);
    }
    if (miamidx_port) {
        Execute((CONST_STRPTR)"rx \"address MIAMIDX 'QUIT'\" >NIL: <NIL:", 0, 0);
    }

    /* 2. Roadshow shutdown — only for a REAL Roadshow install
     * (z.ai step 9a-2 item 2): bsdsocket.library in the Exec library
     * list whose IdString contains "Roadshow", and no tolunnet port.
     * Our own NetShutdown must never be run against our own daemon:
     * its old IFCTL DOWN left the interface down (bench
     * 20260926-085001). */
    {
        BOOL roadshow = FALSE;
        struct Library *lib;
        Forbid();
        for (lib = (struct Library *)SysBase->LibList.lh_Head;
             lib->lib_Node.ln_Succ != NULL;
             lib = (struct Library *)lib->lib_Node.ln_Succ) {
            if (lib->lib_IdString != NULL &&
                strstr((const char *)lib->lib_IdString, "Roadshow") != NULL) {
                roadshow = TRUE;
                break;
            }
        }
        Permit();
        if (roadshow && FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) == NULL) {
            if (file_exists("C:NetShutdown")) {
                Execute((CONST_STRPTR)"C:NetShutdown >NIL: <NIL:", 0, 0);
            } else if (file_exists("NetShutdown")) {
                Execute((CONST_STRPTR)"NetShutdown >NIL: <NIL:", 0, 0);
            }
        }
    }

    /* 3. AmiTCP shutdown */
    if (assign_exists("AmiTCP") && file_exists("AmiTCP:bin/stopnet")) {
        Execute((CONST_STRPTR)"AmiTCP:bin/stopnet >NIL: <NIL:", 0, 0);
    }

    /* 4. Wait loop up to 10s for legacy stack ports to terminate */
    BOOL any_running = FALSE;
    Forbid();
    any_running = (FindPort((CONST_STRPTR)"MIAMI") != NULL ||
                   FindPort((CONST_STRPTR)"MIAMIDX") != NULL ||
                   FindPort((CONST_STRPTR)"AmiTCP") != NULL ||
                   FindPort((CONST_STRPTR)"AmiTCP_DAEMON") != NULL);
    Permit();
    if (!any_running) return;

    for (int i = 0; i < 20; i++) {
        Delay(25); /* 500ms */
        Forbid();
        any_running = (FindPort((CONST_STRPTR)"MIAMI") != NULL ||
                       FindPort((CONST_STRPTR)"MIAMIDX") != NULL ||
                       FindPort((CONST_STRPTR)"AmiTCP") != NULL ||
                       FindPort((CONST_STRPTR)"AmiTCP_DAEMON") != NULL);
        Permit();
        if (!any_running) break;
    }

    if (any_running) {
        ws->needs_reboot = TRUE;
    }
}

#else /* Host test stubs */

void tn_stack_detect_all(WizardState *ws) { (void)ws; }
BOOL tn_stack_apply_replacement(WizardState *ws) { (void)ws; return TRUE; }
BOOL tn_stack_undo_replacement(void) { return TRUE; }
void tn_stack_request_quit(WizardState *ws) { (void)ws; }

#endif
