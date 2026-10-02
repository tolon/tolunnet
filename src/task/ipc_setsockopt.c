/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Socket Option Setsockopt IPC Handler (ipc_setsockopt.c).
 *
 * ROUND4b §B (Modular Daemon Refactor).
 */
#include "ipc_socket.h"
#include "slot_table.h"
#include "../common/sockaddr_util.h"
#include <string.h>

static inline int tn_load_opt_int(const void *p)
{
    int v;
    memcpy(&v, p, sizeof(int));
    return v;
}

/* bugtrack 4.10: per-slot IP_ADD_MEMBERSHIP records (d->mcast_joins).
 * slot_idx < 0 finds a free record; otherwise the slot's matching join. */
static int tn_mcast_find(TnDaemon *d, int slot_idx, uint32_t grp, uint32_t ifa)
{
    int i;
    for (i = 0; i < TN_MCAST_JOINS_MAX; i++) {
        if (slot_idx < 0) {
            if (!d->mcast_joins[i].in_use) return i;
        } else if (d->mcast_joins[i].in_use &&
                   d->mcast_joins[i].slot == (uint8_t)slot_idx &&
                   d->mcast_joins[i].grp == grp &&
                   d->mcast_joins[i].ifa == ifa) {
            return i;
        }
    }
    return -1;
}

void tn_mcast_leave_all(TnDaemon *d, int slot_idx)
{
    int i;
    if (d == NULL || slot_idx < 0) return;
    for (i = 0; i < TN_MCAST_JOINS_MAX; i++) {
        if (d->mcast_joins[i].in_use && d->mcast_joins[i].slot == (uint8_t)slot_idx) {
            ip4_addr_t grp, ifa;
            grp.addr = d->mcast_joins[i].grp;
            ifa.addr = d->mcast_joins[i].ifa;
            (void)igmp_leavegroup(&ifa, &grp); /* netif may already be gone */
            d->mcast_joins[i].in_use = 0;
        }
    }
}

int tn_ipc_cmd_setsockopt(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG level = imsg->args[1];
    LONG optname = imsg->args[2];
    LONG optlen_arg = imsg->args[3];
    const void *optval = (const void *)imsg->ptrs[0];
    (void)d;

    if (slot == NULL || optval == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_BROADCAST:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_broadcast = (tn_load_opt_int(optval) != 0);
            if (slot->udp_pcb != NULL) {
                if (slot->opt_broadcast) {
                    ip_set_option(slot->udp_pcb, SOF_BROADCAST);
                } else {
                    ip_reset_option(slot->udp_pcb, SOF_BROADCAST);
                }
            }
            break;

        case SO_KEEPALIVE:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_keepalive = (tn_load_opt_int(optval) != 0);
            if (slot->tcp_pcb != NULL) {
                if (slot->opt_keepalive) {
                    ip_set_option(slot->tcp_pcb, SOF_KEEPALIVE);
                } else {
                    ip_reset_option(slot->tcp_pcb, SOF_KEEPALIVE);
                }
            }
            break;

        case SO_REUSEADDR:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_reuseaddr = (tn_load_opt_int(optval) != 0);
            if (slot->tcp_pcb != NULL) {
                if (slot->opt_reuseaddr) {
                    ip_set_option(slot->tcp_pcb, SOF_REUSEADDR);
                } else {
                    ip_reset_option(slot->tcp_pcb, SOF_REUSEADDR);
                }
            }
            if (slot->udp_pcb != NULL) {
                if (slot->opt_reuseaddr) {
                    ip_set_option(slot->udp_pcb, SOF_REUSEADDR);
                } else {
                    ip_reset_option(slot->udp_pcb, SOF_REUSEADDR);
                }
            }
            break;

        case SO_LINGER:
            if (optlen_arg < (LONG)sizeof(struct linger)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                struct linger l;
                memcpy(&l, optval, sizeof(struct linger));
                slot->opt_linger = l;
            }
            break;

        case SO_SNDBUF:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_sndbuf = tn_load_opt_int(optval);
            break;

        case SO_RCVBUF:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_rcvbuf = tn_load_opt_int(optval);
            break;

        case SO_OOBINLINE:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_oobinline = (tn_load_opt_int(optval) != 0);
            break;

        case SO_RCVTIMEO:
            if (optlen_arg >= (LONG)sizeof(struct timeval)) {
                memcpy(&slot->opt_rcvtimeo, optval, sizeof(struct timeval));
            } else if (optlen_arg >= (LONG)sizeof(int)) {
                int ms = tn_load_opt_int(optval);
                slot->opt_rcvtimeo.tv_secs  = ms / 1000;
                slot->opt_rcvtimeo.tv_micro = (ms % 1000) * 1000;
            } else {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            if (slot->pending_recv_msg != NULL) {
                if (slot->opt_rcvtimeo.tv_secs > 0 || slot->opt_rcvtimeo.tv_micro > 0) {
                    uint32_t ms = (uint32_t)slot->opt_rcvtimeo.tv_secs * 1000 +
                                  (uint32_t)(slot->opt_rcvtimeo.tv_micro + 999) / 1000;
                    uint32_t ticks = (ms + 99) / 100;
                    if (ticks == 0) ticks = 1;
                    slot->recv_deadline_tick = (d != NULL) ? (d->mainloop_ticks + ticks) : 0;
                } else {
                    slot->recv_deadline_tick = 0;
                }
            }
            break;

        case SO_SNDTIMEO:
            if (optlen_arg >= (LONG)sizeof(struct timeval)) {
                memcpy(&slot->opt_sndtimeo, optval, sizeof(struct timeval));
            } else if (optlen_arg >= (LONG)sizeof(int)) {
                int ms = tn_load_opt_int(optval);
                slot->opt_sndtimeo.tv_secs  = ms / 1000;
                slot->opt_sndtimeo.tv_micro = (ms % 1000) * 1000;
            } else {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            /* 4.2: re-arm the deadline of an already parked send */
            if (slot->pending_send_msg != NULL) {
                if (slot->opt_sndtimeo.tv_secs > 0 || slot->opt_sndtimeo.tv_micro > 0) {
                    uint32_t ms = (uint32_t)slot->opt_sndtimeo.tv_secs * 1000 +
                                  (uint32_t)(slot->opt_sndtimeo.tv_micro + 999) / 1000;
                    uint32_t ticks = (ms + 99) / 100;
                    if (ticks == 0) ticks = 1;
                    slot->send_deadline_tick = (d != NULL) ? (d->mainloop_ticks + ticks) : 0;
                } else {
                    slot->send_deadline_tick = 0;
                }
            }
            break;

        case SO_ERROR:
        case SO_TYPE:
            imsg->result = -1;
            imsg->err_no = ENOPROTOOPT;
            return 0;

        case SO_EVENTMASK:
            /* TNET-122..127: per-fd event filter stored client-side. The
             * setsockopt IPC carries the client fd in args[4] (marshalled by
             * the LVO), so the daemon just validates and returns OK — the
             * actual mask write happens in the LVO before the IPC call. */
            if (optval == NULL || optlen_arg < (LONG)sizeof(ULONG)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            break;

        default:
            imsg->result = -1;
            imsg->err_no = ENOPROTOOPT;
            return 0;
        }

        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    } else if (level == IPPROTO_TCP) {
        if (slot->type != SOCK_STREAM) {
            imsg->result = -1;
            imsg->err_no = ENOPROTOOPT;
            return 0;
        }

        switch (optname) {
        case TCP_NODELAY:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_nodelay = (tn_load_opt_int(optval) != 0);
            /* z.ai step 7g item 2: TCP_NODELAY is safe on any pcb (flags
             * live in the common header); TCP_MAXSEG writes mss, which
             * tcp_pcb_listen lacks - see the MAXSEG case below. */
            if (slot->tcp_state != TN_TCP_STATE_LISTENING && slot->tcp_pcb != NULL) {
                if (slot->opt_nodelay) {
                    tcp_nagle_disable(slot->tcp_pcb);
                } else {
                    tcp_nagle_enable(slot->tcp_pcb);
                }
            }
            break;

        case TCP_MAXSEG:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                int mss = tn_load_opt_int(optval);
                BOOL live = (slot->tcp_pcb != NULL &&
                             slot->tcp_state != TN_TCP_STATE_LISTENING);
                /* only a connected pcb carries a negotiated mss; before
                 * connect() lwIP holds INITIAL_MSS (536) as a placeholder */
                BOOL conn = (live && (slot->tcp_state == TN_TCP_STATE_ESTABLISHED ||
                                      slot->tcp_state == TN_TCP_STATE_PEER_CLOSED));
                int cur = conn ? (int)slot->tcp_pcb->mss
                               : (slot->opt_mss ? (int)slot->opt_mss : TCP_MSS);
                /* bugtrack 4.10: the mss may only be lowered (never above
                 * the negotiated/default one, never truncated to u16). */
                if (mss < 1 || mss > cur) {
                    imsg->result = -1;
                    imsg->err_no = EINVAL;
                    return 0;
                }
                slot->opt_mss = (u16_t)mss;
                /* z.ai step 7 item 5: never write mss into a LISTEN pcb —
                 * tcp_pcb_listen has no mss field (out-of-bounds). */
                if (live) {
                    slot->tcp_pcb->mss = (u16_t)mss;
                }
            }
            break;

        case TCP_KEEPIDLE:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                int val = tn_load_opt_int(optval);
                /* bugtrack 4.10: seconds > 0, and val * 1000 must fit u32 */
                if (val <= 0 || (u32_t)val > 0xFFFFFFFFUL / 1000UL) {
                    imsg->result = -1;
                    imsg->err_no = EINVAL;
                    return 0;
                }
                slot->opt_keepidle = val;
                /* z.ai step 7g item 2: never write keep_* into a LISTEN
                 * pcb - tcp_pcb_listen has no such fields. */
                if (slot->tcp_pcb != NULL &&
                    slot->tcp_state != TN_TCP_STATE_LISTENING) {
                    slot->tcp_pcb->keep_idle = (u32_t)val * 1000UL;
                }
            }
            break;

        case TCP_KEEPINTVL:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                int val = tn_load_opt_int(optval);
                /* bugtrack 4.10: seconds > 0, and val * 1000 must fit u32 */
                if (val <= 0 || (u32_t)val > 0xFFFFFFFFUL / 1000UL) {
                    imsg->result = -1;
                    imsg->err_no = EINVAL;
                    return 0;
                }
                slot->opt_keepintvl = val;
                /* z.ai step 7g item 2: never write keep_* into a LISTEN
                 * pcb - tcp_pcb_listen has no such fields. */
                if (slot->tcp_pcb != NULL &&
                    slot->tcp_state != TN_TCP_STATE_LISTENING) {
                    slot->tcp_pcb->keep_intvl = (u32_t)val * 1000UL;
                }
            }
            break;

        case TCP_KEEPCNT:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                int val = tn_load_opt_int(optval);
                if (val <= 0) { /* bugtrack 4.10 */
                    imsg->result = -1;
                    imsg->err_no = EINVAL;
                    return 0;
                }
                slot->opt_keepcnt = val;
                /* z.ai step 7g item 2: never write keep_* into a LISTEN
                 * pcb - tcp_pcb_listen has no such fields. */
                if (slot->tcp_pcb != NULL &&
                    slot->tcp_state != TN_TCP_STATE_LISTENING) {
                    slot->tcp_pcb->keep_cnt = (u32_t)val;
                }
            }
            break;

        default:
            imsg->result = -1;
            imsg->err_no = ENOPROTOOPT;
            return 0;
        }

        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    } else if (level == IPPROTO_IP) {
        switch (optname) {
        case IP_TOS:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                u8_t tos = (u8_t)tn_load_opt_int(optval);
                slot->opt_tos = tos;
                if (slot->tcp_pcb != NULL) slot->tcp_pcb->tos = tos;
                if (slot->udp_pcb != NULL) slot->udp_pcb->tos = tos;
                if (slot->raw_pcb != NULL) slot->raw_pcb->tos = tos;
            }
            break;

        case IP_TTL:
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                u8_t ttl = (u8_t)tn_load_opt_int(optval);
                slot->opt_ttl = ttl;
                if (slot->tcp_pcb != NULL) slot->tcp_pcb->ttl = ttl;
                if (slot->udp_pcb != NULL) slot->udp_pcb->ttl = ttl;
                if (slot->raw_pcb != NULL) slot->raw_pcb->ttl = ttl;
            }
            break;

        case IP_HDRINCL:
            if (slot->type != SOCK_RAW) {
                imsg->result = -1;
                imsg->err_no = ENOPROTOOPT;
                return 0;
            }
            if (optlen_arg < (LONG)sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            slot->opt_hdrincl = (tn_load_opt_int(optval) != 0);
            if (slot->raw_pcb != NULL) {
                if (slot->opt_hdrincl) {
                    raw_set_flags(slot->raw_pcb, RAW_FLAGS_HDRINCL);
                } else {
                    raw_clear_flags(slot->raw_pcb, RAW_FLAGS_HDRINCL);
                }
            }
            break;

        case IP_MULTICAST_TTL:
            if (optlen_arg < 1) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                u8_t mttl;
                if (optlen_arg >= (LONG)sizeof(int)) {
                    mttl = (u8_t)tn_load_opt_int(optval);
                } else {
                    mttl = *(const u8_t *)optval;
                }
                slot->opt_multicast_ttl = mttl;
                if (slot->udp_pcb != NULL) slot->udp_pcb->mcast_ttl = mttl;
                if (slot->raw_pcb != NULL) slot->raw_pcb->mcast_ttl = mttl;
            }
            break;

        case IP_MULTICAST_LOOP:
            if (optlen_arg < 1) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                u8_t mloop;
                if (optlen_arg >= (LONG)sizeof(int)) {
                    mloop = (tn_load_opt_int(optval) != 0) ? 1 : 0;
                } else {
                    mloop = (*(const u8_t *)optval != 0) ? 1 : 0;
                }
                slot->opt_multicast_loop = mloop;
                if (slot->udp_pcb != NULL) {
                    if (mloop) {
                        udp_set_flags(slot->udp_pcb, UDP_FLAGS_MULTICAST_LOOP);
                    } else {
                        udp_clear_flags(slot->udp_pcb, UDP_FLAGS_MULTICAST_LOOP);
                    }
                }
            }
            break;

        case IP_ADD_MEMBERSHIP:
            if (optlen_arg < (LONG)sizeof(struct ip_mreq)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                struct ip_mreq mreq;
                ip4_addr_t grp, ifa;
                err_t err;
                int rec = -1;
                memcpy(&mreq, optval, sizeof(struct ip_mreq));
                grp.addr = mreq.imr_multiaddr.s_addr;
                ifa.addr = mreq.imr_interface.s_addr;
                /* bugtrack 4.10: record the join so the slot leaves on close */
                if (d != NULL) {
                    rec = tn_mcast_find(d, -1, 0, 0);
                    if (rec < 0) {
                        imsg->result = -1;
                        imsg->err_no = ENOBUFS;
                        return 0;
                    }
                }
                err = igmp_joingroup(&ifa, &grp);
                if (err != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = (err == ERR_MEM) ? ENOBUFS : EINVAL;
                    return 0;
                }
                if (rec >= 0) {
                    d->mcast_joins[rec].in_use = 1;
                    d->mcast_joins[rec].slot = (uint8_t)(slot - d->sockets);
                    d->mcast_joins[rec].grp = grp.addr;
                    d->mcast_joins[rec].ifa = ifa.addr;
                }
            }
            break;

        case IP_DROP_MEMBERSHIP:
            if (optlen_arg < (LONG)sizeof(struct ip_mreq)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            {
                struct ip_mreq mreq;
                ip4_addr_t grp, ifa;
                err_t err;
                memcpy(&mreq, optval, sizeof(struct ip_mreq));
                grp.addr = mreq.imr_multiaddr.s_addr;
                ifa.addr = mreq.imr_interface.s_addr;
                err = igmp_leavegroup(&ifa, &grp);
                if (err != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = (err == ERR_MEM) ? ENOBUFS : EINVAL;
                    return 0;
                }
                if (d != NULL) {
                    int rec = tn_mcast_find(d, (int)(slot - d->sockets), grp.addr, ifa.addr);
                    if (rec >= 0) d->mcast_joins[rec].in_use = 0;
                }
            }
            break;

        default:
            imsg->result = -1;
            imsg->err_no = ENOPROTOOPT;
            return 0;
        }

        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    } else {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }
}
