/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Shared command-line client infrastructure implementation (CMD-0).
 * All bsdsocket calls use inline-asm LVO wrappers (-noixemul compatible).
 */
#include "cmdlib.h"
#include <string.h>
#include "../common/ipc_client.h"
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

/* z.ai step 8a item 4: sendto (LVO -60) and recvfrom (LVO -72) need the
 * full register set - flags in D2, destination in A1/D3, source addr in
 * A1 and the fromlen pointer in A2. */
LONG tn_call_sendto(LONG fd, const void *buf, LONG len, LONG flags,
                    const struct sockaddr *to, LONG tolen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register const void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;
    register const struct sockaddr *a1 __asm__("a1") = to;
    register LONG d3 __asm__("d3") = tolen;
    __asm__ __volatile__("jsr -60(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2), "r"(a1), "r"(d3) : "d1","d2","d3","a0","a1","memory");
    return d0;
}

LONG tn_call_recvfrom(LONG fd, void *buf, LONG len, LONG flags,
                      struct sockaddr *from, LONG *fromlen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;
    register struct sockaddr *a1 __asm__("a1") = from;
    register LONG *a2 __asm__("a2") = fromlen;
    __asm__ __volatile__("jsr -72(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2), "r"(a1), "r"(a2) : "d1","d2","a0","a1","a2","memory");
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

LONG tn_call_bind(LONG fd, const struct sockaddr *a, LONG len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register const struct sockaddr *a0 __asm__("a0") = a;
    register LONG d1 __asm__("d1") = len;
    __asm__ __volatile__("jsr -36(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(d1) : "d1","a0","a1","memory");
    return d0;
}

LONG tn_call_listen(LONG fd, LONG backlog)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register LONG d1 __asm__("d1") = backlog;
    __asm__ __volatile__("jsr -42(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(d1) : "d1","a0","a1","memory");
    return d0;
}

LONG tn_call_accept(LONG fd, struct sockaddr *a, LONG *len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register struct sockaddr *a0 __asm__("a0") = a;
    register LONG *a1 __asm__("a1") = len;
    __asm__ __volatile__("jsr -48(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(a0), "r"(a1) : "a0","a1","memory");
    return d0;
}

LONG tn_call_shutdown(LONG fd, LONG how)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register LONG d1 __asm__("d1") = how;
    __asm__ __volatile__("jsr -84(%%a6)" : "+r"(d0) : "r"(a6), "r"(d0), "r"(d1) : "d1","a0","a1","memory");
    return d0;
}

LONG tn_call_setsockopt(LONG fd, LONG level, LONG optname, const void *optval, LONG optlen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register LONG d1 __asm__("d1") = level;
    register LONG d2 __asm__("d2") = optname;
    register const void *a0 __asm__("a0") = optval;
    register LONG d3 __asm__("d3") = optlen;
    __asm__ __volatile__("jsr -90(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2), "r"(a0), "r"(d3)
        : "d1","d2","d3","a0","a1","memory");
    return d0;
}

LONG tn_call_getsockopt(LONG fd, LONG level, LONG optname, void *optval, LONG *optlen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register LONG d1 __asm__("d1") = level;
    register LONG d2 __asm__("d2") = optname;
    register void *a0 __asm__("a0") = optval;
    register LONG *a1 __asm__("a1") = optlen;
    __asm__ __volatile__("jsr -96(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2), "r"(a0), "r"(a1)
        : "d1","d2","a0","a1","memory");
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
    /* z.ai step 8a item 2: Inet_NtoA (LVO -174) takes the address in D0
     * and returns the string pointer in D0. */
    register struct Library *a6 __asm__("a6") = SocketBase;
    register ULONG d0 __asm__("d0") = in.s_addr;
    __asm__ __volatile__("jsr -174(%%a6)" : "+r"(d0) : "r"(a6) : "a0","a1","memory");
    return (STRPTR)d0;
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

/* --- z.ai step 9a item 1: live daemon state over IPC only --- */
void tn_cmd_ip_to_str(ULONG ip, char *buf)
{
    /* 68k: a ULONG holding a network-order address is the a.b.c.d value */
    ULONG v[4];
    char tmp[24];
    LONG o = 0, k, i;
    v[0] = (ip >> 24) & 0xFF; v[1] = (ip >> 16) & 0xFF;
    v[2] = (ip >> 8) & 0xFF; v[3] = ip & 0xFF;
    for (i = 0; i < 4; i++) {
        char rev[4];
        LONG rl = 0, pl = 0;
        ULONG x = v[i];
        if (i > 0) tmp[o++] = 46; /* '.' */
        if (x == 0) {
            rev[rl++] = 48; /* '0' */
        } else {
            while (x > 0) { rev[rl++] = (char)(48 + (x % 10)); x /= 10; }
        }
        while (rl > 0) tmp[o++] = rev[--rl];
    }
    tmp[o] = 0;
    for (k = 0; k <= o; k++) buf[k] = tmp[k];
}

int tn_cmd_snapshot(TnSnapshot *snap)
{
    LONG st_args[5], if_args[5], rt_args[5];
    APTR st_ptrs[1], if_ptrs[1], rt_ptrs[1];
    TnIpcMsg msg;

    if (snap == NULL) return -1;
    memset(snap, 0, sizeof(*snap));
    snap->if_count = -1;
    snap->route_count = -1;

    st_args[0] = 0; st_args[1] = 0; st_args[2] = 0; st_args[3] = 0;
    st_args[4] = (LONG)sizeof(TnStatusInfoV2);
    st_ptrs[0] = (APTR)&snap->status;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATUS, st_args, 5, st_ptrs, 1, &msg) != 0)
        return -1;
    snap->socket_count = (LONG)snap->status.active_sockets;

    if_args[0] = TN_IFCTL_LIST; if_args[1] = 0; if_args[2] = 0; if_args[3] = 0;
    if_args[4] = TN_SNAP_MAX_IFS;
    if_ptrs[0] = (APTR)snap->ifs;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_IFCTL, if_args, 5, if_ptrs, 1, &msg) == 0)
        snap->if_count = msg.result;

    rt_args[0] = TN_ROUTECTL_LIST; rt_args[1] = 0; rt_args[2] = 0; rt_args[3] = 0;
    rt_args[4] = TN_SNAP_MAX_ROUTES;
    rt_ptrs[0] = (APTR)snap->routes;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, rt_args, 5, rt_ptrs, 1, &msg) == 0)
        snap->route_count = msg.result;

    return 0;
}
