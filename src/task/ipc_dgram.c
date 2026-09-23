/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet ? Datagram & Raw Socket Handlers Implementation (ipc_dgram.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_dgram.h"
#include "ipc_tcp.h"
#include "ipc_select.h"
#include "slot_table.h"
#include "netif_mgr.h"
#include "ipc_dispatch.h"
#include "../common/sockaddr_util.h"

void tn_udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                   const ip_addr_t *addr, u16_t port)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    struct pbuf *q;
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

    /* Item 9: clone into PBUF_RAM and immediately free the incoming pool pbuf.
     * Prevents an idle UDP listener from exhausting PBUF_POOL (size 32)
     * and starving the SANA2 network driver, ARP, or TCP. */
    q = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
    pbuf_free(p);
    if (q == NULL) {
        return;
    }

    if (tn_rx_queue_push(slot, q, addr, port) != 0) {
        pbuf_free(q);
        return;
    }

    tn_signal_socket(&g_daemon, slot);
    tn_record_socket_event(&g_daemon, slot, FD_READ);
    if (slot->pending_recv_msg != NULL) {
        tn_service_pending_recv(&g_daemon, slot);
    }
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
    if (slot->pending_recv_msg != NULL) {
        tn_service_pending_recv(&g_daemon, slot);
    }
    return 0;
}

int tn_ipc_cmd_bind(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const void *sin_ptr = (const void *)imsg->ptrs[0];
    socklen_t namelen = (socklen_t)imsg->args[1];
    ip_addr_t bind_ip;
    u16_t port;
    u16_t sin_family = 0;
    u16_t sin_port = 0;
    uint32_t sin_addr = 0;
    err_t berr;
    (void)d;

    if (slot == NULL || sin_ptr == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (namelen < (socklen_t)sizeof(struct sockaddr_in)) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    /* TNET-139: client buffer — byte-wise load, no struct cast */
    tn_sockin_load_bytes(sin_ptr, &sin_family, &sin_port, &sin_addr);
    if (sin_family != AF_INET) {
        imsg->result = -1;
        imsg->err_no = EAFNOSUPPORT;
        return 0;
    }

    ip_addr_set_ip4_u32(&bind_ip, sin_addr);
    port = sin_port;

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
    const void *to = (const void *)imsg->ptrs[1];
    (void)d;

    if (slot == NULL || buf == NULL || len < 0) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (slot->type == SOCK_STREAM) {
        /* Directive 3: TCP üzerinde sendto()
         * to == NULL ise send() ile aynı.
         * to != NULL ve soket bağlıysa: adres yok sayılır, veri mevcut akışa yazılır (4.4BSD tcp_usrreq).
         * Bağlı değilse: -1, ENOTCONN.
         * Gönderme yönü kapatılmışsa veya bağlantı kopmuşsa: -1, EPIPE.
         */
        if (slot->tcp_pcb == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOTCONN;
            return 0;
        }
        return tn_ipc_cmd_send(d, imsg, slot);
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        struct pbuf *p;
        ip_addr_t dst_ip;
        u16_t dst_port;
        u16_t send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
        int is_connected = (ip_addr_get_ip4_u32(&slot->udp_pcb->remote_ip) != 0 && slot->udp_pcb->remote_port != 0);

        /* Directive 1:
         * Bağlı bir UDP soketinde to != NULL ile sendto() -> -1, EISCONN (4.4BSD udp_output).
         */
        if (to != NULL) {
            if (is_connected) {
                imsg->result = -1;
                imsg->err_no = EISCONN;
                return 0;
            }
            /* TNET-139: client buffer — byte-wise load */
            u16_t to_port = 0;
            uint32_t to_addr = 0;
            tn_sockin_load_bytes(to, NULL, &to_port, &to_addr);
            ip_addr_set_ip4_u32(&dst_ip, to_addr);
            dst_port = to_port;
        } else {
            if (!is_connected) {
                /* [auto] 4.4BSD udp_output returns ENOTCONN when unconnected and to == NULL */
                imsg->result = -1;
                imsg->err_no = ENOTCONN;
                return 0;
            }
            dst_ip = slot->udp_pcb->remote_ip;
            dst_port = slot->udp_pcb->remote_port;
        }

        p = pbuf_alloc(PBUF_TRANSPORT, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }

        pbuf_take(p, buf, send_len);
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
        int is_connected = (ip_addr_get_ip4_u32(&slot->raw_pcb->remote_ip) != 0);

        if (to != NULL) {
            uint32_t to_addr = 0;
            tn_sockin_load_bytes(to, NULL, NULL, &to_addr);
            ip_addr_set_ip4_u32(&dst_ip, to_addr);
        } else {
            if (!is_connected) {
                /* [auto] 4.4BSD rip_usrreq returns ENOTCONN when unconnected and to == NULL */
                imsg->result = -1;
                imsg->err_no = ENOTCONN;
                return 0;
            }
            dst_ip = slot->raw_pcb->remote_ip;
        }

        p = pbuf_alloc(PBUF_IP, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }
        pbuf_take(p, buf, send_len);

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
    void *from = (void *)imsg->ptrs[1];
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

    /* Directive 2: TCP üzerinde recvfrom()
     * recv yoluna yönlendir.
     * from doldurulmaz. fromlen != NULL ise *fromlen = 0 yazılır.
     * 4.4BSD'de TCP'nin PR_ADDR bayrağı yok, soreceive adres döndürmez ve recvit namelen'i 0 yapar.
     * Bağlı olmayan TCP soketinde → -1, ENOTCONN (PR_CONNREQUIRED kontrolü).
     */
    if (slot->type == SOCK_STREAM) {
        if (slot->tcp_pcb == NULL ||
            (slot->tcp_state != TN_TCP_STATE_ESTABLISHED &&
             slot->tcp_state != TN_TCP_STATE_PEER_CLOSED &&
             slot->rx_head == NULL)) {
            imsg->result = -1;
            imsg->err_no = (slot->tcp_state == TN_TCP_STATE_ERROR) ? ECONNRESET : ENOTCONN;
            return 0;
        }

        int ret = tn_ipc_cmd_recv(d, imsg, slot);
        if (ret == 0 && imsg->result >= 0) {
            if (fromlen != NULL) {
                *fromlen = 0;
            }
        }
        return ret;
    }

    if (slot->type == SOCK_DGRAM) {
        if (slot->rx_head != NULL) {
            u16_t req_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
            TnRxPacket *pkt = slot->rx_head;
            u16_t copied = pbuf_copy_partial(pkt->p, buf, req_len, 0);

            if (from != NULL) {
                /* TNET-139: byte-wise store into client buffer */
                tn_sockin_store_bytes(from, AF_INET, pkt->src_port,
                                      ip_addr_get_ip4_u32(&pkt->src_ip));
                if (fromlen != NULL) *fromlen = sizeof(struct sockaddr_in);
            }

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

            imsg->result = (LONG)copied;
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
    } else if (slot->type == SOCK_RAW) {
        if (slot->rx_head != NULL) {
            u16_t req_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
            TnRxPacket *pkt = slot->rx_head;
            u16_t avail = pkt->p->tot_len;
            u16_t to_copy = (avail < req_len) ? avail : req_len;

            pbuf_copy_partial(pkt->p, buf, to_copy, 0);

            if (from != NULL) {
                /* TNET-139: byte-wise store into client buffer */
                tn_sockin_store_bytes(from, AF_INET, 0,
                                      ip_addr_get_ip4_u32(&pkt->src_ip));
                if (fromlen != NULL) *fromlen = sizeof(struct sockaddr_in);
            }

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