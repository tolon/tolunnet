/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Boot block editor for the user startup script
 * (z.ai step 11l item 2)
 *
 * Replaces the old in-place 64 KB / 256-byte-line rewrite that
 * truncated long startup files and ignored every error. This editor
 * reads the whole file (size from ExamineFH), copies every line
 * outside a "; BEGIN tolunnet ... ; END tolunnet" block byte for
 * byte, installs the result through tn_safe_replace with
 * <path>.tolunnet-prev as the backup, and reports FALSE on every
 * failure with the original file untouched. <path>.tolunnet-bak is
 * created once, only when it does not exist yet (same rule as the
 * stack rewrite). dos.library + safe_replace only.
 */

#include "boot_block.h"
#include "../common/safe_replace.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <string.h>

#define TN_BB_BEGIN "; BEGIN tolunnet"
#define TN_BB_END   "; END tolunnet"
#define TN_BB_BLOCK "; BEGIN tolunnet\n" \
                    "Stack 32768\n" \
                    "Run <NIL: >NIL: C:tolunnet\n" \
                    "; END tolunnet\n"

static BOOL bb_file_exists(const char *path)
{
    BPTR lk = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lk != (BPTR)0) {
        UnLock(lk);
        return TRUE;
    }
    return FALSE;
}

/* Copy src to dst in 1 KB chunks - only used to seed <path>.tolunnet-bak. */
static BOOL bb_copy_once(const char *src, const char *dst)
{
    BPTR in = Open((CONST_STRPTR)src, MODE_OLDFILE);
    BPTR out;
    char chunk[1024];
    LONG n;

    if (in == (BPTR)0) return FALSE;
    out = Open((CONST_STRPTR)dst, MODE_NEWFILE);
    if (out == (BPTR)0) {
        Close(in);
        return FALSE;
    }
    while ((n = Read(in, chunk, sizeof(chunk))) > 0) {
        if (Write(out, chunk, n) != n) {
            Close(in);
            Close(out);
            return FALSE;
        }
    }
    Close(in);
    Close(out);
    return TRUE;
}

static BOOL bb_suffix_path(char *dst, LONG dst_size, const char *path, const char *suffix)
{
    LONG plen = (LONG)strlen(path);
    LONG slen = (LONG)strlen(suffix);

    if (plen == 0 || plen + slen + 1 > dst_size) return FALSE;
    memcpy(dst, path, plen);
    memcpy(dst + plen, suffix, slen + 1);
    return TRUE;
}

BOOL tn_boot_block_apply(const char *path, BOOL enable)
{
    __attribute__((aligned(4))) struct FileInfoBlock fib;
    char new_path[300];
    char prev_path[300];
    char bak_path[300];
    char *in_buf = NULL;
    char *out_buf = NULL;
    LONG size, total, out_len = 0;
    const char *p, *end;
    BOOL inside = FALSE;
    BPTR fh, of;
    LONG w;
    BOOL ok = FALSE;

    if (!bb_suffix_path(new_path, sizeof(new_path), path, ".tolunnet-new") ||
        !bb_suffix_path(prev_path, sizeof(prev_path), path, ".tolunnet-prev") ||
        !bb_suffix_path(bak_path, sizeof(bak_path), path, ".tolunnet-bak")) {
        return FALSE;
    }

    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (fh == (BPTR)0) return FALSE;

    memset(&fib, 0, sizeof(fib));
    if (!ExamineFH(fh, &fib)) {
        Close(fh);
        return FALSE;
    }
    size = fib.fib_Size;

    in_buf = (char *)AllocVec(size + 1, MEMF_PUBLIC | MEMF_CLEAR);
    out_buf = (char *)AllocVec(size + 384, MEMF_PUBLIC | MEMF_CLEAR);
    if (in_buf == NULL || out_buf == NULL) {
        Close(fh);
        goto cleanup;
    }

    total = 0;
    while (total < size) {
        LONG got = Read(fh, in_buf + total, size - total);
        if (got <= 0) break;
        total += got;
    }
    Close(fh);
    fh = (BPTR)0;
    if (total != size) goto cleanup;

    /* Seed <path>.tolunnet-bak once, before anything is replaced. */
    if (!bb_file_exists(bak_path)) {
        bb_copy_once(path, bak_path);
    }

    /* Copy every non-tolunnet line byte for byte. Newlines travel
     * with their own line, so a missing final newline stays missing. */
    p = in_buf;
    end = in_buf + size;
    while (p < end) {
        const char *line = p;
        LONG linelen;
        BOOL has_nl;

        while (p < end && *p != '\n') p++;
        linelen = (LONG)(p - line);
        has_nl = (p < end);
        if (has_nl) p++;

        if (linelen >= (LONG)(sizeof(TN_BB_BEGIN) - 1) &&
            memcmp(line, TN_BB_BEGIN, sizeof(TN_BB_BEGIN) - 1) == 0) {
            inside = TRUE;
            continue;
        }
        if (inside) {
            if (linelen >= (LONG)(sizeof(TN_BB_END) - 1) &&
                memcmp(line, TN_BB_END, sizeof(TN_BB_END) - 1) == 0) {
                inside = FALSE;
            }
            continue;
        }
        memcpy(out_buf + out_len, line, linelen);
        out_len += linelen;
        if (has_nl) out_buf[out_len++] = '\n';
    }

    if (enable) {
        LONG blen = (LONG)sizeof(TN_BB_BLOCK) - 1;
        if (out_len > 0 && out_buf[out_len - 1] != '\n') {
            out_buf[out_len++] = '\n';      /* keep the appended block on its own line */
        }
        memcpy(out_buf + out_len, TN_BB_BLOCK, blen);
        out_len += blen;
    }

    of = Open((CONST_STRPTR)new_path, MODE_NEWFILE);
    if (of == (BPTR)0) goto cleanup;
    w = Write(of, out_buf, out_len);
    Close(of);
    if (w != out_len) {
        DeleteFile((CONST_STRPTR)new_path);
        goto cleanup;
    }

    ok = tn_safe_replace(new_path, path, prev_path);

cleanup:
    if (fh != (BPTR)0) Close(fh);
    if (in_buf != NULL) FreeVec(in_buf);
    if (out_buf != NULL) FreeVec(out_buf);
    return ok;
}
