/*
 * tolunnet — Socket Control & Option IPC Handlers Implementation (ipc_socket.c).
 *
 * ROUND4b §B (Modular Daemon Refactor).
 */
#include "ipc_socket.h"
#include "ipc_tcp.h"
#include "ipc_dgram.h"
#include "ipc_select.h"
#include "slot_table.h"
#include "../common/sockaddr_util.h"
#include <string.h>

int tn_ipc_cmd_open(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    (void)d;
    (void)slot;

    tn_logf(TN_LOG_BASIC, "tolunnet: client task 0x%p opened bsdsocket.library\n",
            imsg->client_task);

    /* TNET-063: hand configured hostname to gethostname() callers */
    if (base != NULL && d->prefs.hostname[0] != '\0') {
        int j = 0;
        while (d->prefs.hostname[j] != '\0' && j < (int)sizeof(base->hostname) - 1) {
            base->hostname[j] = d->prefs.hostname[j];
            j++;
        }
        base->hostname[j] = '\0';
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0; /* TN_IPC_REPLY_NOW */
}

int tn_ipc_cmd_close(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    int i;
    (void)slot;

    tn_logf(TN_LOG_BASIC, "tolunnet: client task 0x%p closing bsdsocket.library\n",
            imsg->client_task);

    if (base != NULL) {
        for (i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
            int slot_idx = base->fd_map[i];
            if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS && d->sockets[slot_idx].in_use) {
                base->fd_map[i] = -1;
                tn_slot_unref(d, slot_idx);
            }
        }
        tn_selector_disarm_all_for_base(d, base);
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_socket(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    int domain   = (int)imsg->args[0];
    int type     = (int)imsg->args[1];
    int protocol = (int)imsg->args[2];
    int pref_fd  = (int)imsg->args[3];
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    int client_fd = -1;
    int slot_idx = -1;
    TnSocketSlot *s;
    (void)slot;

    if (domain != AF_INET) {
        imsg->result = -1;
        imsg->err_no = EAFNOSUPPORT;
        return 0;
    }

    if (type == SOCK_STREAM) {
        if (protocol != 0 && protocol != IPPROTO_TCP) {
            imsg->result = -1;
            imsg->err_no = EPROTONOSUPPORT;
            return 0;
        }
    } else if (type == SOCK_DGRAM) {
        if (protocol != 0 && protocol != IPPROTO_UDP) {
            imsg->result = -1;
            imsg->err_no = EPROTONOSUPPORT;
            return 0;
        }
    } else if (type == SOCK_RAW) {
        if (protocol != IPPROTO_ICMP && protocol != IPPROTO_RAW) {
            imsg->result = -1;
            imsg->err_no = EPROTONOSUPPORT;
            return 0;
        }
    } else {
        imsg->result = -1;
        imsg->err_no = ESOCKTNOSUPPORT;
        return 0;
    }

    if (base != NULL) {
        if (pref_fd >= 0 && pref_fd < TN_MAX_FDS_PER_TASK && base->fd_map[pref_fd] == -1) {
            client_fd = pref_fd;
        } else {
            int i;
            for (i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
                if (base->fd_map[i] == -1) {
                    client_fd = i;
                    break;
                }
            }
        }
    }

    if (client_fd < 0) {
        imsg->result = -1;
        imsg->err_no = EMFILE;
        return 0;
    }

    s = tn_slot_alloc(d, base, imsg->client_task, domain, type, protocol, &slot_idx);
    if (s == NULL) {
        imsg->result = -1;
        imsg->err_no = ENFILE;
        return 0;
    }

    if (type == SOCK_DGRAM) {
        s->udp_pcb = udp_new();
        if (s->udp_pcb == NULL) {
            tn_slot_free(d, slot_idx);
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }
        udp_recv(s->udp_pcb, tn_udp_recv_cb, (void *)(intptr_t)slot_idx);
    } else if (type == SOCK_STREAM) {
        s->tcp_pcb = tcp_new();
        if (s->tcp_pcb == NULL) {
            tn_slot_free(d, slot_idx);
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }
        tcp_arg(s->tcp_pcb, (void *)(intptr_t)slot_idx);
        tcp_err(s->tcp_pcb, tn_tcp_err_cb);
    } else if (type == SOCK_RAW) {
        s->raw_pcb = raw_new((u8_t)protocol);
        if (s->raw_pcb == NULL) {
            tn_slot_free(d, slot_idx);
            imsg->result = -1;
            imsg->err_no = ENOBUFS;
            return 0;
        }
        raw_recv(s->raw_pcb, tn_raw_recv_cb, (void *)(intptr_t)slot_idx);
        if (protocol == IPPROTO_RAW) {
            raw_set_flags(s->raw_pcb, RAW_FLAGS_HDRINCL);
        }
    }

    base->fd_map[client_fd] = slot_idx;
    if (type == SOCK_DGRAM || type == SOCK_RAW) {
        tn_record_socket_event(d, s, FD_WRITE);
    }

    tn_logf(TN_LOG_BASIC, "tolunnet: socket(domain=%d, type=%d, proto=%d) -> fd %d (slot %d)\n",
            domain, type, protocol, client_fd, slot_idx);

    imsg->result = client_fd;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_closesocket(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    int client_fd = (int)imsg->args[0];
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    int slot_idx;

    if (slot == NULL || base == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    slot_idx = (int)(slot - d->sockets);
    base->fd_map[client_fd] = -1;
    tn_slot_unref(d, slot_idx);

    tn_logf(TN_LOG_BASIC, "tolunnet: CloseSocket(fd=%d, slot=%d) -> ok\n",
            client_fd, slot_idx);

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_dup2(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    int old_fd = (int)imsg->args[0];
    int new_fd = (int)imsg->args[1];
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    int old_slot;
    int existing_new_slot;

    if (slot == NULL || base == NULL || new_fd < 0 || new_fd >= TN_MAX_FDS_PER_TASK) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    old_slot = (int)(slot - d->sockets);
    if (old_fd == new_fd) {
        imsg->result = new_fd;
        imsg->err_no = 0;
        return 0;
    }

    /* If new_fd was already open, release it cleanly (TNET-048) */
    existing_new_slot = base->fd_map[new_fd];
    if (existing_new_slot >= 0 && existing_new_slot < TN_MAX_GLOBAL_SOCKETS && d->sockets[existing_new_slot].in_use) {
        base->fd_map[new_fd] = -1;
        tn_slot_unref(d, existing_new_slot);
    }

    base->fd_map[new_fd] = old_slot;
    tn_slot_ref(slot);

    imsg->result = new_fd;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_releasesocket(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    int client_fd = (int)imsg->args[0];
    LONG req_id   = imsg->args[1];
    BOOL copy     = (BOOL)imsg->args[2];
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    LONG assigned_id;
    int i;

    if (slot == NULL || base == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (req_id == UNIQUE_ID) {
        do {
            assigned_id = d->next_park_id++;
            if (d->next_park_id <= 0) d->next_park_id = 1;
            BOOL dup = FALSE;
            for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
                if (d->sockets[i].in_use && d->sockets[i].is_parked && d->sockets[i].park_id == assigned_id) {
                    dup = TRUE;
                    break;
                }
            }
            if (!dup) break;
        } while (1);
    } else {
        if (req_id < 0) {
            imsg->result = -1;
            imsg->err_no = EINVAL;
            return 0;
        }
        for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
            if (d->sockets[i].in_use && d->sockets[i].is_parked && d->sockets[i].park_id == req_id) {
                imsg->result = -1;
                imsg->err_no = EADDRINUSE;
                return 0;
            }
        }
        assigned_id = req_id;
    }

    slot->is_parked = TRUE;
    slot->park_id   = assigned_id;

    if (copy) {
        tn_slot_ref(slot);
    } else {
        base->fd_map[client_fd] = -1;
    }

    imsg->result = assigned_id;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_obtainsocket(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG park_id  = imsg->args[0];
    int domain    = (int)imsg->args[1];
    int type      = (int)imsg->args[2];
    int protocol  = (int)imsg->args[3];
    int pref_fd   = (int)imsg->args[4];
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    int client_fd = -1;
    int slot_idx = -1;
    int i;
    (void)slot;

    if (base == NULL) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        if (d->sockets[i].in_use && d->sockets[i].is_parked && d->sockets[i].park_id == park_id) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0) {
        imsg->result = -1;
        imsg->err_no = ENOENT;
        return 0;
    }

    if (domain != 0 && d->sockets[slot_idx].domain != domain) {
        imsg->result = -1;
        imsg->err_no = EPROTOTYPE;
        return 0;
    }
    if (type != 0 && d->sockets[slot_idx].type != type) {
        imsg->result = -1;
        imsg->err_no = EPROTOTYPE;
        return 0;
    }
    if (protocol != 0 && d->sockets[slot_idx].protocol != 0 && d->sockets[slot_idx].protocol != protocol) {
        imsg->result = -1;
        imsg->err_no = EPROTONOSUPPORT;
        return 0;
    }

    if (pref_fd >= 0 && pref_fd < TN_MAX_FDS_PER_TASK && base->fd_map[pref_fd] == -1) {
        client_fd = pref_fd;
    } else {
        for (i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
            if (base->fd_map[i] == -1) {
                client_fd = i;
                break;
            }
        }
    }
    if (client_fd < 0) {
        imsg->result = -1;
        imsg->err_no = EMFILE;
        return 0;
    }

    base->fd_map[client_fd] = slot_idx;
    d->sockets[slot_idx].is_parked = FALSE;
    d->sockets[slot_idx].park_id   = 0;
    d->sockets[slot_idx].owner_base = base;
    d->sockets[slot_idx].owner_task = imsg->client_task;

    imsg->result = client_fd;
    imsg->err_no = 0;
    return 0;
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
            slot->opt_broadcast = (*(const int *)optval != 0);
            if (slot->udp_pcb != NULL) {
                if (slot->opt_broadcast) {
                    ip_set_option(slot->udp_pcb, SOF_BROADCAST);
                } else {
                    ip_reset_option(slot->udp_pcb, SOF_BROADCAST);
                }
            }
            break;

        case SO_KEEPALIVE:
            slot->opt_keepalive = (*(const int *)optval != 0);
            if (slot->tcp_pcb != NULL) {
                if (slot->opt_keepalive) {
                    ip_set_option(slot->tcp_pcb, SOF_KEEPALIVE);
                } else {
                    ip_reset_option(slot->tcp_pcb, SOF_KEEPALIVE);
                }
            }
            break;

        case SO_REUSEADDR:
            slot->opt_reuseaddr = (*(const int *)optval != 0);
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
                const struct linger *l = (const struct linger *)optval;
                slot->opt_linger.l_onoff  = l->l_onoff;
                slot->opt_linger.l_linger = l->l_linger;
            }
            break;

        case SO_SNDBUF:
            slot->opt_sndbuf = *(const int *)optval;
            break;

        case SO_RCVBUF:
            slot->opt_rcvbuf = *(const int *)optval;
            break;

        case SO_OOBINLINE:
            slot->opt_oobinline = (*(const int *)optval != 0);
            break;

        case SO_RCVTIMEO:
            if (optlen_arg >= (LONG)sizeof(struct timeval)) {
                slot->opt_rcvtimeo = *(const struct timeval *)optval;
            } else if (optlen_arg >= (LONG)sizeof(int)) {
                int ms = *(const int *)optval;
                slot->opt_rcvtimeo.tv_secs  = ms / 1000;
                slot->opt_rcvtimeo.tv_micro = (ms % 1000) * 1000;
            } else {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            break;

        case SO_SNDTIMEO:
            if (optlen_arg >= (LONG)sizeof(struct timeval)) {
                slot->opt_sndtimeo = *(const struct timeval *)optval;
            } else if (optlen_arg >= (LONG)sizeof(int)) {
                int ms = *(const int *)optval;
                slot->opt_sndtimeo.tv_secs  = ms / 1000;
                slot->opt_sndtimeo.tv_micro = (ms % 1000) * 1000;
            } else {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            break;

        case SO_ERROR:
        case SO_TYPE:
            imsg->result = -1;
            imsg->err_no = ENOPROTOOPT;
            return 0;

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
            slot->opt_nodelay = (*(const int *)optval != 0);
            if (slot->tcp_pcb != NULL) {
                if (slot->opt_nodelay) {
                    tcp_nagle_disable(slot->tcp_pcb);
                } else {
                    tcp_nagle_enable(slot->tcp_pcb);
                }
            }
            break;

        case TCP_MAXSEG:
            {
                int mss = *(const int *)optval;
                if (mss > 0 && slot->tcp_pcb != NULL) {
                    slot->tcp_pcb->mss = (u16_t)mss;
                }
            }
            break;

        case TCP_KEEPIDLE:
            {
                int val = *(const int *)optval;
                slot->opt_keepidle = val;
                if (slot->tcp_pcb != NULL) {
                    slot->tcp_pcb->keep_idle = (u32_t)val * 1000UL;
                }
            }
            break;

        case TCP_KEEPINTVL:
            {
                int val = *(const int *)optval;
                slot->opt_keepintvl = val;
                if (slot->tcp_pcb != NULL) {
                    slot->tcp_pcb->keep_intvl = (u32_t)val * 1000UL;
                }
            }
            break;

        case TCP_KEEPCNT:
            {
                int val = *(const int *)optval;
                slot->opt_keepcnt = val;
                if (slot->tcp_pcb != NULL) {
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
            {
                u8_t tos = (u8_t)*(const int *)optval;
                slot->opt_tos = tos;
                if (slot->tcp_pcb != NULL) slot->tcp_pcb->tos = tos;
                if (slot->udp_pcb != NULL) slot->udp_pcb->tos = tos;
                if (slot->raw_pcb != NULL) slot->raw_pcb->tos = tos;
            }
            break;

        case IP_TTL:
            {
                u8_t ttl = (u8_t)*(const int *)optval;
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
            slot->opt_hdrincl = (*(const int *)optval != 0);
            if (slot->raw_pcb != NULL) {
                if (slot->opt_hdrincl) {
                    raw_set_flags(slot->raw_pcb, RAW_FLAGS_HDRINCL);
                } else {
                    raw_clear_flags(slot->raw_pcb, RAW_FLAGS_HDRINCL);
                }
            }
            break;

        case IP_MULTICAST_TTL:
            {
                u8_t mttl = (u8_t)*(const int *)optval;
                slot->opt_multicast_ttl = mttl;
                if (slot->udp_pcb != NULL) slot->udp_pcb->mcast_ttl = mttl;
                if (slot->raw_pcb != NULL) slot->raw_pcb->mcast_ttl = mttl;
            }
            break;

        case IP_MULTICAST_LOOP:
            {
                u8_t mloop = (*(const int *)optval != 0) ? 1 : 0;
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
                const struct ip_mreq *mreq = (const struct ip_mreq *)optval;
                ip4_addr_t grp, ifa;
                err_t err;
                grp.addr = mreq->imr_multiaddr.s_addr;
                ifa.addr = mreq->imr_interface.s_addr;
                err = igmp_joingroup(&ifa, &grp);
                if (err != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = (err == ERR_MEM) ? ENOBUFS : EINVAL;
                    return 0;
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
                const struct ip_mreq *mreq = (const struct ip_mreq *)optval;
                ip4_addr_t grp, ifa;
                err_t err;
                grp.addr = mreq->imr_multiaddr.s_addr;
                ifa.addr = mreq->imr_interface.s_addr;
                err = igmp_leavegroup(&ifa, &grp);
                if (err != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = (err == ERR_MEM) ? ENOBUFS : EINVAL;
                    return 0;
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

int tn_ipc_cmd_getsockopt(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG level = imsg->args[1];
    LONG optname = imsg->args[2];
    void *optval = imsg->ptrs[0];
    socklen_t *optlen = (socklen_t *)imsg->ptrs[1];
    (void)d;

    if (slot == NULL || optval == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_BROADCAST:
            *(int *)optval = slot->opt_broadcast ? 1 : 0;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_KEEPALIVE:
            *(int *)optval = slot->opt_keepalive ? 1 : 0;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_REUSEADDR:
            *(int *)optval = slot->opt_reuseaddr ? 1 : 0;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_LINGER:
            if (optlen != NULL && *optlen < sizeof(struct linger)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            *(struct linger *)optval = slot->opt_linger;
            if (optlen != NULL) *optlen = sizeof(struct linger);
            break;

        case SO_ERROR:
            *(int *)optval = (int)slot->last_error;
            slot->last_error = 0;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_SNDBUF:
            *(int *)optval = slot->opt_sndbuf ? slot->opt_sndbuf : TCP_SND_BUF;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_RCVBUF:
            *(int *)optval = slot->opt_rcvbuf ? slot->opt_rcvbuf : TCP_WND;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_TYPE:
            *(int *)optval = slot->type;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_OOBINLINE:
            *(int *)optval = slot->opt_oobinline ? 1 : 0;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case SO_RCVTIMEO:
            if (optlen != NULL && *optlen >= sizeof(struct timeval)) {
                *(struct timeval *)optval = slot->opt_rcvtimeo;
                *optlen = sizeof(struct timeval);
            } else {
                *(int *)optval = slot->opt_rcvtimeo.tv_secs * 1000 + slot->opt_rcvtimeo.tv_micro / 1000;
                if (optlen != NULL) *optlen = sizeof(int);
            }
            break;

        case SO_SNDTIMEO:
            if (optlen != NULL && *optlen >= sizeof(struct timeval)) {
                *(struct timeval *)optval = slot->opt_sndtimeo;
                *optlen = sizeof(struct timeval);
            } else {
                *(int *)optval = slot->opt_sndtimeo.tv_secs * 1000 + slot->opt_sndtimeo.tv_micro / 1000;
                if (optlen != NULL) *optlen = sizeof(int);
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
            *(int *)optval = slot->opt_nodelay ? 1 : 0;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case TCP_MAXSEG:
            *(int *)optval = (slot->tcp_pcb != NULL) ? (int)slot->tcp_pcb->mss : TCP_MSS;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case TCP_KEEPIDLE:
            *(int *)optval = (slot->tcp_pcb != NULL) ? (int)(slot->tcp_pcb->keep_idle / 1000UL) : slot->opt_keepidle;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case TCP_KEEPINTVL:
            *(int *)optval = (slot->tcp_pcb != NULL) ? (int)(slot->tcp_pcb->keep_intvl / 1000UL) : slot->opt_keepintvl;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case TCP_KEEPCNT:
            *(int *)optval = (slot->tcp_pcb != NULL) ? (int)slot->tcp_pcb->keep_cnt : slot->opt_keepcnt;
            if (optlen != NULL) *optlen = sizeof(int);
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
            *(int *)optval = (int)slot->opt_tos;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case IP_TTL:
            *(int *)optval = (int)slot->opt_ttl;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case IP_HDRINCL:
            if (slot->type != SOCK_RAW) {
                imsg->result = -1;
                imsg->err_no = ENOPROTOOPT;
                return 0;
            }
            *(int *)optval = (slot->raw_pcb != NULL) ? (raw_is_flag_set(slot->raw_pcb, RAW_FLAGS_HDRINCL) ? 1 : 0) : (slot->opt_hdrincl ? 1 : 0);
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case IP_MULTICAST_TTL:
            *(int *)optval = (int)slot->opt_multicast_ttl;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case IP_MULTICAST_LOOP:
            *(int *)optval = (int)slot->opt_multicast_loop;
            if (optlen != NULL) *optlen = sizeof(int);
            break;

        case IP_ADD_MEMBERSHIP:
        case IP_DROP_MEMBERSHIP:
            imsg->result = -1;
            imsg->err_no = ENOPROTOOPT;
            return 0;

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

int tn_ipc_cmd_ioctl(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    ULONG req = (ULONG)imsg->args[1];
    APTR argp = imsg->ptrs[0];
    (void)d;

    if (slot == NULL || argp == NULL) {
        imsg->result = -1;
        imsg->err_no = (argp == NULL) ? EINVAL : EBADF;
        return 0;
    }

    if (req == FIONBIO) {
        slot->is_nonblocking = (*(ULONG *)argp != 0);
        imsg->result = 0;
        imsg->err_no = 0;
    } else if (req == FIONREAD) {
        ULONG total = 0;
        TnRxPacket *pkt = slot->rx_head;
        while (pkt != NULL) {
            if (pkt->p != NULL) {
                total += (pkt->p->tot_len - pkt->offset);
            }
            pkt = pkt->next;
        }
        *(ULONG *)argp = total;
        imsg->result = 0;
        imsg->err_no = 0;
    } else if (req == SIOCATMARK) {
        *(int *)argp = 0;
        imsg->result = 0;
        imsg->err_no = 0;
    } else if (req == SIOCADDRT || req == SIOCDELRT) {
        imsg->result = -1;
        imsg->err_no = ENOSYS;
    } else if (req == SIOCGIFCONF || req == OSIOCGIFCONF) {
        struct ifconf *ifc = (struct ifconf *)argp;
        struct ifreq *ifr = ifc->ifc_req;
        LONG space = ifc->ifc_len;
        LONG written = 0;
        struct netif *netif;

        NETIF_FOREACH(netif) {
            if (ifr != NULL && space >= (LONG)sizeof(struct ifreq)) {
                char name[IFNAMSIZ];
                int i;
                for (i = 0; i < IFNAMSIZ; i++) ifr->ifr_name[i] = 0;
                if (netif->name[0] == 'e' && netif->name[1] == 't') {
                    int eth_num = 0;
                    if (netif->state != NULL) {
                        eth_num = (int)((TnSana2If *)netif->state)->unit;
                    }
                    name[0] = 'e'; name[1] = 't'; name[2] = 'h';
                    name[3] = (char)('0' + eth_num);
                    name[4] = '\0';
                } else {
                    name[0] = netif->name[0];
                    name[1] = netif->name[1];
                    name[2] = '0';
                    name[3] = '\0';
                }
                for (i = 0; name[i] && i < IFNAMSIZ - 1; i++) {
                    ifr->ifr_name[i] = name[i];
                }
                ifr->ifr_name[i] = '\0';

                {
                    /* TNET-139: client ifreq buffer — byte-wise store, no struct cast */
                    tn_sockin_store_bytes(&ifr->ifr_addr, AF_INET, 0,
                                          netif_ip4_addr(netif)->addr);
                }
                ifr++;
                space -= sizeof(struct ifreq);
            }
            written += sizeof(struct ifreq);
        }
        ifc->ifc_len = written;
        imsg->result = 0;
        imsg->err_no = 0;
    } else if (req == SIOCGIFFLAGS || req == SIOCGIFADDR || req == OSIOCGIFADDR ||
               req == SIOCGIFNETMASK || req == OSIOCGIFNETMASK ||
               req == SIOCGIFBRDADDR || req == OSIOCGIFBRDADDR ||
               req == SIOCGIFMTU) {
        struct ifreq *ifr = (struct ifreq *)argp;
        struct netif *netif = NULL;
        struct netif *n;

        NETIF_FOREACH(n) {
            char standard_name[IFNAMSIZ];
            char lwip_name[IFNAMSIZ];
            int i;
            int if_num = 0;
            BOOL match_std = TRUE;
            BOOL match_lwip = TRUE;

            if (n->name[0] == 'e' && n->name[1] == 't') {
                if (n->state != NULL) {
                    if_num = (int)((TnSana2If *)n->state)->unit;
                }
                standard_name[0] = 'e'; standard_name[1] = 't'; standard_name[2] = 'h';
                standard_name[3] = (char)('0' + if_num); standard_name[4] = '\0';
            } else {
                standard_name[0] = n->name[0]; standard_name[1] = n->name[1];
                standard_name[2] = '0'; standard_name[3] = '\0';
            }
            lwip_name[0] = n->name[0]; lwip_name[1] = n->name[1];
            lwip_name[2] = (char)('0' + if_num); lwip_name[3] = '\0';

            for (i = 0; ifr->ifr_name[i] || standard_name[i]; i++) {
                char c1 = ifr->ifr_name[i];
                char c2 = standard_name[i];
                if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 32);
                if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 32);
                if (c1 != c2) { match_std = FALSE; break; }
            }
            for (i = 0; ifr->ifr_name[i] || lwip_name[i]; i++) {
                char c1 = ifr->ifr_name[i];
                char c2 = lwip_name[i];
                if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 32);
                if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 32);
                if (c1 != c2) { match_lwip = FALSE; break; }
            }
            if (match_std || match_lwip) {
                netif = n;
                break;
            }
        }

        if (netif == NULL && ifr->ifr_name[0] == '\0') {
            netif = (netif_default != NULL) ? netif_default : netif_list;
        }

        if (netif == NULL) {
            imsg->result = -1;
            imsg->err_no = ENXIO;
            return 0;
        }

        if (req == SIOCGIFFLAGS) {
            short flags = 0;
            if (netif_is_up(netif)) flags |= IFF_UP;
            if (netif_is_link_up(netif) || netif_is_up(netif)) flags |= IFF_RUNNING;
            if (netif->flags & NETIF_FLAG_BROADCAST) flags |= IFF_BROADCAST;
            if (netif->flags & NETIF_FLAG_IGMP) flags |= IFF_MULTICAST;
            ifr->ifr_flags = flags;
            imsg->result = 0;
            imsg->err_no = 0;
        } else if (req == SIOCGIFADDR || req == OSIOCGIFADDR) {
            tn_sockin_store_bytes(&ifr->ifr_addr, AF_INET, 0,
                                  netif_ip4_addr(netif)->addr);
            imsg->result = 0;
            imsg->err_no = 0;
        } else if (req == SIOCGIFNETMASK || req == OSIOCGIFNETMASK) {
            tn_sockin_store_bytes(&ifr->ifr_addr, AF_INET, 0,
                                  netif_ip4_netmask(netif)->addr);
            imsg->result = 0;
            imsg->err_no = 0;
        } else if (req == SIOCGIFBRDADDR || req == OSIOCGIFBRDADDR) {
            u32_t ip = netif_ip4_addr(netif)->addr;
            u32_t nm = netif_ip4_netmask(netif)->addr;
            tn_sockin_store_bytes(&ifr->ifr_broadaddr, AF_INET, 0, ip | ~nm);
            imsg->result = 0;
            imsg->err_no = 0;
        } else if (req == SIOCGIFMTU) {
            ifr->ifr_mtu = (LONG)netif->mtu;
            imsg->result = 0;
            imsg->err_no = 0;
        }
    } else {
        imsg->result = -1;
        imsg->err_no = EINVAL;
    }
    return 0;
}

int tn_ipc_cmd_getsockname(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    void *sin_ptr = (void *)imsg->ptrs[0];
    socklen_t *namelen = (socklen_t *)imsg->ptrs[1];
    u16_t port_host = 0;
    uint32_t addr_be = 0;
    (void)d;

    if (slot == NULL || sin_ptr == NULL || namelen == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (*namelen < (socklen_t)sizeof(struct sockaddr_in)) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    if (slot->type == SOCK_STREAM && slot->tcp_pcb != NULL) {
        port_host = slot->tcp_pcb->local_port;
        addr_be   = ip_2_ip4(&slot->tcp_pcb->local_ip)->addr;
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        port_host = slot->udp_pcb->local_port;
        addr_be   = ip_2_ip4(&slot->udp_pcb->local_ip)->addr;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        port_host = (u16_t)slot->protocol;
        addr_be   = ip_2_ip4(&slot->raw_pcb->local_ip)->addr;
    } else {
        port_host = 0;
        addr_be   = 0;
    }

    /* TNET-139: byte-wise store into client buffer */
    tn_sockin_store_bytes(sin_ptr, AF_INET, port_host, addr_be);
    *namelen = sizeof(struct sockaddr_in);
    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_getpeername(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    void *sin_ptr = (void *)imsg->ptrs[0];
    socklen_t *namelen = (socklen_t *)imsg->ptrs[1];
    u16_t port_host = 0;
    uint32_t addr_be = 0;
    (void)d;

    if (slot == NULL || sin_ptr == NULL || namelen == NULL) {
        imsg->result = -1;
        imsg->err_no = EBADF;
        return 0;
    }

    if (*namelen < (socklen_t)sizeof(struct sockaddr_in)) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    if (slot->type == SOCK_STREAM && slot->tcp_pcb != NULL) {
        if (slot->tcp_state != TN_TCP_STATE_ESTABLISHED &&
            slot->tcp_state != TN_TCP_STATE_CONNECTING) {
            imsg->result = -1;
            imsg->err_no = ENOTCONN;
            return 0;
        }
        port_host = slot->tcp_pcb->remote_port;
        addr_be   = ip_2_ip4(&slot->tcp_pcb->remote_ip)->addr;
    } else if (slot->type == SOCK_DGRAM && slot->udp_pcb != NULL) {
        if (slot->udp_pcb->remote_port == 0) {
            imsg->result = -1;
            imsg->err_no = ENOTCONN;
            return 0;
        }
        port_host = slot->udp_pcb->remote_port;
        addr_be   = ip_2_ip4(&slot->udp_pcb->remote_ip)->addr;
    } else if (slot->type == SOCK_RAW && slot->raw_pcb != NULL) {
        port_host = 0;
        addr_be   = ip_2_ip4(&slot->raw_pcb->remote_ip)->addr;
    } else {
        imsg->result = -1;
        imsg->err_no = ENOTCONN;
        return 0;
    }

    /* TNET-139: byte-wise store into client buffer */
    tn_sockin_store_bytes(sin_ptr, AF_INET, port_host, addr_be);
    *namelen = sizeof(struct sockaddr_in);
    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}
