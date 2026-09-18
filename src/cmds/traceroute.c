/*
 * tolunnet — traceroute command (CMD-3). ReadArgs: HOST/A,MAXHOPS/N,QUERIES/N,WAIT/N,NUMERIC/S
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "HOST/A,MAXHOPS/N,QUERIES/N,WAIT/N,NUMERIC/S"
#define ICMP_ECHO_REQ 8
#define ICMP_ECHO_REP 0
#define ICMP_TTL_EXC   11
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

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 30, 3, 3, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    ULONG addr;
    LONG icmp_fd;
    LONG udp_fd;
    LONG ttl;
    LONG hop;
    LONG probe;
    LONG reached = 0;
    struct sockaddr_in dst;
    static char txpkt[64];
    static char rxbuf[2048];
    struct icmp_hdr *icmph;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"traceroute");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    LONG maxhops = (opts[1] > 0 && opts[1] < 64) ? opts[1] : 30;
    LONG queries = (opts[2] > 0 && opts[2] < 10) ? opts[2] : 3;
    LONG wait_s  = (opts[3] > 0 && opts[3] < 30) ? opts[3] : 3;
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

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = addr;
    dst.sin_port = htons(UDP_PROBE_PORT);

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
            /* Send UDP probe with current TTL */
            {
                LONG ttl_set = hop;
                /* setsockopt via inline would be needed; for now use the
                 * raw socket for receive and just send UDP datagrams */
                tn_call_send(udp_fd, "probe", 5, 0);
            }

            /* Wait for ICMP response */
            FD_ZERO(&rfds);
            FD_SET(icmp_fd, &rfds);
            tv.tv_secs = wait_s;
            tv.tv_micro = 0;
            got = tn_call_waitselect(icmp_fd + 1, &rfds, NULL, NULL, &tv, NULL);

            if (got <= 0) {
                tn_cmd_printf("* ");
                Flush(Output());
                continue;
            }

            got = tn_call_recv(icmp_fd, rxbuf, sizeof(rxbuf), 0);
            if (got > ICMP_HDR_LEN + 20) {
                /* Parse IP header + ICMP to get source */
                struct ip_hdr_fake {
                    UBYTE vhl;
                    UBYTE tos;
                    UWORD len;
                    UWORD id;
                    UWORD frag;
                    UBYTE ttl;
                    UBYTE proto;
                    UWORD chksum;
                    ULONG src;
                    ULONG dst;
                };
                struct ip_hdr_fake *iph = (struct ip_hdr_fake *)(void *)rxbuf;
                icmph = (struct icmp_hdr *)(void *)(rxbuf + ((iph->vhl & 0xF) * 4));
                rx_addr = iph->src;

                if (rx_addr == addr) {
                    reached = 1;
                }
                tn_cmd_printf("%s ", tn_call_inet_ntoa(*(struct in_addr *)&rx_addr));
                Flush(Output());
                break; /* got a response for this hop */
            }
        }

        /* Try reverse DNS unless NUMERIC */
        if (rx_addr != 0 && !numeric) {
            struct hostent *he = tn_call_gethostbyaddr((const char *)&rx_addr, 4, AF_INET);
            if (he != NULL && he->h_name != NULL) {
                tn_cmd_printf("[%s]", he->h_name);
            }
        }
        tn_cmd_printf("\n");

        if (tn_cmd_check_ctrlc()) { rc = TN_CMD_WARN; break; }
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
