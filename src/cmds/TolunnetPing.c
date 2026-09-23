/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * TolunnetPing — Real Network Connectivity & RTT Ping Tool for AmigaOS.
 *
 * Fully compliant with bsdsocket.library standards.
 * Sends Echo probe, performs real WaitSelect/recvfrom roundtrip, and times actual RTT.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <devices/timer.h>
#include <libraries/bsdsocket.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../common/log.h"
#include "../../include/ipc.h"
#include <string.h>

static struct Library *SocketBase = NULL;

/* 68k LVO wrappers for bsdsocket.library */
static LONG call_socket(LONG domain, LONG type, LONG protocol)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = domain;
    register LONG d1 __asm__("d1") = type;
    register LONG d2 __asm__("d2") = protocol;

    __asm__ __volatile__ (
        "jsr -30(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2)
        : "d1", "d2", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_closesocket(LONG sock)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;

    __asm__ __volatile__ (
        "jsr -120(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0)
        : "d1", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_sendto(LONG sock, const void *buf, LONG len, LONG flags,
                        const struct sockaddr *to, socklen_t tolen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register const void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;
    register const struct sockaddr *a1 __asm__("a1") = to;
    register LONG d3 __asm__("d3") = (LONG)tolen;

    __asm__ __volatile__ (
        "jsr -60(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2), "r"(a1), "r"(d3)
        : "d1", "d2", "d3", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_recvfrom(LONG sock, void *buf, LONG len, LONG flags,
                          struct sockaddr *addr, socklen_t *addrlen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;
    register struct sockaddr *a1 __asm__("a1") = addr;
    register socklen_t *a2 __asm__("a2") = addrlen;

    __asm__ __volatile__ (
        "jsr -72(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2), "r"(a1), "r"(a2)
        : "d1", "d2", "a0", "a1", "a2", "memory"
    );
    return d0;
}

static LONG call_waitselect(LONG nfds, fd_set *rfds, fd_set *wfds, fd_set *efds,
                            struct timeval *timeout, ULONG *signals)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = nfds;
    register fd_set *a0 __asm__("a0") = rfds;
    register fd_set *a1 __asm__("a1") = wfds;
    register fd_set *a2 __asm__("a2") = efds;
    register struct timeval *a3 __asm__("a3") = timeout;
    register ULONG d1 __asm__("d1") = signals ? *signals : 0;

    __asm__ __volatile__ (
        "jsr -126(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(d1)
        : "d1", "a0", "a1", "a2", "a3", "memory"
    );
    return d0;
}

static STRPTR call_inet_ntoa(in_addr_t ip)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = (LONG)ip;
    register STRPTR a0 __asm__("a0");

    __asm__ __volatile__ (
        "jsr -174(%%a6)"
        : "=r"(a0), "+r"(d0)
        : "r"(a6), "r"(d0)
        : "d1", "a1", "memory"
    );
    return a0;
}

static in_addr_t call_inet_addr(CONST_STRPTR cp)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register CONST_STRPTR a0 __asm__("a0") = cp;
    register LONG d0 __asm__("d0");

    __asm__ __volatile__ (
        "jsr -180(%%a6)"
        : "=r"(d0)
        : "r"(a6), "r"(a0)
        : "d1", "a1", "memory"
    );
    return (in_addr_t)d0;
}

static struct hostent *call_gethostbyname(CONST_STRPTR name)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register CONST_STRPTR a0 __asm__("a0") = name;
    register struct hostent *res __asm__("a0");

    __asm__ __volatile__ (
        "jsr -210(%%a6)"
        : "=r"(res)
        : "r"(a6), "r"(a0)
        : "d0", "d1", "a1", "memory"
    );
    return res;
}

/* ICMP echo packet structure */
struct tn_icmp_hdr {
    UBYTE type;
    UBYTE code;
    UWORD chksum;
    UWORD id;
    UWORD seq;
};

#define TN_ICMP_ECHO_REQUEST 8
#define TN_ICMP_ECHO_REPLY   0

/* RFC 1071 standard checksum */
static UWORD in_cksum(const UWORD *addr, int len)
{
    LONG sum = 0;
    while (len > 1) {
        sum += *addr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const UBYTE *)addr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (UWORD)(~sum);
}

/* Integer square root for mdev standard deviation (zero float on 68000) */
static ULONG isqrt(ULONG n)
{
    ULONG root = 0;
    ULONG bit = 1UL << 30;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

#define OPT_HOST     0
#define OPT_COUNT    1
#define OPT_SIZE     2
#define OPT_INTERVAL 3
#define OPT_TTL      4
#define OPT_TIMEOUT  5
#define OPT_QUIET    6
#define OPT_UDP      7
#define OPT_NUM_OPTS 8

int main(int argc, char *argv[])
{
    struct Library *DOSBase;
    struct MsgPort *tm_port = NULL;
    struct timerequest *tm_io = NULL;
    struct sockaddr_in dst_sin;
    struct hostent *he;
    CONST_STRPTR target_str;
    char target_ip_str[24];
    in_addr_t target_ip;
    LONG sock, count = 4, size = 56, interval = 1, timeout = 2;
    BOOL quiet = FALSE, use_udp = FALSE;
    UWORD ping_id;
    char tx_buf[1500];
    char rx_buf[1600];
    LONG transmitted = 0, acknowledged = 0;
    ULONG min_us = 0xFFFFFFFFUL, max_us = 0, sum_us = 0;
    ULONG sum_sq_100us = 0;
    struct RDArgs *rdargs = NULL;
    LONG opts[OPT_NUM_OPTS];
    int i;
    (void)argc; (void)argv;

    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 36);
    if (DOSBase == NULL) {
        DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
        if (DOSBase == NULL) return 20;
    }

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    for (i = 0; i < OPT_NUM_OPTS; i++) opts[i] = 0;

    rdargs = ReadArgs((CONST_STRPTR)"HOST/A,COUNT/N,SIZE/N,INTERVAL/N,TTL/N,TIMEOUT/N,QUIET/S,UDP/S", opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"ping");
        CloseLibrary(DOSBase);
        return 20;
    }

    target_str = (CONST_STRPTR)opts[OPT_HOST];
    if (opts[OPT_COUNT])    count = *(LONG *)opts[OPT_COUNT];
    if (opts[OPT_SIZE])     size = *(LONG *)opts[OPT_SIZE];
    if (opts[OPT_INTERVAL]) interval = *(LONG *)opts[OPT_INTERVAL];
    if (opts[OPT_TIMEOUT])  timeout = *(LONG *)opts[OPT_TIMEOUT];
    if (opts[OPT_QUIET])    quiet = TRUE;
    if (opts[OPT_UDP])      use_udp = TRUE;

    if (count <= 0) count = 4;
    if (size < 0) size = 0;
    if (size > 1400) size = 1400;
    if (interval < 0) interval = 1;
    if (timeout <= 0) timeout = 2;

    /* 1. Open timer.device for microsecond RTT measurement */
    tm_port = CreateMsgPort();
    if (tm_port != NULL) {
        tm_io = (struct timerequest *)AllocVec(sizeof(struct timerequest), MEMF_CLEAR | MEMF_PUBLIC);
        if (tm_io != NULL) {
            tm_io->tr_node.io_Message.mn_ReplyPort = tm_port;
            if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tm_io, 0) != 0) {
                FreeVec(tm_io);
                tm_io = NULL;
            }
        }
    }

    /* 2. Open bsdsocket.library */
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr((CONST_STRPTR)"ping: unable to open bsdsocket.library\n");
        if (tm_io) { CloseDevice((struct IORequest *)tm_io); FreeVec(tm_io); }
        if (tm_port) DeleteMsgPort(tm_port);
        FreeArgs(rdargs);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* 3. Resolve Target IP */
    target_ip = call_inet_addr(target_str);
    if (target_ip == (in_addr_t)INADDR_NONE) {
        he = call_gethostbyname(target_str);
        if (he != NULL && he->h_addr_list != NULL && he->h_addr_list[0] != NULL) {
            memcpy(&target_ip, he->h_addr_list[0], sizeof(target_ip)); /* TNET-139 */
        } else {
            tn_logf(TN_LOG_BASIC, "ping: cannot resolve %s\n", target_str);
            if (tm_io) { CloseDevice((struct IORequest *)tm_io); FreeVec(tm_io); }
            if (tm_port) DeleteMsgPort(tm_port);
            FreeArgs(rdargs);
            CloseLibrary(SocketBase);
            CloseLibrary(DOSBase);
            return 20;
        }
    }

    {
        STRPTR ntoa = call_inet_ntoa(target_ip);
        int j = 0;
        while (ntoa && ntoa[j] && j < 23) {
            target_ip_str[j] = ntoa[j];
            j++;
        }
        target_ip_str[j] = '\0';
    }

    /* 4. Open Socket (SOCK_RAW for ICMP, SOCK_DGRAM for UDP) */
    if (use_udp) {
        sock = call_socket(AF_INET, SOCK_DGRAM, 0);
    } else {
        sock = call_socket(AF_INET, 3 /* SOCK_RAW */, 1 /* IPPROTO_ICMP */);
    }

    if (sock < 0) {
        tn_logf(TN_LOG_BASIC, "ping: socket creation failed (rc=%ld)\n", sock);
        if (tm_io) { CloseDevice((struct IORequest *)tm_io); FreeVec(tm_io); }
        if (tm_port) DeleteMsgPort(tm_port);
        FreeArgs(rdargs);
        CloseLibrary(SocketBase);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* Task identity for ICMP identifier */
    ping_id = (UWORD)((ULONG)FindTask(NULL) & 0xFFFF);

    /* Initialize target address */
    for (i = 0; i < (int)sizeof(dst_sin); i++) ((char *)&dst_sin)[i] = 0;
    dst_sin.sin_len         = sizeof(struct sockaddr_in);
    dst_sin.sin_family      = AF_INET;
    dst_sin.sin_port        = use_udp ? htons(7) : 0;
    dst_sin.sin_addr.s_addr = target_ip;

    if (!quiet) {
        tn_logf(TN_LOG_BASIC, "PING %s (%s): %ld data bytes\n",
                target_str, target_ip_str, size);
    }

    /* 5. Ping Transmission & Measurement Loop */
    for (LONG seq = 1; seq <= count; seq++) {
        struct timeval t_start, t_end;
        LONG total_len, sent;
        fd_set rfds;
        struct timeval tv;
        struct sockaddr_in from_sin;
        socklen_t from_len = sizeof(from_sin);
        LONG ready;
        BOOL got_reply = FALSE;

        /* Check for Ctrl-C abort */
        if (CheckSignal(SIGBREAKF_CTRL_C)) {
            SetSignal(0, SIGBREAKF_CTRL_C);
            PutStr((CONST_STRPTR)"\n--- ping interrupted by Ctrl-C ---\n");
            break;
        }

        if (use_udp) {
            total_len = size;
            for (i = 0; i < size; i++) tx_buf[i] = (char)('A' + (i % 26));
        } else {
            struct tn_icmp_hdr *icmp = (struct tn_icmp_hdr *)(void *)tx_buf; /* static buffer, even */
            total_len = sizeof(struct tn_icmp_hdr) + size;
            icmp->type   = TN_ICMP_ECHO_REQUEST;
            icmp->code   = 0;
            icmp->chksum = 0;
            icmp->id     = htons(ping_id);
            icmp->seq    = htons((UWORD)seq);
            for (i = 0; i < size; i++) {
                tx_buf[sizeof(struct tn_icmp_hdr) + i] = (char)(0x20 + (i % 95));
            }
            icmp->chksum = in_cksum((const UWORD *)(const void *)tx_buf, (int)total_len) /* static buffer, even */;
        }

        if (tm_io != NULL) {
            tm_io->tr_node.io_Command = TR_GETSYSTIME;
            DoIO((struct IORequest *)tm_io);
            t_start = tm_io->tr_time;
        }

        sent = call_sendto(sock, tx_buf, total_len, 0,
                           (struct sockaddr *)&dst_sin, sizeof(dst_sin));
        if (sent > 0) {
            transmitted++;

            for (i = 0; i < (int)sizeof(rfds); i++) ((char *)&rfds)[i] = 0;
            rfds.fds_bits[sock / 32] |= (1UL << (sock % 32));

            tv.tv_secs = timeout;
            tv.tv_micro = 0;

            ready = call_waitselect(sock + 1, &rfds, NULL, NULL, &tv, NULL);
            if (ready > 0) {
                LONG rcvd = call_recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                                          (struct sockaddr *)&from_sin, &from_len);
                if (rcvd > 0) {
                    ULONG rtt_us = 0;

                    if (tm_io != NULL) {
                        tm_io->tr_node.io_Command = TR_GETSYSTIME;
                        DoIO((struct IORequest *)tm_io);
                        t_end = tm_io->tr_time;

                        LONG sec_diff = t_end.tv_secs - t_start.tv_secs;
                        LONG us_diff  = t_end.tv_micro - t_start.tv_micro;
                        rtt_us = (ULONG)(sec_diff * 1000000L + us_diff);
                    }

                    if (use_udp) {
                        ULONG rtt_100 = rtt_us / 100;
                        if (rtt_100 > 65535UL) rtt_100 = 65535UL;
                        got_reply = TRUE;
                        acknowledged++;
                        sum_us += rtt_us;
                        sum_sq_100us += (rtt_100 * rtt_100);
                        if (rtt_us < min_us) min_us = rtt_us;
                        if (rtt_us > max_us) max_us = rtt_us;

                        if (!quiet) {
                            tn_logf(TN_LOG_BASIC, "%ld bytes from %s: seq=%ld time=%lu.%02lu ms\n",
                                    rcvd, target_ip_str, seq, rtt_us / 1000, (rtt_us % 1000) / 10);
                        }
                    } else {
                        /* In SOCK_RAW, rx_buf starts with IPv4 header */
                        int ip_hlen = (rx_buf[0] & 0x0F) * 4;
                        if (rcvd >= ip_hlen + (int)sizeof(struct tn_icmp_hdr)) {
                            struct tn_icmp_hdr *rep = (struct tn_icmp_hdr *)(void *)(rx_buf + ip_hlen); /* ip_hlen is a multiple of 4 */
                            UBYTE ttl = (UBYTE)rx_buf[8];

                            if (rep->type == TN_ICMP_ECHO_REPLY && ntohs(rep->id) == ping_id) {
                                ULONG rtt_100 = rtt_us / 100;
                                if (rtt_100 > 65535UL) rtt_100 = 65535UL;
                                got_reply = TRUE;
                                acknowledged++;
                                sum_us += rtt_us;
                                sum_sq_100us += (rtt_100 * rtt_100);
                                if (rtt_us < min_us) min_us = rtt_us;
                                if (rtt_us > max_us) max_us = rtt_us;

                                if (!quiet) {
                                    tn_logf(TN_LOG_BASIC, "%ld bytes from %s: icmp_seq=%ld ttl=%lu time=%lu.%02lu ms\n",
                                            rcvd - ip_hlen, target_ip_str, seq, (ULONG)ttl,
                                            rtt_us / 1000, (rtt_us % 1000) / 10);
                                }
                            }
                        }
                    }
                }
            }

            if (!got_reply && !quiet) {
                tn_logf(TN_LOG_BASIC, "Request timeout for seq %ld\n", seq);
            }
        } else {
            if (!quiet) {
                tn_logf(TN_LOG_BASIC, "Send failed for seq %ld\n", seq);
            }
        }

        if (seq < count && interval > 0) {
            Delay(interval * 50); /* 50 ticks = 1 second */
        }
    }

    /* 6. Summary Statistics */
    {
        ULONG loss_pct = (transmitted > 0) ? (((transmitted - acknowledged) * 100) / transmitted) : 0;
        tn_logf(TN_LOG_BASIC, "--- %s ping statistics ---\n", target_str);
        tn_logf(TN_LOG_BASIC, "%ld packets transmitted, %ld packets received, %lu%% packet loss\n",
                transmitted, acknowledged, loss_pct);

        if (acknowledged > 0) {
            ULONG avg_us = sum_us / acknowledged;
            ULONG avg_100 = avg_us / 100;
            ULONG mean_sq = avg_100 * avg_100;
            ULONG avg_sq = sum_sq_100us / acknowledged;
            ULONG var = (avg_sq > mean_sq) ? (avg_sq - mean_sq) : 0;
            ULONG mdev_us = isqrt(var) * 100;

            tn_logf(TN_LOG_BASIC, "round-trip min/avg/max/mdev = %lu.%02lu/%lu.%02lu/%lu.%02lu/%lu.%02lu ms\n",
                    min_us / 1000, (min_us % 1000) / 10,
                    avg_us / 1000, (avg_us % 1000) / 10,
                    max_us / 1000, (max_us % 1000) / 10,
                    mdev_us / 1000, (mdev_us % 1000) / 10);
        }
    }

    /* 7. Cleanup */
    call_closesocket(sock);

    if (tm_io != NULL) {
        CloseDevice((struct IORequest *)tm_io);
        FreeVec(tm_io);
    }
    if (tm_port != NULL) {
        DeleteMsgPort(tm_port);
    }

    FreeArgs(rdargs);
    CloseLibrary(SocketBase);
    CloseLibrary(DOSBase);
    return (acknowledged > 0) ? 0 : 5;
}
