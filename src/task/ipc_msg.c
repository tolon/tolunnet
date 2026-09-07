/*
 * tolunnet ? Scatter-Gather Message Handlers Implementation (ipc_msg.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_msg.h"
#include "slot_table.h"
#include "netif_mgr.h"

int tn_ipc_cmd_sendmsg(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const struct msghdr *msg = (const struct msghdr *)imsg->ptrs[0];
    LONG flags = imsg->args[1];
    const struct sockaddr_in *to = NULL;
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
        to = (const struct sockaddr_in *)msg->msg_name;
    }

    if (slot->type == SOCK_STREAM && slot->tcp_pcb != NULL) {
        u16_t snd_buf;
        u16_t to_send;
        u16_t remaining;
        u16_t sent_bytes = 0;

        if (slot->tcp_state != TN_TCP_STATE_ESTABLISHED) {
            imsg->result = -1;
            imsg->err_no = (slot->tcp_state == TN_TCP_STATE_ERROR) ? ECONNRESET : ENOTCONN;
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

        tcp_output(slot->tcp_pcb);
        tn_drain_loopback();
        imsg->result = (LONG)sent_bytes;
        imsg->err_no = 0;
        return 0;
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        struct pbuf *p;
        ip_addr_t dst_ip;
        u16_t dst_port;
        u16_t send_len = (total_len > 0xFFFF) ? 0xFFFF : (u16_t)total_len;
        u16_t offset = 0;

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
                        u16_t space = (u16_t)(msg->msg_iov[cur_iov].iov_len - iov_offset);
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
                    while (avail > 0 && cur_iov < msg->msg_iovlen) {
                        u16_t space = (u16_t)(msg->msg_iov[cur_iov].iov_len - iov_offset);
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
                        if (slot->tcp_pcb != NULL) {
                            tcp_recved(slot->tcp_pcb, chunk);
                        }
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
        } else if (slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
            if (msg->msg_name != NULL) msg->msg_namelen = 0;
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
            ULONG cur_iov = 0;
            ULONG iov_offset = 0;
            u16_t pkt_offset = 0;
            ULONG total_copied = 0;

            while (cur_iov < msg->msg_iovlen && pkt_offset < avail) {
                u16_t space = (u16_t)(msg->msg_iov[cur_iov].iov_len - iov_offset);
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
                struct sockaddr_in *from = (struct sockaddr_in *)msg->msg_name;
                from->sin_len = sizeof(struct sockaddr_in);
                from->sin_family = AF_INET;
                from->sin_port   = (slot->type == SOCK_DGRAM) ? lwip_htons(pkt->src_port) : 0;
                from->sin_addr.s_addr = ip_addr_get_ip4_u32(&pkt->src_ip);
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
            imsg->result = -1;
            imsg->err_no = EWOULDBLOCK;
            return 0;
        }
    }

    imsg->result = -1;
    imsg->err_no = EOPNOTSUPP;
    return 0;
}