/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Socket Slot Table & Queue Management Implementation.
 *
 * ROUND4b §B (Modular Daemon Refactor).
 */
#include "slot_table.h"
#include <string.h>
/* Same Amiga/host split as task_ctx.h — on the host the mock_lwip.h
 * AllocVec/FreeVec macros come in via slot_table.h. */
#if defined(__AMIGA__) || defined(__amigaos__) || defined(TN_AMIGA_BUILD)
#include <proto/exec.h>
#endif

/* TNET-107: single-threaded LIFO freelist for TnRxPacket. The daemon is
 * the only producer and consumer, so no locking is needed. Bounded to
 * TN_RXFREELIST_MAX entries; excess frees go back to AllocVec. */
#define TN_RXFREELIST_MAX 128
static TnRxPacket *g_rxfreelist = NULL;
static int g_rxfreelist_count = 0;

TnRxPacket *tn_rxpkt_get(void)
{
    TnRxPacket *pkt;
    if (g_rxfreelist != NULL) {
        pkt = g_rxfreelist;
        g_rxfreelist = pkt->next;
        g_rxfreelist_count--;
        memset(pkt, 0, sizeof(TnRxPacket));
        return pkt;
    }
    return (TnRxPacket *)AllocVec(sizeof(TnRxPacket), MEMF_PUBLIC | MEMF_CLEAR);
}

void tn_rxpkt_put(TnRxPacket *pkt)
{
    if (pkt == NULL) return;
    if (g_rxfreelist_count < TN_RXFREELIST_MAX) {
        pkt->next = g_rxfreelist;
        g_rxfreelist = pkt;
        g_rxfreelist_count++;
        return;
    }
    FreeVec(pkt);
}

void tn_rxpkt_fini(void)
{
    while (g_rxfreelist != NULL) {
        TnRxPacket *next = g_rxfreelist->next;
        FreeVec(g_rxfreelist);
        g_rxfreelist = next;
    }
    g_rxfreelist_count = 0;
}

void tn_slot_table_init(TnDaemon *d)
{
    if (d == NULL) return;
    memset(d->sockets, 0, sizeof(d->sockets));
    /* Always a fresh allocation — callers may pass an uninitialized TnDaemon
     * (host tests use stack locals; the daemon's g_daemon is a zeroed global,
     * so this is simply its one and only allocation). NULL only under
     * catastrophic memory pressure; the daemon startup checks for it. */
    d->selectors = (TnSelector *)AllocVec(TN_MAX_SELECTORS * sizeof(TnSelector),
                                          MEMF_PUBLIC | MEMF_CLEAR);
    d->max_selectors = (d->selectors != NULL) ? TN_MAX_SELECTORS : 0;
    d->selector_count = 0;
    d->next_park_id = 1;
}

BOOL tn_selector_table_grow(TnDaemon *d, uint32_t new_max)
{
    TnSelector *grown;

    if (d == NULL || d->selectors == NULL) return FALSE;
    if (new_max <= d->max_selectors) return TRUE;
    if (new_max > 128) new_max = 128;

    grown = (TnSelector *)AllocVec((ULONG)new_max * sizeof(TnSelector),
                                   MEMF_PUBLIC | MEMF_CLEAR);
    if (grown == NULL) return FALSE;

    memcpy(grown, d->selectors, (size_t)d->max_selectors * sizeof(TnSelector));
    FreeVec(d->selectors);
    d->selectors = grown;
    d->max_selectors = new_max;
    return TRUE;
}

void tn_selector_table_free(TnDaemon *d)
{
    if (d == NULL || d->selectors == NULL) return;
    FreeVec(d->selectors);
    d->selectors = NULL;
    d->max_selectors = 0;
    d->selector_count = 0;
}

int tn_slot_live_count(const TnDaemon *d)
{
    int count = 0;
    int i;
    if (d == NULL) return 0;
    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        if (d->sockets[i].in_use) {
            count++;
        }
    }
    return count;
}

static void tn_init_socket_slot(TnSocketSlot *s, TnSocketBase *base, struct Task *task,
                                int domain, int type, int protocol)
{
    s->in_use              = TRUE;
    s->owner_base          = base;
    s->owner_task          = task;
    s->domain              = domain;
    s->type                = type;
    s->protocol            = protocol;
    s->tcp_state           = TN_TCP_STATE_CLOSED;
    s->is_nonblocking      = FALSE;
    /* 17e0e53 regression: these two were the only new TnSocketSlot fields
     * the init missed - a reused slot kept shut_wr=TRUE from its previous
     * life and the next owner's first send() got EPIPE (bsdsocktest #36) */
    s->shut_wr             = FALSE;
    s->shut_rd             = FALSE;

    /* Level SOL_SOCKET options */
    s->opt_broadcast       = FALSE;
    s->opt_reuseaddr       = FALSE;
    s->opt_keepalive       = FALSE;
    s->opt_oobinline       = FALSE;
    s->opt_linger.l_onoff  = 0;
    s->opt_linger.l_linger = 0;
    s->opt_sndbuf          = TCP_SND_BUF;
    s->opt_rcvbuf          = TCP_WND;
    s->opt_rcvtimeo.tv_secs  = 0;
    s->opt_rcvtimeo.tv_micro = 0;
    s->opt_sndtimeo.tv_secs  = 0;
    s->opt_sndtimeo.tv_micro = 0;

    /* Level IPPROTO_TCP options */
    s->opt_nodelay         = FALSE;
    s->opt_keepidle        = 7200;
    s->opt_keepintvl       = 75;
    s->opt_keepcnt         = 9;

    /* Level IPPROTO_IP options */
    s->opt_tos             = 0;
    s->opt_ttl             = 64;
    s->opt_hdrincl         = (protocol == IPPROTO_RAW);
    s->opt_multicast_ttl   = 1;
    s->opt_multicast_loop  = 1;

    s->last_error          = 0;
    s->rx_count            = 0;
    s->ref_count           = 1;
    s->udp_pcb             = NULL;
    s->tcp_pcb             = NULL;
    s->raw_pcb             = NULL;
    s->rx_head             = NULL;
    s->rx_tail             = NULL;
    s->accept_head         = NULL;
    s->accept_tail         = NULL;
    s->accept_count        = 0;
    s->pending_connect_msg = NULL;
    s->pending_accept_msg  = NULL;
    s->pending_recv_msg    = NULL;
    s->recv_deadline_tick  = 0;
    s->park_id             = 0;
    s->is_parked           = FALSE;
}

TnSocketSlot *tn_slot_alloc(TnDaemon *d, TnSocketBase *base, struct Task *task,
                           int domain, int type, int protocol, int *out_slot_idx)
{
    int slot_idx;
    if (d == NULL) return NULL;

    for (slot_idx = 0; slot_idx < TN_MAX_GLOBAL_SOCKETS; slot_idx++) {
        if (!d->sockets[slot_idx].in_use) {
            tn_init_socket_slot(&d->sockets[slot_idx], base, task, domain, type, protocol);
            if (out_slot_idx != NULL) {
                *out_slot_idx = slot_idx;
            }
            return &d->sockets[slot_idx];
        }
    }
    return NULL;
}

int tn_fd_alloc(TnSocketBase *base, int slot_idx)
{
    int fd;
    if (base == NULL || base->fd_map == NULL || slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        return -1;
    }
    for (fd = 0; fd < base->dtablesize; fd++) {
        if (base->fd_map[fd] == -1) {
            base->fd_map[fd] = slot_idx;
            return fd;
        }
    }
    return -1;
}

TnSocketSlot *tn_slot_lookup(TnDaemon *d, const TnSocketBase *base, int fd, int *out_slot_idx)
{
    int slot_idx;
    TnSocketSlot *slot;

    if (d == NULL || base == NULL || fd < 0 || fd >= base->dtablesize) {
        return NULL;
    }
    if (base->fd_map == NULL) return NULL;
    slot_idx = base->fd_map[fd];
    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        return NULL;
    }
    slot = &d->sockets[slot_idx];
    if (!slot->in_use) {
        return NULL;
    }
    if (out_slot_idx != NULL) {
        *out_slot_idx = slot_idx;
    }
    return slot;
}

void tn_slot_ref(TnSocketSlot *slot)
{
    if (slot != NULL && slot->in_use) {
        slot->ref_count++;
    }
}

void tn_slot_free(TnDaemon *d, int slot_idx)
{
    TnSocketSlot *slot;
    if (d == NULL || slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return;
    slot = &d->sockets[slot_idx];
    if (!slot->in_use) return;

    if (slot->udp_pcb != NULL) {
        udp_remove(slot->udp_pcb);
        slot->udp_pcb = NULL;
    }
    if (slot->tcp_pcb != NULL) {
        tcp_arg(slot->tcp_pcb, NULL);
        tcp_recv(slot->tcp_pcb, NULL);
        tcp_err(slot->tcp_pcb, NULL);
        tcp_accept(slot->tcp_pcb, NULL);
        tcp_close(slot->tcp_pcb);
        slot->tcp_pcb = NULL;
    }
    if (slot->raw_pcb != NULL) {
        raw_remove(slot->raw_pcb);
        slot->raw_pcb = NULL;
    }

    tn_rx_queue_drain(slot);
    tn_accept_queue_drain(slot);

    if (slot->pending_connect_msg != NULL) {
        TnIpcMsg *cmsg = slot->pending_connect_msg;
        slot->pending_connect_msg = NULL;
        cmsg->result = -1;
        cmsg->err_no = EBADF;
        ReplyMsg((struct Message *)cmsg);
    }
    if (slot->pending_accept_msg != NULL) {
        TnIpcMsg *amsg = slot->pending_accept_msg;
        slot->pending_accept_msg = NULL;
        amsg->result = -1;
        amsg->err_no = EBADF;
        ReplyMsg((struct Message *)amsg);
    }
    if (slot->pending_recv_msg != NULL) {
        TnIpcMsg *rmsg = slot->pending_recv_msg;
        slot->pending_recv_msg = NULL;
        slot->recv_deadline_tick = 0;
        rmsg->result = -1;
        rmsg->err_no = EBADF;
        ReplyMsg((struct Message *)rmsg);
    }

    slot->rx_tail             = NULL;
    slot->rx_count            = 0;
    slot->ref_count           = 0;
    slot->park_id             = 0;
    slot->is_parked           = FALSE;
    slot->in_use              = FALSE;
    slot->owner_base          = NULL;
    slot->owner_task          = NULL;
}

void tn_slot_unref(TnDaemon *d, int slot_idx)
{
    TnSocketSlot *slot;
    if (d == NULL || slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return;
    slot = &d->sockets[slot_idx];
    if (!slot->in_use) return;

    if (slot->ref_count > 0) {
        slot->ref_count--;
    }
    if (slot->ref_count == 0) {
        tn_slot_free(d, slot_idx);
    }
}

int tn_rx_queue_push(TnSocketSlot *slot, struct pbuf *p, const ip_addr_t *src_ip, u16_t src_port)
{
    TnRxPacket *pkt;
    if (slot == NULL || !slot->in_use || p == NULL) return -1;
    if (slot->rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        return -1;
    }

    pkt = tn_rxpkt_get();
    if (pkt == NULL) return -1;

    pkt->p = p;
    pkt->offset = 0;
    if (src_ip != NULL) {
        pkt->src_ip = *src_ip;
    }
    pkt->src_port = src_port;
    pkt->next = NULL;

    if (slot->rx_tail != NULL) {
        slot->rx_tail->next = pkt;
    } else {
        slot->rx_head = pkt;
    }
    slot->rx_tail = pkt;
    slot->rx_count++;
    if (slot->rx_count > g_daemon.rx_high_water) {
        g_daemon.rx_high_water = slot->rx_count;
    }
    return 0;
}

TnRxPacket *tn_rx_queue_pop(TnSocketSlot *slot)
{
    TnRxPacket *pkt;
    if (slot == NULL || !slot->in_use || slot->rx_head == NULL) return NULL;
    pkt = slot->rx_head;
    slot->rx_head = pkt->next;
    if (slot->rx_head == NULL) {
        slot->rx_tail = NULL;
    }
    pkt->next = NULL;
    if (slot->rx_count > 0) {
        slot->rx_count--;
    }
    return pkt;
}

void tn_rx_queue_drain(TnSocketSlot *slot)
{
    if (slot == NULL) return;
    while (slot->rx_head != NULL) {
        TnRxPacket *pkt = slot->rx_head;
        slot->rx_head = pkt->next;
        if (pkt->p != NULL) {
            pbuf_free(pkt->p);
        }
        tn_rxpkt_put(pkt);
    }
    slot->rx_tail = NULL;
    slot->rx_count = 0;
}

/* z.ai step 4 item 2: drain AND return the receive window - unread rx data
 * at tcp_close makes lwIP send RST instead of FIN. */
void tn_rx_queue_drain_with_recved(TnSocketSlot *slot)
{
    if (slot == NULL) return;
    while (slot->rx_head != NULL) {
        TnRxPacket *pkt = slot->rx_head;
        if (pkt->p != NULL && slot->tcp_pcb != NULL) {
            tcp_recved(slot->tcp_pcb, pkt->p->tot_len - pkt->offset);
        }
        slot->rx_head = pkt->next;
        if (pkt->p != NULL) {
            pbuf_free(pkt->p);
        }
        tn_rxpkt_put(pkt);
    }
    slot->rx_tail = NULL;
    slot->rx_count = 0;
}

static err_t tn_tcp_queued_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    TnAcceptEntry *ent = (TnAcceptEntry *)arg;
    (void)err;

    if (ent == NULL || ent->new_pcb != pcb) {
        if (p != NULL) pbuf_free(p);
        return ERR_OK;
    }

    if (p == NULL) {
        /* Peer closed connection (FIN) before accept() */
        ent->peer_closed = TRUE;
        return ERR_OK;
    }

    if (ent->early_rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        /* Queue full: hold pbuf in lwIP without freeing */
        return ERR_MEM;
    }

    TnRxPacket *pkt = tn_rxpkt_get();
    if (pkt == NULL) {
        return ERR_MEM;
    }

    pkt->p = p;
    pkt->offset = 0;
    pkt->src_port = 0;
    pkt->next = NULL;

    if (ent->early_rx_tail != NULL) {
        ent->early_rx_tail->next = pkt;
    } else {
        ent->early_rx_head = pkt;
    }
    ent->early_rx_tail = pkt;
    ent->early_rx_count++;
    return ERR_OK;
}

static void tn_tcp_queued_err_cb(void *arg, err_t err)
{
    TnAcceptEntry *ent = (TnAcceptEntry *)arg;
    (void)err;

    if (ent == NULL) return;

    /* lwIP frees the PCB right after/during err callback.
     * We MUST NOT call tcp_abort or touch new_pcb further. */
    ent->new_pcb = NULL;

    if (ent->listening_slot != NULL) {
        tn_accept_queue_remove(ent->listening_slot, ent);
    } else {
        tn_accept_entry_free(ent, FALSE);
    }
}

void tn_accept_entry_free(TnAcceptEntry *ent, BOOL abort_pcb)
{
    if (ent == NULL) return;
    if (abort_pcb && ent->new_pcb != NULL) {
        tcp_arg(ent->new_pcb, NULL);
        tcp_recv(ent->new_pcb, NULL);
        tcp_err(ent->new_pcb, NULL);
        tcp_abort(ent->new_pcb);
        ent->new_pcb = NULL;
    }
    while (ent->early_rx_head != NULL) {
        TnRxPacket *pkt = ent->early_rx_head;
        ent->early_rx_head = pkt->next;
        if (pkt->p != NULL) {
            pbuf_free(pkt->p);
        }
        tn_rxpkt_put(pkt);
    }
    ent->early_rx_tail = NULL;
    ent->early_rx_count = 0;
    FreeVec(ent);
}

void tn_accept_queue_remove(TnSocketSlot *slot, TnAcceptEntry *ent)
{
    TnAcceptEntry *prev = NULL;
    TnAcceptEntry *curr;
    if (slot == NULL || ent == NULL) return;

    for (curr = slot->accept_head; curr != NULL; prev = curr, curr = curr->next) {
        if (curr == ent) {
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                slot->accept_head = curr->next;
            }
            if (slot->accept_tail == curr) {
                slot->accept_tail = prev;
            }
            if (slot->accept_count > 0) {
                slot->accept_count--;
            }
            curr->next = NULL;
            tn_accept_entry_free(curr, FALSE);
            break;
        }
    }
}

int tn_accept_queue_push(TnSocketSlot *slot, struct tcp_pcb *new_pcb)
{
    TnAcceptEntry *ent;
    if (slot == NULL || !slot->in_use || new_pcb == NULL) return -1;
    ent = (TnAcceptEntry *)AllocVec(sizeof(TnAcceptEntry), MEMF_PUBLIC | MEMF_CLEAR);
    if (ent == NULL) return -1;

    ent->new_pcb = new_pcb;
    ent->listening_slot = slot;
    ent->early_rx_head = NULL;
    ent->early_rx_tail = NULL;
    ent->early_rx_count = 0;
    ent->peer_closed = FALSE;
    ent->next = NULL;

    if (slot->accept_tail != NULL) {
        slot->accept_tail->next = ent;
    } else {
        slot->accept_head = ent;
    }
    slot->accept_tail = ent;
    slot->accept_count++;

    tcp_arg(new_pcb, ent);
    tcp_recv(new_pcb, tn_tcp_queued_recv_cb);
    tcp_err(new_pcb, tn_tcp_queued_err_cb);
    return 0;
}

TnAcceptEntry *tn_accept_queue_pop_entry(TnSocketSlot *slot)
{
    TnAcceptEntry *ent;
    if (slot == NULL || !slot->in_use || slot->accept_head == NULL) return NULL;
    ent = slot->accept_head;
    slot->accept_head = ent->next;
    if (slot->accept_head == NULL) {
        slot->accept_tail = NULL;
    }
    if (slot->accept_count > 0) {
        slot->accept_count--;
    }
    ent->next = NULL;
    return ent;
}

struct tcp_pcb *tn_accept_queue_pop(TnSocketSlot *slot)
{
    TnAcceptEntry *ent = tn_accept_queue_pop_entry(slot);
    if (ent == NULL) return NULL;
    struct tcp_pcb *pcb = ent->new_pcb;
    if (pcb != NULL) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
    }
    tn_accept_entry_free(ent, FALSE);
    return pcb;
}

void tn_accept_queue_drain(TnSocketSlot *slot)
{
    if (slot == NULL) return;
    while (slot->accept_head != NULL) {
        TnAcceptEntry *ent = slot->accept_head;
        slot->accept_head = ent->next;
        ent->next = NULL;
        tn_accept_entry_free(ent, TRUE);
    }
    slot->accept_tail = NULL;
    slot->accept_count = 0;
}

void tn_record_socket_event(TnDaemon *d, TnSocketSlot *slot, ULONG event_mask)
{
    if (d != NULL && slot != NULL && slot->in_use && slot->owner_base != NULL) {
        TnSocketBase *base = slot->owner_base;
        int slot_idx = (int)(slot - d->sockets);
        int fd;
        BOOL posted = FALSE;
        if (base->fd_map == NULL || base->events == NULL) return;

        for (fd = 0; fd < base->dtablesize; fd++) {
            if (base->fd_map[fd] == slot_idx) {
                /* TNET-122..127: SO_EVENTMASK gate — Roadshow default is 0
                 * = NO events recorded; a socket only signals when the
                 * application explicitly setsockopt()ed an event mask. */
                ULONG filter = (base->event_masks != NULL) ? base->event_masks[fd] : 0;
                ULONG effective = event_mask & filter;
                if (effective != 0) {
                    base->events[fd] |= effective;
                    posted = TRUE;
                }
            }
        }
        if (posted && base->sig_event != 0 && slot->owner_task != NULL) {
            Signal(slot->owner_task, base->sig_event);
        }
    }
}

int tn_slot_park_recv(TnDaemon *d, TnSocketSlot *slot, TnIpcMsg *imsg)
{
    if (slot == NULL || imsg == NULL) return 0;

    if (slot->pending_recv_msg != NULL) {
        if (slot->pending_recv_msg == imsg) {
            return 1; /* TN_IPC_DEFER */
        }
        imsg->result = -1;
        imsg->err_no = EALREADY;
        return 0;
    }

    slot->pending_recv_msg = imsg;

    if (slot->opt_rcvtimeo.tv_secs > 0 || slot->opt_rcvtimeo.tv_micro > 0) {
        uint32_t ms = (uint32_t)slot->opt_rcvtimeo.tv_secs * 1000 +
                      (uint32_t)(slot->opt_rcvtimeo.tv_micro + 999) / 1000;
        uint32_t ticks = (ms + 99) / 100;
        if (ticks == 0) ticks = 1;
        slot->recv_deadline_tick = (d != NULL) ? (d->mainloop_ticks + ticks) : 0;
    } else {
        slot->recv_deadline_tick = 0;
    }

    return 1; /* TN_IPC_DEFER */
}

void tn_slot_check_recv_timeouts(TnDaemon *d)
{
    int i;
    if (d == NULL) return;
    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        TnSocketSlot *slot = &d->sockets[i];
        if (slot->in_use && slot->pending_recv_msg != NULL && slot->recv_deadline_tick != 0) {
            if ((int32_t)(d->mainloop_ticks - slot->recv_deadline_tick) >= 0) {
                TnIpcMsg *imsg = slot->pending_recv_msg;
                slot->pending_recv_msg = NULL;
                slot->recv_deadline_tick = 0;
                imsg->result = -1;
                imsg->err_no = EWOULDBLOCK;
                ReplyMsg((struct Message *)imsg);
            }
        }
    }
}

void tn_recv_cancel_for_base(TnDaemon *d, TnSocketBase *base)
{
    int i;
    if (d == NULL || base == NULL) return;
    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        TnSocketSlot *slot = &d->sockets[i];
        if (slot->in_use && slot->pending_recv_msg != NULL) {
            if (slot->pending_recv_msg->socket_base == (struct Library *)base) {
                TnIpcMsg *rmsg = slot->pending_recv_msg;
                slot->pending_recv_msg = NULL;
                slot->recv_deadline_tick = 0;
                rmsg->result = -1;
                rmsg->err_no = ECONNABORTED;
                ReplyMsg((struct Message *)rmsg);
            }
        }
    }
}

void tn_slot_clear_owner_base(TnDaemon *d, const TnSocketBase *base)
{
    int i;
    if (d == NULL || base == NULL) return;
    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        if (d->sockets[i].in_use && d->sockets[i].owner_base == base) {
            d->sockets[i].owner_base = NULL;
            d->sockets[i].owner_task = NULL;
        }
    }
}
