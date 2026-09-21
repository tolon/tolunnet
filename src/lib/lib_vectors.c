/*
 * tolunnet — bsdsocket.library LVO vector table and stub implementations.
 *
 * Implements standard AmigaOS 68k library vectors with Exec IPC dispatch
 * to the tolunnet network daemon, fully compliant with Roadshow SDK 1.8
 * and AmiTCP V4 specifications (TOLUNNET-COMPAT.md).
 */

#include "../../include/ipc.h"
#include "../../include/version.h"
#include "../common/log.h"
#include "../common/inet_parse.h"
#include "../common/sbtc_dispatch.h"
#include "../common/errstr.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <utility/tagitem.h>
#include <devices/timer.h>
#include <libraries/bsdsocket.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <string.h>
#include "../common/fdset_util.h"
#include "../common/rawfmt.h"

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
/* TNET-151: 1/50 s ticks from DateStamp - the ground truth the watchdog
 * and WaitSelect verify signal wakes against (a wedged/stale timer request
 * completes instantly and would otherwise be trusted). */
static ULONG tn_ticks_now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400UL * 50UL +
           (ULONG)ds.ds_Minute * 60UL * 50UL + (ULONG)ds.ds_Tick;
}

static LONG tn_ipc_call(TnSocketBase *base, TnIpcCmd cmd);
static BOOL tn_ensure_timer(TnSocketBase *base);

static LONG tn_ipc_call(TnSocketBase *base, TnIpcCmd cmd)
{
    TnIpcMsg *msg;
    TnIpcMsg *heap_msg = NULL;

    if (base == NULL) return -1;

    if (base->tolunnet_port == NULL) {
        base->tolunnet_port = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
        if (base->tolunnet_port == NULL) {
            tn_set_errno_val(base, ENETDOWN);
            return -1;
        }
    }

    if (base->ipc_timeout_ms > 0) {
        /* TNET-111: timeout mode uses a heap message per call. A timed-out
         * request may STILL be replied by the daemon at any time; reusing
         * the embedded ipc_msg would let that late reply alias (and corrupt)
         * the next request. TNET-150 hard rule: an abandoned heap message
         * is NEVER freed mid-session — a deferred daemon handler (DNS)
         * may still hold it. Late replies are drained off the port; the
         * chain (ipc_orphan) is freed in tn_lib_close after the CLOSE IPC
         * reply guarantees the daemon is done with it. */
        if (base->ipc_orphan != NULL) {
            struct Message *m;
            while ((m = GetMsg(base->reply_port)) != NULL) {
                TnIpcMsg *o = (TnIpcMsg *)m;
                TnIpcMsg *c;
                int is_orphan = 0;
                for (c = (TnIpcMsg *)base->ipc_orphan; c != NULL; c = (TnIpcMsg *)c->orphan_next) {
                    if (c == o) { is_orphan = 1; break; }
                }
                if (is_orphan) break; /* drained; stays allocated in the chain */
            }
        }
        heap_msg = (TnIpcMsg *)AllocVec(sizeof(TnIpcMsg), MEMF_CLEAR | MEMF_PUBLIC);
        if (heap_msg == NULL) {
            tn_set_errno_val(base, ENOBUFS);
            return -1;
        }
        /* the wrapper pre-filled args/ptrs in the embedded message — carry
         * them over; header fields and result are set fresh below */
        *heap_msg = base->ipc_msg;
        heap_msg->result = 0;
        heap_msg->err_no = 0;
        msg = heap_msg;
    } else {
        msg = &base->ipc_msg;
    }

    msg->msg.mn_Node.ln_Type = NT_MESSAGE;
    msg->msg.mn_Node.ln_Pri  = 0;
    msg->msg.mn_ReplyPort    = base->reply_port;
    msg->msg.mn_Length       = sizeof(TnIpcMsg);
    msg->cmd                 = cmd;
    msg->client_task         = base->owner_task;
    msg->socket_base         = (APTR)base;

    PutMsg(base->tolunnet_port, (struct Message *)msg);

    if (heap_msg != NULL && tn_ensure_timer(base)) {
        /* Reply watchdog (TNET-111): a wedged daemon fails the call fast
         * instead of hanging the client process. The timer channel is the
         * same one WaitSelect uses; the two wait paths never overlap in a
         * single-threaded client. */
        struct timerequest *tm = (struct timerequest *)base->timer_io;
        ULONG tm_sig    = 1UL << base->timer_port->mp_SigBit;
        ULONG reply_sig = 1UL << base->reply_port->mp_SigBit;
        struct Message *m;
        ULONG fired;

        while ((m = GetMsg(base->timer_port)) != NULL) {}
        SetSignal(0, tm_sig);

        /* TNET-151: time-verified watchdog. A wedged or stale timer
         * request completes instantly; trusting tm_sig alone turns every
         * affected call into a false ETIMEDOUT (20260921-111354: queries
         * failed while data sat queued). tm_sig is only honored when the
         * budget actually elapsed; otherwise the wait resumes with the
         * remaining budget. */
        {
            ULONG t0 = tn_ticks_now();
            ULONG budget_ms = base->ipc_timeout_ms;

            for (;;) {
                ULONG elapsed_ms;

                tm->tr_node.io_Command = TR_ADDREQUEST;
                tm->tr_time.tv_secs    = budget_ms / 1000;
                tm->tr_time.tv_micro   = (budget_ms % 1000) * 1000;
                SendIO((struct IORequest *)tm);

                fired = Wait(reply_sig | tm_sig);

                if (!CheckIO((struct IORequest *)tm)) {
                    AbortIO((struct IORequest *)tm);
                }
                WaitIO((struct IORequest *)tm);
                while ((m = GetMsg(base->timer_port)) != NULL) {}
                SetSignal(0, tm_sig);

                if (fired & reply_sig) break;

                elapsed_ms = (tn_ticks_now() - t0) * 20UL;
                if (elapsed_ms + 40UL >= budget_ms) break; /* genuine timeout */
                /* timer lied: resume with the full remaining budget */
            }
        }

        if (!(fired & reply_sig)) {
            base->ipc_timeouts++;
            /* TNET-150: chain onto any earlier orphan; freed at close */
            heap_msg->orphan_next = base->ipc_orphan;
            base->ipc_orphan = heap_msg;
            tn_set_errno_val(base, ETIMEDOUT);
            return -1;
        }
    } else {
        WaitPort(base->reply_port);
    }
    {
        /* The reply we GetMsg must be the message THIS call sent. A stale
         * reply on the port (any late ReplyMsg from the daemon for an
         * earlier watchdog-abandoned request, or a spurious reply-port
         * signal) used to make the client read and free its own message
         * while the daemon still owned it — the freed block was then
         * reused for the next call and every following result came back
         * as garbage heap pointers (bench 20260920-000915: recv returned
         * 0x2D3828-class values from test 40 on, both profiles). Verify
         * identity; drain anything else and wait again, bounded. */
        struct Message *got = GetMsg(base->reply_port);
        int spins = 0;
        while (got != (struct Message *)msg && spins < 4) {
            /* a foreign reply belongs to an abandoned request — it stays
             * allocated in the orphan chain (TNET-150: never freed
             * mid-session); it is off the port now, which is what matters */
            WaitPort(base->reply_port);
            got = GetMsg(base->reply_port);
            spins++;
        }
        if (got != (struct Message *)msg) {
            /* ours never arrived: treat as a timeout and abandon ours */
            if (heap_msg != NULL) {
                base->ipc_timeouts++;
                heap_msg->orphan_next = base->ipc_orphan;
                base->ipc_orphan = heap_msg;
                tn_set_errno_val(base, ETIMEDOUT);
                return -1;
            }
        }

        if (msg->result < 0 && msg->err_no != 0) {
            tn_set_errno_val(base, msg->err_no);
        }

        {
            LONG r = msg->result;
            if (heap_msg != NULL) {
                FreeVec(heap_msg);
            }
            return r;
        }
    }
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

    /* AllocVec is 8-byte aligned and the generated vector table keeps every
     * LVO slot word-aligned, so raw_mem + neg_size is a valid struct base.
     * TNET-139: the odd-neg_size case is refused instead of trusted. */
    if (neg_size & 1) {
        FreeVec(raw_mem);
        Forbid();
        lib->lib_OpenCnt--;
        Permit();
        return NULL;
    }
    base = (TnSocketBase *)(void *)(raw_mem + neg_size);
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
    base->sig_event    = 0;

    /* Allocate private signal bit for WaitSelect event-driven wakeups (§D) */
    {
        BYTE sel_sig = AllocSignal(-1);
        if (sel_sig != -1) {
            base->sig_select_bit = sel_sig;
            base->sig_select     = (1UL << (UWORD)sel_sig);
        } else {
            base->sig_select_bit = -1;
            base->sig_select     = 0;
        }
    }

    base->fd_callback  = NULL;
    base->log_stat     = 0;
    base->log_tag_ptr  = NULL;
    base->log_facility = 0;
    base->log_mask     = 0xFF;
    base->udp_checksum = 1;
    base->ip_default_ttl = 64;
    base->timer_port   = NULL;
    base->timer_io     = NULL;
    base->inet_ntoa_buf[0] = '\0';
    base->hostname[0]  = '\0';
    base->domain_name[0] = '\0';
    base->netent_idx = 0;
    base->servent_idx = 0;
    base->protoent_idx = 0;

    /* TN-bugtrack-2 item 4: per-base dynamic descriptor table */
    base->dtablesize = TN_DEFAULT_DTABLESIZE;
    base->fd_map  = (LONG *)AllocVec(sizeof(LONG) * base->dtablesize, MEMF_PUBLIC | MEMF_CLEAR);
    base->events  = (ULONG *)AllocVec(sizeof(ULONG) * base->dtablesize, MEMF_PUBLIC | MEMF_CLEAR);
    base->event_masks = (ULONG *)AllocVec(sizeof(ULONG) * base->dtablesize, MEMF_PUBLIC | MEMF_CLEAR);
    if (base->fd_map == NULL || base->events == NULL || base->event_masks == NULL) {
        if (base->fd_map) { FreeVec(base->fd_map); base->fd_map = NULL; }
        if (base->events) { FreeVec(base->events); base->events = NULL; }
        if (base->event_masks) { FreeVec(base->event_masks); base->event_masks = NULL; }
        FreeVec((UBYTE *)base - base->lib_node.lib_NegSize);
        Forbid();
        lib->lib_OpenCnt--;
        Permit();
        return NULL;
    }
    for (i = 0; i < base->dtablesize; i++) {
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

        /* TNET-150: the CLOSE reply guarantees the daemon no longer holds
         * any deferred message of this base (pending DNS was cancelled) —
         * NOW the watchdog-abandoned chain is safe to free. Drain any late
         * replies still parked on the port first so ReplyMsg nodes are not
         * left queued. */
        {
            struct Message *m;
            while ((m = GetMsg(base->reply_port)) != NULL) {}
            {
                TnIpcMsg *o = (TnIpcMsg *)base->ipc_orphan;
                while (o != NULL) {
                    TnIpcMsg *next = (TnIpcMsg *)o->orphan_next;
                    FreeVec(o);
                    o = next;
                }
                base->ipc_orphan = NULL;
            }
        }

        if (base->sig_select_bit != -1) {
            FreeSignal(base->sig_select_bit);
            base->sig_select_bit = -1;
            base->sig_select     = 0;
        }

        if (base->timer_io != NULL) {
            struct timerequest *tm = (struct timerequest *)base->timer_io;
            if (tm->tr_node.io_Device != NULL) {
                CloseDevice((struct IORequest *)tm);
            }
            FreeVec(tm);
            base->timer_io = NULL;
        }
        if (base->timer_port != NULL) {
            DeleteMsgPort(base->timer_port);
            base->timer_port = NULL;
        }

        /* TN-bugtrack-2 item 4: free the dynamic descriptor table */
        if (base->fd_map != NULL) {
            FreeVec(base->fd_map);
            base->fd_map = NULL;
        }
        if (base->events != NULL) {
            FreeVec(base->events);
            base->events = NULL;
        }
        if (base->event_masks != NULL) {
            FreeVec(base->event_masks);
            base->event_masks = NULL;
        }

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
    LONG res;
    if (base == NULL) return -1;
    base->ipc_msg.args[0] = domain;
    base->ipc_msg.args[1] = type;
    base->ipc_msg.args[2] = protocol;
    base->ipc_msg.args[3] = -1;
    if (base->fd_callback != NULL) {
        int i;
        typedef int (*fdcb_t)(int, int);
        fdcb_t cb = (fdcb_t)base->fd_callback;
        for (i = 0; i < base->dtablesize; i++) {
            if (base->fd_map[i] == -1 && cb(i, FDCB_CHECK) == 0) {
                base->ipc_msg.args[3] = i;
                break;
            }
        }
    }
    res = tn_ipc_call(base, TN_IPC_CMD_SOCKET);
    if (res >= 0 && base->fd_callback != NULL) {
        typedef int (*fdcb_t)(int, int);
        ((fdcb_t)base->fd_callback)((int)res, FDCB_ALLOC);
    }
    return res;
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
    LONG res;
    if (base == NULL || sock < 0) return -1;
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)addr;
    base->ipc_msg.ptrs[1] = (APTR)addrlen;
    base->ipc_msg.args[3] = -1;
    if (base->fd_callback != NULL) {
        int i;
        typedef int (*fdcb_t)(int, int);
        fdcb_t cb = (fdcb_t)base->fd_callback;
        for (i = 0; i < base->dtablesize; i++) {
            if (base->fd_map[i] == -1 && cb(i, FDCB_CHECK) == 0) {
                base->ipc_msg.args[3] = i;
                break;
            }
        }
    }
    res = tn_ipc_call(base, TN_IPC_CMD_ACCEPT);
    if (res >= 0 && base->fd_callback != NULL) {
        typedef int (*fdcb_t)(int, int);
        ((fdcb_t)base->fd_callback)((int)res, FDCB_ALLOC);
    }
    return res;
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
    /* TNET-122..127: SO_EVENTMASK filter lives client-side so the event
     * recording path can AND against it without an IPC round-trip. */
    if (level == SOL_SOCKET && optname == SO_EVENTMASK &&
        optval != NULL && optlen >= (LONG)sizeof(ULONG)) {
        base->event_masks[sock] = *(const ULONG *)optval;
        return 0;
    }
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
    /* TNET-132..134: a bad fd must raise EBADF through the client's errno
     * pointer — the plain -1 return never reached tn_set_errno_val. */
    if (base == NULL) return -1;
    if (sock < 0) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (base->fd_callback != NULL && sock < base->dtablesize && base->fd_map[sock] >= 0) {
        typedef int (*fdcb_t)(int, int);
        ((fdcb_t)base->fd_callback)((int)sock, FDCB_FREE);
    }
    base->ipc_msg.args[0] = sock;
    return tn_ipc_call(base, TN_IPC_CMD_CLOSESOCKET);
}

static BOOL tn_ensure_timer(TnSocketBase *base)
{
    if (base == NULL) return FALSE;
    if (base->timer_port == NULL) {
        base->timer_port = CreateMsgPort();
        if (base->timer_port == NULL) return FALSE;
    }
    if (base->timer_io == NULL) {
        struct timerequest *tm = (struct timerequest *)AllocVec(sizeof(struct timerequest), MEMF_CLEAR | MEMF_PUBLIC);
        if (tm == NULL) return FALSE;
        tm->tr_node.io_Message.mn_ReplyPort = base->timer_port;
        tm->tr_node.io_Message.mn_Length = sizeof(struct timerequest);
        tm->tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
        if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tm, 0UL) != 0) {
            FreeVec(tm);
            return FALSE;
        }
        base->timer_io = (APTR)tm;
    }
    return TRUE;
}

/* -126: WaitSelect(nfds, read_fds, write_fds, except_fds, timeout, signals) */
LONG tn_lvo_waitselect(LONG nfds, fd_set *read_fds, fd_set *write_fds,
                       fd_set *except_fds, struct timeval *timeout,
                       ULONG *signals, TnSocketBase *base)
{
    LONG res;
    ULONG orig_r = 0, orig_w = 0, orig_e = 0;
    ULONG sig_mask = 0;
    ULONG received_sigs = 0;
    BOOL has_timeout = (timeout != NULL);
    BOOL zero_timeout = (has_timeout && timeout->tv_secs == 0 && timeout->tv_micro == 0);
    int chk;

    if (base == NULL) return -1;

    chk = tn_fdset_check_nfds((int)nfds);
    if (chk != 0) {
        tn_set_errno_val(base, chk);
        return -1;
    }

    if (read_fds)   orig_r = read_fds->fds_bits[0];
    if (write_fds)  orig_w = write_fds->fds_bits[0];
    if (except_fds) orig_e = except_fds->fds_bits[0];

    if (signals != NULL) {
        sig_mask = *signals;
    }
    if (base->sig_int != 0) {
        sig_mask |= base->sig_int;
    }

    /* Check AmigaOS user signals already pending */
    if (sig_mask != 0) {
        received_sigs = SetSignal(0, 0) & sig_mask;
        if (received_sigs != 0) {
            SetSignal(0, received_sigs);
            if (signals != NULL) *signals = received_sigs;
            /* AmiTCP WaitSelect: signal interrupt returns 0 with fd_sets
             * zeroed, but errno is set to EINTR (Roadshow autodocs: 'returns
             * 0 and sets errno to EINTR') — bsdsocktest #66 checks rc==0,
             * our tc_waitselect_eintr checks errno==EINTR. */
            if (read_fds)   read_fds->fds_bits[0] = 0;
            if (write_fds)  write_fds->fds_bits[0] = 0;
            if (except_fds) except_fds->fds_bits[0] = 0;
            tn_set_errno_val(base, EINTR);
            return 0;
        }
    }

    /* Fast path: arm selector with atomic readiness poll (§D).
     * TNET-151: clear any stale wake bits FIRST — a wake signal that
     * arrived while we were not in Wait() (e.g. while blocked inside a
     * synchronous SystemTags child) stays pending forever and makes every
     * later Wait() return instantly, reporting a bogus "timeout" while
     * the data is in fact there (bench 20260921-010932: wait failed after
     * 0 ticks). Clearing before the authoritative ARM poll cannot lose a
     * wake: ARM reads the real readiness state at this instant. */
    if (base->sig_select != 0) SetSignal(0, base->sig_select);
    if (base->sig_io != 0)     SetSignal(0, base->sig_io);

    if (nfds > 0) {
        base->ipc_msg.args[0] = nfds;
        base->ipc_msg.args[1] = orig_r;
        base->ipc_msg.args[2] = orig_w;
        base->ipc_msg.args[3] = orig_e;
        base->ipc_msg.ptrs[0] = (APTR)read_fds;
        base->ipc_msg.ptrs[1] = (APTR)write_fds;
        base->ipc_msg.ptrs[2] = (APTR)except_fds;

        res = tn_ipc_call(base, TN_IPC_CMD_SELECT_ARM);
        if (res < 0) {
            /* TNET-151: the ARM request may still complete daemon-side
             * (watchdog abort = client gave up, not daemon refused) and
             * leave an ARMED selector for this live base with no client
             * wait pending - every later data arrival then signals us
             * spurious wakes that turn every Wait() into an instant
             * bogus timeout (0-tick evidence 20260921-012936). Fire a
             * best-effort DISARM so the slot cannot stay armed. */
            tn_ipc_call(base, TN_IPC_CMD_SELECT_DISARM);
            return -1;
        }
        if (res > 0) {
            if (signals != NULL) *signals = 0;
            return res;
        }

        if (zero_timeout) {
            tn_ipc_call(base, TN_IPC_CMD_SELECT_DISARM);
            if (read_fds)   read_fds->fds_bits[0] = 0;
            if (write_fds)  write_fds->fds_bits[0] = 0;
            if (except_fds) except_fds->fds_bits[0] = 0;
            if (signals != NULL) *signals = 0;
            return 0;
        }
    } else {
        /* nfds == 0: pure select-based sleep */
        if (!has_timeout || zero_timeout) {
            if (signals != NULL) *signals = 0;
            return 0;
        }
    }

    /* Event-driven wait using timer.device and selector/SIGIO signal (TNET-151:
     * TIME is the ground truth, not signal bits. A stale/instantly-completing
     * TR_ADDREQUEST on the shared per-base timer request used to make Wait()
     * return within 0-1 ticks with only tm_sig fired - waitselect then reported
     * a bogus timeout while the data was in fact queued (probe evidence
     * 20260921-025310: timer bit 29 fired instantly, direct recv got=5). Each
     * attempt therefore measures ELAPSED time; a "timeout" is only accepted
     * when the budget actually elapsed, otherwise the wait retries with the
     * remaining budget. */
    {
        ULONG wait_sig = base->sig_select | base->sig_io;
        ULONG tm_sig = 0;
        struct timerequest *tm = NULL;
        ULONG wait_mask2 = sig_mask;
        ULONG budget_ticks = 0;   /* 0 = no timeout (infinite wait) */
        ULONG t0 = 0;
        int attempt;

        if (nfds > 0) {
            wait_mask2 |= wait_sig;
        }
        if (nfds > 0 && wait_sig == 0) {
            tn_ipc_call(base, TN_IPC_CMD_SELECT_DISARM);
            tn_set_errno_val(base, ENOBUFS);
            return -1;
        }
        if (!tn_ensure_timer(base)) {
            if (nfds > 0) tn_ipc_call(base, TN_IPC_CMD_SELECT_DISARM);
            tn_set_errno_val(base, ENOBUFS);
            return -1;
        }
        tm = (struct timerequest *)base->timer_io;
        tm_sig = 1UL << base->timer_port->mp_SigBit;

        if (has_timeout) {
            ULONG micros = (ULONG)timeout->tv_secs * 1000000UL + (ULONG)timeout->tv_micro;
            budget_ticks = (micros + 19999UL) / 20000UL; /* 1 tick = 20 ms */
            if (budget_ticks == 0) budget_ticks = 1;
        }

        for (attempt = 0; ; attempt++) {
            struct Message *m;
            ULONG fired;
            ULONG elapsed;
            struct DateStamp ds;

            while ((m = GetMsg(base->timer_port)) != NULL) {}
            SetSignal(0, tm_sig);

            DateStamp(&ds);
            if (attempt == 0) {
                t0 = (ULONG)ds.ds_Days * 86400UL * 50UL +
                     (ULONG)ds.ds_Minute * 60UL * 50UL + (ULONG)ds.ds_Tick;
            }

            if (has_timeout) {
                ULONG gone = (ULONG)ds.ds_Days * 86400UL * 50UL +
                             (ULONG)ds.ds_Minute * 60UL * 50UL + (ULONG)ds.ds_Tick;
                ULONG remain_ticks = (gone - t0 >= budget_ticks) ? 0 : budget_ticks - (gone - t0);
                if (remain_ticks == 0) {
                    /* budget already spent */
                    if (read_fds)   read_fds->fds_bits[0] = 0;
                    if (write_fds)  write_fds->fds_bits[0] = 0;
                    if (except_fds) except_fds->fds_bits[0] = 0;
                    if (signals != NULL) *signals = 0;
                    if (nfds > 0) tn_ipc_call(base, TN_IPC_CMD_SELECT_DISARM);
                    return 0;
                }
                tm->tr_node.io_Command = TR_ADDREQUEST;
                tm->tr_time.tv_secs  = (remain_ticks * 20000UL) / 1000000UL;
                tm->tr_time.tv_micro = (remain_ticks * 20000UL) % 1000000UL;
                SendIO((struct IORequest *)tm);
            }

            fired = Wait(wait_mask2 | (has_timeout ? tm_sig : 0));

            if (has_timeout) {
                if (!CheckIO((struct IORequest *)tm)) {
                    AbortIO((struct IORequest *)tm);
                }
                WaitIO((struct IORequest *)tm);
                while ((m = GetMsg(base->timer_port)) != NULL) {}
                SetSignal(0, tm_sig);
            }

            /* Disarm selector immediately upon waking */
            if (nfds > 0) {
                tn_ipc_call(base, TN_IPC_CMD_SELECT_DISARM);
            }

            /* If interrupted by user signal */
            if (sig_mask != 0 && (fired & sig_mask)) {
                ULONG received_sigs = fired & sig_mask;
                SetSignal(0, received_sigs);
                if (signals != NULL) *signals = received_sigs;
                if (read_fds)   read_fds->fds_bits[0] = 0;
                if (write_fds)  write_fds->fds_bits[0] = 0;
                if (except_fds) except_fds->fds_bits[0] = 0;
                tn_set_errno_val(base, EINTR);
                return 0;
            }

            DateStamp(&ds);
            {
                ULONG now = (ULONG)ds.ds_Days * 86400UL * 50UL +
                            (ULONG)ds.ds_Minute * 60UL * 50UL + (ULONG)ds.ds_Tick;
                elapsed = now - t0;
            }

            /* Socket activity or genuine timeout: query final ready sets.
             * A res==0 query with budget REMAINING means the wake was a
             * timer lie (or spurious) - retry instead of reporting timeout. */
            if (read_fds)   read_fds->fds_bits[0] = orig_r;
            if (write_fds)  write_fds->fds_bits[0] = orig_w;
            if (except_fds) except_fds->fds_bits[0] = orig_e;

            base->ipc_msg.args[0] = nfds;
            base->ipc_msg.ptrs[0] = (APTR)read_fds;
            base->ipc_msg.ptrs[1] = (APTR)write_fds;
            base->ipc_msg.ptrs[2] = (APTR)except_fds;
            res = tn_ipc_call(base, TN_IPC_CMD_WAITSELECT);
            if (signals != NULL) *signals = 0;
            if (res != 0) return res;

            if (!has_timeout) {
                /* infinite wait returned empty - spurious; retry */
                if (attempt >= 3) return 0;
                continue;
            }
            if (elapsed + 3UL >= budget_ticks) {
                /* genuine timeout (3-tick measurement slack) */
                if (read_fds)   read_fds->fds_bits[0] = 0;
                if (write_fds)  write_fds->fds_bits[0] = 0;
                if (except_fds) except_fds->fds_bits[0] = 0;
                return 0;
            }
            /* timer lied or spurious wake - retry with remaining budget */
        }
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
    if (base == NULL) return TN_DEFAULT_DTABLESIZE;
    return base->dtablesize;
}

/* -144: ObtainSocket(...) */
LONG tn_lvo_obtainsocket(LONG id, LONG domain, LONG type, LONG protocol, TnSocketBase *base)
{
    LONG res;
    if (base == NULL) return -1;
    base->ipc_msg.args[0] = id;
    base->ipc_msg.args[1] = domain;
    base->ipc_msg.args[2] = type;
    base->ipc_msg.args[3] = protocol;
    base->ipc_msg.args[4] = -1; /* preferred client_fd */

    if (base->fd_callback != NULL) {
        int i;
        typedef int (*fdcb_t)(int, int);
        fdcb_t cb = (fdcb_t)base->fd_callback;
        for (i = 0; i < base->dtablesize; i++) {
            if (base->fd_map[i] == -1 && cb(i, FDCB_CHECK) == 0) {
                base->ipc_msg.args[4] = i;
                break;
            }
        }
    }

    res = tn_ipc_call(base, TN_IPC_CMD_OBTAINSOCKET);
    if (res >= 0 && base->fd_callback != NULL) {
        typedef int (*fdcb_t)(int, int);
        ((fdcb_t)base->fd_callback)((int)res, FDCB_ALLOC);
    }
    return res;
}

/* -150: ReleaseSocket(...) */
LONG tn_lvo_releasesocket(LONG sock, LONG id, TnSocketBase *base)
{
    if (base == NULL || sock < 0 || sock >= base->dtablesize || base->fd_map[sock] < 0) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (base->fd_callback != NULL) {
        typedef int (*fdcb_t)(int, int);
        ((fdcb_t)base->fd_callback)((int)sock, FDCB_FREE);
    }
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.args[1] = id;
    base->ipc_msg.args[2] = 0; /* copy = FALSE */
    return tn_ipc_call(base, TN_IPC_CMD_RELEASESOCKET);
}

/* -156: ReleaseCopyOfSocket(...) */
LONG tn_lvo_releasecopyofsocket(LONG sock, LONG id, TnSocketBase *base)
{
    if (base == NULL || sock < 0 || sock >= base->dtablesize || base->fd_map[sock] < 0) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.args[1] = id;
    base->ipc_msg.args[2] = 1; /* copy = TRUE */
    return tn_ipc_call(base, TN_IPC_CMD_RELEASESOCKET);
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
             TN_RAWFMT_PUTCH,
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
    /* res==0: daemon answered NULL (not found); res==-1: transport failure
     * incl. the watchdog ETIMEDOUT — both are "no hostent", never a
     * (hostent *)-1 poison pointer for callers that only check != NULL */
    if (res <= 0) {
        tn_set_herrno_val(base, HOST_NOT_FOUND);
        return NULL;
    }
    return (struct hostent *)(intptr_t)res;
}

/* -216: gethostbyaddr(addr, len, type) */
struct hostent *tn_lvo_gethostbyaddr(CONST_STRPTR addr, LONG len, LONG type, TnSocketBase *base)
{
    (void)addr;
    (void)len;
    (void)type;
    if (base != NULL) {
        tn_set_herrno_val(base, HOST_NOT_FOUND);
        tn_set_errno_val(base, ENOENT);
    }
    return NULL;
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
    LONG res;
    if (base == NULL) return -1;
    if (old_sock < 0 || old_sock >= base->dtablesize) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    /* TNET-130: Dup2Socket(fd, -1) picks the lowest free descriptor */
    if (new_sock == -1) {
        LONG i;
        for (i = 0; i < base->dtablesize; i++) {
            if (base->fd_map[i] < 0 && i != old_sock) {
                new_sock = i;
                break;
            }
        }
        if (new_sock == -1) {
            tn_set_errno_val(base, EMFILE);
            return -1;
        }
    }
    if (new_sock < 0 || new_sock >= base->dtablesize) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (base->fd_map[old_sock] < 0) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (old_sock == new_sock) return new_sock;

    if (base->fd_callback != NULL && base->fd_map[new_sock] >= 0) {
        typedef int (*fdcb_t)(int, int);
        ((fdcb_t)base->fd_callback)((int)new_sock, FDCB_FREE);
    }

    base->ipc_msg.args[0] = old_sock;
    base->ipc_msg.args[1] = new_sock;
    res = tn_ipc_call(base, TN_IPC_CMD_DUP2);
    if (res >= 0 && base->fd_callback != NULL) {
        typedef int (*fdcb_t)(int, int);
        ((fdcb_t)base->fd_callback)((int)new_sock, FDCB_ALLOC);
    }
    return res;
}

/* -270: sendmsg(sock, msg, flags) */
LONG tn_lvo_sendmsg(LONG sock, struct msghdr *msg, LONG flags, TnSocketBase *base)
{
    if (base == NULL) return -1;
    if (sock < 0 || sock >= base->dtablesize || base->fd_map[sock] < 0) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (msg == NULL) {
        tn_set_errno_val(base, EINVAL);
        return -1;
    }
    if (flags & MSG_OOB) {
        tn_set_errno_val(base, EOPNOTSUPP);
        return -1;
    }
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)msg;
    base->ipc_msg.args[1] = flags;
    return tn_ipc_call(base, TN_IPC_CMD_SENDMSG);
}

/* -276: recvmsg(sock, msg, flags) */
LONG tn_lvo_recvmsg(LONG sock, struct msghdr *msg, LONG flags, TnSocketBase *base)
{
    if (base == NULL) return -1;
    if (sock < 0 || sock >= base->dtablesize || base->fd_map[sock] < 0) {
        tn_set_errno_val(base, EBADF);
        return -1;
    }
    if (msg == NULL) {
        tn_set_errno_val(base, EINVAL);
        return -1;
    }
    if (flags & MSG_OOB) {
        tn_set_errno_val(base, EOPNOTSUPP);
        return -1;
    }
    base->ipc_msg.args[0] = sock;
    base->ipc_msg.ptrs[0] = (APTR)msg;
    base->ipc_msg.args[1] = flags;
    return tn_ipc_call(base, TN_IPC_CMD_RECVMSG);
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

/* -288: gethostid() (COMPAT-3 / TNET-078) */
in_addr_t tn_lvo_gethostid(TnSocketBase *base)
{
    if (base == NULL) return INADDR_NONE;
    if (tn_ipc_call(base, TN_IPC_CMD_GETSTATUS) == 0) {
        return (in_addr_t)base->ipc_msg.args[0];
    }
    return INADDR_NONE;
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
    _Static_assert(TN_SBTC_SIGURGMASK == SBTC_SIGURGMASK, "SBTC code drift");
    _Static_assert(TN_SBTC_SIGEVENTMASK == SBTC_SIGEVENTMASK, "SBTC code drift");
    _Static_assert(TN_SBTC_FDCALLBACK == SBTC_FDCALLBACK, "SBTC code drift");
    _Static_assert(TN_SBTC_LOGSTAT == SBTC_LOGSTAT, "SBTC code drift");
    _Static_assert(TN_SBTC_LOGTAGPTR == SBTC_LOGTAGPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_LOGFACILITY == SBTC_LOGFACILITY, "SBTC code drift");
    _Static_assert(TN_SBTC_LOGMASK == SBTC_LOGMASK, "SBTC code drift");
    _Static_assert(TN_SBTC_ERRNOSTRPTR == SBTC_ERRNOSTRPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_HERRNOSTRPTR == SBTC_HERRNOSTRPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_IOERRNOSTRPTR == SBTC_IOERRNOSTRPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_S2ERRNOSTRPTR == SBTC_S2ERRNOSTRPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_S2WERRNOSTRPTR == SBTC_S2WERRNOSTRPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_ERRNOLONGPTR == SBTC_ERRNOLONGPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_HERRNOLONGPTR == SBTC_HERRNOLONGPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_DTABLESIZE == SBTC_DTABLESIZE, "SBTC code drift");
    _Static_assert(TN_SBTC_RELEASESTRPTR == SBTC_RELEASESTRPTR, "SBTC code drift");
    _Static_assert(TN_SBTC_UDP_CHECKSUM == SBTC_UDP_CHECKSUM, "SBTC code drift");
    _Static_assert(TN_SBTC_IP_DEFAULT_TTL == SBTC_IP_DEFAULT_TTL, "SBTC code drift");
    _Static_assert(TN_SBTC_HAVE_DNS_API == SBTC_HAVE_DNS_API, "SBTC code drift");
    _Static_assert(TN_SBTC_HAVE_STATUS_API == SBTC_HAVE_STATUS_API, "SBTC code drift");
    _Static_assert(TN_SBTC_HAVE_GETHOSTADDR_R_API == SBTC_HAVE_GETHOSTADDR_R_API, "SBTC code drift");

    if (base == NULL || tags == NULL) return 0;

    while ((tag = NextTagItem(&tstate)) != NULL) {
        TnSbtcState st;
        TnSbtcResult r;

        st.sig_int      = base->sig_int;
        st.sig_io       = base->sig_io;
        st.sig_urg      = base->sig_urg;
        st.sig_event    = base->sig_event;
        st.errno_val    = base->task_errno;
        st.herrno_val   = base->task_herrno;
        st.errno_ptr    = (uint32_t)(uintptr_t)base->errno_ptr;
        st.errno_width  = (uint32_t)base->errno_width;
        st.herrno_ptr   = (uint32_t)(uintptr_t)base->herrno_ptr;
        st.dtablesize   = (uint32_t)base->dtablesize;
        st.fd_callback  = (uint32_t)(uintptr_t)base->fd_callback;
        st.log_stat     = (uint32_t)base->log_stat;
        st.log_tag_ptr  = (uint32_t)(uintptr_t)base->log_tag_ptr;
        st.log_facility = (uint32_t)base->log_facility;
        st.log_mask     = (uint32_t)base->log_mask;
        st.udp_checksum = (uint32_t)base->udp_checksum;
        st.ip_default_ttl = (uint32_t)base->ip_default_ttl;
        st.have_bits    = TN_SBTC_HAVE_DNS_API_BIT | TN_SBTC_HAVE_LOCAL_DB_API_BIT |
                          TN_SBTC_HAVE_ADDR_CONV_API_BIT | TN_SBTC_HAVE_GETHOSTADDR_R_BIT |
                          TN_SBTC_HAVE_SERVER_API_BIT;
        st.release_str  = (uint32_t)(uintptr_t)"tolunnet " TOLUNNET_VERSION " (bsdsocket 4.1)";

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
        case TN_SBTC_OP_SET_SIGEVENT:
            base->sig_event = (r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value;
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
        case TN_SBTC_OP_SET_FDCALLBACK:
            base->fd_callback = (APTR)(uintptr_t)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value);
            break;
        case TN_SBTC_OP_SET_LOGSTAT:
            base->log_stat = (LONG)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value);
            break;
        case TN_SBTC_OP_SET_LOGTAGPTR:
            base->log_tag_ptr = (APTR)(uintptr_t)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value);
            break;
        case TN_SBTC_OP_SET_LOGFACILITY:
            base->log_facility = (LONG)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value);
            break;
        case TN_SBTC_OP_SET_LOGMASK:
            base->log_mask = (LONG)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value);
            break;
        case TN_SBTC_OP_SET_UDPCHECKSUM:
            base->udp_checksum = (LONG)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value);
            break;
        case TN_SBTC_OP_SET_IPDEFAULTTTL:
            base->ip_default_ttl = (LONG)((r.is_ref && r.value != 0) ? *(ULONG *)(uintptr_t)r.value : r.value);
            break;
        case TN_SBTC_OP_SET_DTABLESIZE:
            {
                /* TNET-121: grow the per-base descriptor table live.
                 * Never shrink below the highest open fd + 1. */
                LONG newsize = (LONG)r.value;
                LONG highest_open = -1;
                LONG i;
                if (newsize < 4) newsize = 4;
                if (newsize > TN_MAX_FDS_PER_TASK) newsize = TN_MAX_FDS_PER_TASK;
                if (newsize <= base->dtablesize) {
                    for (i = base->dtablesize - 1; i >= 0; i--) {
                        if (base->fd_map[i] >= 0) { highest_open = i; break; }
                    }
                    if (newsize <= highest_open + 1) break; /* EINVAL per spec */
                }
                if (newsize != base->dtablesize) {
                    LONG  *nm = (LONG *)AllocVec(sizeof(LONG) * newsize, MEMF_PUBLIC | MEMF_CLEAR);
                    ULONG *ne = (ULONG *)AllocVec(sizeof(ULONG) * newsize, MEMF_PUBLIC | MEMF_CLEAR);
                    ULONG *nk = (ULONG *)AllocVec(sizeof(ULONG) * newsize, MEMF_PUBLIC | MEMF_CLEAR);
                    if (nm != NULL && ne != NULL && nk != NULL) {
                        LONG copy_n = (newsize < base->dtablesize) ? newsize : base->dtablesize;
                        for (i = 0; i < copy_n; i++) {
                            nm[i] = base->fd_map[i];
                            ne[i] = base->events[i];
                            nk[i] = base->event_masks[i];
                        }
                        for (i = copy_n; i < newsize; i++) {
                            nm[i] = -1;
                        }
                        FreeVec(base->fd_map);
                        FreeVec(base->events);
                        FreeVec(base->event_masks);
                        base->fd_map = nm;
                        base->events = ne;
                        base->event_masks = nk;
                        base->dtablesize = newsize;
                    } else {
                        if (nm) FreeVec(nm);
                        if (ne) FreeVec(ne);
                        if (nk) FreeVec(nk);
                    }
                }
            }
            break;
        case TN_SBTC_OP_GET_ERRNO_STR:
            {
                int err = (int)((r.is_ref && tag->ti_Data != 0) ? *(ULONG *)(uintptr_t)tag->ti_Data : r.value);
                const char *s = tn_strerror(err);
                if (r.is_ref && tag->ti_Data != 0) {
                    *(ULONG *)(uintptr_t)tag->ti_Data = (uint32_t)(uintptr_t)s;
                } else {
                    tag->ti_Data = (uint32_t)(uintptr_t)s;
                }
            }
            break;
        case TN_SBTC_OP_GET_HERRNO_STR:
            {
                int err = (int)((r.is_ref && tag->ti_Data != 0) ? *(ULONG *)(uintptr_t)tag->ti_Data : r.value);
                const char *s = tn_hstrerror(err);
                if (r.is_ref && tag->ti_Data != 0) {
                    *(ULONG *)(uintptr_t)tag->ti_Data = (uint32_t)(uintptr_t)s;
                } else {
                    tag->ti_Data = (uint32_t)(uintptr_t)s;
                }
            }
            break;
        case TN_SBTC_OP_GET_IOERRNO_STR:
            {
                int err = (int)((r.is_ref && tag->ti_Data != 0) ? *(ULONG *)(uintptr_t)tag->ti_Data : r.value);
                const char *s = tn_ioerror(err);
                if (r.is_ref && tag->ti_Data != 0) {
                    *(ULONG *)(uintptr_t)tag->ti_Data = (uint32_t)(uintptr_t)s;
                } else {
                    tag->ti_Data = (uint32_t)(uintptr_t)s;
                }
            }
            break;
        case TN_SBTC_OP_GET_S2ERRNO_STR:
            {
                int err = (int)((r.is_ref && tag->ti_Data != 0) ? *(ULONG *)(uintptr_t)tag->ti_Data : r.value);
                const char *s = tn_s2error(err);
                if (r.is_ref && tag->ti_Data != 0) {
                    *(ULONG *)(uintptr_t)tag->ti_Data = (uint32_t)(uintptr_t)s;
                } else {
                    tag->ti_Data = (uint32_t)(uintptr_t)s;
                }
            }
            break;
        case TN_SBTC_OP_GET_S2WERRNO_STR:
            {
                int err = (int)((r.is_ref && tag->ti_Data != 0) ? *(ULONG *)(uintptr_t)tag->ti_Data : r.value);
                const char *s = tn_s2werror(err);
                if (r.is_ref && tag->ti_Data != 0) {
                    *(ULONG *)(uintptr_t)tag->ti_Data = (uint32_t)(uintptr_t)s;
                } else {
                    tag->ti_Data = (uint32_t)(uintptr_t)s;
                }
            }
            break;
        default:
            break; /* handled no-ops (e.g. SET on GET-only tags) */
        }
    }
    return count;
}

/* -300: GetSocketEvents(event_ptr) (C4, TNET-125..127 Roadshow semantics)
 * Returns the fd of a socket with pending events, writes that socket's
 * mask to *event_ptr and consumes the event. Round-robins across
 * sockets; returns -1 when no events are pending. */
LONG tn_lvo_getsocketevents(ULONG *event_ptr, TnSocketBase *base)
{
    int fd;
    static int last_fd = -1; /* round-robin cursor (per-base is overkill:
                              * bsdsocktest drives from a single task) */
    if (base == NULL || event_ptr == NULL) {
        if (base != NULL) tn_set_errno_val(base, EINVAL);
        return -1;
    }
    Forbid();
    for (fd = 0; fd < base->dtablesize; fd++) {
        int check = (last_fd + 1 + fd) % base->dtablesize;
        if (base->events[check] != 0) {
            *event_ptr = base->events[check];
            base->events[check] = 0;
            last_fd = check;
            Permit();
            return check;
        }
    }
    Permit();
    return -1;
}

/* ========================================================= §D.5 EXTENSIONS */

/* -540: setnetent(stay_open) */
VOID tn_lvo_setnetent(LONG stay_open, TnSocketBase *base)
{
    (void)stay_open;
    if (base != NULL) base->netent_idx = 0;
}

/* -546: endnetent() */
VOID tn_lvo_endnetent(TnSocketBase *base)
{
    if (base != NULL) base->netent_idx = 0;
}

/* -222: getnetbyname(name) */
struct netent *tn_lvo_getnetbyname(CONST_STRPTR name, TnSocketBase *base)
{
    static const struct { const char *name; LONG net_num; } nets[] = {
        { "loopback",   127 },   /* 127.0.0.0/8 */
        { "localnet",   127 },
        { "arpanet",     10 },   /* 10.0.0.0/8 */
        { "milnet",      26 },   /* 26.0.0.0/8 */
    };
    if (base == NULL || name == NULL) return NULL;
    {
        int i;
        for (i = 0; i < (int)(sizeof(nets) / sizeof(nets[0])); i++) {
            if (tn_strcasecmp((const char *)name, nets[i].name) == 0) {
                int j = 0;
                while (name[j] && j < 31) { base->netent_name[j] = name[j]; j++; }
                base->netent_name[j] = '\0';
                base->netent_aliases[0] = NULL;
                base->netent_data.n_name = (STRPTR)base->netent_name;
                base->netent_data.n_aliases = base->netent_aliases;
                base->netent_data.n_addrtype = AF_INET;
                base->netent_data.n_net = nets[i].net_num;
                return &base->netent_data;
            }
        }
    }
    return NULL;
}

/* -228: getnetbyaddr(net, type) */
struct netent *tn_lvo_getnetbyaddr(in_addr_t net, LONG type, TnSocketBase *base)
{
    if (base == NULL || type != AF_INET) return NULL;
    if (net == 127 || net == 10 || net == 26) {
        return tn_lvo_getnetbyname(
            (CONST_STRPTR)((net == 127) ? "loopback" : (net == 10) ? "arpanet" : "milnet"),
            base);
    }
    return NULL;
}

/* -552: getnetent() */
struct netent *tn_lvo_getnetent(TnSocketBase *base)
{
    if (base == NULL) return NULL;
    if (base->netent_idx == 0) {
        base->netent_idx++;
        return tn_lvo_getnetbyname((CONST_STRPTR)"loopback", base);
    }
    return NULL;
}

/* -558: setprotoent(stay_open) */
VOID tn_lvo_setprotoent(LONG stay_open, TnSocketBase *base)
{
    (void)stay_open;
    if (base != NULL) base->protoent_idx = 0;
}

/* -564: endprotoent() */
VOID tn_lvo_endprotoent(TnSocketBase *base)
{
    if (base != NULL) base->protoent_idx = 0;
}

/* -570: getprotoent() */
struct protoent *tn_lvo_getprotoent(TnSocketBase *base)
{
    if (base == NULL) return NULL;
    if (base->protoent_idx < 0 || g_protocols[base->protoent_idx].name == NULL) return NULL;
    return tn_lvo_getprotobyname((CONST_STRPTR)g_protocols[base->protoent_idx++].name, base);
}

/* -576: setservent(stay_open) */
VOID tn_lvo_setservent(LONG stay_open, TnSocketBase *base)
{
    (void)stay_open;
    if (base != NULL) base->servent_idx = 0;
}

/* -582: endservent() */
VOID tn_lvo_endservent(TnSocketBase *base)
{
    if (base != NULL) base->servent_idx = 0;
}

/* -588: getservent() */
struct servent *tn_lvo_getservent(TnSocketBase *base)
{
    if (base == NULL) return NULL;
    if (base->servent_idx < 0 || g_services[base->servent_idx].name == NULL) return NULL;
    return tn_lvo_getservbyname((CONST_STRPTR)g_services[base->servent_idx++].name, NULL, base);
}

/* -594: inet_aton(cp, addr) */
LONG tn_lvo_inet_aton(CONST_STRPTR cp, struct in_addr *addr, TnSocketBase *base)
{
    uint32_t out_ip = 0;
    (void)base;
    if (cp == NULL) return 0;
    if (tn_inet_addr_parse_ex((const char *)cp, &out_ip)) {
        if (addr != NULL) addr->s_addr = out_ip;
        return 1;
    }
    return 0;
}

/* -600: inet_ntop(af, src, dst, size) */
STRPTR tn_lvo_inet_ntop(LONG af, const void *src, STRPTR dst, LONG size, TnSocketBase *base)
{
    if (af != AF_INET) {
        tn_set_errno_val(base, EAFNOSUPPORT);
        return NULL;
    }
    if (src == NULL || dst == NULL || size < 16) {
        tn_set_errno_val(base, ENOSPC);
        return NULL;
    }
    /* TNET-139: client src buffer — byte-wise load, no struct cast */
    uint32_t ip = 0;
    {
        struct in_addr tmpl;
        memcpy(&tmpl, src, sizeof(tmpl));
        ip = ntohl(tmpl.s_addr);
    }
    ULONG args[4];
    args[0] = (ip >> 24) & 0xFF;
    args[1] = (ip >> 16) & 0xFF;
    args[2] = (ip >> 8)  & 0xFF;
    args[3] = ip & 0xFF;
    RawDoFmt((CONST_STRPTR)"%lu.%lu.%lu.%lu", (APTR)args, TN_RAWFMT_PUTCH, dst);
    return dst;
}

/* -606: inet_pton(af, src, dst) */
LONG tn_lvo_inet_pton(LONG af, CONST_STRPTR src, void *dst, TnSocketBase *base)
{
    if (af != AF_INET) {
        tn_set_errno_val(base, EAFNOSUPPORT);
        return -1;
    }
    if (src == NULL || dst == NULL) return 0;
    uint32_t out_ip = 0;
    if (tn_inet_addr_parse_ex((const char *)src, &out_ip)) {
        /* TNET-139: client dst buffer — byte-wise store, no struct cast */
        memcpy(dst, &out_ip, sizeof(out_ip));
        return 1;
    }
    return 0;
}

/* -612: In_LocalAddr(address) */
LONG tn_lvo_in_localaddr(in_addr_t address, TnSocketBase *base)
{
    (void)base;
    uint32_t ip = ntohl(address);
    if ((ip >> 24) == 127 || (ip >> 24) == 10) return 1;
    return 0;
}

/* -618: In_CanForward(address) */
LONG tn_lvo_in_canforward(in_addr_t address, TnSocketBase *base)
{
    (void)base;
    uint32_t ip = ntohl(address);
    uint32_t b0 = (ip >> 24);
    if (b0 == 127 || (b0 >= 224 && b0 <= 255)) return 0;
    return 1;
}

/* -702: GetDefaultDomainName(buffer, buffer_size) */
BOOL tn_lvo_getdefaultdomainname(STRPTR buffer, LONG buffer_size, TnSocketBase *base)
{
    const char *dom = "local";
    if (buffer == NULL || buffer_size <= 0) return FALSE;
    if (base != NULL && base->domain_name[0] != '\0') dom = base->domain_name;
    int len = 0;
    while (dom[len]) len++;
    if (len >= buffer_size) return FALSE;
    for (int i = 0; i <= len; i++) buffer[i] = dom[i];
    return TRUE;
}

/* -708: SetDefaultDomainName(buffer) */
VOID tn_lvo_setdefaultdomainname(CONST_STRPTR buffer, TnSocketBase *base)
{
    if (base != NULL && buffer != NULL) {
        int i = 0;
        while (buffer[i] && i < 63) { base->domain_name[i] = buffer[i]; i++; }
        base->domain_name[i] = '\0';
    }
}

/* -738: gethostbyname_r(name, hp, buf, buflen, he) */
struct hostent *tn_lvo_gethostbyname_r(CONST_STRPTR name, struct hostent *hp, APTR buf, ULONG buflen, LONG *he, TnSocketBase *base)
{
    if (base == NULL || hp == NULL || buf == NULL) {
        if (he) *he = NO_RECOVERY;
        return NULL;
    }
    struct hostent *res = tn_lvo_gethostbyname((STRPTR)name, base);
    if (res == NULL) {
        if (he) *he = base->task_herrno;
        return NULL;
    }
    if (buflen < 128) {
        if (he) *he = NO_RECOVERY;
        tn_set_errno_val(base, ERANGE);
        return NULL;
    }
    char *p = (char *)buf;
    hp->h_name = (STRPTR)p;
    int i = 0;
    while (res->h_name && res->h_name[i] && i < 63) { p[i] = res->h_name[i]; i++; }
    p[i++] = '\0';
    p = (char *)(((uintptr_t)p + 3) & ~3);

    /* p was 4-aligned above; the void* hop records that guarantee (TNET-139) */
    STRPTR *aliases = (STRPTR *)(void *)p;
    aliases[0] = NULL;
    hp->h_aliases = aliases;
    p += sizeof(STRPTR) * 2;

    hp->h_addrtype = res->h_addrtype;
    hp->h_length = res->h_length;

    uint32_t *ip_storage = (uint32_t *)(void *)p;
    uint32_t src_ip = 0;
    memcpy(&src_ip, res->h_addr_list[0], sizeof(src_ip)); /* daemon buffer: no alignment guarantee */
    *ip_storage = src_ip;
    p += sizeof(uint32_t);

    STRPTR *addrs = (STRPTR *)(void *)p;
    addrs[0] = (STRPTR)ip_storage;
    addrs[1] = NULL;
    hp->h_addr_list = (char **)addrs;

    if (he) *he = 0;
    return hp;
}

/* -744: gethostbyaddr_r(addr, len, type, hp, buf, buflen, he) */
struct hostent *tn_lvo_gethostbyaddr_r(CONST_STRPTR addr, LONG len, LONG type, struct hostent *hp, APTR buf, ULONG buflen, LONG *he, TnSocketBase *base)
{
    if (base == NULL || hp == NULL || buf == NULL) {
        if (he) *he = NO_RECOVERY;
        return NULL;
    }
    struct hostent *res = tn_lvo_gethostbyaddr((STRPTR)addr, len, type, base);
    if (res == NULL) {
        if (he) *he = base->task_herrno;
        return NULL;
    }
    if (buflen < 128) {
        if (he) *he = NO_RECOVERY;
        tn_set_errno_val(base, ERANGE);
        return NULL;
    }
    char *p = (char *)buf;
    hp->h_name = (STRPTR)p;
    int i = 0;
    while (res->h_name && res->h_name[i] && i < 63) { p[i] = res->h_name[i]; i++; }
    p[i++] = '\0';
    p = (char *)(((uintptr_t)p + 3) & ~3);

    /* p was 4-aligned above; the void* hop records that guarantee (TNET-139) */
    STRPTR *aliases = (STRPTR *)(void *)p;
    aliases[0] = NULL;
    hp->h_aliases = aliases;
    p += sizeof(STRPTR) * 2;

    hp->h_addrtype = res->h_addrtype;
    hp->h_length = res->h_length;

    uint32_t *ip_storage = (uint32_t *)(void *)p;
    uint32_t src_ip = 0;
    memcpy(&src_ip, res->h_addr_list[0], sizeof(src_ip)); /* daemon buffer: no alignment guarantee */
    *ip_storage = src_ip;
    p += sizeof(uint32_t);

    STRPTR *addrs = (STRPTR *)(void *)p;
    addrs[0] = (STRPTR)ip_storage;
    addrs[1] = NULL;
    hp->h_addr_list = (char **)addrs;

    if (he) *he = 0;
    return hp;
}

/* -258: vsyslog(pri, msg, args) */
VOID tn_lvo_vsyslog(LONG pri, CONST_STRPTR msg, APTR args, TnSocketBase *base)
{
    (void)base;
    (void)args;
    if (msg != NULL) {
        tn_logf(TN_LOG_BASIC, "[syslog:%ld] %s\n", pri, (const char *)msg);
    }
}

/* -690: ProcessIsServer(pr) */
BOOL tn_lvo_processisserver(struct Process *pr, TnSocketBase *base)
{
    (void)base;
    if (pr == NULL) {
        struct Task *t = FindTask(NULL);
        if (t != NULL && t->tc_Node.ln_Type == NT_PROCESS) {
            pr = (struct Process *)t;
        }
    }
    if (pr == NULL || pr->pr_Task.tc_Node.ln_Type != NT_PROCESS) {
        return FALSE;
    }
    return (pr->pr_ExitData != 0) ? TRUE : FALSE;
}

/* -696: ObtainServerSocket() */
LONG tn_lvo_obtainserversocket(TnSocketBase *base)
{
    struct Task *t;
    struct Process *pr;
    struct DaemonMessage *dm;

    if (base == NULL) return -1;
    t = FindTask(NULL);
    pr = (t != NULL && t->tc_Node.ln_Type == NT_PROCESS) ? (struct Process *)t : NULL;
    if (pr == NULL || !tn_lvo_processisserver(pr, base)) {
        tn_set_errno_val(base, EINVAL);
        return -1;
    }

    dm = (struct DaemonMessage *)(uintptr_t)pr->pr_ExitData;
    if (dm == NULL) {
        tn_set_errno_val(base, EINVAL);
        return -1;
    }

    return tn_lvo_obtainsocket(dm->dm_ID, (LONG)dm->dm_Family, (LONG)dm->dm_Type, 0, base);
}

/* ================================================================ */
/* ANX-05: getaddrinfo / freeaddrinfo / getnameinfo / gai_strerror  */
/* Thin implementations over gethostbyname/gethostbyaddr — enough   */
/* for AmiSSL, curl/wget ports, and OS4 backports that use the     */
/* POSIX resolve API.                                               */
/* ================================================================ */

/* -810: getaddrinfo(hostname, servname, hints, res) */
LONG tn_lvo_getaddrinfo(CONST_STRPTR hostname, CONST_STRPTR servname,
                        const struct addrinfo *hints, struct addrinfo **res,
                        TnSocketBase *base)
{
    struct addrinfo *ai;
    struct sockaddr_in *sa;
    struct hostent *he = NULL;
    ULONG addr = 0;
    int socktype = SOCK_STREAM;
    int family = AF_INET;

    if (base == NULL || res == NULL) return EAI_FAIL;
    *res = NULL;

    if (hints != NULL) {
        if (hints->ai_family != AF_INET && hints->ai_family != 0) {
            return EAI_FAMILY;
        }
        if (hints->ai_socktype != 0 && hints->ai_socktype != SOCK_STREAM &&
            hints->ai_socktype != SOCK_DGRAM && hints->ai_socktype != SOCK_RAW) {
            return EAI_SOCKTYPE;
        }
        if (hints->ai_socktype != 0) socktype = hints->ai_socktype;
    }

    /* Resolve: numeric first, then DNS */
    if (hostname != NULL && hostname[0] != '\0') {
        addr = tn_lvo_inet_addr((CONST_STRPTR)hostname, base);
        if (addr == INADDR_NONE) {
            if (hints != NULL && (hints->ai_flags & AI_NUMERICHOST)) {
                return EAI_NONAME;
            }
            he = tn_lvo_gethostbyname((CONST_STRPTR)hostname, base);
            if (he == NULL || he->h_addr_list[0] == NULL) {
                return EAI_NONAME;
            }
            memcpy(&addr, he->h_addr_list[0], 4);
        }
    } else {
        if (hints != NULL && (hints->ai_flags & AI_PASSIVE)) {
            addr = 0; /* INADDR_ANY */
        } else {
            addr = 0x7F000001; /* INADDR_LOOPBACK */
        }
    }

    /* Allocate one addrinfo node (caller frees with freeaddrinfo) */
    ai = (struct addrinfo *)AllocVec(sizeof(struct addrinfo) + sizeof(struct sockaddr_in) + 256,
                                      MEMF_PUBLIC | MEMF_CLEAR);
    if (ai == NULL) return EAI_MEMORY;

    sa = (struct sockaddr_in *)(void *)((UBYTE *)ai + sizeof(struct addrinfo));
    ai->ai_flags   = (hints != NULL) ? hints->ai_flags : 0;
    ai->ai_family  = family;
    ai->ai_socktype = socktype;
    ai->ai_protocol = (socktype == SOCK_DGRAM) ? IPPROTO_UDP : IPPROTO_TCP;
    ai->ai_addrlen = sizeof(struct sockaddr_in);
    ai->ai_addr    = (struct sockaddr *)sa;
    ai->ai_next    = NULL;

    sa->sin_len    = sizeof(struct sockaddr_in);
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = addr;

    /* Service name → port */
    if (servname != NULL && servname[0] != '\0') {
        LONG port = 0;
        /* Try numeric first */
        {
            const char *p = (const char *)servname;
            port = 0;
            while (*p >= '0' && *p <= '9') {
                port = port * 10 + (*p - '0');
                p++;
            }
            if (*p != '\0') port = -1; /* not numeric */
        }
        if (port < 0) {
            /* Look up service name */
            struct servent *se = tn_lvo_getservbyname((CONST_STRPTR)servname,
                (CONST_STRPTR)((socktype == SOCK_DGRAM) ? "udp" : "tcp"), base);
            if (se != NULL) {
                port = (LONG)ntohs(se->s_port);
            } else {
                port = 0;
            }
        }
        if (port > 0 && port < 65536) {
            sa->sin_port = htons((UWORD)port);
        }
    }

    /* Canonical name if requested */
    if (hints != NULL && (hints->ai_flags & AI_CANONNAME)) {
        const char *cname = (he != NULL && he->h_name != NULL)
                          ? (const char *)he->h_name
                          : (const char *)hostname;
        if (cname != NULL) {
            char *cn = (char *)((UBYTE *)ai + sizeof(struct addrinfo) + sizeof(struct sockaddr_in));
            int i = 0;
            while (cname[i] && i < 254) { cn[i] = cname[i]; i++; }
            cn[i] = '\0';
            ai->ai_canonname = cn;
        }
    }

    *res = ai;
    return 0; /* EAI_NONE = success */
}

/* -804: freeaddrinfo(ai) */
VOID tn_lvo_freeaddrinfo(struct addrinfo *ai, TnSocketBase *base)
{
    (void)base;
    /* The node was allocated as one block (addrinfo + sockaddr + canonname) */
    while (ai != NULL) {
        struct addrinfo *next = ai->ai_next;
        FreeVec(ai);
        ai = next;
    }
}

/* -822: gai_strerror(errnum) */
STRPTR tn_lvo_gai_strerror(LONG errnum, TnSocketBase *base)
{
    (void)base;
    switch (errnum) {
    case 0:         return (STRPTR)"no error";
    case EAI_BADFLAGS:    return (STRPTR)"invalid value for ai_flags";
    case EAI_NONAME:      return (STRPTR)"name or service not known";
    case EAI_AGAIN:       return (STRPTR)"temporary failure in resolution";
    case EAI_FAIL:        return (STRPTR)"non-recoverable failure";
    case EAI_NODATA:      return (STRPTR)"no address associated with name";
    case EAI_FAMILY:      return (STRPTR)"ai_family not supported";
    case EAI_SOCKTYPE:    return (STRPTR)"ai_socktype not supported";
    case EAI_SERVICE:     return (STRPTR)"service not supported";
    case EAI_MEMORY:      return (STRPTR)"memory allocation failure";
    case EAI_SYSTEM:      return (STRPTR)"system error";
    case EAI_BADHINTS:    return (STRPTR)"invalid value for hints";
    case EAI_PROTOCOL:    return (STRPTR)"resolved protocol unknown";
    default:              return (STRPTR)"unknown error";
    }
}

/* -816: getnameinfo(sa, salen, host, hostlen, serv, servlen, flags) */
LONG tn_lvo_getnameinfo(const struct sockaddr *sa, ULONG salen,
                        STRPTR host, ULONG hostlen,
                        STRPTR serv, ULONG servlen, ULONG flags,
                        TnSocketBase *base)
{
    struct sockaddr_in sin_local;
    const struct sockaddr_in *sin;

    (void)base;
    if (sa == NULL || salen < sizeof(struct sockaddr_in)) {
        return EAI_FAIL;
    }

    memcpy(&sin_local, sa, sizeof(sin_local)); sin = &sin_local;
    if (sin->sin_family != AF_INET) {
        return EAI_FAMILY;
    }

    if (host != NULL && hostlen > 0) {
        if (flags & NI_NUMERICHOST) {
            struct in_addr ia;
            ia.s_addr = sin->sin_addr.s_addr;
            strncpy((char *)host, (const char *)tn_lvo_inet_ntoa(ia.s_addr, base), hostlen - 1);
            ((char *)host)[hostlen - 1] = '\0';
        } else {
            struct hostent *he = tn_lvo_gethostbyaddr(
                (CONST_STRPTR)&sin_local.sin_addr.s_addr, 4, AF_INET, base);
            if (he != NULL && he->h_name != NULL) {
                strncpy((char *)host, (const char *)he->h_name, hostlen - 1);
                ((char *)host)[hostlen - 1] = '\0';
            } else {
                if (flags & NI_NAMEREQD) return EAI_NONAME;
                struct in_addr ia;
                ia.s_addr = sin->sin_addr.s_addr;
                strncpy((char *)host, (const char *)tn_lvo_inet_ntoa(ia.s_addr, base), hostlen - 1);
                ((char *)host)[hostlen - 1] = '\0';
            }
        }
    }

    if (serv != NULL && servlen > 0) {
        if (flags & NI_NUMERICSERV) {
            ULONG port = ntohs(sin->sin_port);
            char num[8];
            int i = 0;
            if (port == 0) { num[i++] = '0'; }
            while (port > 0 && i < 7) { num[i++] = '0' + (port % 10); port /= 10; }
            num[i] = '\0';
            /* reverse */
            { int j; char tmp; for (j = 0; j < i/2; j++) { tmp = num[j]; num[j] = num[i-1-j]; num[i-1-j] = tmp; } }
            strncpy((char *)serv, num, servlen - 1);
            ((char *)serv)[servlen - 1] = '\0';
        } else {
            struct servent *se = tn_lvo_getservbyport(sin->sin_port,
                (CONST_STRPTR)((flags & NI_DGRAM) ? "udp" : "tcp"), base);
            if (se != NULL && se->s_name != NULL) {
                strncpy((char *)serv, (const char *)se->s_name, servlen - 1);
                ((char *)serv)[servlen - 1] = '\0';
            } else {
                /* Fall back to numeric */
                ULONG port = ntohs(sin->sin_port);
                char num[8];
                int i = 0;
                if (port == 0) { num[i++] = '0'; }
                while (port > 0 && i < 7) { num[i++] = '0' + (port % 10); port /= 10; }
                num[i] = '\0';
                { int j; char tmp; for (j = 0; j < i/2; j++) { tmp = num[j]; num[j] = num[i-1-j]; num[i-1-j] = tmp; } }
                strncpy((char *)serv, num, servlen - 1);
                ((char *)serv)[servlen - 1] = '\0';
            }
        }
    }

    return 0;
}

