/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — nslookup / host command (CMD-1).
 * ReadArgs: NAME/A,SERVER,PORT/K/N
 *
 * z.ai step 9b item 4: with SERVER (or for PTR) the query is built here
 * over UDP (id, RD, one question; PTR = d.c.b.a.in-addr.arpa), 3 s x 2
 * tries, answers parsed with name-compression support. Plain A without
 * SERVER still uses gethostbyname.
 */
#include "cmdlib.h"
#include <string.h>
TN_VERSTAG_DEF("nslookup");

#define TEMPLATE "NAME/A,SERVER,PORT/K/N"

#define DNS_TYPE_A   1
#define DNS_TYPE_PTR 12
#define DNS_CLASS_IN 1

static int dns_is_ipv4(const char *s)
{
    int part, len = 0;
    for (part = 0; part < 4; part++) {
        int digits = 0, v = 0;
        while (s[len] >= '0' && s[len] <= '9') {
            v = v * 10 + (s[len] - '0');
            if (v > 255) return 0;
            len++; digits++;
            if (digits > 3) return 0;
        }
        if (digits == 0) return 0;
        if (part < 3) {
            if (s[len] != '.') return 0;
            len++;
        }
    }
    return s[len] == '\0';
}

/* skip a (possibly compressed) name; returns offset past it or -1 */
static LONG dns_skip_name(const UBYTE *pkt, LONG plen, LONG off)
{
    while (off < plen) {
        UBYTE l = pkt[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) {
            if (off + 1 >= plen) return -1;
            return off + 2;
        }
        if ((l & 0xC0) != 0) return -1; /* 6.4: 0x40/0x80 label types */
        off += 1 + l;
    }
    return -1;
}

/* append QNAME; returns end offset or -1 */
static int dns_put_qname(UBYTE *q, const char *name, int is_ptr)
{
    char tmp[280];
    int n = 0, o = 0;

    if (is_ptr) {
        /* a.b.c.d -> d.c.b.a.in-addr.arpa (digits are NOT reversed) */
        int o1 = -1, o2 = -1, o3 = -1, o4 = -1;
        int vals[4] = {0, 0, 0, 0};
        int part = 0, idx = 0;
        const char *s = name;
        while (*s && part < 4) {
            if (*s == '.') { part++; idx = 0; s++; continue; }
            if (*s < '0' || *s > '9') return -1;
            vals[part] = vals[part] * 10 + (*s - '0');
            idx++;
            if (idx > 3) return -1;
            s++;
        }
        if (part != 3) return -1;
        o1 = vals[3]; o2 = vals[2]; o3 = vals[1]; o4 = vals[0];
        n = 0;
        tmp[n++] = (char)('0' + (o1 / 100) % 10);
        if (o1 >= 100) { }
        {
            char one[4];
            int k = 0;
            int vv;
            /* emit each octet as decimal, dot separated */
            n = 0;
            vv = o1;
            one[0] = (char)('0' + vv / 100); one[1] = (char)('0' + (vv / 10) % 10);
            one[2] = (char)('0' + vv % 10); one[3] = 0;
            if (vv >= 100) { tmp[n++] = one[0]; tmp[n++] = one[1]; tmp[n++] = one[2]; }
            else if (vv >= 10) { tmp[n++] = one[1]; tmp[n++] = one[2]; }
            else tmp[n++] = one[2];
            tmp[n++] = '.';
            vv = o2;
            if (vv >= 100) { tmp[n++] = (char)('0' + vv / 100); tmp[n++] = (char)('0' + (vv / 10) % 10); tmp[n++] = (char)('0' + vv % 10); }
            else if (vv >= 10) { tmp[n++] = (char)('0' + vv / 10); tmp[n++] = (char)('0' + vv % 10); }
            else tmp[n++] = (char)('0' + vv);
            tmp[n++] = '.';
            vv = o3;
            if (vv >= 100) { tmp[n++] = (char)('0' + vv / 100); tmp[n++] = (char)('0' + (vv / 10) % 10); tmp[n++] = (char)('0' + vv % 10); }
            else if (vv >= 10) { tmp[n++] = (char)('0' + vv / 10); tmp[n++] = (char)('0' + vv % 10); }
            else tmp[n++] = (char)('0' + vv);
            tmp[n++] = '.';
            vv = o4;
            if (vv >= 100) { tmp[n++] = (char)('0' + vv / 100); tmp[n++] = (char)('0' + (vv / 10) % 10); tmp[n++] = (char)('0' + vv % 10); }
            else if (vv >= 10) { tmp[n++] = (char)('0' + vv / 10); tmp[n++] = (char)('0' + vv % 10); }
            else tmp[n++] = (char)('0' + vv);
            (void)k;
        }
        strcpy(tmp + n, ".in-addr.arpa");
        n += 13;
    } else {
        while (name[n] && n < (int)sizeof(tmp) - 14) { tmp[n] = name[n]; n++; }
    }
    tmp[n] = '\0';

    {
        const char *p2 = tmp;
        while (*p2) {
            const char *dot = strchr(p2, '.');
            int l = dot ? (int)(dot - p2) : (int)strlen(p2);
            if (l == 0 || l > 63) return -1;
            q[o++] = (UBYTE)l;
            memcpy(q + o, p2, l);
            o += l;
            if (!dot) break;
            p2 = dot + 1;
        }
        q[o++] = 0;
    }
    return o;
}

/* parse the first matching answer; supports compressed names.
 * diag: on failure prints one '#' line with the reply length. */
static int dns_parse_reply(const UBYTE *pkt, LONG plen, UWORD want_type,
                           char *out, LONG out_size)
{
    LONG off;
    UWORD qdcount, ancount;
    int an;

    if (plen < 12) return -1;
    qdcount = (UWORD)((pkt[4] << 8) | pkt[5]);
    ancount = (UWORD)((pkt[6] << 8) | pkt[7]);
    if (ancount == 0) return -1;

    off = 12;
    {
        int qd;
        for (qd = 0; qd < qdcount; qd++) {
            off = dns_skip_name(pkt, plen, off);
            if (off < 0) return -1;
            off += 4;
        }
    }

    for (an = 0; an < ancount; an++) {
        UWORD type, rdlength;
        LONG rdo;
        off = dns_skip_name(pkt, plen, off);
        if (off < 0 || off + 10 > plen) return -1;
        type = (UWORD)((pkt[off] << 8) | pkt[off + 1]);
        rdlength = (UWORD)((pkt[off + 8] << 8) | pkt[off + 9]);
        rdo = off + 10;
        if (rdo + rdlength > plen) return -1;

        if (type == want_type && rdlength == 4 && want_type == DNS_TYPE_A) {
            ULONG a = ((ULONG)pkt[rdo] << 24) | ((ULONG)pkt[rdo + 1] << 16) |
                      ((ULONG)pkt[rdo + 2] << 8) | (ULONG)pkt[rdo + 3];
            tn_cmd_ip_to_str(a, out);
            return 0;
        }
        if (type == DNS_TYPE_PTR && want_type == DNS_TYPE_PTR) {
            /* 6.4: every read is bounded by lim - the RDATA end until
             * the first compression pointer, the packet end after it. */
            LONG p2 = rdo;
            LONG lim = rdo + rdlength;
            int guard2 = 0, o = 0;
            for (;;) {
                UBYTE l;
                if (p2 >= lim) return -1;
                l = pkt[p2];
                if (l == 0) break;
                if ((l & 0xC0) == 0xC0) {
                    if (p2 + 1 >= lim) return -1;
                    p2 = ((l & 0x3F) << 8) | pkt[p2 + 1];
                    lim = plen;
                    if (++guard2 > 32) return -1;
                    continue;
                }
                if ((l & 0xC0) != 0) return -1; /* 0x40/0x80 label types */
                if (p2 + 1 + l > lim) return -1;
                if (o > 0 && o < out_size - 1) out[o++] = '.';
                {
                    LONG k;
                    for (k = 0; k < l && o < out_size - 1; k++) out[o++] = (char)pkt[p2 + 1 + k];
                }
                p2 += 1 + l;
            }
            out[o] = '\0';
            return 0;
        }
        off = rdo + rdlength;
    }
    return -1;
}

int main(int argc, char **argv)
{
    LONG opts[3] = { 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    const char *name;
    const char *server = NULL;
    LONG port = 53;
    ULONG server_addr = 0;
    UWORD qtype;
    char qname[280];

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"nslookup");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    name = (const char *)opts[0];
    server = (opts[1] != 0) ? (const char *)opts[1] : NULL;
    if (opts[2] != 0) port = *(LONG *)opts[2];
    if (port <= 0 || port > 65535) port = 53;

    /* plain dotted-quad without SERVER keeps the old round-trip:
     * net_inet_ntoa row expects Address: <ip> without any DNS. */
    if (server == NULL && dns_is_ipv4(name)) {
        ULONG addr = tn_call_inet_addr(name);
        if (addr != INADDR_NONE) {
            struct in_addr ia;
            ia.s_addr = addr;
            tn_cmd_printf("Name:      %s\nAddress:   %s\n", name, tn_call_inet_ntoa(ia));
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_OK;
        }
    }

    if (dns_is_ipv4(name)) qtype = DNS_TYPE_PTR;
    else qtype = DNS_TYPE_A;
    /* 6.3: NAME is unbounded user input; a DNS name is <= 253 chars */
    if (strlen(name) > 253) {
        tn_cmd_printf("** name too long (max 253)\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    strcpy(qname, name);

    if (server != NULL || qtype == DNS_TYPE_PTR) {
        UBYTE tx[512];
        UBYTE rx[512];
        struct sockaddr_in dst, from;
        LONG fd, qn, txlen, got, tries;
        UWORD txid;
        char out[256];
        int done = 0;

        if (server == NULL) {
            /* 11x item 4: PTR without SERVER asks the daemon's own
             * configured DNS - the first non-zero of dns1/dns2, the
             * same values C:GetNetStatus DNS prints - never a
             * hardcoded slirp address. */
            static TnSnapshot snap;
            static char server_buf[16];
            const UBYTE *d;
            ULONG raw;

            if (tn_cmd_snapshot(&snap) != 0) {
                tn_cmd_printf("no DNS server configured - use SERVER <ip>\n");
                FreeArgs(rdargs); tn_cmd_fini();
                return TN_CMD_FAIL;
            }
            d = (snap.status.dns1[0] | snap.status.dns1[1] |
                 snap.status.dns1[2] | snap.status.dns1[3])
                ? snap.status.dns1 : snap.status.dns2;
            if ((d[0] | d[1] | d[2] | d[3]) == 0) {
                tn_cmd_printf("no DNS server configured - use SERVER <ip>\n");
                FreeArgs(rdargs); tn_cmd_fini();
                return TN_CMD_FAIL;
            }
            raw = ((ULONG)d[0] << 24) | ((ULONG)d[1] << 16) |
                  ((ULONG)d[2] << 8) | (ULONG)d[3];
            tn_cmd_ip_to_str(raw, server_buf);
            server = server_buf;
        }
        server_addr = tn_cmd_resolve(server);
        if (server_addr == INADDR_NONE) {
            tn_cmd_printf("** can't resolve server %s\n", server);
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }

        fd = tn_call_socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

        memset(&dst, 0, sizeof(dst));
        dst.sin_len = sizeof(dst);
        dst.sin_family = AF_INET;
        dst.sin_port = htons((UWORD)port);
        dst.sin_addr.s_addr = server_addr;

        txid = (UWORD)(((ULONG)FindTask(NULL) >> 4) & 0xFFFF);
        if (txid == 0) txid = 0x4242;

        memset(tx, 0, sizeof(tx));
        tx[0] = (UBYTE)(txid >> 8); tx[1] = (UBYTE)(txid & 0xFF);
        tx[2] = 0x01; tx[3] = 0x00; /* RD */
        tx[4] = 0; tx[5] = 1;       /* qdcount */
        qn = dns_put_qname(tx + 12, qname, qtype == DNS_TYPE_PTR);
        if (qn < 0) {
            tn_cmd_printf("** bad name\n");
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        txlen = 12 + qn;
        tx[txlen++] = 0; tx[txlen++] = (UBYTE)qtype;
        tx[txlen++] = 0; tx[txlen++] = (UBYTE)DNS_CLASS_IN;

        tn_cmd_printf("Server: %s:%ld\n", server, port);

        for (tries = 0; tries < 2 && !done; tries++) {
            fd_set rfds;
            struct timeval tv;
            LONG fromlen;
            LONG sel;
            if (tn_call_sendto(fd, tx, txlen, 0, (struct sockaddr *)&dst, sizeof(dst)) != txlen) {
                tn_cmd_printf("** send failed (errno=%ld)\n", tn_call_errno());
                break;
            }
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_secs = 3;
            tv.tv_micro = 0;
            sel = tn_call_waitselect(fd + 1, &rfds, NULL, NULL, &tv, NULL);
            if (sel <= 0) {
                tn_cmd_printf("** try %ld: waitselect=%ld\n", (LONG)(tries + 1), sel);
                continue;
            }
            fromlen = (LONG)sizeof(from);
            got = tn_call_recvfrom(fd, rx, sizeof(rx), 0, (struct sockaddr *)&from, &fromlen);
            if (got < 12) {
                tn_cmd_printf("** try %ld: recv got=%ld\n", (LONG)(tries + 1), got);
                continue;
            }
            /* 6.4: only the server we asked may answer */
            if (from.sin_addr.s_addr != dst.sin_addr.s_addr ||
                from.sin_port != dst.sin_port) {
                tn_cmd_printf("** try %ld: reply from unexpected source\n", (LONG)(tries + 1));
                continue;
            }
            if (rx[0] != tx[0] || rx[1] != tx[1]) {
                tn_cmd_printf("** try %ld: txid mismatch\n", (LONG)(tries + 1));
                continue;
            }
            if (dns_parse_reply(rx, got, qtype, out, (LONG)sizeof(out)) == 0) {
                done = 1;
                if (qtype == DNS_TYPE_A) {
                    tn_cmd_printf("Name:      %s\nAddress:   %s\n", name, out);
                } else {
                    tn_cmd_printf("Name:      %s\nHostname:  %s\n", name, out);
                }
            } else {
                LONG k;
                tn_cmd_printf("** parse failed got=%ld an=%ld qd=%ld bytes:",
                              got, (LONG)((rx[6] << 8) | rx[7]),
                              (LONG)((rx[4] << 8) | rx[5]));
                for (k = 0; k < got && k < 32; k++) {
                    tn_cmd_printf(" %02lx", (ULONG)rx[k]);
                }
                tn_cmd_printf("\n");
            }
        }
        tn_call_closesocket(fd);
        if (!done) {
            if (tries >= 2) tn_cmd_printf("** no response from %s:%ld\n", server, port);
            rc = TN_CMD_FAIL;
        }
        FreeArgs(rdargs);
        tn_cmd_fini();
        return rc;
    }

    /* plain A lookup via gethostbyname */
    {
        ULONG addr = tn_call_inet_addr(name);
        if (addr != INADDR_NONE) {
            struct in_addr ia;
            ia.s_addr = addr;
            tn_cmd_printf("Name:      %s\nAddress:   %s\n", name, tn_call_inet_ntoa(ia));
        } else {
            struct hostent *he = tn_call_gethostbyname(name);
            if (he == NULL || he->h_addr_list[0] == NULL) {
                tn_cmd_printf("** %s doesn't exist\n", name);
                rc = TN_CMD_FAIL;
            } else {
                int i;
                for (i = 0; he->h_addr_list[i] != NULL && i < 4; i++) {
                    struct in_addr a;
                    memcpy(&a, he->h_addr_list[i], 4);
                    tn_cmd_printf("Address%ld:   %s\n", (LONG)(i + 1), tn_call_inet_ntoa(a));

                }
                if (he->h_name != NULL) {
                    tn_cmd_printf("Canonical: %s\n", he->h_name);
                }
            }
        }
    }

    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
