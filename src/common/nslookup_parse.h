/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nslookup_parse — parse C:nslookup output (11x item 2)
 *
 * Pure C, host-tested (tests/host/test_nslookup_parse.c). Extracts
 * the ANSWER address from nslookup output: the Server: line comes
 * first and must be ignored; the answer is the first line with a
 * dotted quad in the Address position. Returns 0 when the output
 * only carries an error line ("**") or no answer at all.
 */

#ifndef TOLUNNET_NSLOOKUP_PARSE_H
#define TOLUNNET_NSLOOKUP_PARSE_H

#include <stddef.h>

/* Returns 1 and copies the answer IPv4 into ip (NUL-terminated,
 * truncated to n bytes) when the nslookup output carries one;
 * returns 0 otherwise. */
int tn_parse_nslookup_answer(const char *text, char *ip, size_t n);

#endif /* TOLUNNET_NSLOOKUP_PARSE_H */
