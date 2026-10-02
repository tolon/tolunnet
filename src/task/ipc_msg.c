/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet ? Scatter-Gather Message Handlers Implementation (ipc_msg.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_msg.h"
#include "slot_table.h"
#include "ipc_tcp.h" /* tn_ipc_cmd_send (single-iovec TCP sendmsg) */
#include "netif_mgr.h"
#include "../common/sockaddr_util.h"

int tn_ipc_cmd_sendmsg(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const struct msghdr *msg = (const struct msghdr *)imsg->ptrs[0];
    LONG flags = imsg->args[1];
    uint32_t to_addr_be = 0;
    u16_t to_port_host = 0;
    int have_to = 0;
    ULONG total_len = 0;
    ULONG i;
    (void)d;

    if (slot == NULL || msg == NULL) {
        imsg->result = -1;
        imsg->err_no = (msg == NULL) ? EINVAL : EBADF;
        return 0;
    }

    if (flags & MSG_OOB) {
        imsg->result = -1;
        imsg->err_no = EOPNOTSUPP;
        return 0;
    }

    if (msg->msg_iov == NULL || msg->msg_iovlen == 0 || msg->msg_iovlen > 1024) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    for (i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base == NULL) {
            imsg->result = -1;
            imsg->err_no = EFAULT;
            return 0;
        }
        total_len += msg->msg_iov[i].iov_len;
    }

    if (msg->msg_name != NULL) {
        if (msg->msg_namelen < sizeof(struct sockaddr_in)) {
            imsg->result = -1;
            imsg->err_no = EINVAL;
            return 0;
        }
        /* TNET-139: client msg_name buffer — byte-wise load */
        tn_sockin_load_bytes(msg->msg_name, NULL, &to_port_host, &to_addr_be);
        have_to = 1;
    }

    if (slot->type == SOCK_STREAM && msg->msg_iovlen == 1) {
        /* 4.2: one iovec is a plain send() - reuse its blocking/park path.
         * The message is re-shaped in place to send()'s layout; the client
         * only reads result/err_no back. */
        imsg->ptrs[0] = msg->msg_iov[0].iov_base;
        imsg->args[1] = (LONG)msg->msg_iov[0].iov_len;
        imsg->args[2] = flags;
        return tn_ipc_cmd_send(d, imsg, slot);
    } else if (slot->type == SOCK_STREAM) {
        /* multi-iovec: non-blocking semantics kept (EWOULDBLOCK when the
         * send buffer is full, short count otherwise) */
        u16_t snd_buf;
        u16_t to_send;
        u16_t remaining;
        u16_t sent_bytes = 0;

        /* 4.4BSD CLOSE_WAIT: send() after the peer's FIN still works (the
         * mirror of the send() gate in ipc_tcp.c; shut_wr/EPIPE only after
         * OUR shutdown(SHUT_WR) or a hard error). */
        if (slot->shut_wr) {
            imsg->result = -1;
            imsg->err_no = EPIPE;
            return 0;
        }
        if (slot->tcp_state == TN_TCP_STATE_ERROR) {
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

        snd_buf = tcp_sndbuf(slot->tcp_pcb);
        if (snd_buf == 0 && total_len > 0) {
            imsg->result = -1;
            imsg->err_no = EWOULDBLOCK;
            return 0;
        }

        to_send = (total_len > (ULONG)snd_buf) ? snd_buf : (u16_t)total_len;
        remaining = to_send;

        for (i = 0; i < msg->msg_iovlen && remaining > 0; i++) {
            u16_t chunk = (msg->msg_iov[i].iov_len > (size_t)remaining) ? remaining : (u16_t)msg->msg_iov[i].iov_len;
            if (chunk > 0) {
                err_t werr = tcp_write(slot->tcp_pcb, msg->msg_iov[i].iov_base, chunk, TCP_WRITE_FLAG_COPY);
                if (werr != ERR_OK) {
                    if (sent_bytes == 0) {
                        imsg->result = -1;
                        imsg->err_no = ENOBUFS;
                        return 0;
                    }
                    break;
                }
                sent_bytes += chunk;
                remaining -= chunk;
            }
        }

        /* TNET-115 data half: a fresh connection has pending handshake
         * packets in the loopback queue that must be processed before the
         * data can flow. Multiple output+drain rounds ensure the full
         * exchange (data → server recv → ACK → client) completes. */
        {
            /* z.ai step 7 item 4: re-check the pcb each round — the drain
             * can RST the connection and NULL slot->tcp_pcb. */
            int flush_i;
            for (flush_i = 0; flush_i < 4; flush_i++) {
                if (slot->tcp_pcb == NULL) break;
                tcp_output(slot->tcp_pcb);
                tn_drain_loopback();
            }
            if (slot->tcp_pcb == NULL) {
                imsg->result = -1;
                imsg->err_no = ECONNRESET;
                return 0;
            }
        }
        imsg->result = (LONG)sent_bytes;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        struct pbuf *p;
        ip_addr_t dst_ip;
        u16_t dst_port;
        u16_t send_len;
        u16_t offset = 0;
        err_t uerr;
        int is_connected = (ip_addr_get_ip4_u32(&slot->udp_pcb->remote_ip) != 0 &&
                            slot->udp_pcb->remote_port != 0);

        /* 3.4: same destination/size/err_t checks as sendto (ipc_dgram.c) */
        if (have_to) {
            if (is_connected) {
                imsg->result = -1;
                imsg->err_no = EISCONN;
                return 0;
            }
            ip_addr_set_ip4_u32(&dst_ip, to_addr_be);
            dst_port = to_port_host;
        } else {
            if (!is_connected) {
                imsg->result = -1;
                imsg->err_no = EDESTADDRREQ;
                return 0;
            }
            dst_ip = slot->udp_pcb->remote_ip;
            dst_port = slot->udp_pcb->remote_port;
        }
        if (total_len > 65507) { /* 65535 - 8 UDP - 20 IP */
            imsg->result = -1;
            imsg->err_no = EMSGSIZE;
            return 0;
        }
        send_len = (u16_t)total_len;

        p = pbuf_alloc(PBUF_TRANSPORT, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }

        for (i = 0; i < msg->msg_iovlen && offset < send_len; i++) {
            u16_t chunk = (msg->msg_iov[i].iov_len > (size_t)(send_len - offset)) ? (u16_t)(send_len - offset) : (u16_t)msg->msg_iov[i].iov_len;
            if (chunk > 0) {
                pbuf_take_at(p, msg->msg_iov[i].iov_base, chunk, offset);
                offset += chunk;
            }
        }

        uerr = udp_sendto(slot->udp_pcb, p, &dst_ip, dst_port);
        pbuf_free(p);
        tn_drain_loopback();
        if (uerr != ERR_OK) {
            imsg->result = -1;
            imsg->err_no = (uerr == ERR_MEM) ? ENOBUFS : EHOSTUNREACH;
            return 0;
        }

        imsg->result = (LONG)send_len;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        struct pbuf *p;
        ip_addr_t dst_ip;
        u16_t send_len = (total_len > 0xFFFF) ? 0xFFFF : (u16_t)total_len;
        u16_t offset = 0;
        err_t serr;

        p = pbuf_alloc(PBUF_IP, send_len, PBUF_RAM);
        if (p == NULL) {
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }

        for (i = 0; i < msg->msg_iovlen && offset < send_len; i++) {
            u16_t chunk = (msg->msg_iov[i].iov_len > (size_t)(send_len - offset)) ? (u16_t)(send_len - offset) : (u16_t)msg->msg_iov[i].iov_len;
            if (chunk > 0) {
                pbuf_take_at(p, msg->msg_iov[i].iov_base, chunk, offset);
                offset += chunk;
            }
        }

        if (have_to) {
            ip_addr_set_ip4_u32(&dst_ip, to_addr_be);
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

int tn_ipc_cmd_recvmsg(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    struct msghdr *msg = (struct msghdr *)imsg->ptrs[0];
    LONG flags = imsg->args[1];
    ULONG total_space = 0;
    ULONG i;
    (void)d;

    if (slot == NULL || msg == NULL) {
        imsg->result = -1;
        imsg->err_no = (msg == NULL) ? EINVAL : EBADF;
        return 0;
    }

    if (flags & MSG_OOB) {
        imsg->result = -1;
        imsg->err_no = EOPNOTSUPP;
        return 0;
    }

    if (msg->msg_iov == NULL || msg->msg_iovlen == 0 || msg->msg_iovlen > 1024) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    for (i = 0; i < msg->msg_iovlen; i++) {
        if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base == NULL) {
            imsg->result = -1;
            imsg->err_no = EFAULT;
            return 0;
        }
        total_space += msg->msg_iov[i].iov_len;
    }

    msg->msg_flags = 0;
    if (msg->msg_control != NULL) {
        msg->msg_controllen = 0;
    }

    if (slot->type == SOCK_STREAM) {
        if (slot->rx_head != NULL) {
            ULONG cur_iov = 0;
            ULONG iov_offset = 0;
            ULONG total_copied = 0;

            if (flags & MSG_PEEK) {
                TnRxPacket *cur = slot->rx_head;
                while (cur != NULL && cur_iov < msg->msg_iovlen && total_copied < total_space) {
                    u16_t off = (cur == slot->rx_head) ? cur->offset : 0;
                    u16_t avail = cur->p->tot_len - off;
                    while (avail > 0 && cur_iov < msg->msg_iovlen) {
                        ULONG rem_space = msg->msg_iov[cur_iov].iov_len - iov_offset;
                        u16_t space = (rem_space > 0xFFFF) ? 0xFFFF : (u16_t)rem_space;
                        if (space == 0) {
                            cur_iov++;
                            iov_offset = 0;
                            continue;
                        }
                        u16_t chunk = (avail < space) ? avail : space;
                        pbuf_copy_partial(cur->p, (char *)msg->msg_iov[cur_iov].iov_base + iov_offset, chunk, off);
                        off += chunk;
                        avail -= chunk;
                        iov_offset += chunk;
                        total_copied += chunk;
                    }
                    cur = cur->next;
                }
            } else {
                while (slot->rx_head != NULL && cur_iov < msg->msg_iovlen && total_copied < total_space) {
                    TnRxPacket *pkt = slot->rx_head;
                    u16_t avail = pkt->p->tot_len - pkt->offset;
                    u16_t pkt_copied = 0;
                    while (avail > 0 && cur_iov < msg->msg_iovlen) {
                        ULONG rem_space = msg->msg_iov[cur_iov].iov_len - iov_offset;
                        u16_t space = (rem_space > 0xFFFF) ? 0xFFFF : (u16_t)rem_space;
                        if (space == 0) {
                            cur_iov++;
                            iov_offset = 0;
                            continue;
                        }
                        u16_t chunk = (avail < space) ? avail : space;
                        pbuf_copy_partial(pkt->p, (char *)msg->msg_iov[cur_iov].iov_base + iov_offset, chunk, pkt->offset);
                        pkt->offset += chunk;
                        avail -= chunk;
                        iov_offset += chunk;
                        total_copied += chunk;
                        pkt_copied += chunk;
                    }
                    /* one window update per consumed packet, not per chunk
                     * (3 iovecs of one frame must not trigger 3 ACKs) */
                    if (pkt_copied > 0 && slot->tcp_pcb != NULL) {
                        tcp_recved(slot->tcp_pcb, pkt_copied);
                    }
                    if (pkt->offset >= pkt->p->tot_len) {
                        slot->rx_head = pkt->next;
                        if (slot->rx_head == NULL) {
                            slot->rx_tail = NULL;
                        }
                        pbuf_free(pkt->p);
                        FreeVec(pkt);
                        if (slot->rx_count > 0) slot->rx_count--;
                    }
                }
            }

            if (msg->msg_name != NULL) msg->msg_namelen = 0;
            imsg->result = (LONG)total_copied;
            imsg->err_no = 0;
            return 0;
        } else if (slot->shut_rd || slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
            if (msg->msg_name != NULL) msg->msg_namelen = 0;
            imsg->result = 0; /* EOF */
            imsg->err_no = 0;
            return 0;
        } else if (slot->tcp_state == TN_TCP_STATE_ERROR) {
            imsg->result = -1;
            imsg->err_no = ECONNRESET;
            return 0;
        } else if (slot->tcp_state != TN_TCP_STATE_CONNECTING &&
                   slot->tcp_state != TN_TCP_STATE_ESTABLISHED) {
            /* 4.4BSD soreceive: no circuit on CLOSED/LISTENING - this path
             * parked forever before and never honoured shut_rd (z.ai step 4
             * item 1). */
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
            TnRxPacket *pkt = slot->rx_head;
            u16_t avail = pkt->p->tot_len;
            ULONG cur_iov = 0;
            ULONG iov_offset = 0;
            u16_t pkt_offset = 0;
            ULONG total_copied = 0;

            while (cur_iov < msg->msg_iovlen && pkt_offset < avail) {
                ULONG rem_space = msg->msg_iov[cur_iov].iov_len - iov_offset;
                u16_t space = (rem_space > 0xFFFF) ? 0xFFFF : (u16_t)rem_space;
                if (space == 0) {
                    cur_iov++;
                    iov_offset = 0;
                    continue;
                }
                u16_t remaining = avail - pkt_offset;
                u16_t chunk = (remaining < space) ? remaining : space;
                pbuf_copy_partial(pkt->p, (char *)msg->msg_iov[cur_iov].iov_base + iov_offset, chunk, pkt_offset);
                pkt_offset += chunk;
                iov_offset += chunk;
                total_copied += chunk;
            }

            if (avail > total_copied) {
                msg->msg_flags |= MSG_TRUNC;
            }

            if (msg->msg_name != NULL && msg->msg_namelen >= sizeof(struct sockaddr_in)) {
                /* TNET-139: byte-wise store into client msg_name buffer */
                tn_sockin_store_bytes(msg->msg_name, AF_INET,
                                      (slot->type == SOCK_DGRAM) ? pkt->src_port : 0,
                                      ip_addr_get_ip4_u32(&pkt->src_ip));
                msg->msg_namelen = sizeof(struct sockaddr_in);
            }

            if (!(flags & MSG_PEEK)) {
                slot->rx_head = pkt->next;
                if (slot->rx_head == NULL) {
                    slot->rx_tail = NULL;
                }
                pbuf_free(pkt->p);
                FreeVec(pkt);
                if (slot->rx_count > 0) slot->rx_count--;
            }

            imsg->result = (LONG)total_copied;
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