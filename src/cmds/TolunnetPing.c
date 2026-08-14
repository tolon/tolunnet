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
#include <devices/timer.h>
#include <libraries/bsdsocket.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../common/log.h"
#include "../../include/ipc.h"

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

int main(int argc, char *argv[])
{
    struct Library *DOSBase;
    struct MsgPort *tm_port = NULL;
    struct timerequest *tm_io = NULL;
    struct sockaddr_in dst_sin;
    struct hostent *he;
    CONST_STRPTR target_str = (CONST_STRPTR)"10.0.2.2";
    in_addr_t target_ip;
    LONG sock, count = 3, seq;
    char payload[32];
    char rx_payload[64];
    int i;

    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    if (argc >= 2) {
        const char *arg = argv[1];
        if (arg[0] == '?' || (arg[0] == '-' && (arg[1] == '?' || arg[1] == 'h'))) {
            PutStr((CONST_STRPTR)"Usage: ping <host|ip> [count]\nExample: ping 1.1.1.1 4\nExample: ping aminet.net\n");
            CloseLibrary(DOSBase);
            return 0;
        }
        target_str = (CONST_STRPTR)argv[1];
    }
    if (argc >= 3) {
        LONG parsed = 0;
        if (StrToLong((CONST_STRPTR)argv[2], &parsed) > 0 && parsed > 0) {
            count = parsed;
        }
    }

    /* 1. Open timer.device for precise RTT measurement */
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
        CloseLibrary(DOSBase);
        return 20;
    }

    /* 3. Resolve Target IP */
    target_ip = call_inet_addr(target_str);
    if (target_ip == (in_addr_t)INADDR_NONE) {
        he = call_gethostbyname(target_str);
        if (he != NULL && he->h_addr_list != NULL && he->h_addr_list[0] != NULL) {
            target_ip = *(in_addr_t *)he->h_addr_list[0];
        } else {
            tn_logf(TN_LOG_BASIC, "ping: cannot resolve %s\n", target_str);
            if (tm_io) { CloseDevice((struct IORequest *)tm_io); FreeVec(tm_io); }
            if (tm_port) DeleteMsgPort(tm_port);
            CloseLibrary(SocketBase);
            CloseLibrary(DOSBase);
            return 20;
        }
    }

    /* 4. Open UDP Echo socket */
    sock = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        tn_logf(TN_LOG_BASIC, "ping: socket creation failed (rc=%ld)\n", sock);
        if (tm_io) { CloseDevice((struct IORequest *)tm_io); FreeVec(tm_io); }
        if (tm_port) DeleteMsgPort(tm_port);
        CloseLibrary(SocketBase);
        CloseLibrary(DOSBase);
        return 20;
    }

    for (i = 0; i < 32; i++) payload[i] = (char)('A' + (i % 26));

    /* Initialize BSD44 sockaddr_in (TNET-028 & TNET-029) */
    for (i = 0; i < (int)sizeof(dst_sin); i++) ((char *)&dst_sin)[i] = 0;
    dst_sin.sin_len         = sizeof(struct sockaddr_in);
    dst_sin.sin_family      = AF_INET;
    dst_sin.sin_port        = htons(7); /* Echo port 7 in network byte order */
    dst_sin.sin_addr.s_addr = target_ip;

    tn_logf(TN_LOG_BASIC, "PING %s (%s): 32 data bytes\n",
            target_str, call_inet_ntoa(target_ip));

    LONG acknowledged = 0;

    /* 5. Probe Loop with Real Roundtrip Timing (TNET-038) */
    for (seq = 1; seq <= count; seq++) {
        struct timeval t_start, t_end;
        LONG sent, rtt_ms = 0, rtt_us = 0;

        if (tm_io != NULL) {
            tm_io->tr_node.io_Command = TR_GETSYSTIME;
            DoIO((struct IORequest *)tm_io);
            t_start = tm_io->tr_time;
        }

        sent = call_sendto(sock, payload, sizeof(payload), 0,
                           (struct sockaddr *)&dst_sin, sizeof(dst_sin));

        if (sent > 0) {
            fd_set rfds;
            struct timeval tv;
            struct sockaddr_in from_sin;
            socklen_t from_len = sizeof(from_sin);
            LONG ready;

            for (i = 0; i < (int)sizeof(rfds); i++) ((char *)&rfds)[i] = 0;
            rfds.fds_bits[sock / 32] |= (1UL << (sock % 32));

            tv.tv_secs = 1; /* 1 second timeout */
            tv.tv_micro = 0;

            ready = call_waitselect(sock + 1, &rfds, NULL, NULL, &tv, NULL);
            if (ready > 0) {
                LONG rcvd = call_recvfrom(sock, rx_payload, sizeof(rx_payload), 0,
                                          (struct sockaddr *)&from_sin, &from_len);
                if (rcvd > 0) {
                    if (tm_io != NULL) {
                        tm_io->tr_node.io_Command = TR_GETSYSTIME;
                        DoIO((struct IORequest *)tm_io);
                        t_end = tm_io->tr_time;

                        rtt_ms = (t_end.tv_secs - t_start.tv_secs) * 1000L +
                                 (t_end.tv_micro - t_start.tv_micro) / 1000L;
                        rtt_us = (t_end.tv_micro - t_start.tv_micro) % 1000L;
                        if (rtt_us < 0) rtt_us = 0;
                    }
                    acknowledged++;
                    tn_logf(TN_LOG_BASIC, "32 bytes from %s: seq=%ld time=%ld.%01ld ms\n",
                            call_inet_ntoa(target_ip), seq, rtt_ms, rtt_us / 100);
                } else {
                    tn_logf(TN_LOG_BASIC, "Request timeout for seq %ld\n", seq);
                }
            } else {
                tn_logf(TN_LOG_BASIC, "Request timeout for seq %ld\n", seq);
            }
        } else {
            tn_logf(TN_LOG_BASIC, "Send failed for seq %ld\n", seq);
        }

        /* Brief delay between probes */
        if (seq < count) {
            Delay(25); /* 500 ms delay */
        }
    }

    tn_logf(TN_LOG_BASIC, "--- %s ping statistics ---\n", target_str);
    tn_logf(TN_LOG_BASIC, "%ld packets transmitted, %ld packets received\n", count, acknowledged);

    /* 6. Clean Up */
    call_closesocket(sock);

    if (tm_io != NULL) {
        CloseDevice((struct IORequest *)tm_io);
        FreeVec(tm_io);
    }
    if (tm_port != NULL) {
        DeleteMsgPort(tm_port);
    }

    CloseLibrary(SocketBase);
    CloseLibrary(DOSBase);
    return (acknowledged > 0) ? 0 : 5;
}
