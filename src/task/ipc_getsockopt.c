/*
 * tolunnet — getsockopt IPC handler with Roadshow semantics (SEC item 7).
 *
 * Compiled separately in src/task/ipc_getsockopt.c (Amiga daemon) and
 * included by tests/host/test_sockopt.c (host test).
 * Roadshow semantics: if *optlen < sizeof(int) -> returns -1 with EINVAL, optval untouched;
 * else writes value and updates *optlen.
 */

#include "ipc_socket.h"
#include "slot_table.h"

#ifndef RAW_FLAGS_HDRINCL
#define RAW_FLAGS_HDRINCL 0x01
#endif
#ifndef raw_is_flag_set
#define raw_is_flag_set(pcb, flag) 0
#endif

#define WRITE_OPT_INT(val) do { \
    if (*optlen < sizeof(int)) { \
        imsg->result = -1; \
        imsg->err_no = EINVAL; \
        return 0; \
    } \
    *(int *)optval = (val); \
    *optlen = sizeof(int); \
} while (0)

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
    if (optlen == NULL) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_BROADCAST:
            WRITE_OPT_INT(slot->opt_broadcast ? 1 : 0);
            break;

        case SO_KEEPALIVE:
            WRITE_OPT_INT(slot->opt_keepalive ? 1 : 0);
            break;

        case SO_REUSEADDR:
            WRITE_OPT_INT(slot->opt_reuseaddr ? 1 : 0);
            break;

        case SO_LINGER:
            if (*optlen < sizeof(struct linger)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            *(struct linger *)optval = slot->opt_linger;
            *optlen = sizeof(struct linger);
            break;

        case SO_ERROR:
            if (*optlen < sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            *(int *)optval = (int)slot->last_error;
            slot->last_error = 0;
            *optlen = sizeof(int);
            break;

        case SO_SNDBUF:
            WRITE_OPT_INT(slot->opt_sndbuf ? slot->opt_sndbuf : TCP_SND_BUF);
            break;

        case SO_RCVBUF:
            WRITE_OPT_INT(slot->opt_rcvbuf ? slot->opt_rcvbuf : TCP_WND);
            break;

        case SO_TYPE:
            WRITE_OPT_INT(slot->type);
            break;

        case SO_OOBINLINE:
            WRITE_OPT_INT(slot->opt_oobinline ? 1 : 0);
            break;

        case SO_RCVTIMEO:
            if (*optlen < sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            if (*optlen >= sizeof(struct timeval)) {
                *(struct timeval *)optval = slot->opt_rcvtimeo;
                *optlen = sizeof(struct timeval);
            } else {
                *(int *)optval = slot->opt_rcvtimeo.tv_secs * 1000 + slot->opt_rcvtimeo.tv_micro / 1000;
                *optlen = sizeof(int);
            }
            break;

        case SO_SNDTIMEO:
            if (*optlen < sizeof(int)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return 0;
            }
            if (*optlen >= sizeof(struct timeval)) {
                *(struct timeval *)optval = slot->opt_sndtimeo;
                *optlen = sizeof(struct timeval);
            } else {
                *(int *)optval = slot->opt_sndtimeo.tv_secs * 1000 + slot->opt_sndtimeo.tv_micro / 1000;
                *optlen = sizeof(int);
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
            WRITE_OPT_INT(slot->opt_nodelay ? 1 : 0);
            break;

        case TCP_MAXSEG:
            WRITE_OPT_INT((slot->tcp_pcb != NULL) ? (int)slot->tcp_pcb->mss : TCP_MSS);
            break;

        case TCP_KEEPIDLE:
            WRITE_OPT_INT((slot->tcp_pcb != NULL) ? (int)(slot->tcp_pcb->keep_idle / 1000UL) : slot->opt_keepidle);
            break;

        case TCP_KEEPINTVL:
            WRITE_OPT_INT((slot->tcp_pcb != NULL) ? (int)(slot->tcp_pcb->keep_intvl / 1000UL) : slot->opt_keepintvl);
            break;

        case TCP_KEEPCNT:
            WRITE_OPT_INT((slot->tcp_pcb != NULL) ? (int)slot->tcp_pcb->keep_cnt : slot->opt_keepcnt);
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
            WRITE_OPT_INT((int)slot->opt_tos);
            break;

        case IP_TTL:
            WRITE_OPT_INT((int)slot->opt_ttl);
            break;

        case IP_HDRINCL:
            if (slot->type != SOCK_RAW) {
                imsg->result = -1;
                imsg->err_no = ENOPROTOOPT;
                return 0;
            }
            WRITE_OPT_INT((slot->raw_pcb != NULL) ? (raw_is_flag_set(slot->raw_pcb, RAW_FLAGS_HDRINCL) ? 1 : 0) : (slot->opt_hdrincl ? 1 : 0));
            break;

        case IP_MULTICAST_TTL:
            WRITE_OPT_INT((int)slot->opt_multicast_ttl);
            break;

        case IP_MULTICAST_LOOP:
            WRITE_OPT_INT((int)slot->opt_multicast_loop);
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
#undef WRITE_OPT_INT
