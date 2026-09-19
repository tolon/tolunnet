/*
 * tolunnet — Shared command-line client infrastructure implementation (CMD-0).
 * All bsdsocket calls use inline-asm LVO wrappers (-noixemul compatible).
 */
#include "cmdlib.h"
#include <string.h>
#include <stdarg.h>

struct Library *SocketBase = NULL;

/* ---- LVO wrappers ---- */
LONG tn_call_socket(LONG d, LONG t, LONG p)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = d;
    register LONG d1 __asm__("d1") = t;
    register LONG d2 __asm__("d2") = p;
    __asm__ __volatile__("jsr -30(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(d1), "r"(d2) : "d1","d2","a0","a1","memory");
    return d0;
}

LONG tn_call_connect(LONG fd, const struct sockaddr *a, LONG len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register const void *a0 __asm__("a0") = a;
    register LONG d1 __asm__("d1") = len;
    __asm__ __volatile__("jsr -54(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(d1) : "d1","a0","a1","memory");
    return d0;
}

LONG tn_call_send(LONG fd, const void *buf, LONG len, LONG flags)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register const void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;
    __asm__ __volatile__("jsr -66(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2) : "d1","d2","a0","a1","memory");
    return d0;
}

LONG tn_call_recv(LONG fd, void *buf, LONG len, LONG flags)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;
    __asm__ __volatile__("jsr -78(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2) : "d1","d2","a0","a1","memory");
    return d0;
}

LONG tn_call_closesocket(LONG fd)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    __asm__ __volatile__("jsr -120(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0) : "a0","a1","memory");
    return d0;
}

LONG tn_call_gethostname(STRPTR name, LONG len)
{
    /* TNET-141: -282 is gethostname; -240 (used here before) is
     * getservbyport, which never writes the buffer — hostname,
     * ShowNetStatus and GetNetStatus printed stack garbage. */
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = len;
    register STRPTR a0 __asm__("a0") = name;
    __asm__ __volatile__("jsr -282(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0) : "a0","a1","memory");
    return d0;
}

LONG tn_call_ioctl(LONG fd, ULONG req, APTR argp)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register ULONG d1 __asm__("d1") = req;
    register APTR a0 __asm__("a0") = argp;
    __asm__ __volatile__("jsr -114(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(d1), "r"(a0) : "d1","a0","a1","memory");
    return d0;
}

LONG tn_call_errno(void)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0");
    __asm__ __volatile__("jsr -162(%%a6)" : "=r"(d0) : "r"(a6) : "a0","a1","memory");
    return d0;
}

ULONG tn_call_inet_addr(const char *cp)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register ULONG d0 __asm__("d0");
    register const char *a0 __asm__("a0") = cp;
    __asm__ __volatile__("jsr -180(%%a6)" : "=r"(d0) : "r"(a6), "r"(a0) : "a0","a1","memory");
    return d0;
}

STRPTR tn_call_inet_ntoa(struct in_addr in)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register STRPTR d0 __asm__("d0");
    register ULONG a0 __asm__("a0") = in.s_addr;
    __asm__ __volatile__("jsr -174(%%a6)" : "=r"(d0) : "r"(a6), "r"(a0) : "a0","a1","memory");
    return d0;
}

struct hostent *tn_call_gethostbyname(const char *name)
{
    /* TNET-141: -210 is gethostbyname (-156 is ReleaseCopyOfSocket). */
    register struct Library *a6 __asm__("a6") = SocketBase;
    register struct hostent *d0 __asm__("d0");
    register const char *a0 __asm__("a0") = name;
    __asm__ __volatile__("jsr -210(%%a6)" : "=r"(d0) : "r"(a6), "r"(a0) : "a0","a1","memory");
    return d0;
}

struct hostent *tn_call_gethostbyaddr(const char *addr, LONG len, LONG type)
{
    /* TNET-141: -216 is gethostbyaddr (-150 is ReleaseSocket). */
    register struct Library *a6 __asm__("a6") = SocketBase;
    register struct hostent *d0 __asm__("d0");
    register const char *a0 __asm__("a0") = addr;
    register LONG d0_len __asm__("d0") = len;
    register LONG d1 __asm__("d1") = type;
    __asm__ __volatile__("jsr -216(%%a6)" : "=r"(d0) : "r"(a6), "r"(a0), "r"(d0_len), "r"(d1) : "d1","a0","a1","memory");
    return d0;
}

LONG tn_call_waitselect(LONG nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv, ULONG *sigmask)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = nfds;
    register fd_set *a0 __asm__("a0") = r;
    register fd_set *a1 __asm__("a1") = w;
    register fd_set *a2 __asm__("a2") = e;
    register struct timeval *a3 __asm__("a3") = tv;
    register ULONG *d1_ptr __asm__("d1") = (ULONG *)sigmask;
    __asm__ __volatile__("jsr -126(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(d1_ptr) : "d1","a0","a1","a2","a3","memory");
    return d0;
}

/* ---- Helpers ---- */
int tn_cmd_init(void)
{
    if (SocketBase != NULL) return TN_CMD_OK;
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr((CONST_STRPTR)"tolunnet: bsdsocket.library not available - is the daemon running?\n");
        return TN_CMD_FAIL;
    }
    return TN_CMD_OK;
}

void tn_cmd_fini(void)
{
    if (SocketBase != NULL) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

void tn_cmd_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    VPrintf((CONST_STRPTR)fmt, ap);
    va_end(ap);
    Flush(Output());
}

BOOL tn_cmd_check_ctrlc(void)
{
    return CheckSignal(SIGBREAKF_CTRL_C);
}

ULONG tn_cmd_resolve(const char *host)
{
    ULONG addr;

    if (host == NULL || host[0] == '\0') return INADDR_NONE;

    addr = tn_call_inet_addr(host);
    if (addr != INADDR_NONE) return addr;

    tn_cmd_printf("Resolving %s... ", host);
    {
        struct hostent *he = tn_call_gethostbyname(host);
        if (he == NULL || he->h_addr_list[0] == NULL) {
            tn_cmd_printf("failed\n");
            return INADDR_NONE;
        }
        {
            ULONG result;
            memcpy(&result, he->h_addr_list[0], 4);
            tn_cmd_printf("%s\n", tn_call_inet_ntoa(*(struct in_addr *)&result));
            return result;
        }
    }
}
