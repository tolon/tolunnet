/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — iperf command (CLOSE §B.6 / ANX-18g).
 * ReadArgs: CLIENT/K,SERVER/S,PORT/N,SECONDS/N,NUMERIC? — no, minimal:
 *   iperf SERVER PORT 5001        — TCP sink: accept, count, report
 *   iperf CLIENT 10.0.2.2 PORT 5001 SECONDS 10 — TCP blast for N seconds
 * Defaults: PORT 5201, SECONDS 10. Plain TCP stream (iperf2-style: the
 * server drains and counts; the client is the throughput source). Time
 * base is DateStamp ticks (1/50 s) — ample for 10 s runs.
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "CLIENT/K,SERVER/S,PORT/N,SECONDS/N"
#define IPERF_DEFAULT_PORT 5201
#define IPERF_DEFAULT_SECONDS 10
#define IPERF_BUF 8192

/* 1/50 s ticks since an arbitrary epoch (wraps every ~2.5 years) */
static ULONG tn_ticks(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400UL * 50UL +
           (ULONG)ds.ds_Minute * 60UL * 50UL +
           (ULONG)ds.ds_Tick;
}

static void fmt_kbps(char *out, ULONG bytes, ULONG ticks)
{
    /* bytes * 50 / ticks = bytes/s; /1024 -> KB/s, all in 32-bit chunks */
    ULONG bps;
    if (ticks == 0) ticks = 1;
    bps = (bytes / ticks) * 50UL + ((bytes % ticks) * 50UL) / ticks;
    {
        ULONG k = bps / 1024UL;
        ULONG rem = ((bps % 1024UL) * 10UL) / 1024UL;
        ULONG v = k;
        int n = 0;
        char tmp[12];
        int i = 0;
        if (v == 0) tmp[i++] = '0';
        while (v > 0 && i < 11) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
        while (i > 0) out[n++] = tmp[--i];
        out[n++] = '.';
        out[n++] = (char)('0' + rem);
        out[n] = '\0';
    }
}

static void fmt_mb(char *out, ULONG bytes)
{
    ULONG mb = bytes / (1024UL * 1024UL);
    ULONG tenth = (bytes % (1024UL * 1024UL)) / (104857UL); /* /1048576*10 */
    ULONG v = mb;
    int n = 0;
    char tmp[12];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v > 0 && i < 11) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) out[n++] = tmp[--i];
    out[n++] = '.';
    out[n++] = (char)('0' + tenth);
    out[n] = '\0';
}

int main(int argc, char **argv)
{
    LONG opts[4] = { 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG port = IPERF_DEFAULT_PORT;
    LONG seconds = IPERF_DEFAULT_SECONDS;
    static char buf[IPERF_BUF];
    LONG fd;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"iperf");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }
    if (opts[2] != 0) port = *(LONG *)opts[2];
    if (opts[3] != 0) seconds = *(LONG *)opts[3];
    if (seconds < 1) seconds = 1;

    if (opts[1]) {
        /* ---- server: accept, drain, count ---- */
        LONG lst = tn_call_socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sin;
        int i;
        if (lst < 0) {
            tn_cmd_printf("iperf: socket failed\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        memset(&sin, 0, sizeof(sin));
        sin.sin_len = sizeof(sin);
        sin.sin_family = AF_INET;
        sin.sin_port = htons((unsigned short)port);
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        if (tn_call_bind(lst, (struct sockaddr *)&sin, sizeof(sin)) != 0 ||
            tn_call_listen(lst, 1) != 0) {
            tn_cmd_printf("iperf: bind/listen port %ld failed (errno %ld)\n",
                          port, tn_call_errno());
            tn_call_closesocket(lst);
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        tn_cmd_printf("iperf: server listening on port %ld (CTRL-C to stop)\n", port);
        while (!tn_cmd_check_ctrlc()) {
            LONG conn = tn_call_accept(lst, NULL, NULL);
            ULONG total = 0;
            ULONG t0, t1;
            char rate[16], mb[16];
            if (conn < 0) break;
            tn_cmd_printf("iperf: client connected\n");
            t0 = tn_ticks();
            for (i = 0; i < IPERF_BUF; i++) buf[i] = (char)(i & 0x7F);
            while (!tn_cmd_check_ctrlc()) {
                LONG got = tn_call_recv(conn, buf, IPERF_BUF, 0);
                if (got <= 0) break;
                total += (ULONG)got;
            }
            t1 = tn_ticks();
            tn_call_closesocket(conn);
            fmt_mb(mb, total);
            fmt_kbps(rate, total, t1 - t0);
            tn_cmd_printf("iperf: received %s MB in %ld.%ld s = %s KB/s\n",
                          mb, (t1 - t0) / 50, (t1 - t0) % 50, rate);
        }
        tn_call_closesocket(lst);
    } else if (opts[0] != 0) {
        /* ---- client: blast for N seconds ---- */
        const char *host = (const char *)opts[0];
        ULONG addr = tn_cmd_resolve(host);
        struct sockaddr_in dst;
        ULONG t0, t1;
        ULONG total = 0;
        char rate[16], mb[16];
        int i;

        if (addr == INADDR_NONE) {
            tn_cmd_printf("iperf: cannot resolve %s\n", host);
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        fd = tn_call_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            tn_cmd_printf("iperf: socket failed\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        memset(&dst, 0, sizeof(dst));
        dst.sin_len = sizeof(dst);
        dst.sin_family = AF_INET;
        dst.sin_port = htons((unsigned short)port);
        dst.sin_addr.s_addr = addr;
        if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
            tn_cmd_printf("iperf: connect %s:%ld failed (errno %ld)\n",
                          host, port, tn_call_errno());
            tn_call_closesocket(fd);
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        for (i = 0; i < IPERF_BUF; i++) buf[i] = (char)(i * 7 + 3);
        tn_cmd_printf("iperf: blasting %s:%ld for %ld s...\n", host, port, seconds);
        t0 = tn_ticks();
        while (!tn_cmd_check_ctrlc()) {
            LONG sent = tn_call_send(fd, buf, IPERF_BUF, 0);
            if (sent <= 0) break;
            total += (ULONG)sent;
            t1 = tn_ticks();
            if ((t1 - t0) >= (ULONG)seconds * 50UL) break;
        }
        t1 = tn_ticks();
        tn_call_shutdown(fd, 1); /* FIN: tell the server we are done */
        tn_call_closesocket(fd);
        fmt_mb(mb, total);
        fmt_kbps(rate, total, t1 - t0);
        tn_cmd_printf("iperf: sent %s MB in %ld.%ld s = %s KB/s\n",
                      mb, (t1 - t0) / 50, (t1 - t0) % 50, rate);
    } else {
        tn_cmd_printf("iperf: use SERVER or CLIENT <host> (see README)\n");
        rc = TN_CMD_USAGE;
    }

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
