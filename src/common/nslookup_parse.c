/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nslookup_parse — parse C:nslookup output (11x item 2)
 *
 * C:nslookup prints "Server: <ip>:<port>" FIRST, then the answer
 * ("Name: ..." + "Address: <ip>" / "Address1..3: <ip>"). The
 * parser walks the output line by line: the Server: line is
 * skipped (it names the DNS server, not the answer), error lines
 * ("** ...") carry no dotted quad, and the first line with a
 * trailing dotted quad wins.
 *
 * tn_parse_first_ipv4 (11x item 3) is the simpler cousin for
 * one-quad-per-line commands (C:GetNetStatus GATEWAY / DNS): the
 * first dotted quad anywhere in the text wins.
 */

#include "nslookup_parse.h"

#include <string.h>

/* Is [s, s+len) a dotted quad: 4 dot-separated runs of 1-3 digits,
 * each octet 0..255? */
static int is_ipv4_span(const char *s, size_t len)
{
    size_t i = 0;
    int octets = 0, digits = 0;
    long val = 0;

    while (i < len) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            digits++;
            val = val * 10 + (c - '0');
            if (digits > 3 || val > 255) return 0;
            i++;
            continue;
        }
        if (c == '.') {
            if (digits == 0) return 0;
            octets++;
            if (octets > 3) return 0;
            digits = 0;
            val = 0;
            i++;
            continue;
        }
        return 0;
    }
    return (digits > 0 && octets == 3);
}

/* Copy the quad at [start, end) of text into ip, bounded by n. */
static void copy_quad(const char *text, size_t start, size_t end,
                      char *ip, size_t n)
{
    size_t k = 0;

    while (start + k < end && k + 1 < n) {
        ip[k] = text[start + k];
        k++;
    }
    ip[k] = '\0';
}

/* Scan [from, eol) for the first dotted quad; returns 1 and copies
 * it into ip. */
static int first_quad_in_range(const char *text, const char *from,
                               const char *eol, char *ip, size_t n)
{
    const char *q;

    for (q = from; q < eol; q++) {
        size_t start, end;

        if (*q < '0' || *q > '9') continue;
        /* extend to the maximal digit/dot run ending in a digit */
        start = (size_t)(q - text);
        end = start;
        while (end < (size_t)(eol - text) &&
               ((text[end] >= '0' && text[end] <= '9') || text[end] == '.')) {
            end++;
        }
        while (end > start && text[end - 1] == '.') end--;
        if (is_ipv4_span(text + start, end - start)) {
            copy_quad(text, start, end, ip, n);
            return 1;
        }
        q = text + end;   /* skip the scanned run */
    }
    return 0;
}

int tn_parse_nslookup_answer(const char *text, char *ip, size_t n)
{
    const char *line = text;

    if (!text || !ip || n == 0) return 0;
    ip[0] = '\0';

    while (*line != '\0') {
        size_t llen = 0;
        const char *eol;

        while (line[llen] != '\0' && line[llen] != '\n' && line[llen] != '\r') llen++;
        eol = line + llen;

        /* skip the Server: line — it names the DNS server, not the answer */
        if (strncmp(line, "Server:", 7) != 0 &&
            first_quad_in_range(text, line, eol, ip, n)) {
            return 1;
        }
        line = eol;
        while (*line == '\n' || *line == '\r') line++;
    }
    return 0;
}

/* 11x item 3 T2: first dotted quad anywhere in simple command
 * output. "0.0.0.0" is returned as-is — the caller decides
 * whether it is a usable value. */
int tn_parse_first_ipv4(const char *text, char *ip, size_t n)
{
    if (!text || !ip || n == 0) return 0;
    ip[0] = '\0';
    return first_quad_in_range(text, text, text + strlen(text), ip, n);
}
