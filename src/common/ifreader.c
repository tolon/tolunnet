/*
 * tolunnet — Roadshow-style interfaces file parser (CLOSE §B.7).
 *
 * Pure string parsing, no AmigaOS/lwIP headers: host-testable.
 */
#include "ifreader.h"

#include <string.h>

static int tn_if_copy(char *dst, int dstsize, const char *src, int srclen)
{
    int i;
    if (srclen >= dstsize) srclen = dstsize - 1;
    for (i = 0; i < srclen; i++) dst[i] = src[i];
    dst[srclen] = '\0';
    return srclen;
}

static int tn_if_parse_long(const char *s, int len, long *out)
{
    long v = 0;
    int digits = 0;
    while (len > 0 && (*s == ' ' || *s == '\t')) { s++; len--; }
    while (len > 0 && *s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
        len--;
        digits++;
    }
    if (digits == 0) return 0;
    *out = v;
    return 1;
}

static int tn_if_yes(const char *s, int len)
{
    if (len == 3 &&
        (s[0] == 'Y' || s[0] == 'y') &&
        (s[1] == 'E' || s[1] == 'e') &&
        (s[2] == 'S' || s[2] == 's')) return 1;
    if (len == 1 && (s[0] == '1')) return 1;
    return 0;
}

static void tn_if_entry_init(TnIfEntry *e)
{
    memset(e, 0, sizeof(*e));
    e->unit = -1;
}

/* Apply one KEY=VALUE token to the current entry. */
static void tn_if_apply_kv(TnIfEntry *e, const char *key, int klen,
                           const char *val, int vlen)
{
    if (klen == 6 && strncmp(key, "DEVICE", 6) == 0) {
        tn_if_copy(e->device, sizeof(e->device), val, vlen);
    } else if (klen == 4 && strncmp(key, "UNIT", 4) == 0) {
        long u;
        if (tn_if_parse_long(val, vlen, &u)) e->unit = u;
    } else if (klen == 7 && strncmp(key, "ADDRESS", 7) == 0) {
        tn_if_copy(e->address, sizeof(e->address), val, vlen);
    } else if (klen == 7 && strncmp(key, "NETMASK", 7) == 0) {
        tn_if_copy(e->netmask, sizeof(e->netmask), val, vlen);
    } else if (klen == 7 && strncmp(key, "GATEWAY", 7) == 0) {
        tn_if_copy(e->gateway, sizeof(e->gateway), val, vlen);
    } else if (klen == 4 && strncmp(key, "DHCP", 4) == 0) {
        e->dhcp = tn_if_yes(val, vlen);
    }
    /* unknown keys ignored */
}

int tn_if_parse_lines(const char *text, TnIfEntry *out, int max)
{
    int count = 0;
    int cur = -1; /* index of the interface being continued */
    const char *p = text;

    if (text == NULL || out == NULL || max <= 0) return -1;

    while (*p) {
        const char *line = p;
        const char *eol = p;
        int len;
        while (*eol && *eol != '\n') eol++;
        len = (int)(eol - line);
        p = (*eol) ? eol + 1 : eol;

        /* strip trailing CR */
        while (len > 0 && (line[len - 1] == '\r')) len--;

        /* comment / blank */
        if (len == 0 || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == ' ' || line[0] == '\t') {
            /* continuation: KEY=VALUE for the current interface */
            if (cur < 0) continue;
            {
                const char *q = line;
                int rem = len;
                while (rem > 0) {
                    while (rem > 0 && (*q == ' ' || *q == '\t')) { q++; rem--; }
                    if (rem <= 0) break;
                    {
                        const char *tok = q;
                        int tlen = 0;
                        while (rem > 0 && *q != ' ' && *q != '\t') { q++; rem--; tlen++; }
                        /* tok: KEY=VALUE */
                        {
                            const char *eq = tok;
                            int pos = 0;
                            while (pos < tlen && eq[pos] != '=') pos++;
                            if (pos < tlen) {
                                tn_if_apply_kv(&out[cur], tok, pos,
                                               tok + pos + 1, tlen - pos - 1);
                            }
                        }
                    }
                }
            }
        } else {
            /* new interface block: name KEY=VALUE ... */
            if (count >= max) break;
            cur = count++;
            tn_if_entry_init(&out[cur]);
            {
                const char *q = line;
                int rem = len;
                /* first token is the name (no '=') */
                while (rem > 0 && *q != ' ' && *q != '\t') { q++; rem--; }
                {
                    int nlen = (int)(q - line);
                    char nm[16];
                    tn_if_copy(nm, sizeof(nm), line, nlen);
                    tn_if_copy(out[cur].name, sizeof(out[cur].name), nm, (int)strlen(nm));
                }
                /* remaining tokens are KEY=VALUE */
                while (rem > 0) {
                    while (rem > 0 && (*q == ' ' || *q == '\t')) { q++; rem--; }
                    if (rem <= 0) break;
                    {
                        const char *tok = q;
                        int tlen = 0;
                        while (rem > 0 && *q != ' ' && *q != '\t') { q++; rem--; tlen++; }
                        {
                            const char *eq = tok;
                            int pos = 0;
                            while (pos < tlen && eq[pos] != '=') pos++;
                            if (pos < tlen) {
                                tn_if_apply_kv(&out[cur], tok, pos,
                                               tok + pos + 1, tlen - pos - 1);
                            }
                        }
                    }
                }
            }
        }
    }
    return count;
}
