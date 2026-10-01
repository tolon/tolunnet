/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tn_manifest_match - line-set compare for the floppy-install row
 * (11af item 2). Pure C so the Amiga suite and the host tests share
 * the exact same logic: exp and got are newline-separated line sets;
 * the row is green when every expected line exists in got and every
 * got line exists in exp (sizes included in the lines). Empty lines
 * are ignored - a leading newline sentinel or a trailing newline MUST
 * NOT change the result (the 11ad first-line bug).
 */
#ifndef TOLUNNET_TN_MANIFEST_MATCH_H
#define TOLUNNET_TN_MANIFEST_MATCH_H

#include <string.h>

/* how many lines of buf are exactly `line` (length llen) */
static int tn_mm_count(const char *buf, const char *line, int llen)
{
    int n = 0;
    const char *p = buf;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        if (len == llen && memcmp(p, line, llen) == 0) n++;
        p = eol ? eol + 1 : NULL;
    }
    return n;
}

/* Compare the line sets. Returns 1 when they match; the four counters
 * are always filled: expn/matched over exp, gotn/mirrored over got. */
static int tn_manifest_match(const char *exp, const char *got,
                             int *expn, int *matched,
                             int *gotn, int *mirrored)
{
    const char *p;

    *expn = *matched = *gotn = *mirrored = 0;

    p = exp;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        if (len > 0) {
            (*expn)++;
            if (tn_mm_count(got, p, len) > 0) (*matched)++;
        }
        p = eol ? eol + 1 : NULL;
    }

    p = got;
    while (p && *p) {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        if (len > 0) {
            const char *q = exp;
            (*gotn)++;
            while (q && *q) {
                const char *qe = strchr(q, '\n');
                int ql = qe ? (int)(qe - q) : (int)strlen(q);
                if (ql == len && memcmp(q, p, len) == 0) {
                    (*mirrored)++;
                    break;
                }
                q = qe ? qe + 1 : NULL;
            }
        }
        p = eol ? eol + 1 : NULL;
    }

    return (*expn > 0 && *matched == *expn &&
            *gotn == *mirrored && *mirrored == *expn);
}

#endif /* TOLUNNET_TN_MANIFEST_MATCH_H */
