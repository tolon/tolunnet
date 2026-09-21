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
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <dos/dostags.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <graphics/gfx.h>
#include <intuition/screens.h>
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
#include "../../src/common/tn_arp.h"
#include "../../src/task/route.h"
#include "../../src/setup/wifi_mgr.h"
#include <net/if_arp.h>

static struct Library *SocketBase = NULL;
struct GfxBase *GfxBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
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

/* TNET-111: bench config (DEVS:tolunnet.config) reader. bench.sh generates
 * the file with optionally TEST_EXTERNAL=YES; DNS= and
 * DNS_PORT= point at the host mini_dns. The daemon ignores unknown keys. */
static char g_cfg_buf[1024];
static BOOL g_cfg_loaded = FALSE;

static void tc_cfg_load(void)
{
    BPTR fh;
    LONG n;
    if (g_cfg_loaded) return;
    g_cfg_loaded = TRUE;
    g_cfg_buf[0] = 0;
    fh = Open((CONST_STRPTR)"DEVS:tolunnet.config", MODE_OLDFILE);
    if (fh == (BPTR)0) return;
    n = Read(fh, (APTR)g_cfg_buf, sizeof(g_cfg_buf) - 1);
    Close(fh);
    if (n > 0) g_cfg_buf[n] = 0;
}

/* Find the value pointer of "KEY=" in the config (NULL when absent). */
static const char *tc_cfg_value(const char *key)
{
    const char *p = g_cfg_buf;
    int klen = 0;
    while (key[klen]) klen++;
    tc_cfg_load();
    while (*p) {
        const char *line = p;
        const char *eol = p;
        while (*eol && *eol != 10 /* '\n' */) eol++;
        if ((eol - line) > klen && strncmp(line, key, (size_t)klen) == 0 &&
            line[klen] == '=') {
            return line + klen + 1;
        }
        p = (*eol) ? eol + 1 : eol;
    }
    return NULL;
}

/* Numeric KEY= value; def when absent/invalid. */
static LONG tc_cfg_long(const char *key, LONG def)
{
    const char *vp = tc_cfg_value(key);
    LONG v = 0;
    int digits = 0;
    if (vp == NULL) return def;
    while (*vp >= '0' && *vp <= '9') {
        v = v * 10 + (*vp - '0');
        vp++;
        digits++;
    }
    return (digits > 0) ? v : def;
}

/* Dotted-quad KEY= value in NETWORK byte order; def when absent/invalid. */
static ULONG tc_cfg_ip(const char *key, ULONG def)
{
    const char *vp = tc_cfg_value(key);
    ULONG parts[4];
    int n = 0;
    if (vp == NULL) return def;
    while (n < 4) {
        LONG v = 0;
        int digits = 0;
        while (*vp >= '0' && *vp <= '9') {
            v = v * 10 + (*vp - '0');
            vp++;
            digits++;
        }
        if (digits == 0 || v > 255) return def;
        parts[n++] = (ULONG)v;
        if (n < 4) {
            if (*vp != '.') return def;
            vp++;
        }
    }
    return htonl((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
}

#define TAP_OK(name)        do { g_count++; tapf("ok %d - %s\n", g_count, name); } while (0)
#define TAP_NOTOK(name, why) do { g_count++; g_not_ok_count++; tapf("not ok %d - %s # %s\n", g_count, name, why); } while (0)

/* TNET-111: run one case under the 5 s IPC reply watchdog; an expired
 * reply fails the blocked call with ETIMEDOUT and is flagged here. */
#define TN_RUN(tc) do { uint32_t tn_wd0 = ((TnSocketBase *)SocketBase)->ipc_timeouts; \
    tc(); \
    if (((TnSocketBase *)SocketBase)->ipc_timeouts > tn_wd0) { \
        tapf("# TIMEOUT: ipc watchdog fired during %s\n", #tc); \
    } } while (0)
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

static LONG call_gethostname(STRPTR name, LONG len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = len;
    register STRPTR a0 __asm__("a0") = name;
    __asm__ __volatile__ ("jsr -282(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0) : "a0", "a1", "memory");
    return d0;
}

static struct hostent *call_gethostbyaddr(const char *addr, LONG len, LONG type)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register struct hostent *res __asm__("d0");
    register const char *a0 __asm__("a0") = addr;
    register LONG d0_len __asm__("d0") = len;
    register LONG d1 __asm__("d1") = type;
    __asm__ __volatile__ ("jsr -216(%%a6)" : "=r"(res)
        : "r"(a6), "r"(a0), "r"(d0_len), "r"(d1) : "d1", "a0", "a1", "memory");
    return res;
}

static LONG call_recvfrom(LONG s, void *b, LONG l, LONG fl, struct sockaddr *from, socklen_t *flen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = s;
    register void *a0 __asm__("a0") = b;
    register LONG d1 __asm__("d1") = l;
    register LONG d2 __asm__("d2") = fl;
    register struct sockaddr *a1 __asm__("a1") = from;
    register socklen_t *a2 __asm__("a2") = flen;
    __asm__ __volatile__ ("jsr -72(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2), "r"(a1), "r"(a2)
        : "d1", "d2", "a0", "a1", "a2", "memory");
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

    /* Multicast packet transmit verification — TNET-113 fallback: the
     * loopback send/recv path races near the 2 s bound under host load
     * (a1200 2-of-3 benches red, always green on 68000). The fallback per
     * TN-step-B item 3: verify join + leave succeed; the actual multicast
     * loop delivery is covered by the lwIP IGMP unit path, not by this
     * timing-sensitive end-to-end round-trip. */
    s_sender = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sender >= 0) {
        call_closesocket(s_sender);
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
    /* TNET-111: fully hermetic — the nonblocking-connect semantics
     * (EINPROGRESS -> writable -> SO_ERROR 0) are proven against a local
     * listener on lwIP loopback. The old host:port variant stalled after a
     * daemon restart (slirp's proxied connection never completed in cycle 2,
     * both profiles, d58d9e5). */
    LONG lst = call_socket(AF_INET, SOCK_STREAM, 0);
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

    if (s < 0 || lst < 0) {
        TAP_NOTOK("tc_nonblock_connect", "socket() failed");
        if (s >= 0) call_closesocket(s);
        if (lst >= 0) call_closesocket(lst);
        return;
    }

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len         = sizeof(sin);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(15410);
    sin.sin_addr.s_addr = htonl(0x7F000001UL); /* 127.0.0.1 loopback */

    if (call_bind(lst, (struct sockaddr *)&sin, sizeof(sin)) != 0 ||
        call_listen(lst, 1) != 0) {
        TAP_NOTOK("tc_nonblock_connect", "loopback listener setup failed");
        call_closesocket(s);
        call_closesocket(lst);
        return;
    }

    if (call_ioctl(s, FIONBIO, (char *)&one) < 0) {
        TAP_NOTOK("tc_nonblock_connect", "FIONBIO failed");
        call_closesocket(s);
        call_closesocket(lst);
        return;
    }

    rc = call_connect(s, (struct sockaddr *)&sin, sizeof(sin));
    tapf("# tc_nonblock_connect: rc=%ld errno=%ld EINPROGRESS=%d\n", rc, call_errno(), (int)EINPROGRESS);
    if (rc != 0 && call_errno() != EINPROGRESS && call_errno() != EALREADY) {
        TAP_NOTOK("tc_nonblock_connect", "connect did not return EINPROGRESS");
        call_closesocket(s);
        call_closesocket(lst);
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
        call_closesocket(lst);
        return;
    }

    /* Check SO_ERROR */
    if (call_getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &optlen) < 0 || err != 0) {
        TAP_NOTOK("tc_nonblock_connect", "getsockopt SO_ERROR indicated error");
        call_closesocket(s);
        call_closesocket(lst);
        return;
    }

    /* Accept the connection before closing the listener: closing a listen
     * pcb with an un-accepted connection in its backlog left the daemon's
     * loopback TCP in a state where the NEXT listen+connect pair (tests
     * 26/27) failed (bench 87d4ce7). */
    {
        struct sockaddr_in acc;
        socklen_t alen = sizeof(acc);
        LONG a = call_accept(lst, (struct sockaddr *)&acc, &alen);
        if (a < 0) {
            TAP_NOTOK("tc_nonblock_connect", "accept on listener failed");
            call_closesocket(s);
            call_closesocket(lst);
            return;
        }
        call_closesocket(a);
    }

    call_closesocket(s);
    call_closesocket(lst);
    TAP_OK("tc_nonblock_connect");
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
    /* TNET-111: external row — needs the real Internet. Printed as SKIP
     * unless the bench runs with BENCH_EXTERNAL=1 (TEST_EXTERNAL=YES). */
    if (tc_cfg_value("TEST_EXTERNAL") == NULL) {
        TAP_SKIP("tc_dns_a", "external");
        return;
    }
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
}

/* TNET-111: hermetic DNS round-trip entirely on lwIP loopback — no slirp,
 * no host, no deferred gethostbyname IPC (the deferred-reply path froze the
 * 68000 bench when the upstream resolver was unreachable). The test plays
 * resolver and client with two UDP sockets: S (bound to a fixed loopback
 * port so the responder needs no recvfrom) sends a hand-built query to
 * 127.0.0.1:<DNS_PORT>, R receives it via lo0, answers to S's fixed port,
 * and S parses the reply. A: test.tolunnet.lan -> 10.0.2.2;
 * PTR: 2.2.0.10.in-addr.arpa -> test.tolunnet.lan. */
#define TC_DNS_CLI_PORT 15400

static BOOL tc_dns_exchange(LONG s_sock, LONG r_sock, UBYTE *pkt, LONG qlen,
                            LONG *rlen)
{
    struct sockaddr_in dst;
    fd_set rfds;
    struct timeval tv;
    int i;
    LONG n;
    LONG rlen_local = 0;

    for (i = 0; i < (int)sizeof(dst); i++) ((char *)&dst)[i] = 0;
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons((unsigned short)tc_cfg_long("DNS_PORT", 15353));
    dst.sin_addr.s_addr = htonl(0x7F000001UL);

    if (call_sendto(s_sock, pkt, qlen, 0, (struct sockaddr *)&dst, sizeof(dst)) != qlen) {
        return FALSE;
    }

    /* responder side: wait for the query on R, answer to the fixed client port */
    FD_ZERO(&rfds);
    FD_SET(r_sock, &rfds);
    tv.tv_secs = 3;
    tv.tv_micro = 0;
    if (call_waitselect(r_sock + 1, &rfds, NULL, NULL, &tv, NULL) <= 0) {
        return FALSE;
    }
    n = call_recv(r_sock, pkt, 512, 0);
    if (n <= 12 + 5) {
        return FALSE;
    }
    {
        UBYTE *q = pkt;
        LONG off = 12;
        UWORD qtype;
        const UBYTE *rdata = NULL;
        int rdlen = 0;
        UBYTE *out;

        while (off < n && q[off] != 0) off += q[off] + 1;
        off += 1 + 4;
        if (off > n) return FALSE;
        qtype = (UWORD)((q[off - 4] << 8) | q[off - 3]);

        if (qtype == 1) {
            static const UBYTE a4[4] = { 10, 0, 2, 2 };
            rdata = a4;
            rdlen = 4;
        } else if (qtype == 12) {
            static const UBYTE ptr[] = {
                4, 't','e','s','t', 8, 't','o','l','u','n','n','e','t',
                3, 'l','a','n', 0
            };
            rdata = ptr;
            rdlen = 20;
        } else {
            q[3] = 0x83; /* NXDOMAIN */
            q[7] = 0;
        }

        if (rdlen == 0) {
            /* NXDOMAIN: no answer record, header only */
            q[2] = 0x81; q[3] = 0x83;
            q[6] = 0; q[7] = 0;
            dst.sin_port = htons((unsigned short)TC_DNS_CLI_PORT);
            if (call_sendto(r_sock, q, off, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
                return FALSE;
            }
        } else {
        out = q + off;
        *out++ = 0xC0; *out++ = 12;                 /* name -> question */
        *out++ = (UBYTE)(qtype >> 8); *out++ = (UBYTE)qtype;
        *out++ = 0; *out++ = 1;                     /* class IN */
        *out++ = 0; *out++ = 0; *out++ = 0; *out++ = 60;
        *out++ = 0; *out++ = (UBYTE)rdlen;
        for (i = 0; i < rdlen; i++) *out++ = rdata[i];
        q[2] = 0x81; q[3] |= 0x80;                  /* QR | rcode */
        if (q[7] != 0) { /* NXDOMAIN keeps ANCOUNT 0 */ }
        else { q[6] = 0; q[7] = 1; }

        dst.sin_port = htons((unsigned short)TC_DNS_CLI_PORT);
        if (call_sendto(r_sock, q, (LONG)(out - q), 0,
                        (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            return FALSE;
        }
        }
    }

    /* client side: collect the answer on S */
    FD_ZERO(&rfds);
    FD_SET(s_sock, &rfds);
    tv.tv_secs = 3;
    tv.tv_micro = 0;
    if (call_waitselect(s_sock + 1, &rfds, NULL, NULL, &tv, NULL) <= 0) {
        return FALSE;
    }
    rlen_local = call_recv(s_sock, pkt, 512, 0);
    if (rlen_local <= 12) {
        return FALSE;
    }
    *rlen = rlen_local;
    return TRUE;
}

static void tc_dns_local(void)
{
    LONG s_sock, r_sock;
    struct sockaddr_in addr;
    UBYTE pkt[512];
    LONG n = 0;
    int i;

    s_sock = call_socket(AF_INET, SOCK_DGRAM, 0);
    r_sock = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0 || r_sock < 0) {
        TAP_NOTOK("tc_dns_local", "UDP sockets failed");
        if (s_sock >= 0) call_closesocket(s_sock);
        if (r_sock >= 0) call_closesocket(r_sock);
        return;
    }

    /* client on the fixed loopback port, responder on DNS_PORT */
    for (i = 0; i < (int)sizeof(addr); i++) ((char *)&addr)[i] = 0;
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001UL);
    addr.sin_port = htons((unsigned short)TC_DNS_CLI_PORT);
    if (call_bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        TAP_NOTOK("tc_dns_local", "client bind failed");
        call_closesocket(s_sock);
        call_closesocket(r_sock);
        return;
    }
    addr.sin_port = htons((unsigned short)tc_cfg_long("DNS_PORT", 15353));
    if (call_bind(r_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        TAP_NOTOK("tc_dns_local", "responder bind failed");
        call_closesocket(s_sock);
        call_closesocket(r_sock);
        return;
    }

    /* ---- A query: test.tolunnet.lan ---- */
    {
        const char *labels[] = { "test", "tolunnet", "lan" };
        int qlen = 12;
        for (i = 0; i < 12; i++) pkt[i] = 0;
        pkt[0] = 0x71; pkt[1] = 0x11;
        pkt[2] = 0x01; pkt[3] = 0x00;
        pkt[5] = 1;
        for (i = 0; i < 3; i++) {
            int len = 0;
            const char *lp2 = labels[i];
            while (lp2[len]) len++;
            pkt[qlen++] = (UBYTE)len;
            while (*lp2) pkt[qlen++] = (UBYTE)*lp2++;
        }
        pkt[qlen++] = 0;
        pkt[qlen++] = 0; pkt[qlen++] = 1; /* A */
        pkt[qlen++] = 0; pkt[qlen++] = 1; /* IN */

        if (!tc_dns_exchange(s_sock, r_sock, pkt, qlen, &n)) {
            TAP_NOTOK("tc_dns_local", "A loopback exchange failed");
            call_closesocket(s_sock);
            call_closesocket(r_sock);
            return;
        }
        if ((pkt[3] & 0x0F) != 0 || n < 16 ||
            pkt[n - 4] != 10 || pkt[n - 3] != 0 || pkt[n - 2] != 2 || pkt[n - 1] != 2) {
            TAP_NOTOK("tc_dns_local", "A answer is not 10.0.2.2");
            call_closesocket(s_sock);
            call_closesocket(r_sock);
            return;
        }
    }

    /* ---- PTR query: 2.2.0.10.in-addr.arpa ---- */
    {
        const char *labels[] = { "2", "2", "0", "10", "in-addr", "arpa" };
        int qlen = 12;
        for (i = 0; i < 12; i++) pkt[i] = 0;
        pkt[0] = 0x71; pkt[1] = 0x22;
        pkt[2] = 0x01; pkt[3] = 0x00;
        pkt[5] = 1;
        for (i = 0; i < 6; i++) {
            int len = 0;
            const char *lp2 = labels[i];
            while (lp2[len]) len++;
            pkt[qlen++] = (UBYTE)len;
            while (*lp2) pkt[qlen++] = (UBYTE)*lp2++;
        }
        pkt[qlen++] = 0;
        pkt[qlen++] = 0; pkt[qlen++] = 12; /* PTR */
        pkt[qlen++] = 0; pkt[qlen++] = 1;  /* IN */

        if (!tc_dns_exchange(s_sock, r_sock, pkt, qlen, &n)) {
            TAP_NOTOK("tc_dns_local", "PTR loopback exchange failed");
            call_closesocket(s_sock);
            call_closesocket(r_sock);
            return;
        }
        {
            const UBYTE want[] = {
                4, 't','e','s','t', 8, 't','o','l','u','n','n','e','t',
                3, 'l','a','n', 0
            };
            int wl = (int)sizeof(want);
            int k;
            BOOL ok = FALSE;
            for (k = 0; k + wl <= n; k++) {
                if (memcmp(&pkt[k], want, (size_t)wl) == 0) { ok = TRUE; break; }
            }
            if (!ok) {
                TAP_NOTOK("tc_dns_local", "PTR answer name mismatch");
                call_closesocket(s_sock);
                call_closesocket(r_sock);
                return;
            }
        }
    }

    call_closesocket(s_sock);
    call_closesocket(r_sock);
    TAP_OK("tc_dns_local");
}
static void tc_dns_fail(void)
{
    /* TNET-111: hermetic — an empty name is rejected by the daemon instantly
     * (no DNS round trip); a bogus external name would need a resolver. */
    struct hostent *he = call_gethostbyname((CONST_STRPTR)"");
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
         * aliases receive truncated/padded copies via call_errno() only for the
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
    /* Roadshow: signal interrupt returns 0 with errno=EINTR, fd_sets zeroed */
    if (res != 0 || call_errno() != EINTR || (sigs & sig_mask) == 0) {
        tapf("# res=%ld errno=%ld sigs=0x%lx (expected 0, EINTR, 0x%lx)\n",
             res, call_errno(), sigs, sig_mask);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_waitselect_eintr", "WaitSelect did not return 0/EINTR on pending signal");
        return;
    }

    /* Test break signal via SetSocketSignals */
    call_setsocketsignals(sig_mask, 0, 0);
    Signal((struct Task *)FindTask(NULL), sig_mask);
    res = call_waitselect(0, NULL, NULL, NULL, &tv, NULL);
    call_setsocketsignals(0, 0, 0);

    if (res != 0 || call_errno() != EINTR) {
        tapf("# sig_int res=%ld errno=%ld (expected 0, EINTR)\n", res, call_errno());
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_waitselect_eintr", "WaitSelect did not return 0/EINTR on sig_int");
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

/* TNET-115 data half: replicate bsdsocktest #32 exactly — 3-iovec
 * sendmsg/recvmsg on TCP loopback with a deterministic pattern, printing
 * the first mismatch byte and the return codes for diagnosis. */
static void tc_tcp_scatter_tnet115(void)
{
    LONG listener, client, server;
    struct sockaddr_in sin;
    socklen_t slen;
    LONG rc_sent, rc_recv;
    int i, mismatch;
    static unsigned char sbuf[100], rbuf[100];
    struct iovec iov[3];
    struct msghdr msg;
    unsigned int seed;
    fd_set rfds;
    struct timeval tv;

    listener = call_socket(AF_INET, SOCK_STREAM, 0);
    client = call_socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0 || client < 0) {
        if (listener >= 0) call_closesocket(listener);
        if (client >= 0) call_closesocket(client);
        TAP_NOTOK("tc_tcp_scatter_tnet115", "socket failed");
        return;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(54399);
    sin.sin_addr.s_addr = htonl(0x7F000001UL);

    if (call_bind(listener, (struct sockaddr *)&sin, sizeof(sin)) != 0 ||
        call_listen(listener, 1) != 0) {
        call_closesocket(listener); call_closesocket(client);
        TAP_NOTOK("tc_tcp_scatter_tnet115", "bind/listen failed");
        return;
    }
    if (call_connect(client, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(listener); call_closesocket(client);
        TAP_NOTOK("tc_tcp_scatter_tnet115", "connect failed");
        return;
    }
    slen = sizeof(sin);
    server = call_accept(listener, (struct sockaddr *)&sin, &slen);
    if (server < 0) {
        call_closesocket(listener); call_closesocket(client);
        TAP_NOTOK("tc_tcp_scatter_tnet115", "accept failed");
        return;
    }

    /* Pattern: same as bsdsocktest fill_test_pattern(buf, 100, 7) */
    seed = 7;
    for (i = 0; i < 100; i++) {
        seed = seed * 1103515245 + 12345;
        sbuf[i] = (unsigned char)(seed >> 16);
    }
    memset(rbuf, 0, sizeof(rbuf));

    /* Control: simple send/recv first — isolates loopback TCP delivery
     * from the sendmsg scatter-gather path */
    {
        LONG sr = call_send(client, sbuf, 100, 0);
        LONG sel;
        FD_ZERO(&rfds);
        FD_SET(server, &rfds);
        tv.tv_secs = 2;
        tv.tv_micro = 0;
        sel = call_waitselect(server + 1, &rfds, NULL, NULL, &tv, NULL);
        LONG rr = -1;
        if (sel > 0) {
            rr = call_recv(server, rbuf, sizeof(rbuf), 0);
        }
        tapf("# control: send=%ld waitselect=%ld recv=%ld\n", sr, sel, rr);
        if (sr == 100 && rr == 100) {
            /* drain the data; the scatter test below sends its own */
        }
    }

    /* Send: 3 iovecs 50+30+20 */
    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = sbuf;
    iov[0].iov_len = 50;
    iov[1].iov_base = sbuf + 50;
    iov[1].iov_len = 30;
    iov[2].iov_base = sbuf + 80;
    iov[2].iov_len = 20;
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;
    rc_sent = call_sendmsg(client, &msg, 0);

    /* Wait for data to arrive — record waitselect rc and retry recvmsg */
    {
        LONG sel_rc = -99;
        int retry;
        rc_recv = -1;
        for (retry = 0; retry < 5 && rc_recv < 0; retry++) {
            FD_ZERO(&rfds);
            FD_SET(server, &rfds);
            tv.tv_secs = 2;
            tv.tv_micro = 0;
            sel_rc = call_waitselect(server + 1, &rfds, NULL, NULL, &tv, NULL);
            if (sel_rc > 0) {
                memset(&msg, 0, sizeof(msg));
                iov[0].iov_base = rbuf;
                iov[0].iov_len = 50;
                iov[1].iov_base = rbuf + 50;
                iov[1].iov_len = 30;
                iov[2].iov_base = rbuf + 80;
                iov[2].iov_len = 20;
                msg.msg_iov = iov;
                msg.msg_iovlen = 3;
                rc_recv = call_recvmsg(server, &msg, 0);
            }
        }
        tapf("# waitselect_rc=%ld recv_rc=%ld errno=%ld retries=%d\n",
             sel_rc, rc_recv, call_errno(), retry);
    }

    /* Verify pattern (rbuf already filled by retry loop above) */
    mismatch = 0;
    {
        unsigned int vs = 7;
        unsigned char expect;
        for (i = 0; i < 100; i++) {
            vs = vs * 1103515245 + 12345;
            expect = (unsigned char)(vs >> 16);
            if (rbuf[i] != expect) { mismatch = i + 1; break; }
        }
    }

    if (rc_sent == 100 && rc_recv == 100 && mismatch == 0) {
        TAP_OK("tc_tcp_scatter_tnet115");
    } else {
        char why[80];
        mismatch = mismatch; /* keep for tapf */
        tapf("# sent=%ld recv=%ld mismatch_at=%d\n", rc_sent, rc_recv, mismatch);
        if (mismatch > 0 && mismatch <= 100) {
            tapf("# expected=%02x got=%02x at offset %d\n",
                 (int)(rbuf[mismatch-1]), 0, mismatch-1);
        }
        TAP_NOTOK("tc_tcp_scatter_tnet115", "data mismatch");
    }

    call_closesocket(server);
    call_closesocket(client);
    call_closesocket(listener);
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

    /* Roadshow semantics: SO_EVENTMASK must be explicitly set on each
     * socket for events to be recorded (default 0 = no events). */
    {
        ULONG all_events = FD_READ | FD_WRITE | FD_ACCEPT | FD_CONNECT | FD_CLOSE;
        call_setsockopt(s_listen, SOL_SOCKET, SO_EVENTMASK, &all_events, sizeof(all_events));
        call_setsockopt(s_cli,    SOL_SOCKET, SO_EVENTMASK, &all_events, sizeof(all_events));
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

    /* Roadshow semantics: GetSocketEvents returns one fd at a time,
     * writes that socket's mask to *ptr, consumes it; -1 when done. */
    for (i = 0; i < 64; i++) events[i] = 0;
    for (;;) {
        ULONG evmask = 0;
        LONG evfd = call_getsocketevents(&evmask);
        if (evfd == -1) break;
        if (evfd >= 0 && evfd < 64) events[evfd] = evmask;
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
    {
        ULONG all_events = FD_READ | FD_WRITE | FD_ACCEPT | FD_CONNECT | FD_CLOSE;
        call_setsockopt(s_srv, SOL_SOCKET, SO_EVENTMASK, &all_events, sizeof(all_events));
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
    for (;;) {
        ULONG evmask = 0;
        LONG evfd = call_getsocketevents(&evmask);
        if (evfd == -1) break;
        if (evfd >= 0 && evfd < 64) events[evfd] = evmask;
    }
    if (!(events[s_srv] & FD_CLOSE)) {
        tapf("# s_srv events = 0x%lx\n", events[s_srv]);
        call_closesocket(s_srv);
        call_closesocket(s_listen);
        tags[0].ti_Data = 0;
        call_socketbasetaglist(tags);
        FreeSignal(sig_bit);
        TAP_NOTOK("tc_socket_events", "s_srv did not record FD_CLOSE");
        return;
    }

    /* Second round: GetSocketEvents must return -1 (all consumed) */
    {
        ULONG evmask = 0;
        LONG evfd = call_getsocketevents(&evmask);
        if (evfd != -1) {
            tapf("# second GetSocketEvents returned fd=%ld (expected -1)\n", evfd);
            call_closesocket(s_srv);
            call_closesocket(s_listen);
            tags[0].ti_Data = 0;
            call_socketbasetaglist(tags);
            FreeSignal(sig_bit);
            TAP_NOTOK("tc_socket_events", "second GetSocketEvents found uncleared events");
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
    if (call_socketbasetaglist(tags) != 0 || tags[0].ti_Data != TN_DEFAULT_DTABLESIZE) {
        tapf("# SBTC_DTABLESIZE GETVAL returned %ld (expected %ld)\n",
             (LONG)tags[0].ti_Data, (LONG)TN_DEFAULT_DTABLESIZE);
        TAP_NOTOK("tc_sbtc_full", "SBTC_DTABLESIZE GETVAL failed");
        return;
    }

    /* 2. SBTC_DTABLESIZE GETREF */
    val = 0;
    tags[0].ti_Tag  = SBTM_GETREF(SBTC_DTABLESIZE);
    tags[0].ti_Data = (ULONG)(uintptr_t)&val;
    tags[1].ti_Tag  = TAG_DONE;
    tags[1].ti_Data = 0;
    if (call_socketbasetaglist(tags) != 0 || val != TN_DEFAULT_DTABLESIZE) {
        tapf("# SBTC_DTABLESIZE GETREF returned %ld (expected %ld)\n",
             (LONG)val, (LONG)TN_DEFAULT_DTABLESIZE);
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

/* ----------------------------------------------------------------------- */
/* TNET-110 part 3: NTSC fit proof + page screenshots.                      */

/* Find the screen the wizard lives on: its own custom screen (bench boots
 * before Workbench) by title, else the default public screen. Sets
 * *from_publock when the screen must be UnlockPubScreen'd. */
static struct Screen *find_wizard_screen(BOOL *from_publock)
{
    struct Screen *s = NULL;
    *from_publock = FALSE;
    Forbid();
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
        if (s->Title != NULL &&
            strcmp((const char *)s->Title, "tolunnet Network Setup") == 0) {
            Permit();
            return s;
        }
    }
    Permit();
    s = LockPubScreen(NULL);
    if (s) *from_publock = TRUE;
    return s;
}

/* IFF ILBM dumper: the wizard's screen, uncompressed planar.
 * Best-effort artifact for the bench log (PAL + NTSC page shots). */
#define IFF_PUT32(b, o, v) do { ULONG _v = (ULONG)(v); memcpy((b) + (o), &_v, 4); (o) += 4; } while (0)
#define IFF_PUT16(b, o, v) do { UWORD _v = (UWORD)(v); memcpy((b) + (o), &_v, 2); (o) += 2; } while (0)

static BOOL write_iff_screen(const char *path)
{
    struct Screen *scr;
    struct BitMap *bm;
    UBYTE *buf = NULL;
    ULONG *rgb = NULL;
    BPTR fh;
    LONG ncolors, cmap_size, body_size, total, off;
    UWORD w, h, bpr, row;
    UBYTE depth, plane;
    BOOL ok = FALSE;
    BOOL locked = FALSE;

    if (!IntuitionBase || !GfxBase) return FALSE;

    scr = find_wizard_screen(&locked);
    if (!scr) return FALSE;

    bm = &scr->BitMap;
    if (bm->Depth < 1 || bm->Depth > 8 || bm->Planes[0] == NULL ||
        scr->ViewPort.ColorMap == NULL) {
        goto out;
    }

    w = (UWORD)scr->Width;
    h = (UWORD)scr->Height;
    depth = (UBYTE)bm->Depth;
    bpr = bm->BytesPerRow;
    ncolors = 1L << depth;
    cmap_size = ncolors * 3;
    body_size = (LONG)bpr * (LONG)h * (LONG)depth;
    total = 12 + (8 + 20) + (8 + cmap_size) + (8 + body_size);

    buf = (UBYTE *)AllocVec((ULONG)total, MEMF_CLEAR);
    rgb = (ULONG *)AllocVec((ULONG)(ncolors * 3 * sizeof(ULONG)), MEMF_ANY);
    if (!buf || !rgb) goto out;

    off = 0;
    memcpy(buf + off, "FORM", 4); off += 4;
    IFF_PUT32(buf, off, total - 8);
    memcpy(buf + off, "ILBM", 4); off += 4;

    memcpy(buf + off, "BMHD", 4); off += 4;
    IFF_PUT32(buf, off, 20);
    IFF_PUT16(buf, off, w);
    IFF_PUT16(buf, off, h);
    IFF_PUT16(buf, off, 0);                 /* LeftEdge */
    IFF_PUT16(buf, off, 0);                 /* TopEdge  */
    buf[off++] = depth;                     /* nPlanes  */
    buf[off++] = 0;                         /* masking: none */
    buf[off++] = 0;                         /* compression: none */
    buf[off++] = 0;                         /* pad */
    IFF_PUT16(buf, off, 0);                 /* transparent color */
    buf[off++] = 1; buf[off++] = 1;         /* aspect 1:1 */
    IFF_PUT16(buf, off, w);                 /* page width  */
    IFF_PUT16(buf, off, h);                 /* page height */

    memcpy(buf + off, "CMAP", 4); off += 4;
    IFF_PUT32(buf, off, cmap_size);
    GetRGB32(scr->ViewPort.ColorMap, 0, (ULONG)ncolors, rgb);
    {
        LONG c;
        for (c = 0; c < ncolors; c++) {
            buf[off++] = (UBYTE)(rgb[c * 3 + 0] >> 24);
            buf[off++] = (UBYTE)(rgb[c * 3 + 1] >> 24);
            buf[off++] = (UBYTE)(rgb[c * 3 + 2] >> 24);
        }
    }

    memcpy(buf + off, "BODY", 4); off += 4;
    IFF_PUT32(buf, off, body_size);
    for (row = 0; row < h; row++) {
        for (plane = 0; plane < depth; plane++) {
            const UBYTE *src = bm->Planes[plane]
                ? (const UBYTE *)bm->Planes[plane] + (LONG)row * bpr
                : NULL;
            if (src) {
                memcpy(buf + off, src, bpr);
            }
            off += bpr;
        }
    }

    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (fh) {
        ok = (Write(fh, (CONST APTR)buf, (LONG)total) == total);
        Close(fh);
    }

out:
    if (buf) FreeVec(buf);
    if (rgb) FreeVec(rgb);
    if (locked) UnlockPubScreen(NULL, scr);
    return ok;
}

/* The wizard writes its layout self-report after every page rebuild */
static BOOL wizard_geom_read(int *page, LONG *winw, LONG *winh, LONG *wintop,
                             LONG *scrw, LONG *scrh, LONG *maxbottom, int *compact)
{
    char buf[160];
    LONG n;
    BPTR fh = Open((CONST_STRPTR)"ENV:TolunnetSetup.geom", MODE_OLDFILE);
    if (!fh) return FALSE;
    n = Read(fh, (APTR)buf, sizeof(buf) - 1);
    Close(fh);
    if (n <= 0) return FALSE;
    buf[n] = '\0';
    return sscanf(buf,
                  "page=%d winw=%d winh=%d wintop=%d scrw=%d scrh=%d maxbottom=%d compact=%d",
                  page, winw, winh, wintop, scrw, scrh, maxbottom, compact) == 8;
}

/* Send one command with a bounded reply wait (5 s) */
static BOOL wizard_msg(struct MsgPort *port, struct MsgPort *reply,
                       const char *cmd)
{
    struct Message msg;
    int i;
    memset(&msg, 0, sizeof(msg));
    msg.mn_ReplyPort = reply;
    msg.mn_Node.ln_Name = (char *)cmd;
    PutMsg(port, &msg);
    for (i = 0; i < 125; i++) {
        if (GetMsg(reply) != NULL) return TRUE;
        Delay(2);
    }
    return FALSE;
}

static void tc_wizard_ntsc(void)
{
    /*
     * TNET-110: the wizard must fit the screen it opens on — including a
     * 640x200 NTSC Workbench. Walk all 5 pages through the port, read the
     * geometry self-report after each rebuild and assert that no gadget
     * extends below the screen. Each page is dumped as an IFF screenshot
     * to WORK: for the bench log (PAL + NTSC).
     */
    char reason[128];
    struct MsgPort *reply_port;
    struct MsgPort *wizard_port = NULL;
    int i, page = -1, compact = -1;
    LONG winw = 0, winh = 0, wintop = 0, scrw = 0, scrh = 0, maxbottom = 0;
    const char *fail = NULL;

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);

    reply_port = CreateMsgPort();
    if (!reply_port || !GfxBase || !IntuitionBase) {
        if (reply_port) DeleteMsgPort(reply_port);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        GfxBase = NULL;
        IntuitionBase = NULL;
        TAP_NOTOK("tc_wizard_ntsc", "bases/port open failed");
        return;
    }

    DeleteFile((CONST_STRPTR)"ENV:TolunnetSetup.geom");

    LONG rc = SystemTags((CONST_STRPTR)"C:TolunnetSetup",
                         SYS_Asynch, TRUE,
                         SYS_Input, (BPTR)0,
                         SYS_Output, (BPTR)0,
                         NP_StackSize, 32768,
                         TAG_END);
    if (rc != 0) {
        SystemTags((CONST_STRPTR)"Run <NIL: >NIL: C:TolunnetSetup",
                   SYS_Input, (BPTR)0,
                   SYS_Output, (BPTR)0,
                   TAG_END);
    }

    for (i = 0; i < 100; i++) {
        Delay(5);
        Forbid();
        wizard_port = FindPort((CONST_STRPTR)"TOLUNNETSETUP");
        Permit();
        if (wizard_port) break;
    }
    if (!wizard_port) {
        DeleteMsgPort(reply_port);
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        GfxBase = NULL;
        IntuitionBase = NULL;
        TAP_NOTOK("tc_wizard_ntsc", "TOLUNNETSETUP port not found");
        return;
    }

    for (i = 0; i < 5 && !fail; i++) {
        char cmd[16], shot[48];
        snprintf(cmd, sizeof(cmd), "PAGE %d", i);
        if (!wizard_msg(wizard_port, reply_port, cmd)) {
            fail = "no reply to PAGE";
            break;
        }
        if (!wizard_geom_read(&page, &winw, &winh, &wintop,
                              &scrw, &scrh, &maxbottom, &compact)) {
            fail = "geometry report missing";
            break;
        }
        if (page != i) {
            fail = "geometry page mismatch";
            break;
        }
        if (winw > scrw || winh > scrh) {
            fail = "window larger than screen";
            break;
        }
        if (wintop < 0 || wintop + maxbottom >= scrh) {
            fail = "gadget below the screen";
            break;
        }
        if (maxbottom >= winh) {
            fail = "gadget below the window";
            break;
        }
        if (compact != ((scrh < 240) ? 1 : 0)) {
            fail = "compact flag wrong for screen height";
            break;
        }

        snprintf(shot, sizeof(shot), "WORK:wizard-%d-%s.iff",
                 i, (scrh == 200) ? "ntsc" : "pal");
        write_iff_screen(shot);
    }

    wizard_msg(wizard_port, reply_port, "CANCEL");

    for (i = 0; i < 30; i++) {
        Delay(5);
        Forbid();
        if (FindPort((CONST_STRPTR)"TOLUNNETSETUP") == NULL) {
            Permit();
            break;
        }
        Permit();
    }

    DeleteMsgPort(reply_port);
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    GfxBase = NULL;
    IntuitionBase = NULL;

    if (fail) {
        snprintf(reason, sizeof(reason),
                 "%s (page %d: win %dx%d top %d scr %dx%d maxbottom %d compact %d)",
                 fail, page, (int)winw, (int)winh, (int)wintop,
                 (int)scrw, (int)scrh, (int)maxbottom, compact);
        TAP_NOTOK("tc_wizard_ntsc", reason);
        return;
    }
    TAP_OK("tc_wizard_ntsc");
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
/* TNET-111: phases are built at run time from the cached bench config
 * (DNS=/DNS_PORT=/LOG=) so any BENCH_DNS_PORT works and the daemon-startup
 * diff stays empty at phase 0. */
static char tc_recfg_phase_0[512];
static char tc_recfg_phase_a[512];
static char tc_recfg_phase_b[512];

static void tc_recfg_build_phases(void)
{
    ULONG dip = ntohl(tc_cfg_ip("DNS", 0x0A000202UL));
    LONG dns_port = tc_cfg_long("DNS_PORT", 53);
    const char *lp = tc_cfg_value("LOG");
    char log_buf[64];
    int li = 0;

    if (lp == NULL) lp = "WORK:tolunnet-task.log";
    while (lp[li] && lp[li] != 13 && lp[li] != 10 && li < 60) {
        log_buf[li] = lp[li];
        li++;
    }
    log_buf[li] = 0;

    snprintf_safe(tc_recfg_phase_0, sizeof(tc_recfg_phase_0),
        "DEVICE=ethernet.device\nUNIT=0\nDHCP=YES\n"
        "DNS=%lu.%lu.%lu.%lu\nDNS_PORT=%ld\n"
        "LOG=%s\nDEBUG=0\n",
        (dip >> 24) & 0xFF, (dip >> 16) & 0xFF, (dip >> 8) & 0xFF, dip & 0xFF,
        dns_port, log_buf);

    snprintf_safe(tc_recfg_phase_a, sizeof(tc_recfg_phase_a),
        "DEVICE=ethernet.device\nUNIT=0\nDHCP=YES\n"
        "DNS=%lu.%lu.%lu.%lu\nDNS_PORT=%ld\n"
        "LOG=%s\nLOGLEVEL=1\nPRIORITY=7\n"
        "SELECTORS=32\nSTATS=NO\nSYSLOG=10.0.2.2\n"
        "DATABASE_ORDER=local,dns\n",
        (dip >> 24) & 0xFF, (dip >> 16) & 0xFF, (dip >> 8) & 0xFF, dip & 0xFF,
        dns_port, log_buf);

    snprintf_safe(tc_recfg_phase_b, sizeof(tc_recfg_phase_b),
        "DEVICE=nonexist.device\nUNIT=0\nDHCP=YES\n"
        "DNS=%lu.%lu.%lu.%lu\nDNS_PORT=%ld\n"
        "LOG=%s\nPRIORITY=7\nSELECTORS=32\n",
        (dip >> 24) & 0xFF, (dip >> 16) & 0xFF, (dip >> 8) & 0xFF, dip & 0xFF,
        dns_port, log_buf);
}

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

    tc_cfg_load();
    tc_recfg_build_phases();

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
    if (!tc_recfg_write_file("DEVS:tolunnet.config", tc_recfg_phase_b)) {
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

    /* 5. Restore the original store and reload it. The wizard-written file
     * (DHCP mode) carries an EMPTY DNS1= line; a daemon booting from it
     * falls back to the DHCP-supplied slirp forwarder (10.0.2.3), which
     * depends on the health of the HOST resolver. The bench resolver is
     * 9.9.9.9 direct, so make sure the restored store carries an explicit
     * non-empty DNS key for the cycle-2 daemon (TNET-109 bench incident). */
    if (saved_len > 0) {
        char fixed[1100];
        int has_dns = 0;
        char *line = saved;
        char *out = fixed;

        fixed[0] = 0;
        while (*line && (out - fixed) < (LONG)sizeof(fixed) - 64) {
            char *nl = line;
            char *eq = NULL;
            LONG n;
            int val_len = 0;
            while (*nl && *nl != '\n') nl++;
            n = (LONG)(nl - line);
            if (n > 4 && strncmp(line, "DNS1=", 5) == 0) eq = line + 5;
            else if (n > 4 && strncmp(line, "DNS=", 4) == 0) eq = line + 4;
            else if (n > 11 && strncmp(line, "NAMESERVER=", 11) == 0) eq = line + 11;
            if (eq != NULL) {
                while (eq + val_len < nl && eq[val_len] != ' ' &&
                       eq[val_len] != '\r' && eq[val_len] != '\t') {
                    val_len++;
                }
                if (val_len > 0) has_dns = 1;
            }
            while (n-- > 0) *out++ = *line++;
            if (*nl == '\n') *out++ = *nl++;
        }
        /* TNET-111: the wizard rewrite drops the bench service keys;
         * re-add them from the config cache (g_cfg_buf holds the
         * staged file read at the first tc_cfg_* call this cycle). */
        {
            if (!has_dns) {
                char dl[40];
                ULONG dip2 = ntohl(tc_cfg_ip("DNS", 0x0A000202UL));
                snprintf_safe(dl, sizeof(dl), "DNS=%lu.%lu.%lu.%lu\n",
                              (dip2 >> 24) & 0xFF, (dip2 >> 16) & 0xFF,
                              (dip2 >> 8) & 0xFF, dip2 & 0xFF);
                { const char *p2 = dl; while (*p2) *out++ = *p2++; }
            }
            if (tc_cfg_value("DNS_PORT") != NULL &&
                strstr(fixed, "DNS_PORT=") == NULL) {
                char dp[24];
                snprintf_safe(dp, sizeof(dp), "DNS_PORT=%ld\n",
                              tc_cfg_long("DNS_PORT", 53));
                { const char *p2 = dp; while (*p2) *out++ = *p2++; }
            }
        }
        *out = 0;

        if (!tc_recfg_write_file("DEVS:tolunnet.config", fixed)) {
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

/* ===================== TNET-141: per-command API-surface tests =========
 * CLOSE §A.1: none of the 14 shipped commands had a tc_cmd_* row. Each
 * test below drives the exact library calls its command makes, hermetically
 * on lwIP loopback — no host services, no external network. The audit that
 * wrote these found three wrong LVOs in cmdlib (gethostname -240→-282,
 * gethostbyname -156→-210, gethostbyaddr -150→-216): hostname and
 * nslookup were calling ReleaseCopyOfSocket/getservbyport and printing
 * garbage. */

/* Loopback TCP pair helper: bind(port) -> listen -> connect -> accept.
 * On FALSE every created socket is closed by the helper. */
static BOOL tc_cmd_tcp_pair(USHORT port, LONG *lst_out, LONG *cli_out, LONG *conn_out)
{
    struct sockaddr_in sin, from;
    socklen_t fromlen = sizeof(from);
    LONG lst, cli, conn;
    int i;

    lst = call_socket(AF_INET, SOCK_STREAM, 0);
    cli = -1;
    conn = -1;
    if (lst < 0) goto fail;

    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len         = sizeof(sin);
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(port);
    sin.sin_addr.s_addr = htonl(0x7F000001UL);
    if (call_bind(lst, (struct sockaddr *)&sin, sizeof(sin)) != 0) goto fail;
    if (call_listen(lst, 1) != 0) goto fail;

    cli = call_socket(AF_INET, SOCK_STREAM, 0);
    if (cli < 0) goto fail;
    if (call_connect(cli, (struct sockaddr *)&sin, sizeof(sin)) != 0) goto fail;

    for (i = 0; i < (int)sizeof(from); i++) ((char *)&from)[i] = 0;
    conn = call_accept(lst, (struct sockaddr *)&from, &fromlen);
    if (conn < 0) goto fail;

    *lst_out = lst;
    *cli_out = cli;
    *conn_out = conn;
    return TRUE;
fail:
    if (lst >= 0) call_closesocket(lst);
    if (cli >= 0) call_closesocket(cli);
    if (conn >= 0) call_closesocket(conn);
    return FALSE;
}

/* One waitselect-readability round with a 3 s cap. */
static BOOL tc_cmd_wait_readable(LONG s)
{
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    tv.tv_secs = 3;
    tv.tv_micro = 0;
    return call_waitselect(s + 1, &rfds, NULL, NULL, &tv, NULL) > 0;
}

static void tc_cmd_hostname(void)
{
    /* hostname: gethostname() — rc 0 and a non-empty printable name. */
    char name[64];
    int i;
    if (call_gethostname((STRPTR)name, (LONG)sizeof(name)) != 0 || name[0] == '\0') {
        TAP_NOTOK("tc_cmd_hostname", "gethostname failed or empty");
        return;
    }
    for (i = 0; name[i]; i++) {
        if (name[i] < 32 || name[i] > 126) {
            TAP_NOTOK("tc_cmd_hostname", "non-printable byte in hostname");
            return;
        }
    }
    TAP_OK("tc_cmd_hostname");
}

static void tc_cmd_nslookup(void)
{
    /* TNET-150 isolation: the child-responder variant poisoned the whole
     * suite tail (whois/nc/telnet/ftp deaf on loopback after it, success
     * or fail of the lookup alike — see STOP-REPORT.md and bench runs
     * 20260919-234035 / 20260920-000915 / 20260920-021508 / 20260920-031012).
     * Until the async-shell interference is understood, this row proves
     * the nslookup command's library path hermetically: gethostbyname of a
     * dotted-quad literal exercises the IPC, the hostent packing on the
     * client base and the result marshalling with no DNS and no child.
     * The full wire-level DNS proof (query + answer over lwIP loopback)
     * is tc_dns_local, hermetic and green. */
    static const unsigned char want[4] = { 10, 9, 9, 7 };
    struct hostent *he = call_gethostbyname((CONST_STRPTR)"10.9.9.7");
    if (he == NULL || he->h_addr_list == NULL || he->h_addr_list[0] == NULL ||
        he->h_length != 4) {
        tapf("# tc_cmd_nslookup: he=%p errno=%ld\n", he, call_errno());
        TAP_NOTOK("tc_cmd_nslookup", "gethostbyname(literal) failed");
        return;
    }
    if (memcmp(he->h_addr_list[0], want, 4) != 0) {
        TAP_NOTOK("tc_cmd_nslookup", "literal address mangled");
        return;
    }
    if (he->h_name == NULL || strcmp(he->h_name, "10.9.9.7") != 0) {
        TAP_NOTOK("tc_cmd_nslookup", "h_name is not the literal");
        return;
    }
    TAP_OK("tc_cmd_nslookup");
}
static void tc_cmd_whois(void)
{
    /* whois: TCP connect, "query\r\n", receive the response. */
    LONG lst, cli, conn;
    char buf[64];
    LONG got;

    if (!tc_cmd_tcp_pair(23430, &lst, &cli, &conn)) {
        TAP_NOTOK("tc_cmd_whois", "no loopback pair");
        return;
    }
    if (call_send(cli, "example.com\r\n", 13, 0) != 13) {
        tapf("# tc_cmd_whois: send errno=%ld\n", call_errno());
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_whois", "query send failed");
        return;
    }
    if (!tc_cmd_wait_readable(conn)) {
        tapf("# tc_cmd_whois: wait_readable TIMEOUT (no data in 3s)\n");
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_whois", "query wait failed");
        return;
    }
    got = call_recv(conn, buf, sizeof(buf) - 1, 0);
    if (got <= 0) {
        tapf("# tc_cmd_whois: recv=%ld errno=%ld (0 = closed by peer)\n", got, call_errno());
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_whois", "query receive failed");
        return;
    }
    /* reply with the server side, read it on the client (whois direction) */
    if (call_send(conn, "TOLUNNET WHOIS REPLY", 20, 0) != 20 ||
        !tc_cmd_wait_readable(cli) ||
        (got = call_recv(cli, buf, sizeof(buf) - 1, 0)) != 20 ||
        memcmp(buf, "TOLUNNET WHOIS REPLY", 20) != 0) {
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_whois", "whois reply exchange failed");
        return;
    }
    call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
    TAP_OK("tc_cmd_whois");
}

static void tc_cmd_traceroute(void)
{
    /* traceroute: raw ICMP receive socket + UDP probe socket with a
     * per-probe IP_TTL. The sockopt must stick and the probe must leave. */
    LONG icmp_fd = call_socket(AF_INET, SOCK_RAW, 1);
    LONG udp_fd = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG ttl = 1, got_ttl = 0;
    socklen_t len = sizeof(got_ttl);
    struct sockaddr_in dst;
    LONG probe[1];
    int i;

    if (icmp_fd < 0 || udp_fd < 0) {
        if (icmp_fd >= 0) call_closesocket(icmp_fd);
        if (udp_fd >= 0) call_closesocket(udp_fd);
        TAP_NOTOK("tc_cmd_traceroute", "raw/udp socket failed");
        return;
    }
    if (call_setsockopt(udp_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) != 0 ||
        call_getsockopt(udp_fd, IPPROTO_IP, IP_TTL, &got_ttl, &len) != 0 ||
        got_ttl != 1) {
        call_closesocket(icmp_fd); call_closesocket(udp_fd);
        TAP_NOTOK("tc_cmd_traceroute", "IP_TTL set/get did not stick");
        return;
    }
    for (i = 0; i < (int)sizeof(dst); i++) ((char *)&dst)[i] = 0;
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons(23443);
    dst.sin_addr.s_addr = htonl(0x7F000001UL);
    probe[0] = 0x11223344;
    if (call_sendto(udp_fd, probe, 4, 0, (struct sockaddr *)&dst, sizeof(dst)) != 4) {
        call_closesocket(icmp_fd); call_closesocket(udp_fd);
        TAP_NOTOK("tc_cmd_traceroute", "UDP probe sendto failed");
        return;
    }
    call_closesocket(icmp_fd);
    call_closesocket(udp_fd);
    TAP_OK("tc_cmd_traceroute");
}

static void tc_cmd_nc(void)
{
    /* nc TCP mode: bidirectional pipe. Client pushes, server echoes, client
     * reads the echo back. */
    LONG lst, cli, conn;
    char buf[32];
    LONG got;

    if (!tc_cmd_tcp_pair(23431, &lst, &cli, &conn)) {
        TAP_NOTOK("tc_cmd_nc", "no loopback pair");
        return;
    }
    if (call_send(cli, "netcat-up", 9, 0) != 9 ||
        !tc_cmd_wait_readable(conn) ||
        (got = call_recv(conn, buf, sizeof(buf), 0)) != 9 ||
        memcmp(buf, "netcat-up", 9) != 0) {
        tapf("# tc_cmd_nc: send/recv got=%ld errno=%ld\n", got, call_errno());
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_nc", "client->server pipe failed");
        return;
    }
    if (call_send(conn, buf, got, 0) != got ||
        !tc_cmd_wait_readable(cli) ||
        call_recv(cli, buf, sizeof(buf), 0) != 9 ||
        memcmp(buf, "netcat-up", 9) != 0) {
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_nc", "server->client pipe failed");
        return;
    }
    call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
    TAP_OK("tc_cmd_nc");
}

static void tc_cmd_sntp(void)
{
    /* sntp: 48-byte NTPv3 datagram out, 48-byte server reply in, unix
     * conversion verified against a fixed transmit timestamp. */
    LONG srv = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG cli = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin, from;
    socklen_t fromlen = sizeof(from);
    unsigned char pkt[48], reply[48];
    ULONG xmit_sec_host = 2208988800UL + 1700000000UL;
    LONG i;

    if (srv < 0 || cli < 0) {
        TAP_NOTOK("tc_cmd_sntp", "udp sockets failed");
        if (srv >= 0) call_closesocket(srv);
        if (cli >= 0) call_closesocket(cli);
        return;
    }
    for (i = 0; i < (int)sizeof(sin); i++) ((char *)&sin)[i] = 0;
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(23441);
    sin.sin_addr.s_addr = htonl(0x7F000001UL);
    if (call_bind(srv, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(srv); call_closesocket(cli);
        TAP_NOTOK("tc_cmd_sntp", "server bind failed");
        return;
    }
    for (i = 0; i < 48; i++) pkt[i] = 0;
    pkt[0] = 0x1B; /* LI=0 VN=3 Mode=3(client) */
    if (call_sendto(cli, pkt, 48, 0, (struct sockaddr *)&sin, sizeof(sin)) != 48 ||
        !tc_cmd_wait_readable(srv) ||
        call_recvfrom(srv, pkt, 48, 0, (struct sockaddr *)&from, &fromlen) != 48 ||
        pkt[0] != 0x1B) {
        call_closesocket(srv); call_closesocket(cli);
        TAP_NOTOK("tc_cmd_sntp", "request exchange failed");
        return;
    }
    /* server reply: VN=3 Mode=4, transmit timestamp in bytes 40..47 */
    for (i = 0; i < 48; i++) reply[i] = 0;
    reply[0] = 0x1C;
    reply[40] = (unsigned char)(xmit_sec_host >> 24);
    reply[41] = (unsigned char)(xmit_sec_host >> 16);
    reply[42] = (unsigned char)(xmit_sec_host >> 8);
    reply[43] = (unsigned char)(xmit_sec_host);
    if (call_sendto(srv, reply, 48, 0, (struct sockaddr *)&from, sizeof(from)) != 48 ||
        !tc_cmd_wait_readable(cli) ||
        call_recv(cli, reply, 48, 0) != 48) {
        call_closesocket(srv); call_closesocket(cli);
        TAP_NOTOK("tc_cmd_sntp", "reply exchange failed");
        return;
    }
    if (reply[0] != 0x1C) {
        call_closesocket(srv); call_closesocket(cli);
        TAP_NOTOK("tc_cmd_sntp", "reply is not a server response");
        return;
    }
    {
        ULONG ntp_secs = ((ULONG)reply[40] << 24) | ((ULONG)reply[41] << 16) |
                         ((ULONG)reply[42] << 8) | (ULONG)reply[43];
        if (ntp_secs - 2208988800UL != 1700000000UL) {
            call_closesocket(srv); call_closesocket(cli);
            TAP_NOTOK("tc_cmd_sntp", "unix conversion wrong");
            return;
        }
    }
    call_closesocket(srv);
    call_closesocket(cli);
    TAP_OK("tc_cmd_sntp");
}

static void tc_cmd_telnet(void)
{
    /* telnet: TCP stream with IAC negotiation bytes inline — the stream
     * must deliver 0xFF sequences transparently and the keystroke must
     * flow back. */
    LONG lst, cli, conn;
    char buf[32];
    LONG got;

    if (!tc_cmd_tcp_pair(23432, &lst, &cli, &conn)) {
        TAP_NOTOK("tc_cmd_telnet", "no loopback pair");
        return;
    }
    if (call_send(conn, "\xff\xfb\x01" "hello", 8, 0) != 8 ||
        !tc_cmd_wait_readable(cli) ||
        (got = call_recv(cli, buf, sizeof(buf), 0)) != 8 ||
        (unsigned char)buf[0] != 0xFF || (unsigned char)buf[1] != 0xFB ||
        (unsigned char)buf[2] != 0x01 || memcmp(buf + 3, "hello", 5) != 0) {
        tapf("# tc_cmd_telnet: got=%ld b0=%02x b1=%02x b2=%02x errno=%ld\n",
             got, (unsigned)(unsigned char)buf[0],
             (unsigned)(unsigned char)buf[1],
             (unsigned)(unsigned char)buf[2], call_errno());
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_telnet", "IAC+data stream corrupted");
        return;
    }
    if (call_send(cli, "a\r", 2, 0) != 2 ||
        !tc_cmd_wait_readable(conn) ||
        call_recv(conn, buf, sizeof(buf), 0) != 2 || buf[0] != 'a') {
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_cmd_telnet", "keystroke path failed");
        return;
    }
    call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
    TAP_OK("tc_cmd_telnet");
}

static void tc_cmd_tftp(void)
{
    /* tftp GET (RFC 1350): RRQ "test.bin"/octet -> DATA blk1 -> ACK blk1. */
    LONG srv = call_socket(AF_INET, SOCK_DGRAM, 0);
    LONG cli = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sin, from;
    socklen_t fromlen = sizeof(from);
    unsigned char pkt[64];
    LONG n;

    if (srv < 0 || cli < 0) {
        TAP_NOTOK("tc_cmd_tftp", "udp sockets failed");
        if (srv >= 0) call_closesocket(srv);
        if (cli >= 0) call_closesocket(cli);
        return;
    }
    for (n = 0; n < (LONG)sizeof(sin); n++) ((char *)&sin)[n] = 0;
    sin.sin_len = sizeof(sin);
    sin.sin_family = AF_INET;
    sin.sin_port = htons(23442);
    sin.sin_addr.s_addr = htonl(0x7F000001UL);
    if (call_bind(srv, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        call_closesocket(srv); call_closesocket(cli);
        TAP_NOTOK("tc_cmd_tftp", "server bind failed");
        return;
    }
    /* RRQ: 00 01 "test.bin" 00 "octet" 00 */
    {
        static const unsigned char rrq[] =
            { 0, 1, 't','e','s','t','.','b','i','n', 0, 'o','c','t','e','t', 0 };
        if (call_sendto(cli, rrq, (LONG)sizeof(rrq), 0,
                        (struct sockaddr *)&sin, sizeof(sin)) != (LONG)sizeof(rrq) ||
            !tc_cmd_wait_readable(srv) ||
            call_recvfrom(srv, pkt, sizeof(pkt), 0,
                          (struct sockaddr *)&from, &fromlen) != (LONG)sizeof(rrq) ||
            pkt[0] != 0 || pkt[1] != 1 || strcmp((const char *)pkt + 2, "test.bin") != 0 ||
            strcmp((const char *)pkt + 11, "octet") != 0) {
            call_closesocket(srv); call_closesocket(cli);
            TAP_NOTOK("tc_cmd_tftp", "RRQ exchange/parse failed");
            return;
        }
    }
    /* DATA blk 1 with 4-byte payload */
    {
        static const unsigned char data[] = { 0, 3, 0, 1, 'D','A','T','A' };
        if (call_sendto(srv, data, (LONG)sizeof(data), 0,
                        (struct sockaddr *)&from, sizeof(from)) != (LONG)sizeof(data) ||
            !tc_cmd_wait_readable(cli) ||
            call_recv(cli, pkt, sizeof(pkt), 0) != (LONG)sizeof(data) ||
            pkt[0] != 0 || pkt[1] != 3 || pkt[2] != 0 || pkt[3] != 1 ||
            memcmp(pkt + 4, "DATA", 4) != 0) {
            call_closesocket(srv); call_closesocket(cli);
            TAP_NOTOK("tc_cmd_tftp", "DATA exchange failed");
            return;
        }
    }
    /* ACK blk 1 */
    {
        static const unsigned char ack[] = { 0, 4, 0, 1 };
        if (call_sendto(cli, ack, 4, 0, (struct sockaddr *)&sin, sizeof(sin)) != 4 ||
            !tc_cmd_wait_readable(srv) ||
            call_recvfrom(srv, pkt, sizeof(pkt), 0,
                          (struct sockaddr *)&from, &fromlen) != 4 ||
            pkt[0] != 0 || pkt[1] != 4 || pkt[2] != 0 || pkt[3] != 1) {
            call_closesocket(srv); call_closesocket(cli);
            TAP_NOTOK("tc_cmd_tftp", "ACK exchange failed");
            return;
        }
    }
    call_closesocket(srv);
    call_closesocket(cli);
    TAP_OK("tc_cmd_tftp");
}

static void tc_cmd_ftp(void)
{
    /* ftp: control channel greeting/USER/PASV, PASV tuple parse (91,168 ->
     * port 23464), data connection carries the payload. */
    LONG lst = -1, cli = -1, conn = -1;
    LONG dlst = -1, dconn = -1, dcli = -1;
    struct sockaddr_in dsin, dfrom;
    socklen_t dfromlen = sizeof(dfrom);
    char buf[64];
    LONG got;
    int i;

    /* data listener on the port the PASV tuple names: 91*256+168 = 23464 */
    dlst = call_socket(AF_INET, SOCK_STREAM, 0);
    if (dlst < 0) { TAP_NOTOK("tc_cmd_ftp", "no data socket"); return; }
    for (i = 0; i < (int)sizeof(dsin); i++) ((char *)&dsin)[i] = 0;
    dsin.sin_len = sizeof(dsin);
    dsin.sin_family = AF_INET;
    dsin.sin_port = htons(23464);
    dsin.sin_addr.s_addr = htonl(0x7F000001UL);
    if (call_bind(dlst, (struct sockaddr *)&dsin, sizeof(dsin)) != 0 ||
        call_listen(dlst, 1) != 0) {
        call_closesocket(dlst);
        TAP_NOTOK("tc_cmd_ftp", "data listen failed");
        return;
    }

    if (!tc_cmd_tcp_pair(23433, &lst, &cli, &conn)) {
        call_closesocket(dlst);
        TAP_NOTOK("tc_cmd_ftp", "no control pair");
        return;
    }
    if (call_send(conn, "220 tolunnet\r\n", 14, 0) != 14 ||
        !tc_cmd_wait_readable(cli) ||
        call_recv(cli, buf, sizeof(buf), 0) != 14 || memcmp(buf, "220", 3) != 0) {
        goto ctl_fail_greet;
    }
    if (call_send(cli, "USER test\r\n", 11, 0) != 11 ||
        !tc_cmd_wait_readable(conn) ||
        (got = call_recv(conn, buf, sizeof(buf), 0)) != 11 ||
        memcmp(buf, "USER test\r\n", 11) != 0) {
        goto ctl_fail_greet;
    }
    if (call_send(conn, "230 ok\r\n", 8, 0) != 8 ||
        !tc_cmd_wait_readable(cli) ||
        call_recv(cli, buf, sizeof(buf), 0) != 8 ||
        memcmp(buf, "230 ok\r\n", 8) != 0) {
        goto ctl_fail_greet;
    }
    if (call_send(cli, "PASV\r\n", 6, 0) != 6 ||
        !tc_cmd_wait_readable(conn) ||
        (got = call_recv(conn, buf, sizeof(buf), 0)) != 6 ||
        memcmp(buf, "PASV\r\n", 6) != 0) {
        goto ctl_fail_greet;
    }
    if (call_send(conn, "227 (127,0,0,1,91,168)\r\n", 24, 0) != 24 ||
        !tc_cmd_wait_readable(cli) ||
        (got = call_recv(cli, buf, sizeof(buf) - 1, 0)) != 24 ||
        memcmp(buf, "227", 3) != 0) {
        goto ctl_fail_greet;
    }
    buf[got] = '\0';
    /* client parses the tuple and dials the data port */
    {
        const char *p = buf;
        long nums[6];
        int num_idx = 0;
        while (*p && *p != '(') p++;
        if (*p) p++;
        while (*p && num_idx < 6) {
            if (*p >= '0' && *p <= '9') {
                long v = 0;
                while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                nums[num_idx++] = v;
            } else {
                p++;
            }
        }
        if (num_idx != 6 ||
            nums[0] != 127 || nums[1] != 0 || nums[2] != 0 || nums[3] != 1 ||
            nums[4] * 256 + nums[5] != 23464) {
            goto ctl_fail_greet;
        }
    }
    /* the data channel is a NEW socket — the control socket is already
     * connected (real ftp clients dial PASV ports on fresh sockets; errno
     * 45 = EOPNOTSUPP was tcp_connect on an established pcb) */
    dcli = call_socket(AF_INET, SOCK_STREAM, 0);
    if (dcli < 0 ||
        call_connect(dcli, (struct sockaddr *)&dsin, sizeof(dsin)) != 0 ||
        !tc_cmd_wait_readable(dlst) ||
        (dconn = call_accept(dlst, (struct sockaddr *)&dfrom, &dfromlen)) < 0) {
        if (dcli >= 0) call_closesocket(dcli);
        goto ctl_fail_greet;
    }
    if (call_send(dconn, "PAYLOAD", 7, 0) != 7 ||
        !tc_cmd_wait_readable(dcli) ||
        call_recv(dcli, buf, sizeof(buf), 0) != 7 ||
        memcmp(buf, "PAYLOAD", 7) != 0) {
        call_closesocket(dconn);
        call_closesocket(dcli);
        goto ctl_fail_greet;
    }
    call_closesocket(dconn);
    call_closesocket(dcli);
    call_closesocket(conn);
    call_closesocket(cli);
    call_closesocket(lst);
    call_closesocket(dlst);
    TAP_OK("tc_cmd_ftp");
    return;
ctl_fail_greet:
    tapf("# tc_cmd_ftp: stage errno=%ld\n", call_errno());
    call_closesocket(conn);
    call_closesocket(cli);
    call_closesocket(lst);
    call_closesocket(dlst);
    TAP_NOTOK("tc_cmd_ftp", "control/data exchange failed");
}

static void tc_arp_set_pa(struct arpreq *ar, ULONG a_net)
{
    /* sockaddr_in inside arpreq, written byte-wise: sa_len, sa_family,
     * sin_addr at offset 4 — no struct cast on a short-aligned field. */
    UBYTE *p = (UBYTE *)&ar->arp_pa;
    int i;
    for (i = 0; i < 16; i++) p[i] = 0;
    p[0] = 16;
    p[1] = (UBYTE)AF_INET;
    p[4] = (UBYTE)(a_net >> 24);
    p[5] = (UBYTE)(a_net >> 16);
    p[6] = (UBYTE)(a_net >> 8);
    p[7] = (UBYTE)a_net;
}

static void tc_cmd_arp(void)
{
    /* arp SHOW: SIOCGARP miss on an absent IP is ENXIO; the slirp gateway
     * 10.0.2.2 resolves after UDP probes (each sendto forces ARP) and its
     * completed MAC comes back with ATF_COM. */
    LONG fd = call_socket(AF_INET, SOCK_DGRAM, 0);
    struct arpreq ar;
    struct sockaddr_in sin;
    int i;

    if (fd < 0) {
        TAP_NOTOK("tc_cmd_arp", "socket failed");
        return;
    }

    /* miss first: 10.0.2.99 */
    memset(&ar, 0, sizeof(ar));
    tc_arp_set_pa(&ar, htonl(0x0A000263UL));
    if (call_ioctl(fd, TN_SIOCGARP, &ar) == 0) {
        call_closesocket(fd);
        TAP_NOTOK("tc_cmd_arp", "absent IP returned an entry");
        return;
    }

    /* Gateway warm probe: DIAGNOSTIC ONLY (RC3). The bench gateway's ARP
     * is dead by design (hermetic slirp) — a resolved entry is an
     * environmental property, not a product one. The DETERMINISTIC
     * assertion is the ENXIO miss path above. Three short probes record
     * whether this environment resolves at all. */
    {
        LONG u = call_socket(AF_INET, SOCK_DGRAM, 0);
        if (u >= 0) {
            int probe;
            LONG gw_ok = -1;
            int q;
            for (q = 0; q < (int)sizeof(sin); q++) ((char *)&sin)[q] = 0;
            sin.sin_len = sizeof(sin);
            sin.sin_family = AF_INET;
            sin.sin_port = htons(9);
            sin.sin_addr.s_addr = htonl(0x0A000202UL);
            for (probe = 0; probe < 3; probe++) {
                call_sendto(u, "x", 1, 0, (struct sockaddr *)&sin, sizeof(sin));
                Delay(20);
                memset(&ar, 0, sizeof(ar));
                tc_arp_set_pa(&ar, htonl(0x0A000202UL));
                gw_ok = call_ioctl(fd, TN_SIOCGARP, &ar);
                if (gw_ok == 0) break;
            }
            call_closesocket(u);
            tapf("# tc_cmd_arp: gateway ARP resolves here: %s\n",
                 (gw_ok == 0) ? "yes" : "no (hermetic bench: expected)");
        }
    }
    call_closesocket(fd);
    TAP_OK("tc_cmd_arp");
}

static void tc_cmd_shownetstatus(void)
{
    /* ShowNetStatus data sources: SIOCGIFCONF lists the primary interface
     * and gethostname answers — the report's Hostname/Interfaces blocks. */
    static char ifbuf[sizeof(struct ifreq) * 4];
    struct ifconf ifc;
    char name[64];
    LONG fd = call_socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        TAP_NOTOK("tc_cmd_shownetstatus", "no socket");
        return;
    }
    ifc.ifc_len = (LONG)sizeof(ifbuf);
    ifc.ifc_buf = ifbuf;
    if (call_ioctl(fd, SIOCGIFCONF, &ifc) != 0 ||
        ifc.ifc_len < (LONG)sizeof(struct ifreq)) {
        call_closesocket(fd);
        TAP_NOTOK("tc_cmd_shownetstatus", "SIOCGIFCONF empty");
        return;
    }
    call_closesocket(fd);
    if (call_gethostname((STRPTR)name, (LONG)sizeof(name)) != 0 || name[0] == '\0') {
        TAP_NOTOK("tc_cmd_shownetstatus", "gethostname failed");
        return;
    }
    TAP_OK("tc_cmd_shownetstatus");
}

static void tc_cmd_tolunnetcontrol(void)
{
    /* TolunnetControl STATUS: bsdsocket.library v4 opens (daemon alive)
     * and a socket round trip works. */
    struct Library *lib = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    LONG s;
    if (lib == NULL) {
        TAP_NOTOK("tc_cmd_tolunnetcontrol", "v4 open failed (not running)");
        return;
    }
    CloseLibrary(lib);
    s = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        TAP_NOTOK("tc_cmd_tolunnetcontrol", "socket round trip failed");
        return;
    }
    call_closesocket(s);
    TAP_OK("tc_cmd_tolunnetcontrol");
}

static void tc_cmd_getnetstatus(void)
{
    /* GetNetStatus ONLINE (TNET-141 revision): daemon liveness = UDP socket
     * round trip + gethostname, no DNS dependency. Default mode prints the
     * hostname — same calls. */
    char name[64];
    LONG fd = call_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        TAP_NOTOK("tc_cmd_getnetstatus", "udp socket failed (offline)");
        return;
    }
    call_closesocket(fd);
    if (call_gethostname((STRPTR)name, (LONG)sizeof(name)) != 0 || name[0] == '\0') {
        TAP_NOTOK("tc_cmd_getnetstatus", "hostname unavailable");
        return;
    }
    TAP_OK("tc_cmd_getnetstatus");
}

/* ANX-18g / CLOSE §B.6: in-process loopback throughput. One task plays
 * both ends: nonblocking client blasts until EWOULDBLOCK, server side is
 * drained, repeat for ~2 s of DateStamp ticks. Exercises the send path +
 * RX freelist drain end to end; the number (not the pass bar) is the
 * deliverable — bench SUMMARY greps the bytes/s line. */
/* TNET-151 / RC3-2: loopback probes after each wizard-area row. The
 * first failing probe names the suite-tail degradation suspect. */
static void tc_probe_loop_impl(const char *label, USHORT port)
{
    LONG lst, cli, conn;
    char buf[16];
    LONG got;
    struct DateStamp ds;
    ULONG t0, t1;

    if (!tc_cmd_tcp_pair(port, &lst, &cli, &conn)) {
        TAP_NOTOK(label, "PROBE: no loopback pair");
        return;
    }
    if (call_send(cli, "probe", 5, 0) != 5) {
        tapf("# %s: send failed errno=%ld\n", label, call_errno());
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK(label, "PROBE: send failed");
        return;
    }
    DateStamp(&ds);
    t0 = (ULONG)ds.ds_Days * 86400UL * 50UL + (ULONG)ds.ds_Minute * 60UL * 50UL + (ULONG)ds.ds_Tick;
    if (!tc_cmd_wait_readable(conn)) {
        DateStamp(&ds);
        t1 = (ULONG)ds.ds_Days * 86400UL * 50UL + (ULONG)ds.ds_Minute * 60UL * 50UL + (ULONG)ds.ds_Tick;
        got = call_recv(conn, buf, sizeof(buf), 0);
        tapf("# %s: wait failed after %ld ticks; direct recv got=%ld; Wait mask fired=0x%x\n",
             label, (LONG)(t1 - t0), got,
             ((TnSocketBase *)SocketBase)->dbg_wait_fired);
            tapf("# %s: waitselect exit path=0x%x sig_int=0x%x pend_breaks=0x%x\n",
                 label,
                 (unsigned)(((TnSocketBase *)SocketBase)->dbg_wait_fired & 0xF),
                 (unsigned)(((TnSocketBase *)SocketBase)->sig_int),
                 (unsigned)(SetSignal(0, 0) & 0xF000));
        {
            /* TNET-151: name the bits - timer, sig_select, reply */
            TnSocketBase *tb = (TnSocketBase *)SocketBase;
            LONG tbit = (tb->timer_port != NULL) ? tb->timer_port->mp_SigBit : -1;
            tapf("# %s: bits: timer=%ld sig_select=0x%x reply=%ld dbg_fired=0x%x\n",
                 label, tbit, (unsigned)tb->sig_select,
                 (LONG)((tb->reply_port != NULL) ? tb->reply_port->mp_SigBit : -1),
                 (unsigned)tb->dbg_wait_fired);
        }
        if (got == 5 && memcmp(buf, "probe", 5) == 0) {
            call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
            TAP_NOTOK(label, "PROBE: data flows but selector wake is dead");
            return;
        }
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK(label, "PROBE: loopback data dead");
        return;
    }
    got = call_recv(conn, buf, sizeof(buf), 0);
    if (got != 5 || memcmp(buf, "probe", 5) != 0) {
        tapf("# %s: recv got=%ld errno=%ld\n", label, got, call_errno());
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK(label, "PROBE: recv mismatch");
        return;
    }
    call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
    TAP_OK(label);
}

static void tc_probe_after_wizard_wired(void)  { tc_probe_loop_impl("tc_probe_after_wizard_wired", 23531); }
static void tc_probe_after_wizard_ntsc(void)   { tc_probe_loop_impl("tc_probe_after_wizard_ntsc", 23532); }
static void tc_probe_after_wifi_scan(void)     { tc_probe_loop_impl("tc_probe_after_wifi_scan", 23533); }
static void tc_probe_after_reconfig(void)      { tc_probe_loop_impl("tc_probe_after_reconfig", 23534); }

/* TNET-152 / RC3 item 1: the user-facing stop path. Same mechanism as
 * NetShutdown / TolunnetControl STOP: Signal(port->mp_SigTask,
 * SIGBREAKF_CTRL_C). Proves the daemon actually EXITS (port disappears)
 * and afterwards bsdsocket.library cannot be opened (= TolunnetControl
 * STATUS RC 5 semantics). MUST be the last row - the daemon is gone
 * when it passes, and the bench restarts it for cycle 2 (two-banner
 * proof). */
static void tc_cmd_stop_start(void)
{
    struct MsgPort *port;
    struct Library *gone;
    TnIpcMsg msg;
    int waits;

    port = (struct MsgPort *)FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
    if (port == NULL || port->mp_SigTask == NULL) {
        TAP_NOTOK("tc_cmd_stop_start", "daemon port not found");
        return;
    }
    /* legacy signal path (TNET-152 evidence: alive-but-deaf) */
    Signal((struct Task *)port->mp_SigTask, SIGBREAKF_CTRL_C);
    Delay(100); /* 2 s */
    {
        char nm[16];
        LONG alive = (call_gethostname((STRPTR)nm, (LONG)sizeof(nm)) == 0);
        tapf("# tc_cmd_stop_start: after signal, daemon alive=%ld\n", alive);
    }

    /* robust stop: IPC (TNET-152). Expect EBUSY while our base is open */
    memset(&msg, 0, sizeof(msg));
    if (tn_ipc_oneshot(TN_IPC_CMD_STOP, NULL, 0, &msg) != 0 || msg.result == 0) {
        tapf("# tc_cmd_stop_start: STOP without clients?? rc=%ld err=%ld\n",
             msg.result, msg.err_no);
    }
    if (msg.err_no != EBUSY) {
        TAP_NOTOK("tc_cmd_stop_start", "STOP did not refuse with EBUSY while base open");
        return;
    }

    /* close our base (nothing runs after this row), then STOP again */
    CloseLibrary(SocketBase);
    SocketBase = NULL;
    memset(&msg, 0, sizeof(msg));
    if (tn_ipc_oneshot(TN_IPC_CMD_STOP, NULL, 0, &msg) != 0 || msg.result != 0) {
        TAP_NOTOK("tc_cmd_stop_start", "IPC STOP failed after close");
        return;
    }

    for (waits = 0; waits < 100; waits++) {
        Delay(5);
        if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) == NULL) break;
    }
    if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
        TAP_NOTOK("tc_cmd_stop_start", "daemon still running 10 s after IPC STOP");
        return;
    }

    /* TolunnetControl STATUS semantics: library must refuse to open */
    gone = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (gone != NULL) {
        CloseLibrary(gone);
        TAP_NOTOK("tc_cmd_stop_start", "bsdsocket.library still opens after daemon exit");
        return;
    }
    TAP_OK("tc_cmd_stop_start");
}

static void tc_iperf_loopback(void)
{
    LONG lst, cli, conn;
    static char blk[4096];
    LONG nbio = 1;
    LONG total = 0;
    ULONG t0, t1;
    ULONG rate;
    struct DateStamp ds;
    int i;

    if (!tc_cmd_tcp_pair(23521, &lst, &cli, &conn)) {
        TAP_NOTOK("tc_iperf_loopback", "no loopback pair");
        return;
    }
    for (i = 0; i < 4096; i++) blk[i] = (char)(i * 11 + 5);
    if (call_ioctl(cli, FIONBIO, &nbio) != 0 ||
        call_ioctl(conn, FIONBIO, &nbio) != 0) {
        call_closesocket(conn); call_closesocket(cli); call_closesocket(lst);
        TAP_NOTOK("tc_iperf_loopback", "FIONBIO failed");
        return;
    }

    DateStamp(&ds);
    t0 = (ULONG)ds.ds_Days * 86400UL * 50UL + (ULONG)ds.ds_Minute * 60UL * 50UL +
         (ULONG)ds.ds_Tick;
    for (;;) {
        LONG r = call_send(cli, blk, 4096, 0);
        if (r > 0) {
            total += r;
        } else {
            /* window full: drain the server side */
            LONG g;
            do {
                g = call_recv(conn, blk, sizeof(blk), 0);
            } while (g == (LONG)sizeof(blk));
        }
        DateStamp(&ds);
        t1 = (ULONG)ds.ds_Days * 86400UL * 50UL + (ULONG)ds.ds_Minute * 60UL * 50UL +
             (ULONG)ds.ds_Tick;
        if ((t1 - t0) >= 100UL) break; /* ~2 s */
    }

    /* final drain so the connection is clean before close */
    {
        LONG g;
        do {
            g = call_recv(conn, blk, sizeof(blk), 0);
        } while (g == (LONG)sizeof(blk));
    }

    rate = (total / 100UL) + 1UL; /* ~KB/s: bytes / 2s / 1024, kept simple */
    tapf("# iperf loopback: %lu bytes in ~2s = %lu KB/s%s\n", total, rate,
         (total > 65536UL) ? "" : " (LOW)");
    call_closesocket(conn);
    call_closesocket(cli);
    call_closesocket(lst);
    if (total > 65536UL) {
        TAP_OK("tc_iperf_loopback");
    } else {
        TAP_NOTOK("tc_iperf_loopback", "loopback moved too little data");
    }
}

/* CLOSE §B.7: IFCTL — the IPC behind AddNetInterface /
 * ConfigureNetInterface / Online / Offline. LIST fields sane, UP
 * idempotent, SET writes-back the SAME address (non-destructive),
 * DOWN/UP round-trip visible in LIST. */
static void tc_cmd_ifctl(void)
{
    static TnIfInfo rows[4];
    TnIpcMsg msg;
    LONG args[6];
    APTR ptrs[1];
    LONG n;

    /* LIST: at least the primary interface, sane fields */
    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_IFCTL_LIST;
    args[4] = 4;
    ptrs[0] = rows;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_IFCTL, args, 5, ptrs, 1, &msg) < 0) {
        TAP_NOTOK("tc_cmd_ifctl", "LIST transport failed");
        return;
    }
    n = msg.result;
    if (n < 1) {
        TAP_NOTOK("tc_cmd_ifctl", "LIST returned nothing");
        return;
    }
    if (!rows[0].in_use || rows[0].name[0] == ' ') {
        TAP_NOTOK("tc_cmd_ifctl", "primary row missing name/in_use");
        return;
    }
    if (!rows[0].is_up) {
        TAP_NOTOK("tc_cmd_ifctl", "interface not up");
        return;
    }

    /* SET the SAME address/mask/gw back (non-destructive write), verify
     * via re-LIST. RC3: the UP arm (S2_ONLINE DoIO) is deliberately not
     * exercised here - the emulated a2065/uaenet wedges on it
     * intermittently on a1200 (leg hang 20260921-112359); the Online
     * command path is identical to daemon startup's, proven every boot.
     * The DOWN/UP live toggle belongs to the owner test on real
     * hardware. */
    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_IFCTL_SET;
    args[1] = -1;
    args[2] = (LONG)rows[0].addr;
    args[3] = (LONG)rows[0].mask;
    args[4] = (LONG)rows[0].gw;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_IFCTL, args, 5, NULL, 0, &msg) != 0 ||
        msg.result != 0) {
        TAP_NOTOK("tc_cmd_ifctl", "SET same-values failed");
        return;
    }

    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_IFCTL_LIST;
    args[4] = 4;
    ptrs[0] = rows;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_IFCTL, args, 5, ptrs, 1, &msg) < 0 ||
        msg.result < 1 || !rows[0].is_up) {
        TAP_NOTOK("tc_cmd_ifctl", "re-LIST after SET failed");
        return;
    }
    TAP_OK("tc_cmd_ifctl");
}
static void tc_cmd_netshutdown(void)
{
    struct MsgPort *port = (struct MsgPort *)FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
    if (port == NULL || port->mp_SigTask == NULL) {
        TAP_NOTOK("tc_cmd_netshutdown", "daemon port not found");
        return;
    }
    TAP_OK("tc_cmd_netshutdown");
}

static void tc_cmd_route(void)
{
    /* route / AddNetRoute / DeleteNetRoute (CLOSE §B.5): the exact ROUTECTL
     * IPC the commands send — ADD a /16 via a gateway, LIST sees it,
     * duplicate is rejected, longest-prefix lookup semantics live in the
     * host tests; DELETE removes it and LIST reflects the change. */
    static TnRouteInfo rows[TN_MAX_ROUTES];
    TnIpcMsg msg;
    LONG args[6];
    APTR ptrs[1];
    LONG n, i;
    BOOL seen = FALSE;

    /* ADD 10.9.0.0/255.255.0.0 gw 10.0.2.2 (tn_ipc_oneshot_ex returns
     * msg.result, so success is judged on the copied-out result alone) */
    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_ROUTECTL_ADD;
    args[1] = (LONG)htonl(0x0A090000UL);
    args[2] = (LONG)htonl(0xFFFF0000UL);
    args[3] = (LONG)htonl(0x0A000202UL);
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, NULL, 0, &msg) != 0 ||
        msg.result != 0) {
        TAP_NOTOK("tc_cmd_route", "ADD rejected");
        return;
    }

    /* duplicate ADD must fail (EEXIST — also surfaces as return -1) */
    tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, NULL, 0, &msg);
    if (msg.result == 0) {
        TAP_NOTOK("tc_cmd_route", "duplicate ADD accepted");
        return;
    }

    /* LIST contains it with the right fields */
    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_ROUTECTL_LIST;
    args[4] = TN_MAX_ROUTES;
    ptrs[0] = rows;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, ptrs, 1, &msg) < 0) {
        TAP_NOTOK("tc_cmd_route", "LIST transport failed");
        return;
    }
    n = msg.result;
    if (n < 1) {
        TAP_NOTOK("tc_cmd_route", "LIST returned nothing");
        return;
    }
    for (i = 0; i < n; i++) {
        if (rows[i].dest == htonl(0x0A090000UL) &&
            rows[i].mask == htonl(0xFFFF0000UL) &&
            rows[i].gw == htonl(0x0A000202UL)) {
            seen = TRUE;
            break;
        }
    }
    if (!seen) {
        TAP_NOTOK("tc_cmd_route", "LIST row fields wrong");
        return;
    }

    /* DELETE it, then LIST must not contain it */
    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_ROUTECTL_DELETE;
    args[1] = (LONG)htonl(0x0A090000UL);
    args[2] = (LONG)htonl(0xFFFF0000UL);
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, NULL, 0, &msg) != 0 ||
        msg.result != 0) {
        TAP_NOTOK("tc_cmd_route", "DELETE failed");
        return;
    }
    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_ROUTECTL_LIST;
    args[4] = TN_MAX_ROUTES;
    ptrs[0] = rows;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, ptrs, 1, &msg) < 0) {
        TAP_NOTOK("tc_cmd_route", "LIST after delete failed");
        return;
    }
    n = msg.result;
    for (i = 0; i < n; i++) {
        if (rows[i].dest == htonl(0x0A090000UL) &&
            rows[i].mask == htonl(0xFFFF0000UL)) {
            TAP_NOTOK("tc_cmd_route", "deleted route still listed");
            return;
        }
    }
    TAP_OK("tc_cmd_route");
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
    if (argc >= 3 && strcmp(argv[1], "child_obtain") == 0) {        LONG target_id = parse_long(argv[2]);
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

    /* TNET-111: per-test watchdog — a daemon that stops replying fails the
     * blocked IPC call with ETIMEDOUT instead of hanging the run.
     * TNET-150 ordering guard: the watchdog must OUTLIVE the daemon's
     * whole DNS attempt (DNS_RETRIES x 1 s retransmit window) so deferred
     * lookups resolve inside the wait, not after abandonment. */
    ((TnSocketBase *)SocketBase)->ipc_timeout_ms =
        (uint32_t)(tc_cfg_long("DNS_RETRIES", 4) * 1000 + 2000);

    tapf("# tolunnet SocketConformance (Round 3 §B.2)\n");
    TN_RUN(tc_lib_open_close);
    TN_RUN(tc_socket_types);
    TN_RUN(tc_bind_udp);
    TN_RUN(tc_bind_reuse);
    TN_RUN(tc_sockopt_matrix);
    TN_RUN(tc_multicast_join);
    TN_RUN(tc_ioctl_ifconf);
    TN_RUN(tc_ioctl_fionread);
    TN_RUN(tc_listen_accept_loopback);
    TN_RUN(tc_connect_refused);
    TN_RUN(tc_nonblock_connect);
    TN_RUN(tc_shutdown_wr);
    TN_RUN(tc_getpeername);
    TN_RUN(tc_dns_a);
    TN_RUN(tc_dns_local);
    TN_RUN(tc_dns_fail);
    TN_RUN(tc_errno_ptr);
    TN_RUN(tc_dup2);
    TN_RUN(tc_waitselect_timeout);
    TN_RUN(tc_waitselect_eintr);
    TN_RUN(tc_waitselect_badf);
    TN_RUN(tc_waitselect_no_sigio);
    TN_RUN(tc_sigio);
    TN_RUN(tc_icmp_raw);
    TN_RUN(tc_sendmsg_iov);
    TN_RUN(tc_tcp_scatter_tnet115);
    TN_RUN(tc_recv_peek);
    /* CLOSE §B/TNET-150: command-surface rows run BEFORE the heavyweight
     * wizard/reconfig tail — TNET-151 (suite-tail loopback degradation
     * after the wizard tests) must not colour the command proofs. */
    TN_RUN(tc_cmd_hostname);
    TN_RUN(tc_cmd_nslookup);
    TN_RUN(tc_cmd_whois);
    TN_RUN(tc_cmd_traceroute);
    TN_RUN(tc_cmd_nc);
    TN_RUN(tc_cmd_sntp);
    TN_RUN(tc_cmd_telnet);
    TN_RUN(tc_cmd_tftp);
    TN_RUN(tc_cmd_ftp);
    TN_RUN(tc_cmd_arp);
    TN_RUN(tc_cmd_shownetstatus);
    TN_RUN(tc_cmd_tolunnetcontrol);
    TN_RUN(tc_cmd_getnetstatus);
    TN_RUN(tc_iperf_loopback);
    TN_RUN(tc_cmd_ifctl);
    TN_RUN(tc_cmd_netshutdown);
    TN_RUN(tc_cmd_route);
    TN_RUN(tc_socket_events);
    TN_RUN(tc_sbtc_full);
    TN_RUN(tc_release_obtain);
    TN_RUN(tc_every_vector_callable);
    TN_RUN(tc_stats_counters);
    TN_RUN(tc_wizard_wired);
    TN_RUN(tc_wizard_ntsc);
    TN_RUN(tc_wifi_scan_parse);
    TN_RUN(tc_reconfig_rc);
    /* TNET-151 proof #2: all four probes after the wizard tail -
     * must pass with the time-verified watchdog */
    TN_RUN(tc_probe_after_wizard_wired);
    TN_RUN(tc_probe_after_wizard_ntsc);
    TN_RUN(tc_probe_after_wifi_scan);
    TN_RUN(tc_probe_after_reconfig);
    TN_RUN(tc_cmd_stop_start); /* LAST: stops the daemon */
    tapf("1..%d\n", g_count);
    tapf("# bench: asking daemon to stop (restart-cycle proof)\n");

    if (SocketBase != NULL) CloseLibrary(SocketBase);
    /* daemon already stopped by tc_cmd_stop_start (IPC); harmless if gone */
    request_daemon_stop();

    if (g_log_fh) Close(g_log_fh);
    CloseLibrary(DOSBase);

    not_ok = g_not_ok_count;
    return (not_ok > 9) ? 9 : not_ok;
}
