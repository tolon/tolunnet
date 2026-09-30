/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * wrap — pixel-measured word wrap (z.ai step 11w item 1)
 *
 * Guarantees (host-tested under ASan/UBSan in test_wrap.c):
 *  - every stored row measures <= maxw (the indent is part of the
 *    measurement), except when the indent alone is >= maxw: then
 *    each row still holds at least 1 word character (progress);
 *  - no stored row is ever longer than 95 characters;
 *  - words wider than a whole row are hard-split at the pixel
 *    boundary; no extra space is inserted inside a split word;
 *  - words longer than the row body budget are hard-split too (W4:
 *    no mid-word space is invented);
 *  - rows beyond row_max are dropped, nothing is written past
 *    rows[][96].
 *
 * Candidate rows are built with memcpy on known-length pieces (no
 * unbounded %.*s, so -Wformat-truncation stays quiet).
 */

#include "wrap.h"

#include <string.h>

#define WRAP_ROW_CAP  96

/* Store a row only while there is room; returns the (possibly
 * unchanged) row count. The stored row is indent + body[:blen],
 * truncated to 95 characters total. */
static int row_store(char rows[][96], int nr, int row_max,
                     const char *indent, const char *body, size_t blen)
{
    size_t ilen = strlen(indent);

    if (nr >= row_max) return nr;
    if (ilen > WRAP_ROW_CAP - 1) ilen = WRAP_ROW_CAP - 1;
    if (blen > WRAP_ROW_CAP - 1 - ilen) blen = WRAP_ROW_CAP - 1 - ilen;
    memcpy(rows[nr], indent, ilen);
    memcpy(rows[nr] + ilen, body, blen);
    rows[nr][ilen + blen] = '\0';
    return nr + 1;
}

int tn_wrap_rows(const char *text, tn_wrap_measure measure, void *ud,
                 long maxw, char rows[][96], int row_max,
                 const char *indent)
{
    char cur[WRAP_ROW_CAP];          /* body of the current row */
    size_t cur_len = 0;
    int row_empty = 1;               /* explicit empty-row flag */
    int nr = 0;
    const char *p = text;
    size_t ilen;                     /* indent length, clamped once (W3) */
    size_t max_body;                 /* row body characters after indent */

    if (!measure || rows == NULL || row_max <= 0) return 0;
    if (!text) text = "";
    if (!indent) indent = "";
    if (maxw < 1) maxw = 1;
    ilen = strlen(indent);
    if (ilen > WRAP_ROW_CAP - 1) ilen = WRAP_ROW_CAP - 1;
    max_body = WRAP_ROW_CAP - 1 - ilen;
    p = text;

    while (*p != '\0' && nr < row_max) {
        const char *word;            /* W4: points into text, never cut */
        size_t wlen = 0;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        word = p;
        while (p[wlen] != '\0' && p[wlen] != ' ' && p[wlen] != '\t') {
            wlen++;                  /* W4: full word, no 95-char cap */
        }
        p += wlen;

        /* Place the word (or its next piece). Candidate = indent +
         * [cur + ' ' +] word-prefix; the indent is part of the
         * measurement, and the body can never exceed max_body
         * characters (W1). */
        for (;;) {
            size_t used = cur_len + (row_empty ? 0 : 1);
            size_t room = (used < max_body) ? (max_body - used) : 0;
            size_t take = (wlen < room) ? wlen : room;
            size_t blen;             /* candidate length incl. indent */
            char cand[WRAP_ROW_CAP * 2];
            int fits;

            memcpy(cand, indent, ilen);
            if (row_empty) {
                memcpy(cand + ilen, word, take);
                blen = ilen + take;
            } else {
                memcpy(cand + ilen, cur, cur_len);
                cand[ilen + cur_len] = ' ';
                memcpy(cand + ilen + cur_len + 1, word, take);
                blen = ilen + cur_len + 1 + take;
            }
            cand[blen] = '\0';
            fits = (take > 0 && measure(cand, (int)blen, ud) <= maxw);

            if (fits) {
                /* accept: cur = cand minus the indent (W1: the body
                 * is capped by max_body, cur[96] never overflows) */
                memcpy(cur, cand + ilen, blen - ilen + 1);
                cur_len = blen - ilen;
                row_empty = 0;
                if (take == wlen) break;
                word += take;
                wlen -= take;
                continue;   /* the rest of the word goes on the next row */
            }
            if (!row_empty) {
                /* flush the current row and retry the word on an
                 * empty one */
                nr = row_store(rows, nr, row_max, indent, cur, cur_len);
                cur[0] = '\0';
                cur_len = 0;
                row_empty = 1;
                continue;
            }
            if (take == 0) {
                /* W2 corner: the indent alone fills a row - guarantee
                 * progress with one word character per row (the row
                 * may measure over maxw; documented degenerate case) */
                nr = row_store(rows, nr, row_max, indent, word, 1);
                word += 1;
                wlen -= 1;
                if (wlen == 0) break;
                continue;
            }
            /* the word does not fit even on a row of its own:
             * hard-split at the pixel boundary, at least 1 char.
             * W4: the split adds NO space inside the word. */
            {
                size_t fit = (take < 1) ? 1 : take;
                char probe[WRAP_ROW_CAP * 2];

                while (fit > 1) {
                    memcpy(probe, indent, ilen);
                    memcpy(probe + ilen, word, fit);
                    probe[ilen + fit] = '\0';
                    if (measure(probe, (int)(ilen + fit), ud) <= maxw) break;
                    fit--;
                }
                nr = row_store(rows, nr, row_max, indent, word, fit);
                word += fit;
                wlen -= fit;
                if (wlen == 0) break;
            }
        }
    }

    if (!row_empty && cur_len > 0) {
        nr = row_store(rows, nr, row_max, indent, cur, cur_len);
    }
    return nr;
}
