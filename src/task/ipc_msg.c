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

/* YENI-8: the app's msghdr and iovec array may sit at odd addresses on
 * the 68000 - copy them byte-wise into locals, never deref them typed. */
static void tn_msg_iov_get(const void *iov, ULONG i, struct iovec *out)
{
    memcpy(out, (const UBYTE *)iov + i * sizeof(struct iovec), sizeof(struct iovec));
}

/* write one field of the local msghdr copy back into the app's msghdr */
static void tn_msg_store(void *umsg, const struct msghdr *mh, size_t off, size_t len)
{
    memcpy((UBYTE *)umsg + off, (const UBYTE *)mh + off, len);
}
#define TN_MSG_STORE(umsg, mh, field) \
    tn_msg_store((umsg), (mh), offsetof(struct msghdr, field), sizeof((mh)->field))

int tn_ipc_cmd_sendmsg(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    const void *umsg = imsg->ptrs[0];
    struct msghdr mh;
    const struct msghdr *msg = &mh;
    struct iovec v;
    LONG flags = imsg->args[1];
    uint32_t to_addr_be = 0;
    u16_t to_port_host = 0;
    int have_to = 0;
    ULONG total_len = 0;
    ULONG i;
    (void)d;

    if (slot == NULL || umsg == NULL) {
        imsg->result = -1;
        imsg->err_no = (umsg == NULL) ? EINVAL : EBADF;
        return 0;
    }
    memcpy(&mh, umsg, sizeof(mh));

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
        tn_msg_iov_get(msg->msg_iov, i, &v);
        if (v.iov_len > 0 && v.iov_base == NULL) {
            imsg->result = -1;
            imsg->err_no = EFAULT;
            return 0;
        }
        /* TNET-159: the total must fit the LONG result (POSIX EINVAL) */
        if (v.iov_len > (ULONG)0x7FFFFFFFUL - total_len) {
            imsg->result = -1;
            imsg->err_no = EINVAL;
            return 0;
        }
        total_len += v.iov_len;
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
        tn_msg_iov_get(msg->msg_iov, 0, &v);
        imsg->ptrs[0] = v.iov_base;
        imsg->args[1] = (LONG)v.iov_len;
        imsg->args[2] = flags;
        return tn_ipc_cmd_send(d, imsg, slot);
    } else if (slot->type == SOCK_STREAM) {
        /* TNET-159: multi-iovec takes send()'s path too - blocking parks
         * with the iovec array as cursor (BSD sosend), non-blocking /
         * MSG_DONTWAIT queues what fits. */
        return tn_tcp_send_stream(d, imsg, slot, NULL, msg->msg_iov,
                                  (ULONG)msg->msg_iovlen, (LONG)total_len, flags);
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
            u16_t chunk;
            tn_msg_iov_get(msg->msg_iov, i, &v);
            chunk = (v.iov_len > (size_t)(send_len - offset)) ? (u16_t)(send_len - offset) : (u16_t)v.iov_len;
            if (chunk > 0) {
                pbuf_take_at(p, v.iov_base, chunk, offset);
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
            u16_t chunk;
            tn_msg_iov_get(msg->msg_iov, i, &v);
            chunk = (v.iov_len > (size_t)(send_len - offset)) ? (u16_t)(send_len - offset) : (u16_t)v.iov_len;
            if (chunk > 0) {
                pbuf_take_at(p, v.iov_base, chunk, offset);
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
    void *umsg = imsg->ptrs[0];
    struct msghdr mh;           /* changed fields go back via TN_MSG_STORE */
    struct msghdr *msg = &mh;
    struct iovec v;
    LONG flags = imsg->args[1];
    ULONG total_space = 0;
    ULONG i;
    (void)d;

    if (slot == NULL || umsg == NULL) {
        imsg->result = -1;
        imsg->err_no = (umsg == NULL) ? EINVAL : EBADF;
        return 0;
    }
    memcpy(&mh, umsg, sizeof(mh));

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
        tn_msg_iov_get(msg->msg_iov, i, &v);
        if (v.iov_len > 0 && v.iov_base == NULL) {
            imsg->result = -1;
            imsg->err_no = EFAULT;
            return 0;
        }
        total_space += v.iov_len;
    }

    msg->msg_flags = 0;
    TN_MSG_STORE(umsg, msg, msg_flags);
    if (msg->msg_control != NULL) {
        msg->msg_controllen = 0;
        TN_MSG_STORE(umsg, msg, msg_controllen);
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
                        ULONG rem_space;
                        u16_t space;
                        tn_msg_iov_get(msg->msg_iov, cur_iov, &v);
                        rem_space = v.iov_len - iov_offset;
                        space = (rem_space > 0xFFFF) ? 0xFFFF : (u16_t)rem_space;
                        if (space == 0) {
                            cur_iov++;
                            iov_offset = 0;
                            continue;
                        }
                        u16_t chunk = (avail < space) ? avail : space;
                        pbuf_copy_partial(cur->p, (char *)v.iov_base + iov_offset, chunk, off);
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
                        ULONG rem_space;
                        u16_t space;
                        tn_msg_iov_get(msg->msg_iov, cur_iov, &v);
                        rem_space = v.iov_len - iov_offset;
                        space = (rem_space > 0xFFFF) ? 0xFFFF : (u16_t)rem_space;
                        if (space == 0) {
                            cur_iov++;
                            iov_offset = 0;
                            continue;
                        }
                        u16_t chunk = (avail < space) ? avail : space;
                        pbuf_copy_partial(pkt->p, (char *)v.iov_base + iov_offset, chunk, pkt->offset);
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

            if (msg->msg_name != NULL) {
                msg->msg_namelen = 0;
                TN_MSG_STORE(umsg, msg, msg_namelen);
            }
            imsg->result = (LONG)total_copied;
            imsg->err_no = 0;
            return 0;
        } else if (slot->shut_rd || slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
            if (msg->msg_name != NULL) {
                msg->msg_namelen = 0;
                TN_MSG_STORE(umsg, msg, msg_namelen);
            }
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
                ULONG rem_space;
                u16_t space;
                tn_msg_iov_get(msg->msg_iov, cur_iov, &v);
                rem_space = v.iov_len - iov_offset;
                space = (rem_space > 0xFFFF) ? 0xFFFF : (u16_t)rem_space;
                if (space == 0) {
                    cur_iov++;
                    iov_offset = 0;
                    continue;
                }
                u16_t remaining = avail - pkt_offset;
                u16_t chunk = (remaining < space) ? remaining : space;
                pbuf_copy_partial(pkt->p, (char *)v.iov_base + iov_offset, chunk, pkt_offset);
                pkt_offset += chunk;
                iov_offset += chunk;
                total_copied += chunk;
            }

            if (avail > total_copied) {
                msg->msg_flags |= MSG_TRUNC;
                TN_MSG_STORE(umsg, msg, msg_flags);
            }

            if (msg->msg_name != NULL && msg->msg_namelen >= sizeof(struct sockaddr_in)) {
                /* TNET-139: byte-wise store into client msg_name buffer */
                tn_sockin_store_bytes(msg->msg_name, AF_INET,
                                      (slot->type == SOCK_DGRAM) ? pkt->src_port : 0,
                                      ip_addr_get_ip4_u32(&pkt->src_ip));
                msg->msg_namelen = sizeof(struct sockaddr_in);
                TN_MSG_STORE(umsg, msg, msg_namelen);
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