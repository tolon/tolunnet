/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet ? TCP Stream IPC Handlers & lwIP Callbacks Implementation (ipc_tcp.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_tcp.h"
#include "ipc_select.h"
#include "slot_table.h"
#include "netif_mgr.h"
#include "ipc_dispatch.h"
#include "ipc_dgram.h"
#include "../common/sockaddr_util.h"

/* TNET-159: one entry of a client iovec array, copied byte-wise (68000:
 * no typed load through a client pointer). */
static void tn_tcp_iov_get(const void *iov, ULONG i, struct iovec *out)
{
    memcpy(out, (const char *)iov + i * sizeof(struct iovec), sizeof(struct iovec));
}

/* 4.2: queue as much of the stream [*done..len) as lwIP takes right now -
 * from buf, or (TNET-159, sendmsg) from the client iovec array, *done
 * being the cursor across its entries. ERR_OK = everything queued;
 * ERR_MEM = send buffer or segment queue full (wait or EWOULDBLOCK);
 * anything else is a hard tcp_write error. */
static err_t tn_tcp_write_some(struct tcp_pcb *pcb, const char *buf,
                               const struct iovec *iov, ULONG iovcnt,
                               LONG len, LONG *done)
{
    ULONG vi = 0;
    LONG vbase = 0;             /* stream offset of iov[vi] */
    struct iovec v;

    while (*done < len) {
        u16_t snd_buf = tcp_sndbuf(pcb);
        const char *src;
        LONG left;
        u16_t chunk;
        err_t werr;
        if (snd_buf == 0) return ERR_MEM;
        if (iov == NULL) {
            src  = buf + *done;
            left = len - *done;
        } else {
            for (;;) {          /* entry holding byte *done (skips empty ones) */
                if (vi >= iovcnt) return ERR_VAL;
                tn_tcp_iov_get(iov, vi, &v);
                if (*done - vbase < (LONG)v.iov_len) break;
                vbase += (LONG)v.iov_len;
                vi++;
            }
            src  = (const char *)v.iov_base + (*done - vbase);
            left = (LONG)v.iov_len - (*done - vbase);
        }
        chunk = (left > 0xFFFF) ? 0xFFFF : (u16_t)left;
        if (chunk > snd_buf) chunk = snd_buf;
        werr = tcp_write(pcb, src, chunk, TCP_WRITE_FLAG_COPY);
        if (werr != ERR_OK) return werr;
        *done += chunk;
    }
    return ERR_OK;
}

static LONG tn_tcp_write_errno(err_t werr)
{
    switch (werr) {
    case ERR_CONN:
    case ERR_CLSD: return ENOTCONN;
    case ERR_ARG:
    case ERR_VAL:  return EINVAL;
    default:       return ENOBUFS;
    }
}

/* 4.2: continue a parked blocking send from lwIP callback context (sent /
 * poll). Never drains loopback here - that would re-enter tcp_input; the
 * main loop flushes after the callback returns. */
static void tn_tcp_resume_send(TnSocketSlot *slot)
{
    TnIpcMsg *imsg = slot->pending_send_msg;
    LONG done;
    err_t werr;

    if (imsg == NULL) return;
    if (slot->shut_wr) {
        tn_slot_reply_send(slot, EPIPE, 1);
        return;
    }
    if (slot->tcp_pcb == NULL) {
        tn_slot_reply_send(slot, ECONNRESET, 1);
        return;
    }
    done = slot->send_done;
    werr = tn_tcp_write_some(slot->tcp_pcb, (const char *)imsg->ptrs[0],
                             slot->send_iov, slot->send_iovcnt, imsg->args[1], &done);
    if (done != slot->send_done) {
        tcp_output(slot->tcp_pcb);
    }
    slot->send_done = done;
    if (werr == ERR_MEM) return;            /* still no room: stay parked */
    tn_slot_reply_send(slot, tn_tcp_write_errno(werr), 1); /* ERR_OK -> done */
}

/* 4.2: retry hook for ERR_MEM with nothing in flight (no sent_cb would
 * ever come) - lwIP's api_msg uses tcp_poll the same way. */
static err_t tn_tcp_poll_cb(void *arg, struct tcp_pcb *pcb)
{
    int slot_idx = (int)(intptr_t)arg;
    if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS) {
        TnSocketSlot *slot = &g_daemon.sockets[slot_idx];
        if (slot->in_use && slot->tcp_pcb == pcb && slot->pending_send_msg != NULL) {
            tn_tcp_resume_send(slot);
        }
    }
    return ERR_OK;
}

err_t tn_tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    int slot_idx = (int)(intptr_t)arg;
    (void)len;
    if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS) {
        TnSocketSlot *slot = &g_daemon.sockets[slot_idx];
        if (slot->in_use && slot->tcp_pcb == pcb) {
            if (slot->pending_send_msg != NULL) {
                tn_tcp_resume_send(slot);
            }
            /* 4.6: space freed - wake WaitSelect write sets too */
            tn_signal_socket(&g_daemon, slot);
            tn_record_socket_event(&g_daemon, slot, FD_WRITE);
        }
    }
    return ERR_OK;
}

/* TNET-156: CLOSING / TIME_WAIT (both FINs seen, ours sent by
 * shutdown(SHUT_WR)) are finished by lwIP alone: tcp_slowtmr frees a
 * TIME_WAIT pcb, tcp_kill_timewait aborts one - neither calls errf. Hand
 * the pcb over now (no callback can follow the EOF one) so CloseSocket,
 * select and enumsockets never touch it; never tcp_close it. */
static BOOL tn_tcp_pcb_finished(const struct tcp_pcb *pcb)
{
    return (pcb->state == CLOSING || pcb->state == TIME_WAIT) ? TRUE : FALSE;
}

static void tn_tcp_detach_finished(TnSocketSlot *slot, struct tcp_pcb *pcb)
{
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    slot->tcp_pcb = NULL;
    slot->shut_wr = TRUE;           /* send -> EPIPE, select writable */
    tn_slot_reply_send(slot, EPIPE, 1);
}

/* Audit run-1: queue a PBUF_RAM clone of an incoming segment and release
 * the original. Holding the driver's PBUF_POOL pbufs here let one slow
 * reader pin the whole pool (cap 32 == PBUF_POOL_SIZE) and stall all RX.
 * Returns 0 when queued (p has been freed) or -1 with p untouched, so the
 * caller can still hand p back to lwIP (ERR_MEM) or drop it. */
static int tn_tcp_queue_clone(TnSocketSlot *slot, struct pbuf *p, BOOL capped)
{
    struct pbuf *q;
    int rc;

    if (capped && slot->rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        return -1;
    }
    q = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
    if (q == NULL) {
        return -1;
    }
    rc = capped ? tn_rx_queue_push(slot, q, NULL, 0)
                : tn_rx_queue_push_nocap(slot, q, NULL, 0);
    if (rc != 0) {
        pbuf_free(q);
        return -1;
    }
    pbuf_free(p);
    return 0;
}

err_t tn_tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    (void)err;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        if (p != NULL) pbuf_free(p);
        return ERR_OK;
    }

    slot = &g_daemon.sockets[slot_idx];
    if (!slot->in_use || slot->tcp_pcb != pcb) {
        if (p != NULL) pbuf_free(p);
        return ERR_OK;
    }

    /* Peer closed connection (FIN received) */
    if (p == NULL) {
        slot->tcp_state = TN_TCP_STATE_PEER_CLOSED;
        if (tn_tcp_pcb_finished(pcb)) {
            tn_logf(TN_LOG_VERBOSE, "tolunnet: tcp slot=%d finished (state %d), pcb detached\n",
                    slot_idx, (int)pcb->state);
            tn_tcp_detach_finished(slot, pcb);
        }
        tn_signal_socket(&g_daemon, slot);
        tn_record_socket_event(&g_daemon, slot, FD_CLOSE | FD_READ);
        if (slot->pending_recv_msg != NULL) {
            tn_service_pending_recv(&g_daemon, slot);
        }
        return ERR_OK;
    }

    /* 4.7: after shutdown(SHUT_RD) BSD silently drops new data; keep the
     * window open instead of letting lwIP RST the connection. */
    if (slot->shut_rd) {
        tcp_recved(pcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    if (tn_tcp_queue_clone(slot, p, TRUE) != 0) {
        /* TNET-156: in CLOSING/TIME_WAIT lwIP never redelivers refused
         * data (tcp_fasttmr walks active pcbs only) and would park the
         * FIN on it, so the EOF callback never came. Take it past the
         * cap; drop only when even that fails. */
        if (tn_tcp_pcb_finished(pcb)) {
            if (tn_tcp_queue_clone(slot, p, FALSE) != 0) {
                tn_logf(TN_LOG_BASIC, "tolunnet: tcp slot=%d: %u final bytes dropped (no memory)\n",
                        slot_idx, (unsigned)p->tot_len);
                pbuf_free(p);
                return ERR_OK;
            }
        } else {
            /* Queue full or alloc fail: return ERR_MEM without freeing pbuf so lwIP holds it */
            return ERR_MEM;
        }
    }

    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_READ);
    if (slot->pending_recv_msg != NULL) {
        tn_service_pending_recv(&g_daemon, slot);
    }
    return ERR_OK;
}

err_t tn_tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    (void)err;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return ERR_OK;

    slot = &g_daemon.sockets[slot_idx];
    if (!slot->in_use || slot->tcp_pcb != pcb) return ERR_OK;

    /* TNET-115: disable Nagle on all connections — our loopback delivery
     * model (batch output + drain) interacts badly with Nagle's
     * small-segment hold on the first data after handshake. */
    tcp_nagle_disable(pcb);

    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;
    tn_logf(TN_LOG_VERBOSE, "tolunnet: tcp_connected_cb slot=%d established\n", slot_idx);
    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_CONNECT | FD_WRITE);

    if (slot->pending_connect_msg != NULL) {
        slot->pending_connect_msg->result = 0;
        slot->pending_connect_msg->err_no = 0;
        ReplyMsg((struct Message *)slot->pending_connect_msg);
        slot->pending_connect_msg = NULL;
    }

    return ERR_OK;
}

void tn_tcp_err_cb(void *arg, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    LONG eno;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return;

    slot = &g_daemon.sockets[slot_idx];
    if (!slot->in_use) return;

    slot->tcp_pcb = NULL; /* lwIP frees PCB before calling err_cb */
    tn_logf(TN_LOG_VERBOSE, "tolunnet: tcp_err_cb slot=%d err=%d\n", slot_idx, (int)err);

    /* 4.3: map the lwIP reason. ERR_CLSD is lwIP's normal end of a passive
     * close (LAST_ACK acked, TF_RXCLOSED unset) - not an error: the peer's
     * FIN was already seen, so recv() keeps returning EOF. */
    if (err == ERR_CLSD && slot->tcp_state != TN_TCP_STATE_CONNECTING) {
        slot->tcp_state  = TN_TCP_STATE_PEER_CLOSED;
        slot->last_error = 0;
        tn_signal_socket(&g_daemon, slot);
        tn_record_socket_event(&g_daemon, slot, FD_CLOSE);
        tn_slot_reply_send(slot, EPIPE, 1);
        if (slot->pending_recv_msg != NULL) {
            tn_service_pending_recv(&g_daemon, slot);
        }
        return;
    }
    if (slot->tcp_state == TN_TCP_STATE_CONNECTING) {
        eno = (err == ERR_ABRT) ? ETIMEDOUT : ECONNREFUSED;
    } else {
        eno = (err == ERR_ABRT) ? ECONNABORTED : ECONNRESET;
    }

    slot->tcp_state  = TN_TCP_STATE_ERROR;
    slot->last_error = eno;
    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_ERROR);

    if (slot->pending_connect_msg != NULL) {
        slot->pending_connect_msg->result = -1;
        slot->pending_connect_msg->err_no = eno;
        ReplyMsg((struct Message *)slot->pending_connect_msg);
        slot->pending_connect_msg = NULL;
        slot->last_error = 0; /* reported through connect() */
    }
    tn_slot_reply_send(slot, eno, 1);
    if (slot->pending_recv_msg != NULL) {
        tn_service_pending_recv(&g_daemon, slot);
    }
}

err_t tn_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return ERR_VAL;
    slot = &g_daemon.sockets[slot_idx];
    if (!slot->in_use || slot->tcp_state != TN_TCP_STATE_LISTENING) return ERR_VAL;
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

    /* If a client task is synchronously blocked waiting inside accept() */
    if (slot->pending_accept_msg != NULL) {
        TnIpcMsg *imsg = slot->pending_accept_msg;
        TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
        void *addr = (void *)imsg->ptrs[0];
        socklen_t *addrlen = (socklen_t *)imsg->ptrs[1];
        int client_fd = -1;
        int new_slot_idx = -1;
        TnSocketSlot *new_slot;

        slot->pending_accept_msg = NULL;

        new_slot = tn_slot_alloc(&g_daemon, base, imsg->client_task, AF_INET, SOCK_STREAM, 0, &new_slot_idx);
        if (new_slot == NULL) {
            tcp_abort(newpcb);
            imsg->result = -1;
            imsg->err_no = ENFILE;
            ReplyMsg((struct Message *)imsg);
            return ERR_ABRT;
        }

        client_fd = (int)imsg->args[3];
        if (client_fd >= 0 && client_fd < base->dtablesize && base->fd_map[client_fd] == -1) {
            base->fd_map[client_fd] = new_slot_idx;
        } else {
            client_fd = tn_fd_alloc(base, new_slot_idx);
        }

        if (client_fd < 0) {
            tn_slot_free(&g_daemon, new_slot_idx);
            tcp_abort(newpcb);
            imsg->result = -1;
            imsg->err_no = EMFILE;
            ReplyMsg((struct Message *)imsg);
            return ERR_ABRT;
        }

        new_slot->tcp_state = TN_TCP_STATE_ESTABLISHED;
        new_slot->tcp_pcb   = newpcb;

        /* TNET-115: disable Nagle on accepted connections too */
        tcp_nagle_disable(newpcb);
        new_slot->opt_nodelay = TRUE;

        /* z.ai step 7g item 2: inherit listener slot options here too */
        new_slot->opt_keepalive = slot->opt_keepalive;
        new_slot->opt_linger    = slot->opt_linger;
        new_slot->opt_sndbuf    = slot->opt_sndbuf;
        new_slot->opt_rcvbuf    = slot->opt_rcvbuf;
        new_slot->opt_oobinline = slot->opt_oobinline;
        new_slot->opt_mss       = slot->opt_mss;
        new_slot->opt_keepidle  = slot->opt_keepidle;
        new_slot->opt_keepintvl = slot->opt_keepintvl;
        new_slot->opt_keepcnt   = slot->opt_keepcnt;
        if (slot->opt_keepidle > 0) {
            newpcb->keep_idle  = (u32_t)slot->opt_keepidle * 1000UL;
        }
        if (slot->opt_keepintvl > 0) {
            newpcb->keep_intvl = (u32_t)slot->opt_keepintvl * 1000UL;
        }
        if (slot->opt_keepcnt > 0) {
            newpcb->keep_cnt   = (u32_t)slot->opt_keepcnt;
        }
        if (slot->opt_mss > 0 && (u16_t)slot->opt_mss < newpcb->mss) {
            newpcb->mss = (u16_t)slot->opt_mss;
        }

        tcp_arg(newpcb, (void *)(intptr_t)new_slot_idx);
        tcp_recv(newpcb, tn_tcp_recv_cb);
        tcp_sent(newpcb, tn_tcp_sent_cb);
        tcp_err(newpcb, tn_tcp_err_cb);

        /* TNET-139/4.5: byte-wise, truncating store into client memory */
        tn_store_client_sockaddr(addr, addrlen, newpcb->remote_port,
                                 ip_2_ip4(&newpcb->remote_ip)->addr);

        imsg->result = client_fd;
        imsg->err_no = 0;
        tn_record_socket_event(&g_daemon, new_slot, FD_WRITE);
        ReplyMsg((struct Message *)imsg);
        return ERR_OK;
    }

    /* Queue incoming connection into accept queue (bounded by listen()
     * backlog, at most TN_ACCEPT_QUEUE_MAX) */
    if (slot->accept_count >= slot->listen_backlog || tn_accept_queue_push(slot, newpcb) != 0) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_ACCEPT);
    return ERR_OK;
}

int tn_ipc_cmd_listen(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG backlog = imsg->args[1];
    struct tcp_pcb *lpcb;
    u8_t bl;
    int slot_idx;
    (void)d;

    if (slot == NULL || slot->type != 1 /* TCP */ || slot->tcp_pcb == NULL) {
        imsg->result = -1;
        imsg->err_no = EOPNOTSUPP;
        return 0;
    }

    bl = (backlog <= 0) ? 1 : ((backlog > TN_ACCEPT_QUEUE_MAX) ? TN_ACCEPT_QUEUE_MAX : (u8_t)backlog);

    if (slot->tcp_state == TN_TCP_STATE_LISTENING) {
        slot->listen_backlog = bl; /* BSD: re-listen updates the backlog */
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    lpcb = tcp_listen_with_backlog(slot->tcp_pcb, bl);
    if (lpcb == NULL) {
        imsg->result = -1;
        imsg->err_no = ENOBUFS;
        return 0;
    }

    slot_idx = (int)(slot - d->sockets);
    slot->tcp_pcb   = lpcb;
    slot->tcp_state = TN_TCP_STATE_LISTENING;
    slot->listen_backlog = bl;
    tcp_arg(lpcb, (void *)(intptr_t)slot_idx);
    tcp_accept(lpcb, tn_tcp_accept_cb);

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_accept(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    void *addr = (void *)imsg->ptrs[0];
    socklen_t *addrlen = (socklen_t *)imsg->ptrs[1];
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    (void)d;

    if (slot == NULL || slot->tcp_state != TN_TCP_STATE_LISTENING) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    if (slot->accept_head != NULL) {
        TnAcceptEntry *ent = tn_accept_queue_pop_entry(slot);
        if (ent == NULL) {
            imsg->result = -1;
            imsg->err_no = EWOULDBLOCK;
            return 0;
        }

        struct tcp_pcb *new_pcb = ent->new_pcb;
        int new_fd = -1;
        int new_slot_idx = -1;
        TnSocketSlot *new_slot;

        new_slot = tn_slot_alloc(d, base, imsg->client_task, AF_INET, SOCK_STREAM, 0, &new_slot_idx);
        if (new_slot == NULL) {
            tn_accept_entry_free(ent, TRUE);
            imsg->result = -1;
            imsg->err_no = ENFILE;
            return 0;
        }

        new_fd = (int)imsg->args[3];
        if (new_fd >= 0 && new_fd < base->dtablesize && base->fd_map[new_fd] == -1) {
            base->fd_map[new_fd] = new_slot_idx;
        } else {
            new_fd = tn_fd_alloc(base, new_slot_idx);
        }

        if (new_fd < 0) {
            tn_slot_free(d, new_slot_idx);
            tn_accept_entry_free(ent, TRUE);
            imsg->result = -1;
            imsg->err_no = EMFILE;
            return 0;
        }

        new_slot->tcp_state = ent->peer_closed ? TN_TCP_STATE_PEER_CLOSED : TN_TCP_STATE_ESTABLISHED;
        new_slot->tcp_pcb   = new_pcb;

        /* Transfer early data queue */
        new_slot->rx_head  = ent->early_rx_head;
        new_slot->rx_tail  = ent->early_rx_tail;
        new_slot->rx_count = ent->early_rx_count;

        /* Prevent double-freeing packets upon freeing ent */
        ent->early_rx_head = NULL;
        ent->early_rx_tail = NULL;
        ent->early_rx_count = 0;
        ent->new_pcb = NULL; /* Ownership transferred */

        /* TNET-115: disable Nagle on accepted connections (accept queue path) */
        tcp_nagle_disable(new_pcb);
        new_slot->opt_nodelay = TRUE;

        /* z.ai step 7g item 2: inherit the LISTENING slot's stored socket
         * options (em dash) accepted pcbs come from tcp_pcb_listen which never
 * got the keep-star and mss writes (out of bounds there). */
        new_slot->opt_keepalive = slot->opt_keepalive;
        new_slot->opt_linger    = slot->opt_linger;
        new_slot->opt_sndbuf    = slot->opt_sndbuf;
        new_slot->opt_rcvbuf    = slot->opt_rcvbuf;
        new_slot->opt_oobinline = slot->opt_oobinline;
        new_slot->opt_mss       = slot->opt_mss;
        new_slot->opt_keepidle  = slot->opt_keepidle;
        new_slot->opt_keepintvl = slot->opt_keepintvl;
        new_slot->opt_keepcnt   = slot->opt_keepcnt;
        if (slot->opt_keepidle > 0) {
            new_pcb->keep_idle  = (u32_t)slot->opt_keepidle * 1000UL;
        }
        if (slot->opt_keepintvl > 0) {
            new_pcb->keep_intvl = (u32_t)slot->opt_keepintvl * 1000UL;
        }
        if (slot->opt_keepcnt > 0) {
            new_pcb->keep_cnt   = (u32_t)slot->opt_keepcnt;
        }
        if (slot->opt_mss > 0 && (u16_t)slot->opt_mss < new_pcb->mss) {
            new_pcb->mss = (u16_t)slot->opt_mss;
        }

        tcp_arg(new_pcb, (void *)(intptr_t)new_slot_idx);
        tcp_recv(new_pcb, tn_tcp_recv_cb);
        tcp_sent(new_pcb, tn_tcp_sent_cb);
        tcp_err(new_pcb, tn_tcp_err_cb);

        /* TNET-139/4.5: byte-wise, truncating store into client memory */
        tn_store_client_sockaddr(addr, addrlen, new_pcb->remote_port,
                                 ip_2_ip4(&new_pcb->remote_ip)->addr);

        tn_record_socket_event(d, new_slot, FD_WRITE);
        if (new_slot->rx_count > 0 || new_slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
            tn_record_socket_event(d, new_slot, FD_READ);
            if (new_slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
                tn_record_socket_event(d, new_slot, FD_CLOSE);
            }
        }

        FreeVec(ent);

        imsg->result = new_fd;
        imsg->err_no = 0;
        return 0;
    }

    if (slot->is_nonblocking) {
        imsg->result = -1;
        imsg->err_no = EWOULDBLOCK;
        return 0;
    }

    /* Wait for incoming connection */
    slot->pending_accept_msg = imsg;
    return 1; /* TN_IPC_DEFER */
}

int tn_ipc_cmd_connect(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const void *sin_ptr = (const void *)imsg->ptrs[0];
    LONG namelen = imsg->args[1];
    uint32_t sin_addr = 0;
    u16_t sin_port = 0;
    u16_t sin_family = 0;
    (void)d;

    if (slot == NULL || sin_ptr == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }
    /* 4.4: 4.4BSD sockargs/in_pcbconnect - short name is EINVAL */
    if (namelen < (LONG)sizeof(struct sockaddr_in)) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    /* TNET-139: client buffer — byte-wise load */
    tn_sockin_load_bytes(sin_ptr, &sin_family, &sin_port, &sin_addr);

    if (slot->type == SOCK_STREAM) {
        ip_addr_t dst_ip;
        u16_t dst_port = sin_port;
        err_t cerr;
        LONG eno = 0;

        /* 4.4/4.1: every state without a fresh pcb answers here - the old
         * fall-through returned 0 ("connected") for a dead pcb. LISTENING:
         * 4.4BSD soconnect() refuses SO_ACCEPTCONN sockets with EOPNOTSUPP
         * (and the listen pcb must not see tcp_recv/tcp_sent). */
        switch (slot->tcp_state) {
        case TN_TCP_STATE_LISTENING:   eno = EOPNOTSUPP; break;
        case TN_TCP_STATE_CONNECTING:  eno = EALREADY;   break;
        case TN_TCP_STATE_ESTABLISHED:
        case TN_TCP_STATE_PEER_CLOSED: eno = EISCONN;    break;
        case TN_TCP_STATE_ERROR:
            /* report the async failure (non-blocking connect poll) once */
            eno = (slot->last_error != 0) ? slot->last_error : EINVAL;
            slot->last_error = 0;
            break;
        default:
            if (slot->tcp_pcb == NULL) eno = EINVAL; /* after SHUT_RDWR */
            else if (sin_family != AF_INET) eno = EAFNOSUPPORT;
            break;
        }
        if (eno != 0) {
            imsg->result = -1;
            imsg->err_no = eno;
            return 0;
        }

        ip_addr_set_ip4_u32(&dst_ip, sin_addr);

        slot->tcp_state = TN_TCP_STATE_CONNECTING;
        if (!slot->is_nonblocking) {
            slot->pending_connect_msg = imsg;
        } else {
            slot->pending_connect_msg = NULL;
        }

        tcp_recv(slot->tcp_pcb, tn_tcp_recv_cb);
        tcp_sent(slot->tcp_pcb, tn_tcp_sent_cb);
        cerr = tcp_connect(slot->tcp_pcb, &dst_ip, dst_port, tn_tcp_connected_cb);
        if (cerr == ERR_OK) {
            tcp_output(slot->tcp_pcb);
            tn_drain_loopback();
        }

        if (cerr != ERR_OK) {
            slot->pending_connect_msg = NULL;
            slot->tcp_state           = TN_TCP_STATE_CLOSED;
            imsg->result = -1;
            switch (cerr) {
            case ERR_RTE: imsg->err_no = ENETUNREACH;  break;
            case ERR_USE: imsg->err_no = EADDRINUSE;   break;
            case ERR_BUF:
            case ERR_MEM: imsg->err_no = ENOBUFS;      break;
            case ERR_ARG:
            case ERR_VAL: imsg->err_no = EINVAL;       break;
            default:      imsg->err_no = ECONNREFUSED; break;
            }
            return 0;
        }

        if (slot->is_nonblocking) {
            imsg->result = -1;
            imsg->err_no = EINPROGRESS;
            return 0;
        }

        return 1; /* TN_IPC_DEFER */
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        ip_addr_t dst_ip;
        u16_t dst_port = sin_port;
        ip_addr_set_ip4_u32(&dst_ip, sin_addr);
        udp_connect(slot->udp_pcb, &dst_ip, dst_port);
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        ip_addr_t dst_ip;
        ip_addr_set_ip4_u32(&dst_ip, sin_addr);
        raw_connect(slot->raw_pcb, &dst_ip);
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    /* 4.4: no pcb behind the slot - never report success */
    imsg->result = -1;
    imsg->err_no = EINVAL;
    return 0;
}

/* 4.2 / TNET-159: TCP stream send shared by send() and sendmsg(). Data is
 * buf, or the client iovec array iov[iovcnt] when iov != NULL (client
 * memory stays valid while the caller is blocked, parked included); len
 * is the total. Returns 1 (TN_IPC_DEFER) when a blocking send parks. */
int tn_tcp_send_stream(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot,
                       const char *buf, const struct iovec *iov, ULONG iovcnt,
                       LONG len, LONG flags)
{
    err_t werr;
    LONG done = 0;
    BOOL nonblock = slot->is_nonblocking || (flags & MSG_DONTWAIT) != 0;

    /* 4.4BSD: send() in CLOSE_WAIT (peer sent FIN, ours not sent yet)
     * still works - a server that reads the request to EOF and then
     * replies depends on it. EPIPE only after OUR shutdown(SHUT_WR)
     * or a hard error; ENOTCONN for states without a live circuit.
     * (shut_wr first: after a clean close the pcb is gone too.) */
    if (slot->shut_wr) {
        imsg->result = -1;
        imsg->err_no = EPIPE;
        return 0;
    }
    if (slot->tcp_state == TN_TCP_STATE_ERROR) {
        /* z.ai step 4 item 2: report the captured lwIP error (usually
         * ECONNRESET/RST) once, then plain EPIPE afterwards. */
        imsg->result = -1;
        if (slot->last_error != 0) {
            imsg->err_no = slot->last_error;
            slot->last_error = 0;
        } else {
            imsg->err_no = EPIPE;
        }
        return 0;
    }
    if (slot->tcp_pcb == NULL ||
        (slot->tcp_state != TN_TCP_STATE_ESTABLISHED &&
         slot->tcp_state != TN_TCP_STATE_PEER_CLOSED)) {
        imsg->result = -1;
        imsg->err_no = ENOTCONN;
        return 0;
    }
    /* 4.2: one parked send per socket; nothing may overtake it */
    if (slot->pending_send_msg != NULL) {
        imsg->result = -1;
        imsg->err_no = nonblock ? EWOULDBLOCK : EALREADY;
        return 0;
    }

    /* 4.2: BSD sosend - a blocking send queues ALL of buf (waiting for
     * space), a non-blocking one queues what fits (EWOULDBLOCK if
     * nothing). The drains below can free space (loopback ACKs), so
     * keep writing while that makes progress. */
    for (;;) {
        LONG before = done;
        werr = tn_tcp_write_some(slot->tcp_pcb, buf, iov, iovcnt, len, &done);
        if (done > before) {
            tcp_output(slot->tcp_pcb);
            tn_drain_loopback();
            /* z.ai step 7 item 4: the drain can RST the pcb (slot->tcp_pcb
             * becomes NULL via the error callback) — re-check before the
             * second output. */
            if (slot->tcp_pcb == NULL) break;
            /* TNET-115: second output after drain clears Nagle blocks */
            tcp_output(slot->tcp_pcb);
            tn_drain_loopback();
            if (slot->tcp_pcb == NULL) break;
        }
        if (werr != ERR_MEM || done == before) break;
    }

    if (slot->tcp_pcb == NULL) {
        imsg->result = (done > 0) ? done : -1;
        imsg->err_no = (done > 0) ? 0 : ECONNRESET;
        return 0;
    }
    if (werr == ERR_OK || (werr == ERR_MEM && nonblock && done > 0)) {
        imsg->result = done;
        imsg->err_no = 0;
        return 0;
    }
    if (werr != ERR_MEM) {
        imsg->result = (done > 0) ? done : -1;
        imsg->err_no = (done > 0) ? 0 : tn_tcp_write_errno(werr);
        return 0;
    }
    if (nonblock) {
        imsg->result = -1;
        imsg->err_no = EWOULDBLOCK;
        return 0;
    }

    /* 4.2: blocking and no room - park until tn_tcp_sent_cb / the poll
     * retry frees space; SO_SNDTIMEO bounds the wait like the recv
     * deadline (tn_slot_check_recv_timeouts). */
    slot->pending_send_msg = imsg;
    slot->send_done = done;
    slot->send_iov = iov;       /* TNET-159: cursor source for the resume */
    slot->send_iovcnt = iovcnt;
    imsg->args[1] = len;        /* tn_tcp_resume_send reads the total here */
    if (slot->opt_sndtimeo.tv_secs > 0 || slot->opt_sndtimeo.tv_micro > 0) {
        uint32_t ms = (uint32_t)slot->opt_sndtimeo.tv_secs * 1000 +
                      (uint32_t)(slot->opt_sndtimeo.tv_micro + 999) / 1000;
        uint32_t ticks = (ms + 99) / 100;
        if (ticks == 0) ticks = 1;
        slot->send_deadline_tick = (d != NULL) ? (d->mainloop_ticks + ticks) : 0;
        if (slot->send_deadline_tick == 0) slot->send_deadline_tick = 1;
    } else {
        slot->send_deadline_tick = 0;
    }
    tcp_poll(slot->tcp_pcb, tn_tcp_poll_cb, 1);
    return 1; /* TN_IPC_DEFER */
}

int tn_ipc_cmd_send(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const void *buf = (const void *)imsg->ptrs[0];
    LONG len = imsg->args[1];
    LONG flags = imsg->args[2];

    if (slot == NULL || buf == NULL || len < 0) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (slot->type == SOCK_STREAM) {
        return tn_tcp_send_stream(d, imsg, slot, (const char *)buf, NULL, 0, len, flags);
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        struct pbuf *p;
        u16_t send_len;

        /* [auto] z.ai step 4 item 3 - CORRECTION of the earlier decision:
         * 4.4BSD-Lite sosend() checks 'not connected && !PR_CONNREQUIRED &&
         * no address -> EDESTADDRREQ' BEFORE udp_output. ENOTCONN was the
         * wrong errno (that is the TCP/PR_CONNREQUIRED answer). */
        if (ip_addr_get_ip4_u32(&slot->udp_pcb->remote_ip) == 0 || slot->udp_pcb->remote_port == 0) {
            imsg->result = -1;
            imsg->err_no = EDESTADDRREQ;
            return 0;
        }
        if (len > 65507) { /* 65535 - 8 UDP - 20 IP */
            imsg->result = -1;
            imsg->err_no = EMSGSIZE;
            return 0;
        }

        send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
        p = pbuf_alloc(PBUF_TRANSPORT, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }

        pbuf_take(p, buf, send_len);
        {
            err_t serr = udp_sendto(slot->udp_pcb, p, &slot->udp_pcb->remote_ip,
                                    slot->udp_pcb->remote_port);
            pbuf_free(p);
            /* 4.10: same err_t -> errno map as sendto (ipc_dgram.c) */
            if (serr != ERR_OK) {
                imsg->result = -1;
                imsg->err_no = (serr == ERR_MEM) ? ENOBUFS : EHOSTUNREACH;
                return 0;
            }
        }
        tn_drain_loopback();

        imsg->result = (LONG)send_len;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        struct pbuf *p;
        u16_t send_len;
        err_t serr;

        /* [auto] z.ai step 4 item 3: EDESTADDRREQ, not ENOTCONN */
        if (ip_addr_get_ip4_u32(&slot->raw_pcb->remote_ip) == 0) {
            imsg->result = -1;
            imsg->err_no = EDESTADDRREQ;
            return 0;
        }
        if (len > 65535) {
            imsg->result = -1;
            imsg->err_no = EMSGSIZE;
            return 0;
        }

        send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
        p = pbuf_alloc(PBUF_IP, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }
        pbuf_take(p, buf, send_len);

        serr = raw_send(slot->raw_pcb, p);
        pbuf_free(p);

        if (serr != ERR_OK) {
            imsg->result = -1;
            imsg->err_no = (serr == ERR_MEM) ? ENOBUFS : EHOSTUNREACH;
            return 0;
        }
        imsg->result = (LONG)send_len;
        imsg->err_no = 0;
        return 0;
    }

    imsg->result = -1;
    imsg->err_no = EOPNOTSUPP;
    return 0;
}

int tn_ipc_cmd_recv(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    void *buf = imsg->ptrs[0];
    LONG len = imsg->args[1];
    LONG flags = imsg->args[2];
    (void)d;

    if (slot == NULL || buf == NULL || len < 0) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (flags & MSG_OOB) {
        imsg->result = -1;
        imsg->err_no = EOPNOTSUPP;
        return 0;
    }

    if (slot->type == SOCK_STREAM) {
        u16_t req_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
        if (slot->rx_head != NULL) {
            if (flags & MSG_PEEK) {
                TnRxPacket *cur = slot->rx_head;
                u16_t copied = 0;
                while (cur != NULL && copied < req_len) {
                    u16_t off = (cur == slot->rx_head) ? cur->offset : 0;
                    u16_t avail = cur->p->tot_len - off;
                    u16_t chunk = (avail < (req_len - copied)) ? avail : (req_len - copied);
                    pbuf_copy_partial(cur->p, (char *)buf + copied, chunk, off);
                    copied += chunk;
                    cur = cur->next;
                }
                imsg->result = (LONG)copied;
                imsg->err_no = 0;
                return 0;
            } else {
                TnRxPacket *pkt = slot->rx_head;
                u16_t avail = pkt->p->tot_len - pkt->offset;
                u16_t to_copy = (avail < req_len) ? avail : req_len;

                pbuf_copy_partial(pkt->p, buf, to_copy, pkt->offset);
                pkt->offset += to_copy;

                if (slot->tcp_pcb != NULL) {
                    tcp_recved(slot->tcp_pcb, to_copy);
                }

                if (pkt->offset >= pkt->p->tot_len) {
                    slot->rx_head = pkt->next;
                    if (slot->rx_head == NULL) {
                        slot->rx_tail = NULL;
                    }
                    pbuf_free(pkt->p);
                    tn_rxpkt_put(pkt);
                    if (slot->rx_count > 0) {
                        slot->rx_count--;
                    }
                }

                imsg->result = (LONG)to_copy;
                imsg->err_no = 0;
                return 0;
            }
        } else if (slot->shut_rd || slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
            imsg->result = 0; /* EOF */
            imsg->err_no = 0;
            return 0;
        } else if (slot->tcp_state == TN_TCP_STATE_ERROR) {
            /* 4.3: the mapped lwIP reason (once), else ECONNRESET */
            imsg->result = -1;
            imsg->err_no = (slot->last_error != 0) ? slot->last_error : ECONNRESET;
            slot->last_error = 0;
            return 0;
        } else if (slot->tcp_state != TN_TCP_STATE_CONNECTING &&
                   slot->tcp_state != TN_TCP_STATE_ESTABLISHED) {
            /* 4.4BSD soreceive: CLOSED or LISTENING has no circuit to wait
             * for - parked forever before (z.ai step 4 item 1). */
            imsg->result = -1;
            imsg->err_no = ENOTCONN;
            return 0;
        } else {
            if (slot->is_nonblocking || (flags & MSG_DONTWAIT)) {
                imsg->result = -1;
                imsg->err_no = EWOULDBLOCK;
                return 0;
            }
            return tn_slot_park_recv(d, slot, imsg);
        }
    } else if (slot->type == SOCK_DGRAM || slot->type == SOCK_RAW) {
        if (slot->rx_head != NULL) {
            u16_t req_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
            TnRxPacket *pkt = slot->rx_head;
            u16_t avail = pkt->p->tot_len;
            u16_t to_copy = (avail < req_len) ? avail : req_len;

            pbuf_copy_partial(pkt->p, buf, to_copy, 0);

            if (!(flags & MSG_PEEK)) {
                slot->rx_head = pkt->next;
                if (slot->rx_head == NULL) {
                    slot->rx_tail = NULL;
                }
                pbuf_free(pkt->p);
                tn_rxpkt_put(pkt);
                if (slot->rx_count > 0) {
                    slot->rx_count--;
                }
            }

            imsg->result = (LONG)to_copy;
            imsg->err_no = 0;
            return 0;
        } else {
            if (slot->is_nonblocking || (flags & MSG_DONTWAIT)) {
                imsg->result = -1;
                imsg->err_no = EWOULDBLOCK;
                return 0;
            }
            return tn_slot_park_recv(d, slot, imsg);
        }
    }

    imsg->result = -1;
    imsg->err_no = EOPNOTSUPP;
    return 0;
}

int tn_ipc_cmd_shutdown(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG how = imsg->args[1];
    int shut_rx, shut_tx;
    err_t serr;
    (void)d;

    if (slot == NULL || slot->type != 1 /* TCP */) {
        imsg->result = -1;
        imsg->err_no = ENOTCONN;
        return 0;
    }
    if (how < 0 || how > 2) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }
    /* 4.1: a listener has no connection to shut down - and its pcb is a
     * tcp_pcb_listen that tcp_recv/sent/err/tcp_shutdown must not touch */
    if (slot->tcp_state == TN_TCP_STATE_LISTENING) {
        imsg->result = -1;
        imsg->err_no = ENOTCONN;
        return 0;
    }

    shut_rx = (how == 0 || how == 2) ? 1 : 0;
    shut_tx = (how == 1 || how == 2) ? 1 : 0;

    if (shut_rx) slot->shut_rd = TRUE;
    if (shut_tx) slot->shut_wr = TRUE;

    /* Second shutdown() (or one after the pcb left) is 4.4BSD-quiet: the
     * flags above are already set, nothing to drive. */
    if (slot->tcp_pcb == NULL || slot->tcp_state == TN_TCP_STATE_CLOSED) {
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    if (shut_rx && shut_tx) {
        /* SHUT_RDWR: tcp_shutdown(pcb,1,1) is tcp_close() and can FREE the
         * pcb (and would send RST if rx data is unread). Detach the slot
         * BEFORE closing, drain the rx queue + tcp_recved so the FIN path
         * (not RST) is taken, then close on an unrefenced pcb. (z.ai step 4
         * item 2: the old order called tcp_arg/recv/sent/err on the freed
         * pcb.) */
        tn_rx_queue_drain_with_recved(slot);
        tcp_arg(slot->tcp_pcb, NULL);
        tcp_recv(slot->tcp_pcb, NULL);
        tcp_sent(slot->tcp_pcb, NULL);
        tcp_err(slot->tcp_pcb, NULL);
        tcp_poll(slot->tcp_pcb, NULL, 0);
        /* capture before close: tcp_close may free the pcb */
        {
            struct tcp_pcb *closing = slot->tcp_pcb;
            slot->tcp_pcb = NULL;
            slot->tcp_state = TN_TCP_STATE_CLOSED;
            serr = tcp_close(closing);
            if (serr != ERR_OK) {
                /* 3.9: ERR_MEM leaves the pcb alive with no owner - abort
                 * (RST) instead of orphaning it; callbacks are detached */
                tcp_abort(closing);
            }
        }
        /* 4.2: a parked send of this socket can never complete now */
        tn_slot_reply_send(slot, EPIPE, 1);
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    if (!shut_tx) {
        /* 4.7: SHUT_RD alone stays local (BSD sorflush): drop what is
         * queued, return the window, and discard later data in recv_cb.
         * tcp_shutdown(rx) would make lwIP RST on the next segment. */
        tn_rx_queue_drain_with_recved(slot);
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    serr = tcp_shutdown(slot->tcp_pcb, 0, 1);
    if (serr != ERR_OK) {
        imsg->result = -1;
        imsg->err_no = ECONNRESET;
        return 0;
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}