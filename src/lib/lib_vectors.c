/*
 * tolunnet — bsdsocket.library LVO vector table and stub implementations.
 *
 * Implements standard AmigaOS 68k library vectors with Exec IPC dispatch
 * to the tolunnet network daemon, fully compliant with Roadshow SDK 1.8
 * and AmiTCP V4 specifications (TOLUNNET-COMPAT.md).
 */

#include "../../include/ipc.h"
#include "../common/log.h"
#include "../common/inet_parse.h"
#include "../common/sbtc_dispatch.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <utility/tagitem.h>
#include <libraries/bsdsocket.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/errno.h>

/* Helper to set errno respecting width */
static inline void tn_set_errno_val(TnSocketBase *base, LONG err)
{
    if (base == NULL) return;
    base->task_errno = err;
    if (base->errno_ptr != NULL) {
        if (base->errno_width == 1) {
            *((UBYTE *)base->errno_ptr) = (UBYTE)err;
        } else if (base->errno_width == 2) {
            *((UWORD *)base->errno_ptr) = (UWORD)err;
        } else {
            *base->errno_ptr = err;
        }
    }
}

/* Helper to set h_errno */
static inline void tn_set_herrno_val(TnSocketBase *base, LONG herr)
{
    if (base == NULL) return;
    base->task_herrno = herr;
    if (base->herrno_ptr != NULL) {
        *base->herrno_ptr = herr;
    }
}

/* Helper to record error on unhandled LVOs (TNET-009) */
static inline LONG tn_set_enosys(TnSocketBase *base)
{
    tn_set_errno_val(base, ENOSYS);
    return -1;
}

/* Helper to execute a synchronous IPC call from client task to tolunnet task */
static LONG tn_ipc_call(TnSocketBase *base, TnIpcCmd cmd)
{
    TnIpcMsg *msg;

    if (base == NULL) return -1;

    if (base->tolunnet_port == NULL) {
        base->tolunnet_port = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
        if (base->tolunnet_port == NULL) {
            tn_set_errno_val(base, ENETDOWN);
            return -1;
        }
    }

    msg = &base->ipc_msg;
    msg->msg.mn_Node.ln_Type = NT_MESSAGE;
    msg->msg.mn_Node.ln_Pri  = 0;
    msg->msg.mn_ReplyPort    = base->reply_port;
    msg->msg.mn_Length       = sizeof(TnIpcMsg);
    msg->cmd                 = cmd;
    msg->client_task         = base->owner_task;
    msg->socket_base         = (APTR)base;

    PutMsg(base->tolunnet_port, (struct Message *)msg);
    WaitPort(base->reply_port);
    GetMsg(base->reply_port);

    if (msg->result < 0 && msg->err_no != 0) {
        tn_set_errno_val(base, msg->err_no);
    }

    return msg->result;
}

/* ------------------------------------------------------------------ LIB_OPEN */
struct Library *tn_lib_open(struct Library *lib, ULONG version)
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    TnSocketBase *base;
    ULONG neg_size;
    ULONG total_size;
    UBYTE *raw_mem;
    int i;
    (void)version;

    if (lib == NULL) return NULL;

    /* Increment root library open count safely (TNET-007) */
    Forbid();
    lib->lib_OpenCnt++;
    Permit();

    neg_size   = (ULONG)lib->lib_NegSize;
    total_size = neg_size + sizeof(TnSocketBase);

    raw_mem = (UBYTE *)AllocVec(total_size, MEMF_CLEAR | MEMF_PUBLIC);
    if (raw_mem == NULL) {
        Forbid();
        lib->lib_OpenCnt--;
        Permit();
        return NULL;
    }

    /* Copy jump table (negative offsets) and Library struct template */
    CopyMem((CONST APTR)((UBYTE *)lib - neg_size), (APTR)raw_mem, neg_size + sizeof(struct Library));

    base = (TnSocketBase *)(raw_mem + neg_size);
    base->lib_node.lib_NegSize = (UWORD)neg_size;
    base->lib_node.lib_PosSize = (UWORD)sizeof(TnSocketBase);
    base->lib_node.lib_OpenCnt = 1;

    base->owner_task = SysBase->ThisTask;
    base->reply_port = CreateMsgPort();
    if (base->reply_port == NULL) {
        Forbid();
        lib->lib_OpenCnt--;
        Permit();
        FreeVec(raw_mem);
        return NULL;
    }

    base->tolunnet_port = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
    base->errno_ptr    = &base->task_errno;
    base->task_errno   = 0;
    base->errno_width  = 4;
    base->herrno_ptr   = &base->task_herrno;
    base->task_herrno  = 0;
    base->sig_io       = 0;
    base->sig_urg      = 0;
    base->sig_int      = 0;
    base->inet_ntoa_buf[0] = '\0';
    base->hostname[0]  = '\0';

    for (i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
        base->fd_map[i] = -1;
    }

    /* Notify network task of new client opening */
    tn_ipc_call(base, TN_IPC_CMD_OPEN);

    return (struct Library *)base;
}

/* ----------------------------------------------------------------- LIB_CLOSE */
BPTR tn_lib_close(struct Library *lib)
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    TnSocketBase *base = (TnSocketBase *)lib;
    struct Library *root_lib;

    if (base != NULL) {
        /* Close all remaining open sockets for this task */
        tn_ipc_call(base, TN_IPC_CMD_CLOSE);

        if (base->reply_port != NULL) {
            DeleteMsgPort(base->reply_port);
            base->reply_port = NULL;
        }

        /* Decrement root library open count under Forbid (TNET-007) */
        Forbid();
        root_lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)BSDSOCKET_NAME);
        if (root_lib != NULL && root_lib->lib_OpenCnt > 0) {
            root_lib->lib_OpenCnt--;
        }
        Permit();

        FreeVec((UBYTE *)base - base->lib_node.lib_NegSize);
    }
    return (BPTR)0;
}

/* --------------------------------------------------------------- LIB_EXPUNGE */
BPTR tn_lib_expunge(struct Library *lib)
{
    (void)lib;
    return (BPTR)0;
}

/* -------------------------------------------------------------- LIB_RESERVED */
ULONG tn_lib_reserved(VOID)
{
    return 0;
}

/* ================================================================= BSD LVOs */

/* -30: socket(domain, type, protocol) */
LONG tn_lvo_socket(LONG domain, LONG type, LONG protocol, TnSocketBase *base)
{
    if (base == NULL) return -1;
    base->ipc_msg.args[0] = domain;
    base->ipc_msg.args[1] = type;
    base->ipc_msg.args[2] = protocol;
    return tn_ipc_call(base, TN_IPC_CMD_SOCKET);
}

/* -36: bind(sock, name, namelen) */
LONG tn_lvo_bind(LONG sock, struct sockaddr *name, socklen_t namelen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)name;
    base->ipc_msg.args[1] = (LONG)namelen;
    return tn_ipc_call(base, TN_IPC_CMD_BIND);
}

/* -42: listen(sock, backlog) */
LONG tn_lvo_listen(LONG sock, LONG backlog, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.args[1] = backlog;
    return tn_ipc_call(base, TN_IPC_CMD_LISTEN);
}

/* -48: accept(sock, addr, addrlen) */
LONG tn_lvo_accept(LONG sock, struct sockaddr *addr, socklen_t *addrlen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)addr;
    base->ipc_msg.ptrs[1] = (APTR)addrlen;
    return tn_ipc_call(base, TN_IPC_CMD_ACCEPT);
}

/* -54: connect(sock, name, namelen) */
LONG tn_lvo_connect(LONG sock, struct sockaddr *name, socklen_t namelen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)name;
    base->ipc_msg.args[1] = (LONG)namelen;
    return tn_ipc_call(base, TN_IPC_CMD_CONNECT);
}

/* -60: sendto(sock, buf, len, flags, to, tolen) */
LONG tn_lvo_sendto(LONG sock, const void *buf, LONG len, LONG flags,
                   const struct sockaddr *to, socklen_t tolen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)buf;
    base->ipc_msg.args[1] = len;
    base->ipc_msg.args[2] = flags;
    base->ipc_msg.ptrs[1] = (APTR)to;
    base->ipc_msg.args[3] = (LONG)tolen;
    return tn_ipc_call(base, TN_IPC_CMD_SENDTO);
}

/* -66: send(sock, buf, len, flags) */
LONG tn_lvo_send(LONG sock, const void *buf, LONG len, LONG flags, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)buf;
    base->ipc_msg.args[1] = len;
    base->ipc_msg.args[2] = flags;
    return tn_ipc_call(base, TN_IPC_CMD_SEND);
}

/* -72: recvfrom(sock, buf, len, flags, addr, addrlen) */
LONG tn_lvo_recvfrom(LONG sock, void *buf, LONG len, LONG flags,
                     struct sockaddr *addr, socklen_t *addrlen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)buf;
    base->ipc_msg.args[1] = len;
    base->ipc_msg.args[2] = flags;
    base->ipc_msg.ptrs[1] = (APTR)addr;
    base->ipc_msg.ptrs[2] = (APTR)addrlen;
    return tn_ipc_call(base, TN_IPC_CMD_RECVFROM);
}

/* -78: recv(sock, buf, len, flags) */
LONG tn_lvo_recv(LONG sock, void *buf, LONG len, LONG flags, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)buf;
    base->ipc_msg.args[1] = len;
    base->ipc_msg.args[2] = flags;
    return tn_ipc_call(base, TN_IPC_CMD_RECV);
}

/* -84: shutdown(sock, how) */
LONG tn_lvo_shutdown(LONG sock, LONG how, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.args[1] = how;
    return tn_ipc_call(base, TN_IPC_CMD_SHUTDOWN);
}

/* -90: setsockopt(sock, level, optname, optval, optlen) */
LONG tn_lvo_setsockopt(LONG sock, LONG level, LONG optname, const void *optval,
                       socklen_t optlen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.args[1] = level;
    base->ipc_msg.args[2] = optname;
    base->ipc_msg.ptrs[0] = (APTR)optval;
    base->ipc_msg.args[3] = (LONG)optlen;
    return tn_ipc_call(base, TN_IPC_CMD_SETSOCKOPT);
}

/* -96: getsockopt(sock, level, optname, optval, optlen) */
LONG tn_lvo_getsockopt(LONG sock, LONG level, LONG optname, void *optval,
                       socklen_t *optlen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.args[1] = level;
    base->ipc_msg.args[2] = optname;
    base->ipc_msg.ptrs[0] = (APTR)optval;
    base->ipc_msg.ptrs[1] = (APTR)optlen;
    return tn_ipc_call(base, TN_IPC_CMD_GETSOCKOPT);
}

/* -102: getsockname(sock, name, namelen) */
LONG tn_lvo_getsockname(LONG sock, struct sockaddr *name, socklen_t *namelen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)name;
    base->ipc_msg.ptrs[1] = (APTR)namelen;
    return tn_ipc_call(base, TN_IPC_CMD_GETSOCKNAME);
}

/* -108: getpeername(sock, name, namelen) */
LONG tn_lvo_getpeername(LONG sock, struct sockaddr *name, socklen_t *namelen, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)name;
    base->ipc_msg.ptrs[1] = (APTR)namelen;
    return tn_ipc_call(base, TN_IPC_CMD_GETPEERNAME);
}

/* -114: IoctlSocket(sock, req, argp) */
LONG tn_lvo_ioctlsocket(LONG sock, ULONG req, APTR argp, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.args[1] = (LONG)req;
    base->ipc_msg.ptrs[0] = argp;
    return tn_ipc_call(base, TN_IPC_CMD_IOCTL);
}

/* -120: CloseSocket(sock) */
LONG tn_lvo_closesocket(LONG sock, TnSocketBase *base)
{
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    return tn_ipc_call(base, TN_IPC_CMD_CLOSESOCKET);
}

/* -126: WaitSelect(nfds, read_fds, write_fds, except_fds, timeout, signals) */
LONG tn_lvo_waitselect(LONG nfds, fd_set *read_fds, fd_set *write_fds,
                       fd_set *except_fds, struct timeval *timeout,
                       ULONG *signals, TnSocketBase *base)
{
    LONG res;
    ULONG timeout_ms = 0;
    ULONG sig_mask = 0;
    ULONG received_sigs = 0;

    if (base == NULL) return -1;

    base->ipc_msg.args[0] = nfds;
    base->ipc_msg.ptrs[0] = (APTR)read_fds;
    base->ipc_msg.ptrs[1] = (APTR)write_fds;
    base->ipc_msg.ptrs[2] = (APTR)except_fds;
    base->ipc_msg.ptrs[3] = (APTR)timeout;

    if (timeout != NULL) {
        timeout_ms = timeout->tv_secs * 1000UL + timeout->tv_micro / 1000UL;
    }

    if (signals != NULL) {
        sig_mask = *signals;
    }

    /* WaitSelect loop with responsive signal checking and sleep (TNET-041) */
    ULONG elapsed_ms = 0;
    while (1) {
        res = tn_ipc_call(base, TN_IPC_CMD_WAITSELECT);
        if (res > 0) {
            if (signals != NULL) *signals = 0;
            return res;
        }

        /* Check AmigaOS user/break signals */
        if (sig_mask != 0) {
            received_sigs = SetSignal(0, 0) & sig_mask;
            if (received_sigs != 0) {
                SetSignal(0, received_sigs);
                if (signals != NULL) *signals = received_sigs;
                return 0;
            }
        }

        if (timeout != NULL && elapsed_ms >= timeout_ms) {
            if (signals != NULL) *signals = 0;
            return 0;
        }

        /* 1 tick (20 ms) slice to prevent CPU spinning and remain signal-responsive */
        Delay(1);
        elapsed_ms += 20;
    }
}

/* -132: SetSocketSignals(int_mask, io_mask, urgent_mask) */
VOID tn_lvo_setsocketsignals(ULONG int_mask, ULONG io_mask, ULONG urgent_mask, TnSocketBase *base)
{
    if (base != NULL) {
        base->sig_int = int_mask;
        base->sig_io  = io_mask;
        base->sig_urg = urgent_mask;
    }
}

/* -138: getdtablesize() */
LONG tn_lvo_getdtablesize(TnSocketBase *base)
{
    (void)base;
    return TN_MAX_FDS_PER_TASK;
}

/* -144: ObtainSocket(...) */
LONG tn_lvo_obtainsocket(LONG id, LONG domain, LONG type, LONG protocol, TnSocketBase *base)
{
    (void)id; (void)domain; (void)type; (void)protocol;
    return tn_set_enosys(base);
}

/* -150: ReleaseSocket(...) */
LONG tn_lvo_releasesocket(LONG sock, LONG id, TnSocketBase *base)
{
    (void)sock; (void)id;
    return tn_set_enosys(base);
}

/* -156: ReleaseCopyOfSocket(...) */
LONG tn_lvo_releasecopyofsocket(LONG sock, LONG id, TnSocketBase *base)
{
    (void)sock; (void)id;
    return tn_set_enosys(base);
}

/* -162: Errno() */
LONG tn_lvo_errno(TnSocketBase *base)
{
    if (base == NULL) return 0;
    if (base->errno_ptr != NULL) {
        if (base->errno_width == 1) return (LONG)*((UBYTE *)base->errno_ptr);
        if (base->errno_width == 2) return (LONG)*((UWORD *)base->errno_ptr);
        return *base->errno_ptr;
    }
    return base->task_errno;
}

/* -168: SetErrnoPtr(errno_ptr, size) */
VOID tn_lvo_seterrnoptr(APTR errno_ptr, LONG size, TnSocketBase *base)
{
    if (base != NULL) {
        if (errno_ptr != NULL) {
            base->errno_ptr = (LONG *)errno_ptr;
            if (size == 1 || size == 2 || size == 4) {
                base->errno_width = (UBYTE)size;
            } else {
                base->errno_width = 4;
            }
        } else {
            base->errno_ptr = &base->task_errno;
            base->errno_width = 4;
        }
    }
}

/* -174: Inet_NtoA(ip) (TNET-008) */
STRPTR tn_lvo_inet_ntoa(in_addr_t ip, TnSocketBase *base)
{
    ULONG val = (ULONG)ip;
    ULONG octets[4];

    if (base == NULL) return NULL;

    octets[0] = (val >> 24) & 0xFF;
    octets[1] = (val >> 16) & 0xFF;
    octets[2] = (val >> 8)  & 0xFF;
    octets[3] = val & 0xFF;

    base->inet_ntoa_buf[0] = '\0';
    RawDoFmt((CONST_STRPTR)"%lu.%lu.%lu.%lu",
             (APTR)octets,
             (VOID (*)())"\x16\xc0\x4e\x75",
             base->inet_ntoa_buf);
    return (STRPTR)base->inet_ntoa_buf;
}

/* -180: inet_addr(cp) (TNET-019 & TNET-051; pure parser in src/common/inet_parse.c) */
in_addr_t tn_lvo_inet_addr(CONST_STRPTR cp, TnSocketBase *base)
{
    (void)base;
    return (in_addr_t)tn_inet_addr_parse((const char *)cp);
}

/* -186: Inet_LnaOf(in) (COMPAT-3) */
in_addr_t tn_lvo_inet_lnaof(in_addr_t in, TnSocketBase *base)
{
    ULONG val = (ULONG)in;
    (void)base;
    if ((val & 0x80000000UL) == 0) {
        return (in_addr_t)(val & 0x00FFFFFFUL);
    } else if ((val & 0xC0000000UL) == 0x80000000UL) {
        return (in_addr_t)(val & 0x0000FFFFUL);
    } else {
        return (in_addr_t)(val & 0x000000FFUL);
    }
}

/* -192: Inet_NetOf(in) (COMPAT-3) */
in_addr_t tn_lvo_inet_netof(in_addr_t in, TnSocketBase *base)
{
    ULONG val = (ULONG)in;
    (void)base;
    if ((val & 0x80000000UL) == 0) {
        return (in_addr_t)((val >> 24) & 0xFFUL);
    } else if ((val & 0xC0000000UL) == 0x80000000UL) {
        return (in_addr_t)((val >> 16) & 0xFFFFUL);
    } else {
        return (in_addr_t)((val >> 8) & 0xFFFFFFUL);
    }
}

/* -198: Inet_MakeAddr(net, host) (COMPAT-3) */
in_addr_t tn_lvo_inet_makeaddr(in_addr_t net, in_addr_t host, TnSocketBase *base)
{
    ULONG n = (ULONG)net;
    ULONG h = (ULONG)host;
    (void)base;
    if (n < 128) {
        return (in_addr_t)((n << 24) | (h & 0x00FFFFFFUL));
    } else if (n < 65536UL) {
        return (in_addr_t)((n << 16) | (h & 0x0000FFFFUL));
    } else {
        return (in_addr_t)((n << 8) | (h & 0x000000FFUL));
    }
}

/* -204: inet_network(cp) (COMPAT-3) */
in_addr_t tn_lvo_inet_network(CONST_STRPTR cp, TnSocketBase *base)
{
    in_addr_t addr = tn_lvo_inet_addr(cp, base);
    if (addr == (in_addr_t)INADDR_NONE) return (in_addr_t)INADDR_NONE;
    return (in_addr_t)ntohl((ULONG)addr);
}

/* -210: gethostbyname(name) (M4) */
struct hostent *tn_lvo_gethostbyname(CONST_STRPTR name, TnSocketBase *base)
{
    LONG res;
    if (base == NULL || name == NULL) return NULL;
    base->ipc_msg.ptrs[0] = (APTR)name;
    res = tn_ipc_call(base, TN_IPC_CMD_GETHOSTBYNAME);
    if (res == 0) {
        tn_set_herrno_val(base, HOST_NOT_FOUND);
        return NULL;
    }
    return (struct hostent *)(intptr_t)res;
}

/* Helper string comparison */
static int tn_strcasecmp(const char *s1, const char *s2)
{
    if (!s1 || !s2) return -1;
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (int)(c1 - c2);
        s1++; s2++;
    }
    return (int)(*s1 - *s2);
}

/* Static Services Table (COMPAT-2) */
struct ServiceDef {
    const char *name;
    const char *proto;
    UWORD port; /* Host byte order */
};

static const struct ServiceDef g_services[] = {
    {"echo", "tcp", 7},
    {"echo", "udp", 7},
    {"ftp-data", "tcp", 20},
    {"ftp", "tcp", 21},
    {"ssh", "tcp", 22},
    {"telnet", "tcp", 23},
    {"smtp", "tcp", 25},
    {"time", "tcp", 37},
    {"time", "udp", 37},
    {"domain", "udp", 53},
    {"domain", "tcp", 53},
    {"gopher", "tcp", 70},
    {"finger", "tcp", 79},
    {"http", "tcp", 80},
    {"pop3", "tcp", 110},
    {"ntp", "udp", 123},
    {"imap", "tcp", 143},
    {"https", "tcp", 443},
    {NULL, NULL, 0}
};

/* -234: getservbyname(name, proto) (COMPAT-2) */
struct servent *tn_lvo_getservbyname(CONST_STRPTR name, CONST_STRPTR proto, TnSocketBase *base)
{
    const struct ServiceDef *s;
    if (base == NULL || name == NULL) return NULL;

    for (s = g_services; s->name != NULL; s++) {
        if (tn_strcasecmp((const char *)name, s->name) == 0) {
            if (proto == NULL || tn_strcasecmp((const char *)proto, s->proto) == 0) {
                int i = 0;
                while (s->name[i] && i < 31) { base->servent_name[i] = s->name[i]; i++; }
                base->servent_name[i] = '\0';
                i = 0;
                while (s->proto[i] && i < 15) { base->servent_proto[i] = s->proto[i]; i++; }
                base->servent_proto[i] = '\0';

                base->servent_aliases[0] = NULL;
                base->servent_data.s_name = (STRPTR)base->servent_name;
                base->servent_data.s_aliases = base->servent_aliases;
                base->servent_data.s_port = (LONG)htons(s->port);
                base->servent_data.s_proto = (STRPTR)base->servent_proto;
                return &base->servent_data;
            }
        }
    }
    return NULL;
}

/* -240: getservbyport(port, proto) (COMPAT-2) */
struct servent *tn_lvo_getservbyport(LONG port, CONST_STRPTR proto, TnSocketBase *base)
{
    const struct ServiceDef *s;
    UWORD host_port;
    if (base == NULL) return NULL;
    host_port = ntohs((UWORD)port);

    for (s = g_services; s->name != NULL; s++) {
        if (s->port == host_port) {
            if (proto == NULL || tn_strcasecmp((const char *)proto, s->proto) == 0) {
                int i = 0;
                while (s->name[i] && i < 31) { base->servent_name[i] = s->name[i]; i++; }
                base->servent_name[i] = '\0';
                i = 0;
                while (s->proto[i] && i < 15) { base->servent_proto[i] = s->proto[i]; i++; }
                base->servent_proto[i] = '\0';

                base->servent_aliases[0] = NULL;
                base->servent_data.s_name = (STRPTR)base->servent_name;
                base->servent_data.s_aliases = base->servent_aliases;
                base->servent_data.s_port = (LONG)port; /* already network byte order */
                base->servent_data.s_proto = (STRPTR)base->servent_proto;
                return &base->servent_data;
            }
        }
    }
    return NULL;
}

/* Static Protocols Table (COMPAT-2) */
struct ProtoDef {
    const char *name;
    LONG proto;
};

static const struct ProtoDef g_protocols[] = {
    {"ip", 0},
    {"icmp", 1},
    {"tcp", 6},
    {"udp", 17},
    {NULL, 0}
};

/* -246: getprotobyname(name) (COMPAT-2) */
struct protoent *tn_lvo_getprotobyname(CONST_STRPTR name, TnSocketBase *base)
{
    const struct ProtoDef *p;
    if (base == NULL || name == NULL) return NULL;

    for (p = g_protocols; p->name != NULL; p++) {
        if (tn_strcasecmp((const char *)name, p->name) == 0) {
            int i = 0;
            while (p->name[i] && i < 31) { base->protoent_name[i] = p->name[i]; i++; }
            base->protoent_name[i] = '\0';

            base->protoent_aliases[0] = NULL;
            base->protoent_data.p_name = (STRPTR)base->protoent_name;
            base->protoent_data.p_aliases = base->protoent_aliases;
            base->protoent_data.p_proto = p->proto;
            return &base->protoent_data;
        }
    }
    return NULL;
}

/* -252: getprotobynumber(proto) (COMPAT-2) */
struct protoent *tn_lvo_getprotobynumber(LONG proto, TnSocketBase *base)
{
    const struct ProtoDef *p;
    if (base == NULL) return NULL;

    for (p = g_protocols; p->name != NULL; p++) {
        if (p->proto == proto) {
            int i = 0;
            while (p->name[i] && i < 31) { base->protoent_name[i] = p->name[i]; i++; }
            base->protoent_name[i] = '\0';

            base->protoent_aliases[0] = NULL;
            base->protoent_data.p_name = (STRPTR)base->protoent_name;
            base->protoent_data.p_aliases = base->protoent_aliases;
            base->protoent_data.p_proto = p->proto;
            return &base->protoent_data;
        }
    }
    return NULL;
}

/* -264: Dup2Socket(old_sock, new_sock) (COMPAT-3) */
LONG tn_lvo_dup2socket(LONG old_sock, LONG new_sock, TnSocketBase *base)
{
    if (base == NULL) return -1;
    if (old_sock < 0 || old_sock >= TN_MAX_FDS_PER_TASK ||
        new_sock < 0 || new_sock >= TN_MAX_FDS_PER_TASK) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (base->fd_map[old_sock] < 0) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (old_sock == new_sock) return new_sock;

    base->ipc_msg.args[0] = old_sock;
    base->ipc_msg.args[1] = new_sock;
    return tn_ipc_call(base, TN_IPC_CMD_DUP2);
}

/* -282: gethostname(name, namelen) (COMPAT-3) */
LONG tn_lvo_gethostname(STRPTR name, LONG namelen, TnSocketBase *base)
{
    const char *h = "amiga";
    int i = 0;
    if (name == NULL || namelen <= 0) return -1;
    if (base != NULL && base->hostname[0] != '\0') h = base->hostname;

    while (h[i] && i < namelen - 1) {
        name[i] = h[i];
        i++;
    }
    name[i] = '\0';
    return 0;
}

/* -288: gethostid() (COMPAT-3) */
in_addr_t tn_lvo_gethostid(TnSocketBase *base)
{
    (void)base;
    return (in_addr_t)0x0A00020FUL; /* 10.0.2.15 */
}

/* -294: SocketBaseTagList(tags) (COMPAT-1 / TNET-036; per-tag logic in
 * src/common/sbtc_dispatch.c — host-tested by test_sbtc.c) */
LONG tn_lvo_socketbasetaglist(struct TagItem *tags, TnSocketBase *base)
{
    struct TagItem *tstate = tags;
    struct TagItem *tag;
    LONG count = 0;

    /* The portable dispatcher's code values must match the SDK header */
    _Static_assert(TN_SBTC_BREAKMASK == SBTC_BREAKMASK, "SBTC code drift");
    _Static_assert(TN_SBTC_SIGIOMASK == SBTC_SIGIOMASK, "SBTC code drift");
    _Static_assert(TN_SBTC_ERRNOLONGPTR == SBTC_ERRNOLONGPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_HERRNOLONGPTR == SBTC_HERRNOLONGPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_DTABLESIZE == SBTC_DTABLESIZE, "SBTC code drift");
    _Static_assert(TN_SBTC_RELEASESTRPTR == SBTC_RELEASESTRPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_HAVE_DNS_API == SBTC_HAVE_DNS_API, "SBTC code drift");
    _Static_assert(TN_SBTC_HAVE_STATUS_API == SBTC_HAVE_STATUS_API, "SBTC code drift");

    if (base == NULL || tags == NULL) return 0;

    while ((tag = NextTagItem(&tstate)) != NULL) {
        TnSbtcState st;
        TnSbtcResult r;

        st.sig_int     = base->sig_int;
        st.sig_io      = base->sig_io;
        st.sig_urg     = base->sig_urg;
        st.errno_val   = base->task_errno;
        st.herrno_val  = base->task_herrno;
        st.dtablesize  = TN_MAX_FDS_PER_TASK;
        st.have_bits   = TN_SBTC_HAVE_DNS_API_BIT | TN_SBTC_HAVE_LOCAL_DB_API_BIT |
                         TN_SBTC_HAVE_ADDR_CONV_API_BIT;
        st.release_str = (uint32_t)(uintptr_t)"tolunnet 1.1.0 (bsdsocket 4.1)";

        if (!tn_sbtc_dispatch_tag((uint32_t)tag->ti_Tag, (uint32_t)tag->ti_Data, &st, &r)) {
            count++; /* count unknown tags only (TNET-036) */
            continue;
        }

        /* The dispatcher is pure; apply its op (pointer accesses happen here,
         * on the Amiga, where ti_Data really is a 32-bit pointer). */
        switch (r.op) {
        case TN_SBTC_OP_GET:
            if (r.is_ref && tag->ti_Data != 0) {
                *(ULONG *)(uintptr_t)tag->ti_Data = r.value;
            } else {
                tag->ti_Data = r.value;
            }
            break;
        case TN_SBTC_OP_SET_SIGINT:
            base->sig_int = (r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value;
            break;
        case TN_SBTC_OP_SET_SIGIO:
            base->sig_io = (r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value;
            break;
        case TN_SBTC_OP_SET_SIGURG:
            base->sig_urg = (r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value;
            break;
        case TN_SBTC_OP_SET_ERRNO:
            tn_set_errno_val(base, (LONG)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value));
            break;
        case TN_SBTC_OP_SET_HERRNO:
            tn_set_herrno_val(base, (LONG)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value));
            break;
        case TN_SBTC_OP_SET_ERRNO_PTR:
            base->errno_ptr   = (LONG *)(uintptr_t)r.value;
            base->errno_width = (UBYTE)r.errno_ptr_width;
            break;
        case TN_SBTC_OP_SET_HERRNO_PTR:
            base->herrno_ptr = (LONG *)(uintptr_t)r.value;
            break;
        default:
            break; /* handled no-ops (e.g. SET on GET-only tags) */
        }
    }
    return count;
}
