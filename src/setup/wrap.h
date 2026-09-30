/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * wrap — pixel-measured word wrap (z.ai step 11v item 1)
 *
 * Pure C, host-tested (tests/host/test_wrap.c, ASan/UBSan). The
 * caller supplies a measuring callback so the same code wraps with
 * TextLength() on AmigaOS and with any other metric in tests.
 */

#ifndef TOLUNNET_WRAP_H
#define TOLUNNET_WRAP_H

#include <stddef.h>

/* Return the rendered width of s[0..len) in pixels. */
typedef long (*tn_wrap_measure)(const char *s, int len, void *ud);

/* Word-wrap text into rows of at most maxw measured pixels. Every
 * row is written as indent + words, NUL-terminated, never longer
 * than 96 bytes; the indent is part of the measured width. A word
 * wider than maxw is hard-split at the pixel boundary. Returns the
 * number of rows written (at most row_max; trailing input that no
 * longer fits is dropped). */
int tn_wrap_rows(const char *text, tn_wrap_measure measure, void *ud,
                 long maxw, char rows[][96], int row_max,
                 const char *indent);

#endif /* TOLUNNET_WRAP_H */
