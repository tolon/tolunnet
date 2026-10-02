/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — traceroute command (CMD-3). ReadArgs: HOST/A,MAXHOPS/N,QUERIES/N,WAIT/N,NUMERIC/S
 */
#include "cmdlib.h"
#include <string.h>
TN_VERSTAG_DEF("traceroute");

#define TEMPLATE "HOST/A,MAXHOPS/N,QUERIES/N,WAIT/N,NUMERIC/S"
#define ICMP_ECHO_REQ 8
#define ICMP_ECHO_REP 0
#define ICMP_TTL_EXC   11
#define ICMP_DEST_UNR  3
#define ICMP_HDR_LEN   8
#define UDP_PROBE_PORT 33434

struct icmp_hdr {
    UBYTE type;
    UBYTE code;
    UWORD chksum;
    UWORD id;
    UWORD seq;
};

static UWORD in_cksum(const UWORD *buf, LONG len)
{
    ULONG sum = 0;
    while (len > 1) { sum += *buf++; len -= 2; }
    if (len == 1) sum += *(const UBYTE *)buf;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (UWORD)(~sum);
}

/* 1/50 s ticks since midnight (DateStamp); callers handle the wrap. */
static LONG tr_ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Minute * 3000L + ds.ds_Tick;
}

/* 6.9: does this raw ICMP datagram answer OUR probe? Only Time
 * Exceeded (11) or Destination Unreachable (3) whose quoted datagram
 * is UDP to target:dport counts. Byte reads only (68000 alignment).
 * Returns the ICMP type, or -1 to ignore the packet. */
static int tr_match(const UBYTE *p, LONG len, ULONG target, UWORD dport,
                    ULONG *src)
{
    LONG ihl, in, inner_ihl;
    UBYTE type;
    ULONG idst;

    if (len < 20 || (p[0] >> 4) != 4 || p[9] != 1) return -1;
    ihl = (LONG)(p[0] & 0x0F) * 4;
    if (ihl < 20 || len < ihl + ICMP_HDR_LEN + 20) return -1;
    type = p[ihl];
    if (type != ICMP_TTL_EXC && type != ICMP_DEST_UNR) return -1;
    in = ihl + ICMP_HDR_LEN;               /* quoted IP header */
    inner_ihl = (LONG)(p[in] & 0x0F) * 4;
    if (inner_ihl < 20 || p[in + 9] != 17) return -1;
    if (len < in + inner_ihl + 4) return -1; /* need the UDP ports */
    memcpy(&idst, p + in + 16, 4);
    if (idst != target) return -1;
    if ((UWORD)((p[in + inner_ihl + 2] << 8) | p[in + inner_ihl + 3]) != dport) return -1;
    memcpy(src, p + 12, 4);
    return type;
}

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 0, 0, 0, 0 }; /* /N slots are pointers: start at 0 */
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    ULONG addr;
    LONG icmp_fd;
    LONG udp_fd;
    LONG ttl;
    LONG hop;
    LONG probe;
    LONG reached = 0;
    BOOL broke = FALSE;
    struct sockaddr_in dst;
    static char txpkt[64];
    static char rxbuf[2048];

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"traceroute");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    /* apply defaults after ReadArgs (z.ai step 8a item 3) */
    LONG maxhops = (opts[1] != 0 && *(LONG *)opts[1] > 0 && *(LONG *)opts[1] < 64)
                       ? *(LONG *)opts[1] : 30;
    LONG queries = (opts[2] != 0 && *(LONG *)opts[2] > 0 && *(LONG *)opts[2] < 10)
                       ? *(LONG *)opts[2] : 3;
    LONG wait_s  = (opts[3] != 0 && *(LONG *)opts[3] > 0 && *(LONG *)opts[3] < 30)
                       ? *(LONG *)opts[3] : 3;
    LONG numeric = opts[4];

    const char *host = (const char *)opts[0];
    addr = tn_cmd_resolve(host);
    if (addr == INADDR_NONE) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    tn_cmd_printf("traceroute to %s, %ld hops max\n", host, maxhops);

    icmp_fd = tn_call_socket(AF_INET, SOCK_RAW, 1);
    if (icmp_fd < 0) {
        tn_cmd_printf("traceroute: raw socket failed (errno=%ld)\n", tn_call_errno());
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    udp_fd = tn_call_socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        tn_call_closesocket(icmp_fd);
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    /* 6.9: unconnected - every probe goes to its own destination port
     * (UDP_PROBE_PORT + sequence) so a reply quoting it names its probe */
    memset(&dst, 0, sizeof(dst));
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = addr;

    for (hop = 1; hop <= maxhops && !reached; hop++) {
        struct sockaddr_in from;
        LONG from_len = sizeof(from);
        LONG got;
        struct timeval tv;
        fd_set rfds;
        ULONG rx_addr = 0;
        char name_buf[64];
        name_buf[0] = '\0';

        tn_cmd_printf("%2ld: ", hop);
        Flush(Output());

        for (probe = 0; probe < queries; probe++) {
            UWORD dport = (UWORD)(UDP_PROBE_PORT + (hop - 1) * queries + probe);
            LONG start, left;
            int type = -1;

            if (tn_cmd_check_ctrlc()) { broke = TRUE; break; }

            /* Send UDP probe with current TTL */
            {
                LONG ttl_set = hop;
                tn_call_setsockopt(udp_fd, IPPROTO_IP, IP_TTL, &ttl_set, sizeof(ttl_set));
                dst.sin_port = htons(dport);
                tn_call_sendto(udp_fd, "probe", 5, 0, (struct sockaddr *)&dst, sizeof(dst));
            }

            /* Wait for the ICMP reply to THIS probe; anything else (late
             * replies to earlier probes, ping replies, ...) is dropped
             * and the wait continues until the deadline. */
            start = tr_ticks();
            left = wait_s * 50L;
            while (left > 0) {
                ULONG src = 0;
                FD_ZERO(&rfds);
                FD_SET(icmp_fd, &rfds);
                tv.tv_secs = left / 50;
                tv.tv_micro = (left % 50) * 20000L;
                got = tn_call_waitselect(icmp_fd + 1, &rfds, NULL, NULL, &tv, NULL);
                if (got <= 0) break;
                got = tn_call_recv(icmp_fd, rxbuf, sizeof(rxbuf), 0);
                if (got > 0) {
                    type = tr_match((const UBYTE *)rxbuf, got, addr, dport, &src);
                    if (type >= 0) { rx_addr = src; break; }
                }
                {
                    LONG el = tr_ticks() - start;
                    if (el < 0) el += 24L * 60 * 3000; /* midnight wrap */
                    left = wait_s * 50L - el;
                }
            }

            if (type < 0) {
                tn_cmd_printf("* ");
                Flush(Output());
                continue;
            }

            if (rx_addr == addr) {
                reached = 1;
            }
            {
                struct in_addr ia;
                ia.s_addr = rx_addr;
                tn_cmd_printf("%s ", tn_call_inet_ntoa(ia));
            }
            Flush(Output());
            break; /* got a response for this hop */
        }

        /* Try reverse DNS unless NUMERIC */
        if (rx_addr != 0 && !numeric) {
            struct hostent *he = tn_call_gethostbyaddr((const char *)&rx_addr, 4, AF_INET);
            if (he != NULL && he->h_name != NULL) {
                tn_cmd_printf("[%s]", he->h_name);
            }
        }
        tn_cmd_printf("\n");

        if (broke || tn_cmd_check_ctrlc()) { rc = TN_CMD_WARN; break; }
    }

    if (!reached && rc == TN_CMD_OK) {
        tn_cmd_printf("traceroute: target not reached in %ld hops\n", maxhops);
        rc = TN_CMD_WARN;
    }

    tn_call_closesocket(icmp_fd);
    tn_call_closesocket(udp_fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
