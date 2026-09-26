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

/* append QNAME; returns end offset or -1 */
static int dns_put_qname(UBYTE *q, const char *name, int is_ptr)
{
    char tmp[280];
    int n = 0, o = 0;

    if (is_ptr) {
        const char *p = name;
        char parts[4][4];
        int np[4] = {0,0,0,0}, cnt = 0;
        while (*p && cnt < 4) {
            if (*p == '.') { cnt++; p++; continue; }
            if (np[cnt] < 3) parts[cnt][np[cnt]++] = *p;
            p++;
        }
        for (cnt = 3; cnt >= 0; cnt--) {
            int j;
            for (j = np[cnt] - 1; j >= 0; j--) tmp[n++] = parts[cnt][j];
            if (cnt > 0) tmp[n++] = '.';
        }
        strcpy(tmp + n, "in-addr.arpa");
        n += 12;
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

/* parse the first matching answer; supports compressed names */
static int dns_parse_reply(const UBYTE *pkt, LONG plen, UWORD want_type,
                           char *out, LONG out_size)
{
    LONG off = 12;
    UWORD qdcount, ancount;
    int an;

    if (plen < 12) return -1;
    qdcount = (UWORD)((pkt[4] << 8) | pkt[5]);
    ancount = (UWORD)((pkt[6] << 8) | pkt[7]);
    if (ancount == 0) return -1;

    {
        int qd;
        for (qd = 0; qd < qdcount; qd++) {
            while (off < plen && pkt[off] != 0) {
                if (pkt[off] & 0xC0) { off += 2; goto qdone; }
                off += pkt[off] + 1;
            }
            off += 1;
            off += 4;
        }
    }
qdone:

    for (an = 0; an < ancount; an++) {
        UWORD type, rdlength;
        LONG rdo;
        LONG p = off;
        int guard = 0;

        while (p < plen) {
            UBYTE l = pkt[p];
            if (l == 0) { p += 1; break; }
            if ((l & 0xC0) == 0xC0) { p += 2; break; }
            p += l + 1;
        }
        off = p;
        if (off + 10 > plen) return -1;
        type = (UWORD)((pkt[off] << 8) | pkt[off + 1]);
        rdlength = (UWORD)((pkt[off + 8] << 8) | pkt[off + 9]);
        rdo = off + 10;
        if (rdo + rdlength > plen) return -1;

        if (type == DNS_TYPE_A && want_type == DNS_TYPE_A && rdlength == 4) {
            ULONG a = ((ULONG)pkt[rdo] << 24) | ((ULONG)pkt[rdo + 1] << 16) |
                      ((ULONG)pkt[rdo + 2] << 8) | (ULONG)pkt[rdo + 3];
            tn_cmd_ip_to_str(a, out);
            return 0;
        }
        if (type == DNS_TYPE_PTR && want_type == DNS_TYPE_PTR) {
            LONG p2 = rdo;
            int guard2 = 0, o = 0;
            while (p2 < plen) {
                UBYTE l = pkt[p2];
                if (l == 0) break;
                if ((l & 0xC0) == 0xC0) {
                    p2 = ((l & 0x3F) << 8) | pkt[p2 + 1];
                    if (++guard2 > 32) return -1;
                    continue;
                }
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
    strcpy(qname, name);

    if (server != NULL || qtype == DNS_TYPE_PTR) {
        UBYTE tx[512];
        UBYTE rx[512];
        struct sockaddr_in dst, from;
        LONG fd, qn, txlen, got, tries;
        UWORD txid;
        char out[256];
        int done = 0;

        if (server == NULL) server = "10.0.2.2"; /* PTR default: slirp host */
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
                    tn_cmd_printf("Address%d:   %s\n", (LONG)(i + 1), tn_call_inet_ntoa(a));
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
