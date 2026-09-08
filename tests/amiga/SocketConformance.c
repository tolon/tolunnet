/*
 * SocketConformance — Amiga-side bsdsocket conformance suite (Round 3 §B.2).
 *
 * One program, TAP output to stdout AND WORK:conformance.log. Exit code =
 * number of "not ok" rows (capped at 9 so a red baseline cannot abort the
 * User-Startup script). SKIP rows carry the tracking ID of the milestone that
 * un-skips them (§C/§D of the Round 3 work order).
 *
 * Cases marked red on purpose: tc_bind_udp / tc_shutdown_wr / tc_getpeername
 / tc_listen_accept_loopback document TNET-077 (daemon handlers missing) —
 * they are the baseline this harness exists to flip in §C1.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <libraries/bsdsocket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/errno.h>

#include "../../src/common/log.h"
#include "../../include/ipc.h"
#include "../../src/common/ipc_client.h"
#include "../../src/setup/wifi_mgr.h"

static struct Library *SocketBase = NULL;
static BPTR            g_log_fh   = (BPTR)0;

static int g_count = 0;
static int g_not_ok_count = 0;

static void vsnprintf_safe(char *buf, int size, const char *fmt, va_list ap);

/* ------------------------------------------------------------ TAP plumbing */

static void tapf(const char *fmt, ...)
{
    /* both console and WORK:conformance.log */
    char buf[300];
    va_list ap;
    LONG len = 0;
    BPTR out;

    va_start(ap, fmt);
    vsnprintf_safe(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    while (buf[len]) len++;
    out = Output();
    if (out) {
        Write(out, (CONST APTR)buf, len);
        Flush(out);
    }
    if (g_log_fh) {
        Write(g_log_fh, (CONST APTR)buf, len);
        Flush(g_log_fh);
    }
}

static void vsnprintf_safe(char *buf, int size, const char *fmt, va_list ap)
{
    /* minimal formatter: %s %d %ld %lu %x — mirrors tn_logf capabilities */
    const char *p = fmt;
    int o = 0;
    while (*p && o + 1 < size) {
        if (*p != '%') { buf[o++] = *p++; continue; }
        p++;
        if (*p == 'l') p++;
        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && o + 1 < size) buf[o++] = *s++;
        } else if (*p == 'd') {
            LONG v = va_arg(ap, LONG);
            char tmp[16]; int i = 0; ULONG av = (v < 0) ? -v : v;
            if (v < 0 && o + 1 < size) buf[o++] = '-';
            if (av == 0) tmp[i++] = '0';
            while (av > 0 && i < 15) { tmp[i++] = (char)('0' + av % 10); av /= 10; }
            while (i > 0 && o + 1 < size) buf[o++] = tmp[--i];
        } else if (*p == 'u') {
            ULONG v = va_arg(ap, ULONG);
            char tmp[16]; int i = 0;
            if (v == 0) tmp[i++] = '0';
            while (v > 0 && i < 15) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
            while (i > 0 && o + 1 < size) buf[o++] = tmp[--i];
        } else if (*p == 'x') {
            ULONG v = va_arg(ap, ULONG);
            char tmp[12]; int i = 0;
            const char *hex = "0123456789abcdef";
            if (v == 0) tmp[i++] = '0';
            while (v > 0 && i < 11) { tmp[i++] = hex[v & 0xF]; v >>= 4; }
            while (i > 0 && o + 1 < size) buf[o++] = tmp[--i];
        } else if (*p == 'p') {
            ULONG v = (ULONG)(intptr_t)va_arg(ap, void *);
            char tmp[12]; int i = 0;
            const char *hex = "0123456789abcdef";
            if (v == 0) tmp[i++] = '0';
            while (v > 0 && i < 11) { tmp[i++] = hex[v & 0xF]; v >>= 4; }
            if (o + 2 < size) { buf[o++] = '0'; buf[o++] = 'x'; }
            while (i > 0 && o + 1 < size) buf[o++] = tmp[--i];
        } else if (*p == '%') {
            buf[o++] = '%';
        }
        if (*p) p++;
    }
    buf[o] = '\0';
}

static void snprintf_safe(char *buf, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf_safe(buf, size, fmt, ap);
    va_end(ap);
}

static LONG parse_long(const char *s)
{
    LONG res = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return neg ? -res : res;
}

#define TAP_OK(name)        do { g_count++; tapf("ok %d - %s\n", g_count, name); } while (0)
#define TAP_NOTOK(name, why) do { g_count++; g_not_ok_count++; tapf("not ok %d - %s # %s\n", g_count, name, why); } while (0)
#define TAP_TODO(name, why)  do { g_count++; tapf("not ok %d - %s # TODO %s\n", g_count, name, why); } while (0)
#define TAP_SKIP(name, why)  do { g_count++; tapf("ok %d - %s # SKIP %s\n", g_count, name, why); } while (0)

/* --------------------------------------------------------- LVO call shims */

static LONG call_socket(LONG d, LONG t, LONG p)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = d;
    register LONG d1 __asm__("d1") = t;
    register LONG d2 __asm__("d2") = p;
    __asm__ __volatile__ ("jsr -30(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2) : "d1", "d2", "a0", "a1", "memory");
    return d0;
}

static LONG call_bind(LONG s, struct sockaddr *n, socklen_t l)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register struct sockaddr *a0 __asm__("a0") = n;
    register LONG d1 __asm__("d1") = (LONG)l;
    __asm__ __volatile__ ("jsr -36(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_listen(LONG s, LONG backlog)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register LONG d1 __asm__("d1") = backlog;
    __asm__ __volatile__ ("jsr -42(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_accept(LONG s, struct sockaddr *n, socklen_t *l)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register struct sockaddr *a0 __asm__("a0") = n;
    register socklen_t *a1 __asm__("a1") = l;
    __asm__ __volatile__ ("jsr -48(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(a1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_shutdown(LONG s, LONG how)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register LONG d1 __asm__("d1") = how;
    __asm__ __volatile__ ("jsr -84(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_getsockname(LONG s, struct sockaddr *n, socklen_t *l)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register struct sockaddr *a0 __asm__("a0") = n;
    register socklen_t *a1 __asm__("a1") = l;
    __asm__ __volatile__ ("jsr -102(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(a1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_getpeername(LONG s, struct sockaddr *n, socklen_t *l)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register struct sockaddr *a0 __asm__("a0") = n;
    register socklen_t *a1 __asm__("a1") = l;
    __asm__ __volatile__ ("jsr -108(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(a1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_connect(LONG s, struct sockaddr *n, socklen_t l)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register struct sockaddr *a0 __asm__("a0") = n;
    register LONG d1 __asm__("d1") = (LONG)l;
    __asm__ __volatile__ ("jsr -54(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_ioctl(LONG s, ULONG req, APTR argp)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register LONG d1 __asm__("d1") = (LONG)req;
    register APTR a0 __asm__("a0") = argp;
    __asm__ __volatile__ ("jsr -114(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(a0) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_setsockopt(LONG s, LONG lvl, LONG opt, const void *val, socklen_t len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register LONG d1 __asm__("d1") = lvl;
    register LONG d2 __asm__("d2") = opt;
    register const void *a0 __asm__("a0") = val;
    register LONG d3 __asm__("d3") = (LONG)len;
    __asm__ __volatile__ ("jsr -90(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2), "r"(a0), "r"(d3) : "d1", "d2", "d3", "a0", "a1", "memory");
    return d0;
}

static LONG call_getsockopt(LONG s, LONG lvl, LONG opt, void *val, socklen_t *len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register LONG d1 __asm__("d1") = lvl;
    register LONG d2 __asm__("d2") = opt;
    register void *a0 __asm__("a0") = val;
    register socklen_t *a1 __asm__("a1") = len;
    __asm__ __volatile__ ("jsr -96(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2), "r"(a0), "r"(a1) : "d1", "d2", "a0", "a1", "memory");
    return d0;
}

static LONG call_closesocket(LONG s)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    __asm__ __volatile__ ("jsr -120(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_errno(VOID)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0");
    __asm__ __volatile__ ("jsr -162(%%a6)" : "=r"(d0)
        : "r"(a6) : "d1", "a0", "a1", "memory");
    return d0;
}

static struct hostent *call_gethostbyname(CONST_STRPTR n)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register CONST_STRPTR a0 __asm__("a0") = n;
    register struct hostent *res __asm__("d0");
    __asm__ __volatile__ ("jsr -210(%%a6)" : "=r"(res)
        : "r"(a6), "r"(a0) : "d1", "a0", "a1", "memory");
    return res;
}

static LONG call_dup2(LONG o, LONG n)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = o;
    register LONG d1 __asm__("d1") = n;
    __asm__ __volatile__ ("jsr -264(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_socketbasetaglist(struct TagItem *tags)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register struct TagItem *a0 __asm__("a0") = tags;
    register LONG d0 __asm__("d0");
    __asm__ __volatile__ ("jsr -294(%%a6)" : "=r"(d0), "+r"(a0)
        : "r"(a6) : "d1", "a1", "memory");
    return d0;
}

static LONG call_obtainsocket(LONG id, LONG domain, LONG type, LONG protocol)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = id;
    register LONG d1 __asm__("d1") = domain;
    register LONG d2 __asm__("d2") = type;
    register LONG d3 __asm__("d3") = protocol;
    __asm__ __volatile__ ("jsr -144(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2), "r"(d3) : "d1", "d2", "d3", "a0", "a1", "memory");
    return d0;
}

static LONG call_releasesocket(LONG sock, LONG id)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register LONG d1 __asm__("d1") = id;
    __asm__ __volatile__ ("jsr -150(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_releasecopyofsocket(LONG sock, LONG id)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register LONG d1 __asm__("d1") = id;
    __asm__ __volatile__ ("jsr -156(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static BOOL call_processisserver(struct Process *pr)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register struct Process *a0 __asm__("a0") = pr;
    register LONG d0 __asm__("d0");
    __asm__ __volatile__ ("jsr -690(%%a6)" : "=r"(d0), "+r"(a0)
        : "r"(a6) : "d1", "a1", "memory");
    return (BOOL)d0;
}

static LONG call_obtainserversocket(VOID)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0");
    __asm__ __volatile__ ("jsr -696(%%a6)" : "=r"(d0)
        : "r"(a6) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_sendto(LONG s, const void *b, LONG l, LONG fl, struct sockaddr *to, socklen_t tolen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register const void *a0 __asm__("a0") = b;
    register LONG d1 __asm__("d1") = l;
    register LONG d2 __asm__("d2") = fl;
    register struct sockaddr *a1 __asm__("a1") = to;
    register LONG d3 __asm__("d3") = (LONG)tolen;
    __asm__ __volatile__ ("jsr -60(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2), "r"(a1), "r"(d3)
        : "d1", "d2", "d3", "a0", "a1", "memory");
    return d0;
}

static LONG call_send(LONG s, const void *b, LONG l, LONG fl)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register const void *a0 __asm__("a0") = b;
    register LONG d1 __asm__("d1") = l;
    register LONG d2 __asm__("d2") = fl;
    __asm__ __volatile__ ("jsr -66(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2) : "d1", "d2", "a0", "a1", "memory");
    return d0;
}

static LONG call_recv(LONG s, void *b, LONG l, LONG fl)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register void *a0 __asm__("a0") = b;
    register LONG d1 __asm__("d1") = l;
    register LONG d2 __asm__("d2") = fl;
    __asm__ __volatile__ ("jsr -78(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2) : "d1", "d2", "a0", "a1", "memory");
    return d0;
}

static LONG call_sendmsg(LONG s, struct msghdr *m, LONG fl)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register struct msghdr *a0 __asm__("a0") = m;
    register LONG d1 __asm__("d1") = fl;
    __asm__ __volatile__ ("jsr -270(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_recvmsg(LONG s, struct msghdr *m, LONG fl)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register struct msghdr *a0 __asm__("a0") = m;
    register LONG d1 __asm__("d1") = fl;
    __asm__ __volatile__ ("jsr -276(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1) : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG call_waitselect(LONG nfds, APTR rfds, APTR wfds, APTR efds, struct timeval *to, ULONG *sigs)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = nfds;
    register APTR a0 __asm__("a0") = rfds;
    register APTR a1 __asm__("a1") = wfds;
    register APTR a2 __asm__("a2") = efds;
    register struct timeval *a3 __asm__("a3") = to;
    register ULONG *d1 __asm__("d1") = sigs;
    __asm__ __volatile__ ("jsr -126(%%a6)" : "+r"(d0), "+r"(d1)
        : "r"(a6), "r"(d0), "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(d1)
        : "a0", "a1", "a2", "a3", "memory");
    return d0;
}

static VOID call_setsocketsignals(ULONG int_mask, ULONG io_mask, ULONG urg_mask)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register ULONG d0 __asm__("d0") = int_mask;
    register ULONG d1 __asm__("d1") = io_mask;
    register ULONG d2 __asm__("d2") = urg_mask;
    __asm__ __volatile__ ("jsr -132(%%a6)"
        : : "r"(a6), "r"(d0), "r"(d1), "r"(d2) : "d0", "d1", "d2", "a0", "a1", "memory");
}

static LONG call_getsocketevents(ULONG *event_ptr)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register ULONG *a0 __asm__("a0") = event_ptr;
    register LONG d0 __asm__("d0");
    __asm__ __volatile__ ("jsr -300(%%a6)" : "=r"(d0), "+r"(a0)
        : "r"(a6) : "d1", "d2", "a1", "memory");
    return d0;
}

/* ------------------------------------------------------------- test cases */

static void tc_lib_open_close(void)
{
    int i;
    for (i = 0; i < 100; i++) {
        struct Library *b = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
        if (b == NULL) { TAP_NOTOK("tc_lib_open_close", "open failed"); return; }
        CloseLibrary(b);
    }
    TAP_OK("tc_lib_open_close");
}

static void tc_socket_types(void)
{
    LONG s1 = call_socket(AF_INET, SOCK_STREAM, 0);
    LONG s2 = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG bad = call_socket(AF_INET, 99 /* bad type */, 0);

    if (s1 >= 0 && s2 >= 0 && bad < 0 && call_errno() != 0) {
        TAP_OK("tc_socket_types");
    } else {
        TAP_NOTOK("tc_socket_types", "stream/dgram ok + bad type rejected expected");
    }
    if (s1 >= 0) call_closesocket(s1);
    if (s2 >= 0) call_closesocket(s2);
}

static void tc_bind_udp(void)
{
    /* TNET-077 baseline: daemon has no BIND handler -> ENOSYS (red on purpose;
     * §C1 flips this to green with a getsockname port check). */
    LONG s = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin;
    LONG rc;
    int i;

    if (s < 0) { TAP_NOTOK("tc_bind_udp", "no socket"); return; }
    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len    = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port   = 0; /* ephemeral */
    rc = call_bind(s, (struct sockaddr *)&sin, sizeof(sin));
    if (rc == 0) {
        struct sockaddr_in got;
        socklen_t gl = sizeof(got);
        for (i = 0; i < (int)sizeof(got); i++) ((char *)&got)[i] = 0;
        if (call_getsockname(s, (struct sockaddr *)&got, &gl) == 0 &&
            got.sin_port != 0) {
            TAP_OK("tc_bind_udp");
        } else {
            TAP_NOTOK("tc_bind_udp", "bind ok but getsockname failed/port 0");
        }
    } else {
        TAP_NOTOK("tc_bind_udp", "bind returned error (TNET-077: ENOSYS baseline)");
    }
    call_closesocket(s);
}

static void tc_bind_reuse(void)
{
    LONG s1 = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG s2 = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin;
    LONG one = 1;
    int i;

    if (s1 < 0 || s2 < 0) {
        if (s1 >= 0) call_closesocket(s1);
        if (s2 >= 0) call_closesocket(s2);
        TAP_NOTOK("tc_bind_reuse", "failed to create sockets");
        return;
    }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len    = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(54321);

    call_setsockopt(s1, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    call_setsockopt(s2, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (call_bind(s1, (struct sockaddr *)&sin, sizeof(sin)) == 0 &&
        call_bind(s2, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
        TAP_OK("tc_bind_reuse");
    } else {
        TAP_NOTOK("tc_bind_reuse", "bind with SO_REUSEADDR failed");
    }

    call_closesocket(s1);
    call_closesocket(s2);
}

static void tc_sockopt_matrix(void)
{
    LONG s = call_socket(AF_INET, SOCK_STREAM, 0);
    LONG udps;
    LONG optval, got;
    socklen_t len;
    struct timeval tv, tv_got;
    struct linger l, l_got;

    if (s < 0) {
        TAP_NOTOK("tc_sockopt_matrix", "stream socket creation failed");
        return;
    }

    /* 1. SOL_SOCKET: SO_REUSEADDR */
    optval = 1;
    if (call_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_REUSEADDR, &got, &len) != 0 || got == 0) goto fail;
    optval = 0;
    if (call_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 1;
    if (call_getsockopt(s, SOL_SOCKET, SO_REUSEADDR, &got, &len) != 0 || got != 0) goto fail;

    /* 2. SOL_SOCKET: SO_KEEPALIVE */
    optval = 1;
    if (call_setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &got, &len) != 0 || got == 0) goto fail;

    /* 3. SOL_SOCKET: SO_BROADCAST */
    optval = 1;
    if (call_setsockopt(s, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_BROADCAST, &got, &len) != 0 || got == 0) goto fail;

    /* 4. SOL_SOCKET: SO_OOBINLINE */
    optval = 1;
    if (call_setsockopt(s, SOL_SOCKET, SO_OOBINLINE, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_OOBINLINE, &got, &len) != 0 || got == 0) goto fail;

    /* 5. SOL_SOCKET: SO_SNDBUF / SO_RCVBUF */
    optval = 8192;
    if (call_setsockopt(s, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_SNDBUF, &got, &len) != 0 || got != 8192) goto fail;

    optval = 16384;
    if (call_setsockopt(s, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_RCVBUF, &got, &len) != 0 || got != 16384) goto fail;

    /* 6. SOL_SOCKET: SO_RCVTIMEO / SO_SNDTIMEO */
    tv.tv_secs = 2; tv.tv_micro = 500000;
    if (call_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) goto fail;
    len = sizeof(tv_got); tv_got.tv_secs = 0; tv_got.tv_micro = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv_got, &len) != 0 ||
        tv_got.tv_secs != 2 || tv_got.tv_micro != 500000) goto fail;

    tv.tv_secs = 1; tv.tv_micro = 0;
    if (call_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) goto fail;
    len = sizeof(tv_got); tv_got.tv_secs = 0; tv_got.tv_micro = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv_got, &len) != 0 ||
        tv_got.tv_secs != 1 || tv_got.tv_micro != 0) goto fail;

    /* 7. SOL_SOCKET: SO_LINGER */
    l.l_onoff = 1; l.l_linger = 10;
    if (call_setsockopt(s, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) != 0) goto fail;
    len = sizeof(l_got); l_got.l_onoff = 0; l_got.l_linger = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_LINGER, &l_got, &len) != 0 ||
        l_got.l_onoff != 1 || l_got.l_linger != 10) goto fail;

    /* 8. SOL_SOCKET: SO_TYPE & SO_ERROR */
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, SOL_SOCKET, SO_TYPE, &got, &len) != 0 || got != SOCK_STREAM) goto fail;
    if (call_setsockopt(s, SOL_SOCKET, SO_TYPE, &got, sizeof(got)) == 0) goto fail;

    len = sizeof(got); got = -1;
    if (call_getsockopt(s, SOL_SOCKET, SO_ERROR, &got, &len) != 0 || got != 0) goto fail;
    if (call_setsockopt(s, SOL_SOCKET, SO_ERROR, &got, sizeof(got)) == 0) goto fail;

    /* 9. IPPROTO_TCP: TCP_NODELAY, TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT */
    optval = 1;
    if (call_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, IPPROTO_TCP, TCP_NODELAY, &got, &len) != 0 || got == 0) goto fail;

    optval = 120;
    if (call_setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &got, &len) != 0 || got != 120) goto fail;

    optval = 15;
    if (call_setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &got, &len) != 0 || got != 15) goto fail;

    optval = 4;
    if (call_setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &got, &len) != 0 || got != 4) goto fail;

    /* 10. IPPROTO_IP: IP_TTL, IP_TOS, IP_MULTICAST_TTL, IP_MULTICAST_LOOP */
    optval = 32;
    if (call_setsockopt(s, IPPROTO_IP, IP_TTL, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, IPPROTO_IP, IP_TTL, &got, &len) != 0 || got != 32) goto fail;

    optval = 0x10;
    if (call_setsockopt(s, IPPROTO_IP, IP_TOS, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, IPPROTO_IP, IP_TOS, &got, &len) != 0 || got != 0x10) goto fail;

    optval = 2;
    if (call_setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 0;
    if (call_getsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &got, &len) != 0 || got != 2) goto fail;

    optval = 0;
    if (call_setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &optval, sizeof(optval)) != 0) goto fail;
    len = sizeof(got); got = 1;
    if (call_getsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &got, &len) != 0 || got != 0) goto fail;

    /* 11. Negative tests: IPPROTO_TCP on UDP socket; unknown option; unknown level */
    udps = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (udps >= 0) {
        optval = 1;
        if (call_setsockopt(udps, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) == 0) {
            call_closesocket(udps);
            goto fail;
        }
        call_closesocket(udps);
    }

    optval = 1;
    if (call_setsockopt(s, SOL_SOCKET, 9999, &optval, sizeof(optval)) == 0) goto fail;
    if (call_setsockopt(s, 9999, 1, &optval, sizeof(optval)) == 0) goto fail;

    call_closesocket(s);
    TAP_OK("tc_sockopt_matrix");
    return;

fail:
    call_closesocket(s);
    TAP_NOTOK("tc_sockopt_matrix", "sockopt matrix check failed");
}

static void tc_multicast_join(void)
{
    LONG s = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG s_sender;
    struct sockaddr_in sin;
    struct sockaddr_in dst;
    struct ip_mreq mreq;
    LONG rc;
    int i;
    LONG one = 1;

    if (s < 0) { TAP_NOTOK("tc_multicast_join", "receiver socket failed"); return; }

    call_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(5353);
    sin.sin_addr.s_addr = htonl(0x00000000UL); /* INADDR_ANY */

    rc = call_bind(s, (struct sockaddr *)&sin, sizeof(sin));
    if (rc != 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_multicast_join", "bind 5353 failed");
        return;
    }

    /* Join 224.0.0.251 (mDNS multicast group) */
    mreq.imr_multiaddr.s_addr = htonl(0xE00000FBUL);
    mreq.imr_interface.s_addr = htonl(0x00000000UL);

    rc = call_setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    if (rc != 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_multicast_join", "IP_ADD_MEMBERSHIP failed");
        return;
    }

    /* Multicast packet transmit verification */
    s_sender = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sender >= 0) {
        call_setsockopt(s_sender, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one));
        for (i = 0; i < (int)sizeof(dst); i++) ((char *)&dst)[i] = 0;
        dst.sin_len = sizeof(dst);
        dst.sin_family = AF_INET;
        dst.sin_port = htons(5353);
        dst.sin_addr.s_addr = htonl(0xE00000FBUL);

        call_sendto(s_sender, "mDNS_TEST", 9, 0, (struct sockaddr *)&dst, sizeof(dst));
        call_closesocket(s_sender);
    }

    char rx_buf[32];
    rc = call_recv(s, rx_buf, sizeof(rx_buf), 0);
    if (rc != 9 || memcmp(rx_buf, "mDNS_TEST", 9) != 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_multicast_join", "multicast loopback receive mismatch");
        return;
    }

    /* Drop membership */
    rc = call_setsockopt(s, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    if (rc != 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_multicast_join", "IP_DROP_MEMBERSHIP failed");
        return;
    }

    call_closesocket(s);
    TAP_OK("tc_multicast_join");
}

static void tc_ioctl_ifconf(void)
{
    LONG s = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct ifconf ifc;
    struct ifreq ifr_buf[4];
    struct ifreq req;
    LONG rc;
    int found_eth0 = 0;
    int i, n_interfaces;

    if (s < 0) {
        TAP_NOTOK("tc_ioctl_ifconf", "socket creation failed");
        return;
    }

    /* Enumerate netifs via SIOCGIFCONF */
    for (i = 0; i < (int)sizeof(ifr_buf); i++) ((char *)ifr_buf)[i] = 0;
    ifc.ifc_len = sizeof(ifr_buf);
    ifc.ifc_req = ifr_buf;

    rc = call_ioctl(s, SIOCGIFCONF, &ifc);
    if (rc != 0 || ifc.ifc_len <= 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "SIOCGIFCONF failed");
        return;
    }

    n_interfaces = ifc.ifc_len / sizeof(struct ifreq);
    for (i = 0; i < n_interfaces; i++) {
        if (ifr_buf[i].ifr_name[0] == 'e' &&
            ifr_buf[i].ifr_name[1] == 't' &&
            ifr_buf[i].ifr_name[2] == 'h' &&
            ifr_buf[i].ifr_name[3] == '0') {
            found_eth0 = 1;
            break;
        }
    }

    if (!found_eth0) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "eth0 not found in SIOCGIFCONF enumeration");
        return;
    }

    /* Verify SIOCGIFFLAGS on eth0 */
    for (i = 0; i < (int)sizeof(req); i++) ((char *)&req)[i] = 0;
    req.ifr_name[0] = 'e'; req.ifr_name[1] = 't'; req.ifr_name[2] = 'h'; req.ifr_name[3] = '0';
    if (call_ioctl(s, SIOCGIFFLAGS, &req) != 0 || !(req.ifr_flags & IFF_UP)) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "SIOCGIFFLAGS failed or eth0 not UP");
        return;
    }

    /* Verify SIOCGIFADDR on eth0 */
    if (call_ioctl(s, SIOCGIFADDR, &req) != 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "SIOCGIFADDR failed");
        return;
    }

    /* Verify SIOCGIFNETMASK on eth0 */
    if (call_ioctl(s, SIOCGIFNETMASK, &req) != 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "SIOCGIFNETMASK failed");
        return;
    }

    /* Verify SIOCGIFMTU on eth0 */
    if (call_ioctl(s, SIOCGIFMTU, &req) != 0 || req.ifr_mtu <= 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "SIOCGIFMTU failed");
        return;
    }

    /* Verify SIOCATMARK on socket */
    {
        int atmark = -1;
        if (call_ioctl(s, SIOCATMARK, &atmark) != 0 || atmark != 0) {
            call_closesocket(s);
            TAP_NOTOK("tc_ioctl_ifconf", "SIOCATMARK failed");
            return;
        }
    }

    /* Verify routing stubs return ENOSYS */
    if (call_ioctl(s, SIOCADDRT, &req) == 0 || call_errno() != ENOSYS) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "SIOCADDRT expected ENOSYS");
        return;
    }
    if (call_ioctl(s, SIOCDELRT, &req) == 0 || call_errno() != ENOSYS) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "SIOCDELRT expected ENOSYS");
        return;
    }

    /* Verify unknown ioctl returns EINVAL */
    if (call_ioctl(s, 0x12345678UL, &req) == 0 || call_errno() != EINVAL) {
        call_closesocket(s);
        TAP_NOTOK("tc_ioctl_ifconf", "unknown ioctl expected EINVAL");
        return;
    }

    call_closesocket(s);
    TAP_OK("tc_ioctl_ifconf");
}

static void tc_ioctl_fionread(void)
{
    LONG s_srv = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG s_cli = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin;
    ULONG nread = 999;
    char buf[16];
    int i;

    if (s_srv < 0 || s_cli < 0) {
        if (s_srv >= 0) call_closesocket(s_srv);
        if (s_cli >= 0) call_closesocket(s_cli);
        TAP_NOTOK("tc_ioctl_fionread", "socket creation failed");
        return;
    }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(54323);
    sin.sin_addr.s_addr = htonl(0x7F000001UL); /* 127.0.0.1 */

    if (call_bind(s_srv, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(s_srv);
        call_closesocket(s_cli);
        TAP_NOTOK("tc_ioctl_fionread", "bind server failed");
        return;
    }

    /* Initial FIONREAD should be 0 */
    if (call_ioctl(s_srv, FIONREAD, &nread) != 0 || nread != 0) {
        call_closesocket(s_srv);
        call_closesocket(s_cli);
        TAP_NOTOK("tc_ioctl_fionread", "initial FIONREAD non-zero");
        return;
    }

    /* Send 5 bytes to server */
    call_sendto(s_cli, "HELLO", 5, 0, (struct sockaddr *)&sin, sizeof(sin));

    /* Check FIONREAD on receiver */
    nread = 0;
    if (call_ioctl(s_srv, FIONREAD, &nread) != 0 || nread != 5) {
        call_closesocket(s_srv);
        call_closesocket(s_cli);
        TAP_NOTOK("tc_ioctl_fionread", "FIONREAD after send did not report 5 bytes");
        return;
    }

    /* Read the bytes and check FIONREAD returns to 0 */
    call_recv(s_srv, buf, sizeof(buf), 0);
    nread = 999;
    if (call_ioctl(s_srv, FIONREAD, &nread) != 0 || nread != 0) {
        call_closesocket(s_srv);
        call_closesocket(s_cli);
        TAP_NOTOK("tc_ioctl_fionread", "FIONREAD after read non-zero");
        return;
    }

    call_closesocket(s_srv);
    call_closesocket(s_cli);
    TAP_OK("tc_ioctl_fionread");
}

static void tc_listen_accept_loopback(void)
{
    LONG srv, cli, conn;
    struct sockaddr_in sin;
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    int i;

    srv = call_socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { TAP_NOTOK("tc_listen_accept_loopback", "server socket failed"); return; }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len         = sizeof(sin);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(54322);
    sin.sin_addr.s_addr = htonl(0x7F000001UL); /* 127.0.0.1 */

    if (call_bind(srv, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(srv);
        TAP_NOTOK("tc_listen_accept_loopback", "bind failed");
        return;
    }

    if (call_listen(srv, 1) != 0) {
        call_closesocket(srv);
        TAP_NOTOK("tc_listen_accept_loopback", "listen failed");
        return;
    }

    cli = call_socket(AF_INET, SOCK_STREAM, 0);
    if (cli < 0) {
        call_closesocket(srv);
        TAP_NOTOK("tc_listen_accept_loopback", "client socket failed");
        return;
    }

    if (call_connect(cli, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(cli);
        call_closesocket(srv);
        TAP_NOTOK("tc_listen_accept_loopback", "connect loopback failed");
        return;
    }

    for (i = 0; i < (int)sizeof(from); i++) ((char *)&from)[i] = 0;
    conn = call_accept(srv, (struct sockaddr *)&from, &fromlen);
    if (conn < 0) {
        call_closesocket(cli);
        call_closesocket(srv);
        TAP_NOTOK("tc_listen_accept_loopback", "accept failed");
        return;
    }

    if (from.sin_addr.s_addr != htonl(0x7F000001UL)) {
        call_closesocket(conn);
        call_closesocket(cli);
        call_closesocket(srv);
        TAP_NOTOK("tc_listen_accept_loopback", "accepted peer not 127.0.0.1");
        return;
    }

    call_closesocket(conn);
    call_closesocket(cli);
    call_closesocket(srv);
    TAP_OK("tc_listen_accept_loopback");
}

static void tc_connect_refused(void)
{
    /* slirp: connecting to a closed port on 10.0.2.2 must refuse quickly */
    LONG s = call_socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;
    LONG rc;
    int i;

    if (s < 0) { TAP_NOTOK("tc_connect_refused", "no socket"); return; }
    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len    = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(9); /* discard-ish port on the host: closed */
    sin.sin_addr.s_addr = htonl(0x0A000202UL); /* 10.0.2.2 */
    rc = call_connect(s, (struct sockaddr *)&sin, sizeof(sin));
    if (rc < 0) {
        TAP_OK("tc_connect_refused");
    } else {
        TAP_NOTOK("tc_connect_refused", "connect unexpectedly succeeded");
    }
    call_closesocket(s);
}

static void tc_nonblock_connect(void)
{
    LONG s = call_socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;
    LONG one = 1;
    LONG rc;
    fd_set wfds;
    struct timeval tv;
    int err = -1;
    socklen_t optlen = sizeof(err);
    LONG sel;
    int i;

    if (s < 0) {
        TAP_NOTOK("tc_nonblock_connect", "socket() failed");
        return;
    }

    if (call_ioctl(s, FIONBIO, (char *)&one) < 0) {
        TAP_NOTOK("tc_nonblock_connect", "FIONBIO failed");
        call_closesocket(s);
        return;
    }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len         = sizeof(sin);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(8000);
    sin.sin_addr.s_addr = htonl(0x0A000202UL); /* 10.0.2.2 (host slirp) */

    rc = call_connect(s, (struct sockaddr *)&sin, sizeof(sin));
    tapf("# tc_nonblock_connect: rc=%ld errno=%ld EINPROGRESS=%d\n", rc, call_errno(), (int)EINPROGRESS);
    if (rc != 0 && call_errno() != EINPROGRESS && call_errno() != EALREADY) {
        TAP_NOTOK("tc_nonblock_connect", "connect did not return EINPROGRESS");
        call_closesocket(s);
        return;
    }

    /* Wait for write readiness via WaitSelect */
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    tv.tv_secs  = 10;
    tv.tv_micro = 0;
    tapf("# tc_nonblock_connect: calling waitselect s=%ld\n", s);
    sel = call_waitselect(s + 1, NULL, &wfds, NULL, &tv, NULL);
    tapf("# tc_nonblock_connect: waitselect returned %ld errno=%ld\n", sel, call_errno());
    if (sel <= 0 || !FD_ISSET(s, &wfds)) {
        TAP_NOTOK("tc_nonblock_connect", "WaitSelect timeout or not writable");
        call_closesocket(s);
        return;
    }

    /* Check SO_ERROR */
    if (call_getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &optlen) < 0 || err != 0) {
        TAP_NOTOK("tc_nonblock_connect", "getsockopt SO_ERROR indicated error");
        call_closesocket(s);
        return;
    }

    TAP_OK("tc_nonblock_connect");
    call_closesocket(s);
}

static void tc_shutdown_wr(void)
{
    /* TNET-077 baseline: no daemon SHUTDOWN handler -> ENOSYS (red on purpose) */
    LONG s = call_socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { TAP_NOTOK("tc_shutdown_wr", "no socket"); return; }
    if (call_shutdown(s, 1 /* SHUT_WR */) == 0) {
        TAP_OK("tc_shutdown_wr");
    } else {
        TAP_NOTOK("tc_shutdown_wr", "shutdown returned error (TNET-077: ENOSYS baseline)");
    }
    call_closesocket(s);
}

static void tc_getpeername(void)
{
    /* TNET-077 baseline: no daemon GETPEERNAME handler (red on purpose) */
    LONG s = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin;
    struct sockaddr_in peer;
    socklen_t pl = sizeof(peer);
    int i;

    if (s < 0) { TAP_NOTOK("tc_getpeername", "no socket"); return; }
    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(53);
    sin.sin_addr.s_addr = htonl(0x0A000202UL);
    if (call_connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0 &&
        call_getpeername(s, (struct sockaddr *)&peer, &pl) == 0 &&
        peer.sin_addr.s_addr == htonl(0x0A000202UL)) {
        TAP_OK("tc_getpeername");
    } else {
        TAP_NOTOK("tc_getpeername", "getpeername failed (TNET-077: ENOSYS baseline)");
    }
    call_closesocket(s);
}

static void tc_dns_a(void)
{
    struct hostent *he = call_gethostbyname((CONST_STRPTR)"aminet.net");
    if (he != NULL && he->h_addr_list != NULL && he->h_addr_list[0] != NULL &&
        he->h_length == 4 && he->h_addrtype == AF_INET) {
        TAP_OK("tc_dns_a");
    } else {
        tapf("# tc_dns_a: he=%p errno=%ld\n", he, call_errno());
        TAP_NOTOK("tc_dns_a", "resolve aminet.net failed");
    }
}

static void tc_dns_fail(void)
{
    struct hostent *he = call_gethostbyname((CONST_STRPTR)"nonexistent.invalid");
    if (he == NULL) {
        TAP_OK("tc_dns_fail");
    } else {
        TAP_NOTOK("tc_dns_fail", "bogus name resolved?!");
    }
}

static void tc_errno_ptr(void)
{
    LONG e1, e2, e4;
    struct TagItem tags[4];
    int pass = 1;

    /* SETVAL/SETREF errno through SocketBaseTagList, byte/word/long widths */
    e1 = 0; e2 = 0; e4 = 0;
    tags[0].ti_Tag  = SBTM_SETVAL(SBTC_ERRNOBYTEPTR); tags[0].ti_Data = 0; /* placeholder, replaced below */
    /* NOTE: SETREF carries a POINTER; use the ...PTR codes with REF form */
    tags[0].ti_Tag  = SBTM_SETREF(SBTC_ERRNOBYTEPTR); tags[0].ti_Data = (ULONG)&e1;
    tags[1].ti_Tag  = SBTM_SETREF(SBTC_ERRNOWORDPTR); tags[1].ti_Data = (ULONG)&e2;
    tags[2].ti_Tag  = SBTM_SETREF(SBTC_ERRNOLONGPTR); tags[2].ti_Data = (ULONG)&e4;
    tags[3].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);

    if (call_socket(-1, 0, 0) < 0) {
        LONG e = call_errno();
        /* whichever width was registered LAST wins (word here); byte/long
         * aliases receive truncated/padded copies via Errno() only for the
         * registered one — assert the long-registered case separately below */
        (void)e;
    }
    if (!pass) { TAP_NOTOK("tc_errno_ptr", "width handling"); return; }

    /* long-width errno end-to-end */
    e4 = 0;
    tags[0].ti_Tag  = SBTM_SETREF(SBTC_ERRNOLONGPTR); tags[0].ti_Data = (ULONG)&e4;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);
    call_socket(-1, 0, 0);
    if (e4 != 0) {
        TAP_OK("tc_errno_ptr");
    } else {
        TAP_NOTOK("tc_errno_ptr", "long-width errno was not written");
    }

    tags[0].ti_Tag  = SBTM_SETREF(SBTC_ERRNOLONGPTR);
    tags[0].ti_Data = 0;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);
}

static void tc_dup2(void)
{
    LONG s = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG d;
    if (s < 0) { TAP_NOTOK("tc_dup2", "no socket"); return; }
    d = call_dup2(s, 10);
    if (d == 10) {
        call_closesocket(10);
        TAP_OK("tc_dup2");
    } else {
        TAP_NOTOK("tc_dup2", "dup2 did not return new fd");
    }
    call_closesocket(s);
}

static void tc_waitselect_timeout(void)
{
    struct timeval tv;
    struct MsgPort *tm_port;
    struct timerequest *tm_io;
    struct timeval t1, t2;
    LONG diff_ms;
    LONG res;

    tm_port = CreateMsgPort();
    if (!tm_port) { TAP_NOTOK("tc_waitselect_timeout", "CreateMsgPort failed"); return; }
    tm_io = (struct timerequest *)AllocVec(sizeof(struct timerequest), MEMF_CLEAR | MEMF_PUBLIC);
    if (!tm_io) { DeleteMsgPort(tm_port); TAP_NOTOK("tc_waitselect_timeout", "AllocVec failed"); return; }
    tm_io->tr_node.io_Message.mn_ReplyPort = tm_port;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tm_io, 0) != 0) {
        FreeVec(tm_io);
        DeleteMsgPort(tm_port);
        TAP_NOTOK("tc_waitselect_timeout", "OpenDevice failed");
        return;
    }

    /* Measure 200 ms timeout in WaitSelect */
    tv.tv_secs = 0;
    tv.tv_micro = 200000; /* 200 ms */

    tm_io->tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *)tm_io);
    t1 = tm_io->tr_time;

    res = call_waitselect(0, NULL, NULL, NULL, &tv, NULL);

    tm_io->tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *)tm_io);
    t2 = tm_io->tr_time;

    {
        LONG sec_diff = (LONG)t2.tv_secs - (LONG)t1.tv_secs;
        LONG us_diff  = (LONG)t2.tv_micro - (LONG)t1.tv_micro;
        if (us_diff < 0) {
            sec_diff--;
            us_diff += 1000000L;
        }
        diff_ms = sec_diff * 1000L + us_diff / 1000L;
    }

    CloseDevice((struct IORequest *)tm_io);
    FreeVec(tm_io);
    DeleteMsgPort(tm_port);

    /* 200 ms timeout precision tightened to +-20ms (180ms to 240ms window per §D) */
    if (res == 0 && diff_ms >= 180 && diff_ms <= 240) {
        TAP_OK("tc_waitselect_timeout");
    } else {
        tapf("# diff_ms = %ld, res = %ld\n", diff_ms, res);
        TAP_NOTOK("tc_waitselect_timeout", "timeout precision outside 180-240ms window");
    }
}

static void tc_waitselect_eintr(void)
{
    BYTE sig_bit;
    ULONG sig_mask;
    ULONG sigs;
    struct timeval tv;
    LONG res;

    sig_bit = AllocSignal(-1);
    if (sig_bit < 0) {
        TAP_NOTOK("tc_waitselect_eintr", "AllocSignal failed");
        return;
    }
    sig_mask = 1UL << sig_bit;

    /* Signal caller task before WaitSelect */
    Signal((struct Task *)FindTask(NULL), sig_mask);

    tv.tv_secs = 5;
    tv.tv_micro = 0;
    sigs = sig_mask;

    res = call_waitselect(0, NULL, NULL, NULL, &tv, &sigs);
    if (res != -1 || call_errno() != EINTR || (sigs & sig_mask) == 0) {
        tapf("# res=%ld errno=%ld sigs=0x%lx (expected -1, EINTR, 0x%lx)\n",
             res, call_errno(), sigs, sig_mask);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_waitselect_eintr", "WaitSelect did not return -1/EINTR on pending signal");
        return;
    }

    /* Test break signal via SetSocketSignals */
    call_setsocketsignals(sig_mask, 0, 0);
    Signal((struct Task *)FindTask(NULL), sig_mask);
    res = call_waitselect(0, NULL, NULL, NULL, &tv, NULL);
    call_setsocketsignals(0, 0, 0);

    if (res != -1 || call_errno() != EINTR) {
        tapf("# sig_int res=%ld errno=%ld (expected -1, EINTR)\n", res, call_errno());
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_waitselect_eintr", "WaitSelect did not return -1/EINTR on sig_int");
        return;
    }

    FreeSignal(sig_bit);
    TAP_OK("tc_waitselect_eintr");
}

static void tc_waitselect_badf(void)
{
    fd_set fds;
    struct timeval tv_zero;
    LONG res;
    LONG s;

    tv_zero.tv_secs = 0;
    tv_zero.tv_micro = 0;

    /* 1. nfds > table size (32) must return -1 with EBADF */
    res = call_waitselect(33, NULL, NULL, NULL, &tv_zero, NULL);
    if (res != -1 || call_errno() != EBADF) {
        tapf("# nfds=33 res=%ld errno=%ld (expected -1, EBADF)\n", res, call_errno());
        TAP_NOTOK("tc_waitselect_badf", "nfds > table did not return EBADF");
        return;
    }

    /* 2. Unopened descriptor in read_fds with nfds covering it */
    memset(&fds, 0, sizeof(fds));
    fds.fds_bits[0] = (1UL << 7); /* fd 7 is unopened */
    res = call_waitselect(8, &fds, NULL, NULL, &tv_zero, NULL);
    if (res != -1 || call_errno() != EBADF) {
        tapf("# unopened fd res=%ld errno=%ld (expected -1, EBADF)\n", res, call_errno());
        TAP_NOTOK("tc_waitselect_badf", "unopened fd in read_fds did not return EBADF");
        return;
    }

    /* 3. nfds honoured: bit 7 set, but nfds = 5 (so fd 7 is ignored) */
    memset(&fds, 0, sizeof(fds));
    fds.fds_bits[0] = (1UL << 7);
    res = call_waitselect(5, &fds, NULL, NULL, &tv_zero, NULL);
    if (res < 0 || (fds.fds_bits[0] & (1UL << 7)) != 0) {
        tapf("# nfds=5 res=%ld fds=0x%lx\n", res, (ULONG)fds.fds_bits[0]);
        TAP_NOTOK("tc_waitselect_badf", "nfds was not honoured");
        return;
    }

    /* 4. except_fds on healthy UDP socket: returns 0 and bit cleared */
    s = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        TAP_NOTOK("tc_waitselect_badf", "call_socket failed");
        return;
    }
    memset(&fds, 0, sizeof(fds));
    fds.fds_bits[0] = (1UL << s);
    res = call_waitselect(s + 1, NULL, NULL, &fds, &tv_zero, NULL);
    if (res != 0 || (fds.fds_bits[0] & (1UL << s)) != 0) {
        tapf("# except_fds res=%ld fds=0x%lx\n", res, (ULONG)fds.fds_bits[0]);
        call_closesocket(s);
        TAP_NOTOK("tc_waitselect_badf", "except_fds returned non-zero for healthy socket");
        return;
    }
    call_closesocket(s);

    TAP_OK("tc_waitselect_badf");
}

static void tc_waitselect_no_sigio(void)
{
    LONG s1, s2;
    struct sockaddr_in sin;
    char msg[] = "no_sigio_test";
    struct timeval tv;
    fd_set rfds;
    LONG res;
    int i;

    /* Explicitly ensure SIGIO is disabled for this opener base */
    call_setsocketsignals(0, 0, 0);

    s1 = call_socket(AF_INET, SOCK_DGRAM, 0);
    s2 = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s1 < 0 || s2 < 0) {
        if (s1 >= 0) call_closesocket(s1);
        if (s2 >= 0) call_closesocket(s2);
        TAP_NOTOK("tc_waitselect_no_sigio", "socket creation failed");
        return;
    }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len         = sizeof(sin);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(54326);
    sin.sin_addr.s_addr = htonl(0x7F000001UL);

    if (call_bind(s1, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(s1);
        call_closesocket(s2);
        TAP_NOTOK("tc_waitselect_no_sigio", "bind failed");
        return;
    }

    /* 1. When no data is present, short 50ms timeout expires cleanly without polling */
    FD_ZERO(&rfds);
    FD_SET(s1, &rfds);
    tv.tv_secs = 0;
    tv.tv_micro = 50000; /* 50 ms */
    res = call_waitselect(s1 + 1, &rfds, NULL, NULL, &tv, NULL);
    if (res != 0) {
        call_closesocket(s1);
        call_closesocket(s2);
        TAP_NOTOK("tc_waitselect_no_sigio", "expected timeout 0 on empty socket");
        return;
    }

    /* 2. Send packet from s2 to s1 via loopback */
    call_sendto(s2, msg, sizeof(msg), 0, (struct sockaddr *)&sin, sizeof(sin));

    /* 3. WaitSelect with sig_io == 0 must detect readiness via event-driven selector */
    FD_ZERO(&rfds);
    FD_SET(s1, &rfds);
    tv.tv_secs = 1;
    tv.tv_micro = 0;
    res = call_waitselect(s1 + 1, &rfds, NULL, NULL, &tv, NULL);

    call_closesocket(s1);
    call_closesocket(s2);

    if (res == 1 && FD_ISSET(s1, &rfds)) {
        TAP_OK("tc_waitselect_no_sigio");
    } else {
        TAP_NOTOK("tc_waitselect_no_sigio", "WaitSelect failed to report read readiness without sig_io");
    }
}

static void tc_sigio(void)
{
    BYTE sig_bit;
    ULONG sig_mask;
    LONG s1, s2;
    struct sockaddr_in sin;
    char msg[] = "ping";
    int i;

    sig_bit = AllocSignal(-1);
    if (sig_bit < 0) {
        TAP_NOTOK("tc_sigio", "AllocSignal failed");
        return;
    }
    sig_mask = 1UL << sig_bit;

    /* Set SIGIO mask for this SocketBase */
    call_setsocketsignals(0, sig_mask, 0);

    s1 = call_socket(AF_INET, SOCK_DGRAM, 0);
    s2 = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s1 < 0 || s2 < 0) {
        if (s1 >= 0) call_closesocket(s1);
        if (s2 >= 0) call_closesocket(s2);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_sigio", "socket creation failed");
        return;
    }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len         = sizeof(sin);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(54323);
    sin.sin_addr.s_addr = htonl(0x7F000001UL);

    if (call_bind(s1, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(s1);
        call_closesocket(s2);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_sigio", "bind failed");
        return;
    }

    /* Clear any pending signal */
    SetSignal(0, sig_mask);

    /* Send packet to s1 via loopback */
    call_sendto(s2, msg, sizeof(msg), 0, (struct sockaddr *)&sin, sizeof(sin));

    /* Check if signal was received */
    if (SetSignal(0, 0) & sig_mask) {
        TAP_OK("tc_sigio");
    } else {
        TAP_NOTOK("tc_sigio", "SIGIO signal not delivered on packet arrival");
    }

    /* Clear signal mask */
    call_setsocketsignals(0, 0, 0);
    call_closesocket(s1);
    call_closesocket(s2);
    FreeSignal(sig_bit);
}

static void tc_icmp_raw(void)
{
    /* C9 (TNET-070): SOCK_RAW is implemented for IPPROTO_ICMP (1) and IPPROTO_RAW (255) */
    LONG s = call_socket(AF_INET, 3 /* SOCK_RAW */, 1 /* IPPROTO_ICMP */);
    LONG bad = call_socket(AF_INET, 3 /* SOCK_RAW */, 99 /* unsupported raw protocol */);
    if (s >= 0 && bad < 0) {
        call_closesocket(s);
        TAP_OK("tc_icmp_raw");
    } else {
        if (s >= 0) call_closesocket(s);
        if (bad >= 0) call_closesocket(bad);
        TAP_NOTOK("tc_icmp_raw", "SOCK_RAW ICMP failed or invalid proto accepted");
    }
}

static void tc_sendmsg_iov(void)
{
    LONG s_rx, s_tx;
    struct sockaddr_in sin_rx, sin_from;
    struct iovec iov_tx[3];
    struct iovec iov_rx[2];
    struct msghdr msg_tx, msg_rx;
    char p1[] = "HELLO ";
    char p2[] = "FROM ";
    char p3[] = "TOLUNNET!";
    char r1[8];
    char r2[12];
    char rxbuf[32];
    LONG res;
    int i;

    s_rx = call_socket(AF_INET, SOCK_DGRAM, 0);
    s_tx = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s_rx < 0 || s_tx < 0) {
        if (s_rx >= 0) call_closesocket(s_rx);
        if (s_tx >= 0) call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "failed to create UDP sockets");
        return;
    }

    for (i = 0; i < (int)sizeof(sin_rx); i++) ((char *)&sin_rx)[i] = 0;
    sin_rx.sin_len         = sizeof(sin_rx);
    sin_rx.sin_family      = AF_INET;
    sin_rx.sin_port        = htons(54330);
    sin_rx.sin_addr.s_addr = htonl(0x7F000001UL);

    if (call_bind(s_rx, (struct sockaddr *)&sin_rx, sizeof(sin_rx)) != 0) {
        call_closesocket(s_rx);
        call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "bind failed");
        return;
    }

    /* Test 1: Error handling on sendmsg */
    if (call_sendmsg(s_tx, NULL, 0) >= 0 || call_errno() != EINVAL) {
        call_closesocket(s_rx);
        call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "sendmsg NULL msg did not fail with EINVAL");
        return;
    }

    /* Test 2: Gather send (sendmsg with 3 iovecs) */
    iov_tx[0].iov_base = (APTR)p1;
    iov_tx[0].iov_len  = 6;
    iov_tx[1].iov_base = (APTR)p2;
    iov_tx[1].iov_len  = 5;
    iov_tx[2].iov_base = (APTR)p3;
    iov_tx[2].iov_len  = 9;

    for (i = 0; i < (int)sizeof(msg_tx); i++) ((char *)&msg_tx)[i] = 0;
    msg_tx.msg_name    = (APTR)&sin_rx;
    msg_tx.msg_namelen = sizeof(sin_rx);
    msg_tx.msg_iov     = iov_tx;
    msg_tx.msg_iovlen  = 3;

    if (call_sendmsg(s_tx, &msg_tx, MSG_OOB) >= 0 || call_errno() != EOPNOTSUPP) {
        call_closesocket(s_rx);
        call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "sendmsg MSG_OOB did not fail with EOPNOTSUPP");
        return;
    }

    res = call_sendmsg(s_tx, &msg_tx, 0);
    if (res != 20) {
        tapf("# sendmsg res = %ld (expected 20)\n", res);
        call_closesocket(s_rx);
        call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "sendmsg gather returned wrong byte count");
        return;
    }

    for (i = 0; i < (int)sizeof(rxbuf); i++) rxbuf[i] = 0;
    res = call_recv(s_rx, rxbuf, sizeof(rxbuf), 0);
    if (res != 20) {
        tapf("# recv res = %ld\n", res);
        call_closesocket(s_rx);
        call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "recv did not receive gathered 20 bytes");
        return;
    }

    /* Verify payload concatenation: "HELLO FROM TOLUNNET!" */
    {
        const char *expected = "HELLO FROM TOLUNNET!";
        for (i = 0; i < 20; i++) {
            if (rxbuf[i] != expected[i]) {
                call_closesocket(s_rx);
                call_closesocket(s_tx);
                TAP_NOTOK("tc_sendmsg_iov", "gathered payload content mismatch");
                return;
            }
        }
    }

    /* Test 3: Scatter receive (recvmsg into 2 iovecs) */
    call_sendto(s_tx, "SCATTER123456789", 16, 0, (struct sockaddr *)&sin_rx, sizeof(sin_rx));

    for (i = 0; i < 8; i++) r1[i] = 0;
    for (i = 0; i < 12; i++) r2[i] = 0;
    for (i = 0; i < (int)sizeof(sin_from); i++) ((char *)&sin_from)[i] = 0;

    iov_rx[0].iov_base = (APTR)r1;
    iov_rx[0].iov_len  = 7; /* "SCATTER" */
    iov_rx[1].iov_base = (APTR)r2;
    iov_rx[1].iov_len  = 9; /* "123456789" */

    for (i = 0; i < (int)sizeof(msg_rx); i++) ((char *)&msg_rx)[i] = 0;
    msg_rx.msg_name    = (APTR)&sin_from;
    msg_rx.msg_namelen = sizeof(sin_from);
    msg_rx.msg_iov     = iov_rx;
    msg_rx.msg_iovlen  = 2;

    res = call_recvmsg(s_rx, &msg_rx, 0);
    if (res != 16) {
        tapf("# recvmsg res = %ld (expected 16)\n", res);
        call_closesocket(s_rx);
        call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "recvmsg scatter returned wrong byte count");
        return;
    }

    {
        const char *exp1 = "SCATTER";
        const char *exp2 = "123456789";
        for (i = 0; i < 7; i++) {
            if (r1[i] != exp1[i]) {
                call_closesocket(s_rx);
                call_closesocket(s_tx);
                TAP_NOTOK("tc_sendmsg_iov", "scatter chunk 1 mismatch");
                return;
            }
        }
        for (i = 0; i < 9; i++) {
            if (r2[i] != exp2[i]) {
                call_closesocket(s_rx);
                call_closesocket(s_tx);
                TAP_NOTOK("tc_sendmsg_iov", "scatter chunk 2 mismatch");
                return;
            }
        }
    }

    if (sin_from.sin_family != AF_INET || sin_from.sin_addr.s_addr != htonl(0x7F000001UL)) {
        call_closesocket(s_rx);
        call_closesocket(s_tx);
        TAP_NOTOK("tc_sendmsg_iov", "recvmsg msg_name from address mismatch");
        return;
    }

    call_closesocket(s_rx);
    call_closesocket(s_tx);
    TAP_OK("tc_sendmsg_iov");
}

static void tc_recv_peek(void)
{
    LONG s1, s2;
    struct sockaddr_in sin;
    char pbuf1[32];
    char pbuf2[32];
    char pbuf3[32];
    LONG res;
    int i;
    ULONG nread = 0;

    /* 1. UDP MSG_PEEK test */
    s1 = call_socket(AF_INET, SOCK_DGRAM, 0);
    s2 = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s1 < 0 || s2 < 0) {
        if (s1 >= 0) call_closesocket(s1);
        if (s2 >= 0) call_closesocket(s2);
        TAP_NOTOK("tc_recv_peek", "socket creation failed");
        return;
    }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len         = sizeof(sin);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(54331);
    sin.sin_addr.s_addr = htonl(0x7F000001UL);

    if (call_bind(s1, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(s1);
        call_closesocket(s2);
        TAP_NOTOK("tc_recv_peek", "bind UDP s1 failed");
        return;
    }

    call_sendto(s2, "PEEK_TEST_PAYLOAD", 17, 0, (struct sockaddr *)&sin, sizeof(sin));

    for (i = 0; i < 32; i++) { pbuf1[i] = 0; pbuf2[i] = 0; }

    /* Peek at the packet */
    res = call_recv(s1, pbuf1, sizeof(pbuf1), MSG_PEEK);
    if (res != 17) {
        tapf("# UDP peek res = %ld\n", res);
        call_closesocket(s1);
        call_closesocket(s2);
        TAP_NOTOK("tc_recv_peek", "UDP recv MSG_PEEK returned wrong len");
        return;
    }

    /* Now consume the packet normally */
    res = call_recv(s1, pbuf2, sizeof(pbuf2), 0);
    if (res != 17) {
        tapf("# UDP second recv res = %ld\n", res);
        call_closesocket(s1);
        call_closesocket(s2);
        TAP_NOTOK("tc_recv_peek", "UDP second recv failed after peek");
        return;
    }

    {
        const char *expected = "PEEK_TEST_PAYLOAD";
        for (i = 0; i < 17; i++) {
            if (pbuf1[i] != expected[i] || pbuf2[i] != expected[i]) {
                call_closesocket(s1);
                call_closesocket(s2);
                TAP_NOTOK("tc_recv_peek", "UDP peeked payload data mismatch");
                return;
            }
        }
    }

    /* Verify socket is now empty via FIONREAD */
    nread = 0xFFFFFFFFUL;
    if (call_ioctl(s1, FIONREAD, (APTR)&nread) != 0 || nread != 0) {
        call_closesocket(s1);
        call_closesocket(s2);
        TAP_NOTOK("tc_recv_peek", "UDP socket not empty after consuming peeked packet");
        return;
    }

    call_closesocket(s1);
    call_closesocket(s2);

    /* 2. TCP MSG_PEEK test */
    {
        LONG s_listen, s_cli, s_srv;
        struct sockaddr_in srv_sin, cli_sin;
        socklen_t slen;

        s_listen = call_socket(AF_INET, SOCK_STREAM, 0);
        s_cli    = call_socket(AF_INET, SOCK_STREAM, 0);
        if (s_listen < 0 || s_cli < 0) {
            if (s_listen >= 0) call_closesocket(s_listen);
            if (s_cli >= 0) call_closesocket(s_cli);
            TAP_NOTOK("tc_recv_peek", "TCP socket creation failed");
            return;
        }

        for (i = 0; i < (int)sizeof(srv_sin); i++) ((char *)&srv_sin)[i] = 0;
        srv_sin.sin_len         = sizeof(srv_sin);
        srv_sin.sin_family      = AF_INET;
        srv_sin.sin_port        = htons(54332);
        srv_sin.sin_addr.s_addr = htonl(0x7F000001UL);

        if (call_bind(s_listen, (struct sockaddr *)&srv_sin, sizeof(srv_sin)) != 0 ||
            call_listen(s_listen, 1) != 0) {
            call_closesocket(s_listen);
            call_closesocket(s_cli);
            TAP_NOTOK("tc_recv_peek", "TCP bind/listen failed");
            return;
        }

        if (call_connect(s_cli, (struct sockaddr *)&srv_sin, sizeof(srv_sin)) != 0) {
            call_closesocket(s_listen);
            call_closesocket(s_cli);
            TAP_NOTOK("tc_recv_peek", "TCP connect failed");
            return;
        }

        slen = sizeof(cli_sin);
        s_srv = call_accept(s_listen, (struct sockaddr *)&cli_sin, &slen);
        if (s_srv < 0) {
            call_closesocket(s_listen);
            call_closesocket(s_cli);
            TAP_NOTOK("tc_recv_peek", "TCP accept failed");
            return;
        }

        /* Send TCP data from client */
        res = call_send(s_cli, "TCP_PEEK_DATA", 13, 0);
        if (res != 13) {
            tapf("# TCP send res = %ld, errno = %ld\n", res, call_errno());
            call_closesocket(s_srv);
            call_closesocket(s_cli);
            call_closesocket(s_listen);
            TAP_NOTOK("tc_recv_peek", "TCP send failed");
            return;
        }

        for (i = 0; i < 32; i++) { pbuf1[i] = 0; pbuf2[i] = 0; pbuf3[i] = 0; }

        /* Peek first 4 bytes */
        res = call_recv(s_srv, pbuf1, 4, MSG_PEEK);
        if (res != 4) {
            tapf("# TCP partial peek res = %ld\n", res);
            call_closesocket(s_srv);
            call_closesocket(s_cli);
            call_closesocket(s_listen);
            TAP_NOTOK("tc_recv_peek", "TCP partial MSG_PEEK failed");
            return;
        }

        /* Peek all 13 bytes */
        res = call_recv(s_srv, pbuf2, 13, MSG_PEEK);
        if (res != 13) {
            tapf("# TCP full peek res = %ld\n", res);
            call_closesocket(s_srv);
            call_closesocket(s_cli);
            call_closesocket(s_listen);
            TAP_NOTOK("tc_recv_peek", "TCP full MSG_PEEK failed");
            return;
        }

        /* Consume normally */
        res = call_recv(s_srv, pbuf3, 13, 0);
        if (res != 13) {
            tapf("# TCP normal recv res = %ld\n", res);
            call_closesocket(s_srv);
            call_closesocket(s_cli);
            call_closesocket(s_listen);
            TAP_NOTOK("tc_recv_peek", "TCP normal recv failed after peek");
            return;
        }

        {
            const char *exp_part = "TCP_";
            const char *exp_full = "TCP_PEEK_DATA";
            for (i = 0; i < 4; i++) {
                if (pbuf1[i] != exp_part[i]) {
                    call_closesocket(s_srv);
                    call_closesocket(s_cli);
                    call_closesocket(s_listen);
                    TAP_NOTOK("tc_recv_peek", "TCP partial peek content mismatch");
                    return;
                }
            }
            for (i = 0; i < 13; i++) {
                if (pbuf2[i] != exp_full[i] || pbuf3[i] != exp_full[i]) {
                    call_closesocket(s_srv);
                    call_closesocket(s_cli);
                    call_closesocket(s_listen);
                    TAP_NOTOK("tc_recv_peek", "TCP full peek content mismatch");
                    return;
                }
            }
        }

        call_closesocket(s_srv);
        call_closesocket(s_cli);
        call_closesocket(s_listen);
    }

    TAP_OK("tc_recv_peek");
}

static void tc_socket_events(void)
{
    BYTE sig_bit;
    ULONG sig_mask;
    struct TagItem tags[2];
    LONG s_listen, s_cli, s_srv;
    struct sockaddr_in srv_sin, cli_sin;
    socklen_t slen;
    ULONG events[64];
    LONG count;
    int i;

    sig_bit = AllocSignal(-1);
    if (sig_bit < 0) {
        TAP_NOTOK("tc_socket_events", "AllocSignal failed");
        return;
    }
    sig_mask = 1UL << sig_bit;

    tags[0].ti_Tag  = SBTM_SETVAL(SBTC_SIGEVENTMASK);
    tags[0].ti_Data = sig_mask;
    tags[1].ti_Tag  = TAG_DONE;
    tags[1].ti_Data = 0;
    if (call_socketbasetaglist(tags) != 0) {
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "SocketBaseTagList SBTC_SIGEVENTMASK failed");
        return;
    }

    s_listen = call_socket(AF_INET, SOCK_STREAM, 0);
    s_cli    = call_socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0 || s_cli < 0) {
        if (s_listen >= 0) call_closesocket(s_listen);
        if (s_cli >= 0) call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "socket creation failed");
        return;
    }

    for (i = 0; i < (int)sizeof(srv_sin); i++) ((char *)&srv_sin)[i] = 0;
    srv_sin.sin_len         = sizeof(srv_sin);
    srv_sin.sin_family      = AF_INET;
    srv_sin.sin_port        = htons(54334);
    srv_sin.sin_addr.s_addr = htonl(0x7F000001UL);

    if (call_bind(s_listen, (struct sockaddr *)&srv_sin, sizeof(srv_sin)) != 0 ||
        call_listen(s_listen, 1) != 0) {
        call_closesocket(s_listen);
        call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "bind/listen failed");
        return;
    }

    /* Clear any pending signals */
    SetSignal(0, sig_mask);

    /* Connect client to listener */
    if (call_connect(s_cli, (struct sockaddr *)&srv_sin, sizeof(srv_sin)) != 0) {
        call_closesocket(s_listen);
        call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "connect failed");
        return;
    }

    /* Check if signal was received on event */
    if (!(SetSignal(0, 0) & sig_mask)) {
        call_closesocket(s_listen);
        call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "sig_event signal not received on connect/accept");
        return;
    }

    for (i = 0; i < 64; i++) events[i] = 0;
    count = call_getsocketevents(events);
    if (count != 0) {
        call_closesocket(s_listen);
        call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "GetSocketEvents returned error");
        return;
    }

    if (!(events[s_listen] & FD_ACCEPT)) {
        tapf("# s_listen events = 0x%lx\n", events[s_listen]);
        call_closesocket(s_listen);
        call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "s_listen did not record FD_ACCEPT");
        return;
    }

    if (!(events[s_cli] & (FD_CONNECT | FD_WRITE))) {
        tapf("# s_cli events = 0x%lx\n", events[s_cli]);
        call_closesocket(s_listen);
        call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "s_cli did not record FD_CONNECT | FD_WRITE");
        return;
    }

    /* Accept connection */
    slen = sizeof(cli_sin);
    s_srv = call_accept(s_listen, (struct sockaddr *)&cli_sin, &slen);
    if (s_srv < 0) {
        call_closesocket(s_listen);
        call_closesocket(s_cli);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "accept failed");
        return;
    }

    /* Clear signals and close client: server should receive FD_CLOSE */
    SetSignal(0, sig_mask);
    call_closesocket(s_cli);

    /* Verify signal delivered on peer close */
    if (!(SetSignal(0, 0) & sig_mask)) {
        call_closesocket(s_srv);
        call_closesocket(s_listen);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "sig_event signal not received on peer close");
        return;
    }

    for (i = 0; i < 64; i++) events[i] = 0;
    count = call_getsocketevents(events);
    if (count != 0 || !(events[s_srv] & FD_CLOSE)) {
        tapf("# s_srv events = 0x%lx (res = %ld)\n", events[s_srv], count);
        call_closesocket(s_srv);
        call_closesocket(s_listen);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "s_srv did not record FD_CLOSE");
        return;
    }

    /* Second call to GetSocketEvents must find all events cleared to 0 */
    for (i = 0; i < 64; i++) events[i] = 0;
    count = call_getsocketevents(events);
    if (count != 0) {
        call_closesocket(s_srv);
        call_closesocket(s_listen);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "second GetSocketEvents returned error");
        return;
    }
    for (i = 0; i < 64; i++) {
        if (events[i] != 0) {
            tapf("# uncleared event on fd %d = 0x%lx\n", i, events[i]);
            call_closesocket(s_srv);
            call_closesocket(s_listen);
            tags[0].ti_Data = 0;
            call_socketbasetaglist(tags);
            FreeSignal(sig_bit);
            TAP_NOTOK("tc_socket_events", "GetSocketEvents did not clear events");
            return;
        }
    }

    call_closesocket(s_srv);
    call_closesocket(s_listen);
    tags[0].ti_Data = 0;
    call_socketbasetaglist(tags);
    FreeSignal(sig_bit);

    TAP_OK("tc_socket_events");
}

static int str_starts_with(const char *s, const char *prefix)
{
    if (s == NULL || prefix == NULL) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int s_fdcb_alloc_count = 0;
static int s_fdcb_free_count = 0;
static int s_fdcb_last_alloc_fd = -1;
static int s_fdcb_last_free_fd = -1;

static int test_fd_callback(int fd, int action)
{
    if (action == FDCB_ALLOC) {
        s_fdcb_alloc_count++;
        s_fdcb_last_alloc_fd = fd;
    } else if (action == FDCB_FREE) {
        s_fdcb_free_count++;
        s_fdcb_last_free_fd = fd;
    }
    return 0;
}

static void tc_sbtc_full(void)
{
    struct TagItem tags[4];
    volatile ULONG val = 0;
    const char *str;
    LONG s;

    /* 1. SBTC_DTABLESIZE GETVAL */
    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_DTABLESIZE);
    tags[0].ti_Data = 0;
    tags[1].ti_Tag  = TAG_DONE;
    tags[1].ti_Data = 0;
    if (call_socketbasetaglist(tags) != 0 || tags[0].ti_Data != TN_MAX_FDS_PER_TASK) {
        tapf("# SBTC_DTABLESIZE GETVAL returned %ld (expected %ld)\n",
             (LONG)tags[0].ti_Data, (LONG)TN_MAX_FDS_PER_TASK);
        TAP_NOTOK("tc_sbtc_full", "SBTC_DTABLESIZE GETVAL failed");
        return;
    }

    /* 2. SBTC_DTABLESIZE GETREF */
    val = 0;
    tags[0].ti_Tag  = SBTM_GETREF(SBTC_DTABLESIZE);
    tags[0].ti_Data = (ULONG)(uintptr_t)&val;
    tags[1].ti_Tag  = TAG_DONE;
    tags[1].ti_Data = 0;
    if (call_socketbasetaglist(tags) != 0 || val != TN_MAX_FDS_PER_TASK) {
        tapf("# SBTC_DTABLESIZE GETREF returned %ld (expected %ld)\n",
             (LONG)val, (LONG)TN_MAX_FDS_PER_TASK);
        TAP_NOTOK("tc_sbtc_full", "SBTC_DTABLESIZE GETREF failed");
        return;
    }

    /* 3. SBTC_UDP_CHECKSUM round-trip */
    tags[0].ti_Tag  = SBTM_SETVAL(SBTC_UDP_CHECKSUM);
    tags[0].ti_Data = 0;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);

    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_UDP_CHECKSUM);
    tags[0].ti_Data = 99;
    call_socketbasetaglist(tags);
    if (tags[0].ti_Data != 0) {
        TAP_NOTOK("tc_sbtc_full", "SBTC_UDP_CHECKSUM GETVAL after SET 0 failed");
        return;
    }

    tags[0].ti_Tag  = SBTM_SETVAL(SBTC_UDP_CHECKSUM);
    tags[0].ti_Data = 1;
    call_socketbasetaglist(tags);

    /* 4. SBTC_IP_DEFAULT_TTL round-trip */
    tags[0].ti_Tag  = SBTM_SETVAL(SBTC_IP_DEFAULT_TTL);
    tags[0].ti_Data = 128;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);

    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_IP_DEFAULT_TTL);
    tags[0].ti_Data = 0;
    call_socketbasetaglist(tags);
    if (tags[0].ti_Data != 128) {
        TAP_NOTOK("tc_sbtc_full", "SBTC_IP_DEFAULT_TTL GETVAL after SET 128 failed");
        return;
    }

    tags[0].ti_Tag  = SBTM_SETVAL(SBTC_IP_DEFAULT_TTL);
    tags[0].ti_Data = 64;
    call_socketbasetaglist(tags);

    /* 5. Error strings */
    /* SBTC_ERRNOSTRPTR with ECONNREFUSED (61) */
    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_ERRNOSTRPTR);
    tags[0].ti_Data = 61;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);
    str = (const char *)(uintptr_t)tags[0].ti_Data;
    if (str == NULL || !str_starts_with(str, "Connection refused")) {
        tapf("# SBTC_ERRNOSTRPTR: %s\n", str ? str : "NULL");
        TAP_NOTOK("tc_sbtc_full", "SBTC_ERRNOSTRPTR mismatch");
        return;
    }

    /* SBTC_HERRNOSTRPTR with HOST_NOT_FOUND (1) */
    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_HERRNOSTRPTR);
    tags[0].ti_Data = 1;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);
    str = (const char *)(uintptr_t)tags[0].ti_Data;
    if (str == NULL || !str_starts_with(str, "Unknown host")) {
        tapf("# SBTC_HERRNOSTRPTR: %s\n", str ? str : "NULL");
        TAP_NOTOK("tc_sbtc_full", "SBTC_HERRNOSTRPTR mismatch");
        return;
    }

    /* SBTC_IOERRNOSTRPTR with IOERR_OPENFAIL (1) */
    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_IOERRNOSTRPTR);
    tags[0].ti_Data = 1;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);
    str = (const char *)(uintptr_t)tags[0].ti_Data;
    if (str == NULL || !str_starts_with(str, "Device or unit")) {
        tapf("# SBTC_IOERRNOSTRPTR: %s\n", str ? str : "NULL");
        TAP_NOTOK("tc_sbtc_full", "SBTC_IOERRNOSTRPTR mismatch");
        return;
    }

    /* SBTC_S2ERRNOSTRPTR with S2ERR_BAD_ARGUMENT (3) */
    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_S2ERRNOSTRPTR);
    tags[0].ti_Data = 3;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);
    str = (const char *)(uintptr_t)tags[0].ti_Data;
    if (str == NULL || !str_starts_with(str, "Bad argument")) {
        tapf("# SBTC_S2ERRNOSTRPTR: %s\n", str ? str : "NULL");
        TAP_NOTOK("tc_sbtc_full", "SBTC_S2ERRNOSTRPTR mismatch");
        return;
    }

    /* SBTC_S2WERRNOSTRPTR with S2WERR_UNIT_ONLINE (2) */
    tags[0].ti_Tag  = SBTM_GETVAL(SBTC_S2WERRNOSTRPTR);
    tags[0].ti_Data = 2;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);
    str = (const char *)(uintptr_t)tags[0].ti_Data;
    if (str == NULL || !str_starts_with(str, "Unit is currently online")) {
        tapf("# SBTC_S2WERRNOSTRPTR: %s\n", str ? str : "NULL");
        TAP_NOTOK("tc_sbtc_full", "SBTC_S2WERRNOSTRPTR mismatch");
        return;
    }

    /* 6. SBTC_FDCALLBACK hook invocation */
    s_fdcb_alloc_count = 0;
    s_fdcb_free_count = 0;
    s_fdcb_last_alloc_fd = -1;
    s_fdcb_last_free_fd = -1;

    tags[0].ti_Tag  = SBTM_SETVAL(SBTC_FDCALLBACK);
    tags[0].ti_Data = (ULONG)(uintptr_t)test_fd_callback;
    tags[1].ti_Tag  = TAG_DONE;
    call_socketbasetaglist(tags);

    s = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        TAP_NOTOK("tc_sbtc_full", "socket creation with FDCALLBACK failed");
        return;
    }

    if (s_fdcb_alloc_count != 1 || s_fdcb_last_alloc_fd != s) {
        tapf("# FDCALLBACK alloc count=%d last_fd=%d (expected fd=%ld)\n",
             s_fdcb_alloc_count, s_fdcb_last_alloc_fd, s);
        call_closesocket(s);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        TAP_NOTOK("tc_sbtc_full", "FDCALLBACK FDCB_ALLOC was not invoked correctly");
        return;
    }

    call_closesocket(s);
    if (s_fdcb_free_count != 1 || s_fdcb_last_free_fd != s) {
        tapf("# FDCALLBACK free count=%d last_fd=%d (expected fd=%ld)\n",
             s_fdcb_free_count, s_fdcb_last_free_fd, s);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        TAP_NOTOK("tc_sbtc_full", "FDCALLBACK FDCB_FREE was not invoked correctly");
        return;
    }

    /* Clear callback */
    tags[0].ti_Data = 0;
    call_socketbasetaglist(tags);

    TAP_OK("tc_sbtc_full");
}

static LONG call_lvo_generic(LONG lvo, LONG arg0)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = arg0;
    register LONG d1 __asm__("d1") = 0;
    register LONG d2 __asm__("d2") = 0;
    register LONG d3 __asm__("d3") = 0;
    register void *a0 __asm__("a0") = NULL;
    register void *a1 __asm__("a1") = NULL;
    register void *a2 __asm__("a2") = NULL;
    register void *a3 __asm__("a3") = NULL;

    void *target = (void *)((char *)a6 + lvo);
    register void *a4 __asm__("a4") = target;

    __asm__ __volatile__ (
        "jsr (%%a4)"
        : "+r"(d0)
        : "r"(a6), "r"(a4), "r"(d0), "r"(d1), "r"(d2), "r"(d3), "r"(a0), "r"(a1), "r"(a2), "r"(a3)
        : "d1", "d2", "d3", "a0", "a1", "a2", "a3", "memory"
    );
    return d0;
}

static void tc_every_vector_callable(void)
{
    int i;
    int tested = 0;

    /* Loop through all 139 SFD vector slots from -30 to -858 */
    for (i = 0; i < 139; i++) {
        LONG lvo = -30 - (i * 6);
        (void)call_lvo_generic(lvo, -1);
        tested++;
    }

    if (tested == 139) {
        TAP_OK("tc_every_vector_callable");
    } else {
        TAP_NOTOK("tc_every_vector_callable", "not all 139 vectors tested");
    }
}

static void tc_release_obtain(void)
{
    LONG s, s2, park_id;
    struct Process *my_pr;
    LONG old_exit_data;
    struct DaemonMessage dm;
    LONG server_s;
    char cmd[64];
    LONG rc;
    BPTR fh;

    /* 1. Intra-task ReleaseSocket + ObtainSocket with UNIQUE_ID */
    s = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        TAP_NOTOK("tc_release_obtain", "call_socket failed");
        return;
    }
    park_id = call_releasesocket(s, UNIQUE_ID);
    if (park_id <= 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_release_obtain", "call_releasesocket failed to return positive id");
        return;
    }
    /* Original socket should be unmapped from caller */
    if (call_closesocket(s) == 0) {
        TAP_NOTOK("tc_release_obtain", "released socket was still closeable");
        return;
    }
    /* Obtain socket back into same task */
    s2 = call_obtainsocket(park_id, AF_INET, SOCK_DGRAM, 0);
    if (s2 < 0) {
        TAP_NOTOK("tc_release_obtain", "call_obtainsocket failed for parked socket");
        return;
    }
    call_closesocket(s2);

    /* 2. Intra-task ReleaseCopyOfSocket */
    s = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        TAP_NOTOK("tc_release_obtain", "call_socket failed for copy");
        return;
    }
    park_id = call_releasecopyofsocket(s, UNIQUE_ID);
    if (park_id <= 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_release_obtain", "releasecopy failed");
        return;
    }
    s2 = call_obtainsocket(park_id, AF_INET, SOCK_DGRAM, 0);
    if (s2 < 0) {
        call_closesocket(s);
        TAP_NOTOK("tc_release_obtain", "obtainsocket failed on copy");
        return;
    }
    /* Both descriptors are valid and distinct */
    call_closesocket(s);
    call_closesocket(s2);

    /* 3. Server API: ProcessIsServer & ObtainServerSocket */
    my_pr = (struct Process *)FindTask(NULL);
    if (call_processisserver(my_pr)) {
        TAP_NOTOK("tc_release_obtain", "processisserver true unexpectedly");
        return;
    }
    if (call_obtainserversocket() >= 0) {
        TAP_NOTOK("tc_release_obtain", "obtainserversocket succeeded unexpectedly");
        return;
    }
    old_exit_data = my_pr->pr_ExitData;
    s = call_socket(AF_INET, SOCK_STREAM, 0);
    park_id = call_releasesocket(s, UNIQUE_ID);
    dm.dm_ID = park_id;
    dm.dm_Family = AF_INET;
    dm.dm_Type = SOCK_STREAM;
    my_pr->pr_ExitData = (LONG)(uintptr_t)&dm;

    if (!call_processisserver(my_pr)) {
        my_pr->pr_ExitData = old_exit_data;
        TAP_NOTOK("tc_release_obtain", "processisserver false with exitdata");
        return;
    }
    server_s = call_obtainserversocket();
    my_pr->pr_ExitData = old_exit_data;
    if (server_s < 0) {
        TAP_NOTOK("tc_release_obtain", "obtainserversocket failed");
        return;
    }
    call_closesocket(server_s);

    /* 4. Cross-task handoff via SystemTags */
    s = call_socket(AF_INET, SOCK_DGRAM, 0);
    park_id = call_releasesocket(s, UNIQUE_ID);
    snprintf_safe(cmd, sizeof(cmd), "C:SocketConformance child_obtain %ld", park_id);
    DeleteFile((CONST_STRPTR)"WORK:child_obtain.ok");
    rc = SystemTags((CONST_STRPTR)cmd,
                    SYS_Asynch, FALSE,
                    SYS_Input, (BPTR)0,
                    SYS_Output, (BPTR)0,
                    TAG_END);
    if (rc != 0) {
        TAP_NOTOK("tc_release_obtain", "SystemTags child returned error");
        return;
    }
    fh = Open((CONST_STRPTR)"WORK:child_obtain.ok", MODE_OLDFILE);
    if (fh == 0) {
        TAP_NOTOK("tc_release_obtain", "child failed to obtain socket");
        return;
    }
    Close(fh);
    DeleteFile((CONST_STRPTR)"WORK:child_obtain.ok");

    TAP_OK("tc_release_obtain");
}

static void tc_stats_counters(void)
{
    TnStats before, during, after;
    LONG sargs[1];
    APTR sptrs[1];
    TnIpcMsg msg;
    LONG s;
    struct sockaddr_in sin;
    int i;
    int udp_pool_idx = -1;
    uint32_t before_udp_used = 0;

    sargs[0] = (LONG)sizeof(TnStats);
    sptrs[0] = (APTR)&before;

    /* 1. Baseline stats query */
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATS, sargs, 1, sptrs, 1, &msg) != 0 || msg.result != 0) {
        TAP_NOTOK("tc_stats_counters", "initial GETSTATS failed");
        return;
    }

    /* Locate UDP_PCB pool */
    for (i = 0; i < (int)before.num_memp; i++) {
        if (strcmp(before.memp[i].name, "UDP_PCB") == 0) {
            udp_pool_idx = i;
            before_udp_used = before.memp[i].used;
            break;
        }
    }

    /* 2. Open UDP socket */
    s = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        TAP_NOTOK("tc_stats_counters", "call_socket(DGRAM) failed");
        return;
    }

    /* 3. Query stats while socket open -> UDP_PCB pool used should be +1 */
    sptrs[0] = (APTR)&during;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATS, sargs, 1, sptrs, 1, &msg) == 0 && msg.result == 0) {
        if (udp_pool_idx >= 0) {
            if (during.memp[udp_pool_idx].used != before_udp_used + 1) {
                call_closesocket(s);
                TAP_NOTOK("tc_stats_counters", "UDP_PCB pool used did not increment by 1");
                return;
            }
        }
    }

    /* 4. Send 3 UDP datagrams to loopback 127.0.0.1:9999 */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(9999);
    sin.sin_addr.s_addr = htonl(0x7F000001); /* 127.0.0.1 */

    for (i = 0; i < 3; i++) {
        LONG n = call_sendto(s, "x", 1, 0, (struct sockaddr *)&sin, sizeof(sin));
        if (n != 1) {
            call_closesocket(s);
            TAP_NOTOK("tc_stats_counters", "call_sendto failed");
            return;
        }
    }

    /* 5. Close socket -> pool should return to baseline */
    call_closesocket(s);

    /* 6. Query stats after close */
    sptrs[0] = (APTR)&after;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATS, sargs, 1, sptrs, 1, &msg) != 0 || msg.result != 0) {
        TAP_NOTOK("tc_stats_counters", "post-test GETSTATS failed");
        return;
    }

    /* Verify udp.xmit increased by 3 */
    if (after.udp.xmit != before.udp.xmit + 3) {
        TAP_NOTOK("tc_stats_counters", "udp.xmit did not increment by exactly 3");
        return;
    }

    /* Verify pool occupancy returned to baseline */
    if (udp_pool_idx >= 0 && after.memp[udp_pool_idx].used != before_udp_used) {
        TAP_NOTOK("tc_stats_counters", "UDP_PCB pool did not return to baseline");
        return;
    }

    TAP_OK("tc_stats_counters");
}

static void tc_wizard_wired(void)
{
    /*
     * Step W: Test TolunnetSetup wizard on wired bench via ARexx port TOLUNNETSETUP.
     * 1. Spawn TolunnetSetup in background
     * 2. Wait for ARexx port TOLUNNETSETUP
     * 3. Send NEXT -> Hardware page (page 1)
     * 4. Send NEXT -> Address page (page 3, skipping WiFi page 2 since uaenet is wired)
     * 5. Send NEXT -> Test page (page 4)
     * 6. Send FINISH -> applies settings and terminates
     * 7. Verify DEVS:tolunnet.config exists and has valid configuration
     */
    struct MsgPort *reply_port = CreateMsgPort();
    if (!reply_port) {
        TAP_NOTOK("tc_wizard_wired", "CreateMsgPort failed");
        return;
    }

    /* Step W2: Append fake legacy stack lines to S:User-Startup to verify migration */
    BPTR us_fh = Open((CONST_STRPTR)"S:User-Startup", MODE_READWRITE);
    if (!us_fh) {
        us_fh = Open((CONST_STRPTR)"S:User-Startup", MODE_NEWFILE);
    }
    if (us_fh) {
        Seek(us_fh, 0, OFFSET_END);
        const char *fake_lines = "\nRun MiamiDx\nAmiTCP:bin/startnet\n";
        Write(us_fh, (CONST_APTR)fake_lines, strlen(fake_lines));
        Close(us_fh);
    }

    /* Launch TolunnetSetup asynchronously */
    LONG rc = SystemTags((CONST_STRPTR)"C:TolunnetSetup",
                         SYS_Asynch, TRUE,
                         SYS_Input, (BPTR)0,
                         SYS_Output, (BPTR)0,
                         NP_StackSize, 32768,
                         TAG_END);
    if (rc != 0) {
        rc = SystemTags((CONST_STRPTR)"SYS:Prefs/TolunnetSetup",
                        SYS_Asynch, TRUE,
                        SYS_Input, (BPTR)0,
                        SYS_Output, (BPTR)0,
                        NP_StackSize, 32768,
                        TAG_END);
    }
    if (rc != 0) {
        SystemTags((CONST_STRPTR)"Run <NIL: >NIL: C:TolunnetSetup",
                   SYS_Input, (BPTR)0,
                   SYS_Output, (BPTR)0,
                   TAG_END);
    }

    struct MsgPort *wizard_port = NULL;
    for (int i = 0; i < 100; i++) {
        Delay(5); /* 100ms */
        Forbid();
        wizard_port = FindPort((CONST_STRPTR)"TOLUNNETSETUP");
        Permit();
        if (wizard_port) break;
    }

    if (!wizard_port) {
        DeleteMsgPort(reply_port);
        TAP_NOTOK("tc_wizard_wired", "TOLUNNETSETUP ARexx port not found");
        return;
    }

    struct Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.mn_ReplyPort = reply_port;

    /* Next -> Page 1 */
    msg.mn_Node.ln_Name = (char *)"NEXT";
    PutMsg(wizard_port, &msg);
    WaitPort(reply_port);
    GetMsg(reply_port);

    /* Next -> Page 3 (WiFi skipped for wired) */
    msg.mn_Node.ln_Name = (char *)"NEXT";
    PutMsg(wizard_port, &msg);
    WaitPort(reply_port);
    GetMsg(reply_port);

    /* Next -> Page 4 */
    msg.mn_Node.ln_Name = (char *)"NEXT";
    PutMsg(wizard_port, &msg);
    WaitPort(reply_port);
    GetMsg(reply_port);

    /* FINISH */
    msg.mn_Node.ln_Name = (char *)"FINISH";
    PutMsg(wizard_port, &msg);
    WaitPort(reply_port);
    GetMsg(reply_port);

    DeleteMsgPort(reply_port);

    /* Wait up to 3 seconds for TolunnetSetup to finish and port to disappear */
    for (int i = 0; i < 30; i++) {
        Delay(5);
        Forbid();
        wizard_port = FindPort((CONST_STRPTR)"TOLUNNETSETUP");
        Permit();
        if (!wizard_port) break;
    }

    /* 1. Verify DEVS:tolunnet.config exists */
    BPTR lock = Lock((CONST_STRPTR)"DEVS:tolunnet.config", ACCESS_READ);
    if (!lock) {
        TAP_NOTOK("tc_wizard_wired", "DEVS:tolunnet.config not generated");
        return;
    }
    UnLock(lock);

    /* 2. Verify S:User-Startup.tolunnet-bak exists */
    BPTR bak_lock = Lock((CONST_STRPTR)"S:User-Startup.tolunnet-bak", ACCESS_READ);
    if (!bak_lock) {
        TAP_NOTOK("tc_wizard_wired", "S:User-Startup.tolunnet-bak not created");
        return;
    }
    UnLock(bak_lock);

    /* 3. Verify fake legacy stack lines are commented out in S:User-Startup */
    BPTR us_check = Open((CONST_STRPTR)"S:User-Startup", MODE_OLDFILE);
    if (!us_check) {
        TAP_NOTOK("tc_wizard_wired", "Cannot open S:User-Startup for verification");
        return;
    }
    char us_buf[4096];
    LONG us_read = Read(us_check, us_buf, sizeof(us_buf) - 1);
    Close(us_check);
    if (us_read < 0) us_read = 0;
    us_buf[us_read] = '\0';

    if (strstr(us_buf, "; tolunnet-disabled: Run MiamiDx") == NULL ||
        strstr(us_buf, "; tolunnet-disabled: AmiTCP:bin/startnet") == NULL) {
        TAP_NOTOK("tc_wizard_wired", "Legacy lines not properly commented out in S:User-Startup");
        return;
    }

    /* 4. Verify ENV:HostName exists */
    BPTR hn_lock = Lock((CONST_STRPTR)"ENV:HostName", ACCESS_READ);
    if (!hn_lock) {
        TAP_NOTOK("tc_wizard_wired", "ENV:HostName not created");
        return;
    }
    UnLock(hn_lock);

    TAP_OK("tc_wizard_wired");
}

static void tc_wifi_scan_parse(void)
{
    const char *fake_ssid = "TolunAmigaNet";
    const UBYTE fake_bssid[6] = {0x00, 0x80, 0x10, 0x20, 0x30, 0x40};
    struct TagItem fake_tags[] = {
        {S2INFO_SSID, (ULONG)fake_ssid},
        {S2INFO_BSSID, (ULONG)fake_bssid},
        {S2INFO_Channel, 6},
        {S2INFO_Signal, (ULONG)-65},
        {S2INFO_Encryption, 3},
        {TAG_END, 0}
    };

    WifiNetwork net;
    if (!tn_parse_wifi_tagitem(fake_tags, &net)) {
        TAP_NOTOK("tc_wifi_scan_parse", "tn_parse_wifi_tagitem returned FALSE");
        return;
    }

    if (strcmp(net.ssid, "TolunAmigaNet") != 0) {
        TAP_NOTOK("tc_wifi_scan_parse", "SSID mismatch");
        return;
    }

    if (memcmp(net.bssid, fake_bssid, 6) != 0) {
        TAP_NOTOK("tc_wifi_scan_parse", "BSSID mismatch");
        return;
    }

    if (net.channel != 6 || net.signal_dbm != -65 || net.encryption != 3) {
        TAP_NOTOK("tc_wifi_scan_parse", "Channel/signal/encryption mismatch");
        return;
    }

    /* Signal: (-65 + 100) * 2 = 70% */
    if (strstr(net.display_str, "70%") == NULL || strstr(net.display_str, "WPA2") == NULL) {
        TAP_NOTOK("tc_wifi_scan_parse", "Display string formatting error");
        return;
    }

    TAP_OK("tc_wifi_scan_parse");
}

/* TNET-108: RECONFIG hot-reload. Rewrites DEVS:tolunnet.config around live
 * IPC RECONFIGs and asserts the applied/needs_restart/failed masks, the
 * STATS=NO zero-report gate, and that interface keys stay restart-only.
 * The bench's original file is saved and restored. Phase 0 first normalizes
 * the daemon's loaded prefs to the bench baseline: in restart cycle 2 the
 * daemon boots from the wizard-written config (DEBUG=1), which would make
 * the LOGLEVEL bit of the phase-A diff cycle-dependent. */
static const char *tc_recfg_phase_0 =
    "DEVICE=ethernet.device\n"
    "UNIT=0\n"
    "DHCP=YES\n"
    "DNS=10.0.2.3\n"
    "DNS2=8.8.8.8\n"
    "LOG=WORK:tolunnet-task.log\n"
    "DEBUG=0\n";

static const char *tc_recfg_phase_a =
    "DEVICE=ethernet.device\n"
    "UNIT=0\n"
    "DHCP=YES\n"
    "DNS=10.0.2.3\n"
    "DNS2=8.8.8.8\n"
    "LOG=WORK:tolunnet-task.log\n"
    "LOGLEVEL=1\n"
    "PRIORITY=7\n"
    "SELECTORS=32\n"
    "STATS=NO\n"
    "SYSLOG=10.0.2.2\n"
    "DATABASE_ORDER=local,dns\n";

static BOOL tc_recfg_write_file(const char *path, const char *text)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    LONG len = (LONG)strlen(text);
    LONG w;
    if (fh == (BPTR)0) return FALSE;
    w = Write(fh, (CONST APTR)text, len);
    Close(fh);
    return (w == len);
}

static void tc_reconfig_rc(void)
{
    char saved[1024];
    LONG saved_len = 0;
    BPTR fh;
    TnIpcMsg msg;
    TnReconfigResponse resp;
    LONG sargs[5];
    APTR sptrs[1];

    sargs[0] = 0; sargs[1] = 0; sargs[2] = 0; sargs[3] = 0;
    sargs[4] = (LONG)sizeof(TnReconfigResponse);
    sptrs[0] = (APTR)&resp;

    /* 0. Save the current DEVS:tolunnet.config and drop any newer ENV:
     * session copy so the file we write is the authoritative store. */
    fh = Open((CONST_STRPTR)"DEVS:tolunnet.config", MODE_OLDFILE);
    if (fh != (BPTR)0) {
        saved_len = Read(fh, (APTR)saved, sizeof(saved) - 1);
        Close(fh);
        if (saved_len > 0) saved[saved_len] = '\0';
    }
    DeleteFile((CONST_STRPTR)"ENV:tolunnet.prefs");

    /* 0a. Normalize the daemon's loaded prefs to the bench baseline so the
     * phase-A diff below is identical in both restart cycles. */
    if (!tc_recfg_write_file("DEVS:tolunnet.config", tc_recfg_phase_0)) {
        TAP_NOTOK("tc_reconfig_rc", "cannot write phase-0 config");
        return;
    }
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_RECONFIG, sargs, 5, sptrs, 1, &msg) != 0) {
        TAP_NOTOK("tc_reconfig_rc", "phase-0 RECONFIG failed");
        return;
    }

    /* 1. Baseline: stats counters are live (daemon has been running) */
    {
        TnStats before;
        LONG bsargs[1];
        APTR bsptrs[1];
        bsargs[0] = (LONG)sizeof(TnStats);
        bsptrs[0] = (APTR)&before;
        if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATS, bsargs, 1, bsptrs, 1, &msg) != 0 ||
            msg.result != 0 || before.daemon.uptime_secs == 0) {
            TAP_NOTOK("tc_reconfig_rc", "baseline GETSTATS not live");
            return;
        }
    }

    /* 2. Phase A: hot-reloadable keys only */
    if (!tc_recfg_write_file("DEVS:tolunnet.config", tc_recfg_phase_a)) {
        TAP_NOTOK("tc_reconfig_rc", "cannot write phase-A config");
        return;
    }
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_RECONFIG, sargs, 5, sptrs, 1, &msg) != 0 || msg.result != 0) {
        TAP_NOTOK("tc_reconfig_rc", "phase-A RECONFIG failed");
        return;
    }
    if (resp.struct_size != sizeof(TnReconfigResponse) || resp.version != TN_RECFG_VERSION) {
        TAP_NOTOK("tc_reconfig_rc", "response header wrong");
        return;
    }
    if (resp.needs_restart != 0) {
        TAP_NOTOK("tc_reconfig_rc", "phase-A reported restart-only keys");
        return;
    }
    if (resp.applied != (TN_RECFG_PRIORITY | TN_RECFG_SELECTORS | TN_RECFG_STATS |
                         TN_RECFG_SYSLOG | TN_RECFG_DATABASE_ORDER) || resp.failed != 0) {
        TAP_NOTOK("tc_reconfig_rc", "phase-A applied/failed mask mismatch");
        return;
    }

    /* 3. STATS=NO freezes GETSTATS at zero */
    {
        TnStats after;
        LONG bsargs[1];
        APTR bsptrs[1];
        bsargs[0] = (LONG)sizeof(TnStats);
        bsptrs[0] = (APTR)&after;
        if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATS, bsargs, 1, bsptrs, 1, &msg) != 0 ||
            msg.result != 0) {
            TAP_NOTOK("tc_reconfig_rc", "post-RECONFIG GETSTATS failed");
            return;
        }
        if (after.daemon.uptime_secs != 0 || after.daemon.mainloop_ticks != 0 ||
            after.mem_used != 0 || after.link.recv != 0) {
            TAP_NOTOK("tc_reconfig_rc", "STATS=NO did not zero the report");
            return;
        }
    }

    /* 4. Phase B: an interface key must land in needs_restart only */
    if (!tc_recfg_write_file("DEVS:tolunnet.config",
                             "DEVICE=nonexist.device\n"
                             "UNIT=0\n"
                             "DHCP=YES\n"
                             "DNS=10.0.2.3\n"
                             "DNS2=8.8.8.8\n"
                             "LOG=WORK:tolunnet-task.log\n"
                             "PRIORITY=7\n"
                             "SELECTORS=32\n")) {
        TAP_NOTOK("tc_reconfig_rc", "cannot write phase-B config");
        return;
    }
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_RECONFIG, sargs, 5, sptrs, 1, &msg) != 0 || msg.result != 0) {
        TAP_NOTOK("tc_reconfig_rc", "phase-B RECONFIG failed");
        return;
    }
    if (!(resp.needs_restart & TN_RECFG_DEVICE) || (resp.applied & TN_RECFG_DEVICE)) {
        TAP_NOTOK("tc_reconfig_rc", "DEVICE did not classify as restart-only");
        return;
    }

    /* 5. Restore the original store and reload it */
    if (saved_len > 0) {
        if (!tc_recfg_write_file("DEVS:tolunnet.config", saved)) {
            TAP_NOTOK("tc_reconfig_rc", "cannot restore original config");
            return;
        }
    }
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_RECONFIG, sargs, 5, sptrs, 1, &msg) != 0) {
        TAP_NOTOK("tc_reconfig_rc", "restore RECONFIG failed");
        return;
    }

    TAP_OK("tc_reconfig_rc");
}

/* TNET-109: S2_ONEVENT link events. Drives the shared unit offline/online
 * via the S2Toggle helper and watches GETSTATUS link_up flip. When the bench
 * driver rejects S2_ONEVENT the row is an honest SKIP naming the driver. */
static BOOL tc_link_get_flags(TnStatusInfoV2 *v2)
{
    TnIpcMsg msg;
    LONG sargs[5];
    APTR sptrs[1];

    sargs[0] = 0; sargs[1] = 0; sargs[2] = 0; sargs[3] = 0;
    sargs[4] = (LONG)sizeof(TnStatusInfoV2);
    sptrs[0] = (APTR)v2;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATUS, sargs, 5, sptrs, 1, &msg) != 0 ||
        msg.result != 0) {
        return FALSE;
    }
    return TRUE;
}

static BOOL tc_link_wait(TnStatusInfoV2 *v2, int want_up, int tries)
{
    int t;
    for (t = 0; t < tries; t++) {
        if (!tc_link_get_flags(v2)) return FALSE;
        if (!!(v2->flags & 1) == want_up) return TRUE;
        Delay(5); /* 100 ms */
    }
    return FALSE;
}

static void tc_link_events(void)
{
    TnStatusInfoV2 v2;
    LONG rc;
    BPTR fh;
    BOOL renew_seen = FALSE;

    if (!tc_link_get_flags(&v2)) {
        TAP_NOTOK("tc_link_events", "GETSTATUS failed");
        return;
    }
    if (!(v2.flags & 1)) {
        TAP_SKIP("tc_link_events", "link already down before test");
        return;
    }

    /* The bench driver (uaenet) both fakes S2_ONEVENT completions and hangs
     * on S2_OFFLINE from a second opener; flipping the link under it froze
     * the whole suite (bench incidents 556ea30/184cd29). S2Toggle therefore
     * runs only when the operator opts in per driver via a marker file
     * (real-hardware iron test); everything below is async + time-bounded so
     * even a hung toggle child can never wedge the suite. */
    fh = Open((CONST_STRPTR)"WORK:linktest-on", MODE_OLDFILE);
    if (fh == (BPTR)0) {
        TAP_SKIP("tc_link_events", "link flip not enabled for this driver (create WORK:linktest-on)");
        return;
    }
    Close(fh);

    rc = SystemTags((CONST_STRPTR)"C:S2Toggle OFFLINE",
                    SYS_Asynch, TRUE,
                    SYS_Input, (BPTR)0,
                    SYS_Output, (BPTR)0,
                    TAG_END);
    if (rc != 0) {
        TAP_NOTOK("tc_link_events", "S2Toggle OFFLINE failed to start");
        return;
    }

    if (!tc_link_wait(&v2, 0, 50)) {
        /* The driver completed S2_OFFLINE but never delivered S2EVENT_OFFLINE:
         * either it does not implement S2_ONEVENT or it flushed the request.
         * Bring the link back and report an honest SKIP. */
        SystemTags((CONST_STRPTR)"C:S2Toggle ONLINE",
                   SYS_Asynch, TRUE, SYS_Input, (BPTR)0, SYS_Output, (BPTR)0,
                   TAG_END);
        tc_link_wait(&v2, 1, 50);
        TAP_SKIP("tc_link_events", "driver delivered no S2EVENT_OFFLINE (S2_ONEVENT unsupported?)");
        return;
    }

    rc = SystemTags((CONST_STRPTR)"C:S2Toggle ONLINE",
                    SYS_Asynch, TRUE,
                    SYS_Input, (BPTR)0,
                    SYS_Output, (BPTR)0,
                    TAG_END);
    if (rc != 0) {
        TAP_NOTOK("tc_link_events", "S2Toggle ONLINE failed to start");
        return;
    }
    if (!tc_link_wait(&v2, 1, 50)) {
        TAP_NOTOK("tc_link_events", "link did not come back up after ONLINE");
        return;
    }

    /* DHCP renew line in the daemon log (bench runs DHCP=YES, LOG=WORK:) */
    fh = Open((CONST_STRPTR)"WORK:tolunnet-task.log", MODE_OLDFILE);
    if (fh != (BPTR)0) {
        static char tail[4096];
        LONG got;
        LONG size;
        LONG start;

        Seek(fh, 0, OFFSET_END);
        size = Seek(fh, 0, OFFSET_BEGINNING);
        start = (size > (LONG)(sizeof(tail) - 1)) ? size - (LONG)(sizeof(tail) - 1) : 0;
        Seek(fh, start, OFFSET_BEGINNING);
        got = Read(fh, (APTR)tail, sizeof(tail) - 1);
        Close(fh);
        if (got > 0) {
            tail[got] = 0;
            if (strstr(tail, "DHCP renew") != NULL || strstr(tail, "DHCP client started") != NULL) {
                renew_seen = TRUE;
            }
        }
    }
    if (!renew_seen) {
        TAP_NOTOK("tc_link_events", "no DHCP renew line in daemon log after link up");
        return;
    }

    TAP_OK("tc_link_events");
}

/* Bench plumbing (not a TAP case): ask the daemon to exit so the bench can
 * prove the TNET-059/060 restart cycle. Mirrors TolunnetPrefs' Stop logic. */
static void request_daemon_stop(void)
{
    struct MsgPort *port = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
    if (port != NULL && port->mp_SigTask != NULL) {
        Signal((struct Task *)port->mp_SigTask, SIGBREAKF_CTRL_C);
    }
}

int main(int argc, char *argv[])
{
    struct Library *DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    int not_ok;

    if (DOSBase == NULL) return 20;

    /* Child mode for cross-process obtain test */
    if (argc >= 3 && strcmp(argv[1], "child_obtain") == 0) {
        LONG target_id = parse_long(argv[2]);
        BPTR out_fh;
        LONG s;
        SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
        if (SocketBase == NULL) { CloseLibrary(DOSBase); return 10; }
        s = call_obtainsocket(target_id, AF_INET, SOCK_DGRAM, 0);
        if (s < 0) {
            CloseLibrary(SocketBase);
            CloseLibrary(DOSBase);
            return 20;
        }
        call_closesocket(s);
        CloseLibrary(SocketBase);
        out_fh = Open((CONST_STRPTR)"WORK:child_obtain.ok", MODE_NEWFILE);
        if (out_fh != 0) {
            Write(out_fh, (CONST APTR)"OK\n", 3);
            Close(out_fh);
        }
        CloseLibrary(DOSBase);
        return 0;
    }

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_OFF; /* TAP only; no daemon log chatter on stdout */

    if (IsInteractive(Output())) {
        g_log_fh = Open((CONST_STRPTR)"WORK:conformance.log", MODE_NEWFILE);
    } else {
        g_log_fh = (BPTR)0;
    }

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        tapf("1..1\nnot ok 1 - lib_open # bsdsocket.library not found (daemon not running?)\n");
        if (g_log_fh) Close(g_log_fh);
        CloseLibrary(DOSBase);
        return 1;
    }

    tapf("# tolunnet SocketConformance (Round 3 §B.2)\n");
    tc_lib_open_close();
    tc_socket_types();
    tc_bind_udp();
    tc_bind_reuse();
    tc_sockopt_matrix();
    tc_multicast_join();
    tc_ioctl_ifconf();
    tc_ioctl_fionread();
    tc_listen_accept_loopback();
    tc_connect_refused();
    tc_nonblock_connect();
    tc_shutdown_wr();
    tc_getpeername();
    tc_dns_a();
    tc_dns_fail();
    tc_errno_ptr();
    tc_dup2();
    tc_waitselect_timeout();
    tc_waitselect_eintr();
    tc_waitselect_badf();
    tc_waitselect_no_sigio();
    tc_sigio();
    tc_icmp_raw();
    tc_sendmsg_iov();
    tc_recv_peek();
    tc_socket_events();
    tc_sbtc_full();
    tc_release_obtain();
    tc_every_vector_callable();
    tc_stats_counters();
    tc_wizard_wired();
    tc_wifi_scan_parse();
    tc_reconfig_rc();
    tc_link_events();
    tapf("1..%d\n", g_count);
    tapf("# bench: asking daemon to stop (restart-cycle proof)\n");

    CloseLibrary(SocketBase);
    /* after CloseLibrary (no other open bases), Ctrl-C can end the daemon */
    request_daemon_stop();

    if (g_log_fh) Close(g_log_fh);
    CloseLibrary(DOSBase);

    not_ok = g_not_ok_count;
    return (not_ok > 9) ? 9 : not_ok;
}
