/*
 * tolunnet ? Datagram & Raw Socket Handlers Implementation (ipc_dgram.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_dgram.h"
#include "ipc_select.h"
#include "slot_table.h"
#include "netif_mgr.h"

void tn_udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                   const ip_addr_t *addr, u16_t port)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    (void)pcb;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        if (p != NULL) pbuf_free(p);
        return;
    }

    slot = &g_daemon.sockets[slot_idx];
    if (!slot->in_use || p == NULL) {
        if (p != NULL) pbuf_free(p);
        return;
    }

    if (slot->rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        pbuf_free(p);
        return;
    }

    if (tn_rx_queue_push(slot, p, addr, port) != 0) {
        pbuf_free(p);
        return;
    }

    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_READ);
}

u8_t tn_raw_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                   const ip_addr_t *addr)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    struct pbuf *q;
    (void)pcb;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        return 0;
    }

    slot = &g_daemon.sockets[slot_idx];
    if (!slot->in_use || p == NULL) {
        return 0;
    }

    if (slot->rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        return 0;
    }

    q = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
    if (q == NULL) {
        return 0;
    }

    if (tn_rx_queue_push(slot, q, addr, 0) != 0) {
        pbuf_free(q);
        return 0;
    }

    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_READ);
    return 0;
}

int tn_ipc_cmd_bind(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *)imsg->ptrs[0];
    socklen_t namelen = (socklen_t)imsg->args[1];
    ip_addr_t bind_ip;
    u16_t port;
    err_t berr;
    (void)d;

    if (slot == NULL || sin == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (namelen < (socklen_t)sizeof(struct sockaddr_in)) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    if (sin->sin_family != AF_INET) {
        imsg->result = -1;
        imsg->err_no = EAFNOSUPPORT;
        return 0;
    }

    ip_addr_set_ip4_u32(&bind_ip, sin->sin_addr.s_addr);
    port = lwip_ntohs(sin->sin_port);

    if (slot->type == SOCK_STREAM) {
        if (slot->tcp_pcb == NULL) {
            imsg->result = -1;
            imsg->err_no = EBADF;
            return 0;
        }
        if (slot->opt_reuseaddr) {
            ip_set_option(slot->tcp_pcb, SOF_REUSEADDR);
        }
        berr = tcp_bind(slot->tcp_pcb, &bind_ip, port);
    } else if (slot->type == SOCK_DGRAM) {
        if (slot->udp_pcb == NULL) {
            imsg->result = -1;
            imsg->err_no = EBADF;
            return 0;
        }
        if (slot->opt_reuseaddr) {
            ip_set_option(slot->udp_pcb, SOF_REUSEADDR);
        }
        berr = udp_bind(slot->udp_pcb, &bind_ip, port);
    } else if (slot->type == SOCK_RAW) {
        if (slot->raw_pcb == NULL) {
            imsg->result = -1;
            imsg->err_no = EBADF;
            return 0;
        }
        berr = raw_bind(slot->raw_pcb, &bind_ip);
    } else {
        imsg->result = -1;
        imsg->err_no = EOPNOTSUPP;
        return 0;
    }

    if (berr != ERR_OK) {
        imsg->result = -1;
        imsg->err_no = (berr == ERR_USE) ? EADDRINUSE : EINVAL;
        return 0;
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_sendto(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const void *buf = (const void *)imsg->ptrs[0];
    LONG len = imsg->args[1];
    const struct sockaddr_in *to = (const struct sockaddr_in *)imsg->ptrs[1];
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
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        struct pbuf *p;
        ip_addr_t dst_ip;
        u16_t dst_port;
        u16_t send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;

        p = pbuf_alloc(PBUF_TRANSPORT, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }

        pbuf_take(p, buf, send_len);

        if (to != NULL) {
            ip_addr_set_ip4_u32(&dst_ip, to->sin_addr.s_addr);
            dst_port = lwip_ntohs(to->sin_port);
        } else {
            dst_ip = slot->udp_pcb->remote_ip;
            dst_port = slot->udp_pcb->remote_port;
        }

        udp_sendto(slot->udp_pcb, p, &dst_ip, dst_port);
        pbuf_free(p);
        tn_drain_loopback();

        imsg->result = (LONG)send_len;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        struct pbuf *p;
        ip_addr_t dst_ip;
        u16_t send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
        err_t serr;

        p = pbuf_alloc(PBUF_IP, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }
        pbuf_take(p, buf, send_len);

        if (to != NULL) {
            ip_addr_set_ip4_u32(&dst_ip, to->sin_addr.s_addr);
        } else {
            dst_ip = slot->raw_pcb->remote_ip;
        }

        serr = raw_sendto(slot->raw_pcb, p, &dst_ip);
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

int tn_ipc_cmd_recvfrom(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    void *buf = imsg->ptrs[0];
    LONG len = imsg->args[1];
    LONG flags = imsg->args[2];
    struct sockaddr_in *from = (struct sockaddr_in *)imsg->ptrs[1];
    socklen_t *fromlen = (socklen_t *)imsg->ptrs[2];
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

    if (slot->type == SOCK_DGRAM) {
        if (slot->rx_head != NULL) {
            TnRxPacket *pkt = slot->rx_head;
            u16_t copied = pbuf_copy_partial(pkt->p, buf, (u16_t)len, 0);

            if (from != NULL) {
                from->sin_len = sizeof(struct sockaddr_in);
                from->sin_family = AF_INET;
                from->sin_port   = lwip_htons(pkt->src_port);
                from->sin_addr.s_addr = ip_addr_get_ip4_u32(&pkt->src_ip);
                if (fromlen != NULL) *fromlen = sizeof(struct sockaddr_in);
            }

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

            imsg->result = (LONG)copied;
            imsg->err_no = 0;
            return 0;
        } else {
            imsg->result = -1;
            imsg->err_no = EWOULDBLOCK;
            return 0;
        }
    } else if (slot->type == SOCK_RAW) {
        if (slot->rx_head != NULL) {
            TnRxPacket *pkt = slot->rx_head;
            u16_t avail = pkt->p->tot_len;
            u16_t to_copy = (avail < (u16_t)len) ? avail : (u16_t)len;

            pbuf_copy_partial(pkt->p, buf, to_copy, 0);

            if (from != NULL) {
                from->sin_len = sizeof(struct sockaddr_in);
                from->sin_family = AF_INET;
                from->sin_port   = 0;
                from->sin_addr.s_addr = ip_addr_get_ip4_u32(&pkt->src_ip);
                if (fromlen != NULL) *fromlen = sizeof(struct sockaddr_in);
            }

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