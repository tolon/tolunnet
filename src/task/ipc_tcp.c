/*
 * tolunnet ? TCP Stream IPC Handlers & lwIP Callbacks Implementation (ipc_tcp.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_tcp.h"
#include "ipc_select.h"
#include "slot_table.h"
#include "netif_mgr.h"

err_t tn_tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    int slot_idx = (int)(intptr_t)arg;
    (void)len;
    if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS) {
        TnSocketSlot *slot = &g_daemon.sockets[slot_idx];
        if (slot->in_use && slot->tcp_pcb == pcb) {
            tn_record_socket_event(&g_daemon, slot, FD_WRITE);
        }
    }
    return ERR_OK;
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
        tn_signal_socket(&g_daemon, slot);
        tn_record_socket_event(&g_daemon, slot, FD_CLOSE | FD_READ);
        return ERR_OK;
    }

    if (tn_rx_queue_push(slot, p, NULL, 0) != 0) {
        /* Queue full or alloc fail: return ERR_MEM without freeing pbuf so lwIP holds it */
        return ERR_MEM;
    }

    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_READ);
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

    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;
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
    (void)err;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return;

    slot = &g_daemon.sockets[slot_idx];
    if (!slot->in_use) return;

    slot->tcp_state  = TN_TCP_STATE_ERROR;
    slot->tcp_pcb    = NULL; /* lwIP frees PCB before calling err_cb */
    slot->last_error = ECONNREFUSED;
    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_ERROR);

    if (slot->pending_connect_msg != NULL) {
        slot->pending_connect_msg->result = -1;
        slot->pending_connect_msg->err_no = ECONNREFUSED;
        ReplyMsg((struct Message *)slot->pending_connect_msg);
        slot->pending_connect_msg = NULL;
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
        struct sockaddr_in *addr = (struct sockaddr_in *)imsg->ptrs[0];
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
        if (client_fd >= 0 && client_fd < TN_MAX_FDS_PER_TASK && base->fd_map[client_fd] == -1) {
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

        tcp_arg(newpcb, (void *)(intptr_t)new_slot_idx);
        tcp_recv(newpcb, tn_tcp_recv_cb);
        tcp_sent(newpcb, tn_tcp_sent_cb);
        tcp_err(newpcb, tn_tcp_err_cb);

        if (addr != NULL && addrlen != NULL && *addrlen >= sizeof(struct sockaddr_in)) {
            addr->sin_len    = sizeof(struct sockaddr_in);
            addr->sin_family = AF_INET;
            addr->sin_port   = lwip_htons(newpcb->remote_port);
            addr->sin_addr.s_addr = ip_2_ip4(&newpcb->remote_ip)->addr;
            *addrlen = sizeof(struct sockaddr_in);
        }

        imsg->result = client_fd;
        imsg->err_no = 0;
        tn_record_socket_event(&g_daemon, new_slot, FD_WRITE);
        ReplyMsg((struct Message *)imsg);
        return ERR_OK;
    }

    /* Queue incoming connection into accept queue (bounded to 8) */
    if (slot->accept_count >= 8 || tn_accept_queue_push(slot, newpcb) != 0) {
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

    if (slot->tcp_state == TN_TCP_STATE_LISTENING) {
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    bl = (backlog <= 0) ? 1 : ((backlog > 8) ? 8 : (u8_t)backlog);
    lpcb = tcp_listen_with_backlog(slot->tcp_pcb, bl);
    if (lpcb == NULL) {
        imsg->result = -1;
        imsg->err_no = ENOBUFS;
        return 0;
    }

    slot_idx = (int)(slot - d->sockets);
    slot->tcp_pcb   = lpcb;
    slot->tcp_state = TN_TCP_STATE_LISTENING;
    tcp_arg(lpcb, (void *)(intptr_t)slot_idx);
    tcp_accept(lpcb, tn_tcp_accept_cb);

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_accept(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    struct sockaddr_in *addr = (struct sockaddr_in *)imsg->ptrs[0];
    socklen_t *addrlen = (socklen_t *)imsg->ptrs[1];
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    (void)d;

    if (slot == NULL || slot->tcp_state != TN_TCP_STATE_LISTENING) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    if (slot->accept_head != NULL) {
        struct tcp_pcb *new_pcb = tn_accept_queue_pop(slot);
        int new_fd = -1;
        int new_slot_idx = -1;
        TnSocketSlot *new_slot;

        new_slot = tn_slot_alloc(d, base, imsg->client_task, AF_INET, SOCK_STREAM, 0, &new_slot_idx);
        if (new_slot == NULL) {
            tcp_abort(new_pcb);
            imsg->result = -1;
            imsg->err_no = ENFILE;
            return 0;
        }

        new_fd = (int)imsg->args[3];
        if (new_fd >= 0 && new_fd < TN_MAX_FDS_PER_TASK && base->fd_map[new_fd] == -1) {
            base->fd_map[new_fd] = new_slot_idx;
        } else {
            new_fd = tn_fd_alloc(base, new_slot_idx);
        }

        if (new_fd < 0) {
            tn_slot_free(d, new_slot_idx);
            tcp_abort(new_pcb);
            imsg->result = -1;
            imsg->err_no = EMFILE;
            return 0;
        }

        new_slot->tcp_state = TN_TCP_STATE_ESTABLISHED;
        new_slot->tcp_pcb   = new_pcb;

        tcp_arg(new_pcb, (void *)(intptr_t)new_slot_idx);
        tcp_recv(new_pcb, tn_tcp_recv_cb);
        tcp_sent(new_pcb, tn_tcp_sent_cb);
        tcp_err(new_pcb, tn_tcp_err_cb);

        if (addr != NULL && addrlen != NULL && *addrlen >= sizeof(struct sockaddr_in)) {
            addr->sin_len    = sizeof(struct sockaddr_in);
            addr->sin_family = AF_INET;
            addr->sin_port   = lwip_htons(new_pcb->remote_port);
            addr->sin_addr.s_addr = ip_2_ip4(&new_pcb->remote_ip)->addr;
            *addrlen = sizeof(struct sockaddr_in);
        }

        tn_record_socket_event(d, new_slot, FD_WRITE);
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
    struct sockaddr_in *sin = (struct sockaddr_in *)imsg->ptrs[0];
    (void)d;

    if (slot == NULL || sin == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (slot->type == SOCK_STREAM && slot->tcp_pcb != NULL) {
        ip_addr_t dst_ip;
        u16_t dst_port = lwip_ntohs(sin->sin_port);
        err_t cerr;

        if (slot->tcp_state == TN_TCP_STATE_CONNECTING) {
            imsg->result = -1;
            imsg->err_no = EALREADY;
            return 0;
        }
        if (slot->tcp_state == TN_TCP_STATE_ESTABLISHED) {
            imsg->result = -1;
            imsg->err_no = EISCONN;
            return 0;
        }

        ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);

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
            imsg->err_no = ECONNREFUSED;
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
        u16_t dst_port = lwip_ntohs(sin->sin_port);
        ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);
        udp_connect(slot->udp_pcb, &dst_ip, dst_port);
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        ip_addr_t dst_ip;
        ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);
        raw_connect(slot->raw_pcb, &dst_ip);
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_send(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const void *buf = (const void *)imsg->ptrs[0];
    LONG len = imsg->args[1];
    (void)d;

    if (slot == NULL || buf == NULL || len < 0) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (slot->type == SOCK_STREAM && slot->tcp_pcb != NULL) {
        err_t werr;
        u16_t send_len;
        u16_t snd_buf;

        if (slot->tcp_state != TN_TCP_STATE_ESTABLISHED) {
            imsg->result = -1;
            imsg->err_no = (slot->tcp_state == TN_TCP_STATE_ERROR) ? ECONNRESET : ENOTCONN;
            return 0;
        }

        snd_buf = tcp_sndbuf(slot->tcp_pcb);
        if (snd_buf == 0) {
            imsg->result = -1;
            imsg->err_no = EWOULDBLOCK;
            return 0;
        }

        send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
        if (send_len > snd_buf) send_len = snd_buf;

        werr = tcp_write(slot->tcp_pcb, buf, send_len, TCP_WRITE_FLAG_COPY);
        if (werr != ERR_OK) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }

        tcp_output(slot->tcp_pcb);
        tn_drain_loopback();
        imsg->result = (LONG)send_len;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        struct pbuf *p;
        u16_t send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
        err_t serr;

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
        if (slot->rx_head != NULL) {
            if (flags & MSG_PEEK) {
                TnRxPacket *cur = slot->rx_head;
                u16_t copied = 0;
                while (cur != NULL && copied < (u16_t)len) {
                    u16_t off = (cur == slot->rx_head) ? cur->offset : 0;
                    u16_t avail = cur->p->tot_len - off;
                    u16_t chunk = (avail < ((u16_t)len - copied)) ? avail : ((u16_t)len - copied);
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
                u16_t to_copy = (avail < (u16_t)len) ? avail : (u16_t)len;

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
                    FreeVec(pkt);
                    if (slot->rx_count > 0) {
                        slot->rx_count--;
                    }
                }

                imsg->result = (LONG)to_copy;
                imsg->err_no = 0;
                return 0;
            }
        } else if (slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
            imsg->result = 0; /* EOF */
            imsg->err_no = 0;
            return 0;
        } else if (slot->tcp_state == TN_TCP_STATE_ERROR) {
            imsg->result = -1;
            imsg->err_no = ECONNRESET;
            return 0;
        } else {
            imsg->result = -1;
            imsg->err_no = EWOULDBLOCK;
            return 0;
        }
    } else if (slot->type == SOCK_DGRAM || slot->type == SOCK_RAW) {
        if (slot->rx_head != NULL) {
            TnRxPacket *pkt = slot->rx_head;
            u16_t avail = pkt->p->tot_len;
            u16_t to_copy = (avail < (u16_t)len) ? avail : (u16_t)len;

            pbuf_copy_partial(pkt->p, buf, to_copy, 0);

            if (!(flags & MSG_PEEK)) {
                slot->rx_head = pkt->next;
                if (slot->rx_head == NULL) {
                    slot->rx_tail = NULL;
                }
                pbuf_free(pkt->p);
                FreeVec(pkt);
                if (slot->rx_count > 0) {
                    slot->rx_count--;
                }
            }

            imsg->result = (LONG)to_copy;
            imsg->err_no = 0;
            return 0;
        } else {
            imsg->result = -1;
            imsg->err_no = EWOULDBLOCK;
            return 0;
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

    if (slot == NULL || slot->type != 1 /* TCP */ || slot->tcp_pcb == NULL) {
        imsg->result = -1;
        imsg->err_no = ENOTCONN;
        return 0;
    }

    shut_rx = (how == 0 || how == 2) ? 1 : 0;
    shut_tx = (how == 1 || how == 2) ? 1 : 0;

    if (slot->tcp_state == TN_TCP_STATE_CLOSED) {
        if (shut_tx) {
            slot->tcp_state = TN_TCP_STATE_PEER_CLOSED;
        }
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    serr = tcp_shutdown(slot->tcp_pcb, shut_rx, shut_tx);
    if (serr != ERR_OK) {
        imsg->result = -1;
        imsg->err_no = ECONNRESET;
        return 0;
    }

    if (shut_tx) {
        slot->tcp_state = TN_TCP_STATE_PEER_CLOSED;
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}