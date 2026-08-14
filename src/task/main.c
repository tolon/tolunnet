/*
 * tolunet — Network Task & lwIP Core Mainloop.
 *
 * Master prompt §M2 (IP Alive), §M3 (bsdsocket.library Skeleton), §M4 (UDP / DNS), & §M5 (TCP).
 * - One Amiga task owns lwIP (NO_SYS=1).
 * - SANA-II driver (ethernet.device) wired to lwIP netif.
 * - Periodic 100 ms timer.device ticks drive sys_check_timeouts().
 * - bsdsocket.library instantiated and registered in Exec library list.
 * - Public IPC port ("tolunet.port") dispatches client socket requests.
 * - Full UDP & TCP stream transport + DNS client support.
 * - Clean shutdown on Ctrl-C (SIGBREAKF_CTRL_C).
 */

#include "../sana2/sana2_netif.h"
#include "../lib/lib_init.h"
#include "../common/log.h"
#include "../common/prefs.h"
#include "timers.h"
#include "../../include/ipc.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <sys/errno.h>

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"
#include "lwip/udp.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "netif/ethernet.h"

#define TN_MAX_GLOBAL_SOCKETS 64

/* TCP Socket State Machine */
typedef enum TnTcpState {
    TN_TCP_STATE_CLOSED = 0,
    TN_TCP_STATE_CONNECTING,
    TN_TCP_STATE_ESTABLISHED,
    TN_TCP_STATE_LISTENING,
    TN_TCP_STATE_PEER_CLOSED,
    TN_TCP_STATE_ERROR
} TnTcpState;

/* Linked list node for queued incoming packets / stream chunks */
typedef struct TnRxPacket {
    struct TnRxPacket *next;
    struct pbuf       *p;
    u16_t              offset;
    ip_addr_t          src_ip;
    u16_t              src_port;
} TnRxPacket;

#define TN_MAX_RX_QUEUE_PER_SOCKET 32

/* Internal socket descriptor representation */
typedef struct TnSocketSlot {
    BOOL            in_use;
    TnSocketBase   *owner_base;
    struct Task    *owner_task;
    int             domain;
    int             type;
    int             protocol;
    TnTcpState      tcp_state;
    BOOL            is_nonblocking;
    BOOL            opt_reuseaddr;
    BOOL            opt_keepalive;
    BOOL            opt_nodelay;
    LONG            last_error;
    ULONG           rx_count;
    int             ref_count;
    struct udp_pcb *udp_pcb;
    struct tcp_pcb *tcp_pcb;
    TnRxPacket     *rx_head;
    TnRxPacket     *rx_tail;
    TnIpcMsg       *pending_connect_msg;
} TnSocketSlot;

static TnSana2If     g_s2if;
static struct netif  g_netif;
static TnTimer       g_timer;
static struct MsgPort *g_ipc_port = NULL;
static struct Library *g_bsd_lib  = NULL;
static TnSocketSlot  g_sockets[TN_MAX_GLOBAL_SOCKETS];

/* Callback from lwIP when a UDP datagram arrives on a listening PCB */
static void tn_udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                           const ip_addr_t *addr, u16_t port)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    TnRxPacket *pkt;
    (void)pcb;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        if (p != NULL) pbuf_free(p);
        return;
    }

    slot = &g_sockets[slot_idx];
    if (!slot->in_use || p == NULL) {
        if (p != NULL) pbuf_free(p);
        return;
    }

    if (slot->rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        /* Drop datagram to protect memory from flood DoS */
        pbuf_free(p);
        return;
    }

    pkt = (TnRxPacket *)AllocVec(sizeof(TnRxPacket), MEMF_CLEAR | MEMF_PUBLIC);
    if (pkt == NULL) {
        pbuf_free(p);
        return;
    }

    pkt->p        = p;
    pkt->offset   = 0;
    pkt->src_ip   = *addr;
    pkt->src_port = port;
    pkt->next     = NULL;

    if (slot->rx_tail != NULL) {
        slot->rx_tail->next = pkt;
        slot->rx_tail       = pkt;
    } else {
        slot->rx_head       = pkt;
        slot->rx_tail       = pkt;
    }
    slot->rx_count++;
}

/* Callback from lwIP when TCP stream data or FIN arrives */
static err_t tn_tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    TnRxPacket *pkt;
    (void)pcb; (void)err;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        if (p != NULL) pbuf_free(p);
        return ERR_OK;
    }

    slot = &g_sockets[slot_idx];
    if (!slot->in_use) {
        if (p != NULL) pbuf_free(p);
        return ERR_OK;
    }

    /* Peer closed connection (FIN received) */
    if (p == NULL) {
        slot->tcp_state = TN_TCP_STATE_PEER_CLOSED;
        return ERR_OK;
    }

    if (slot->rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        /* Queue full: return ERR_MEM without freeing pbuf so lwIP holds it (TNET-050) */
        return ERR_MEM;
    }

    pkt = (TnRxPacket *)AllocVec(sizeof(TnRxPacket), MEMF_CLEAR | MEMF_PUBLIC);
    if (pkt == NULL) {
        /* Memory alloc failed: return ERR_MEM without freeing pbuf (TNET-039) */
        return ERR_MEM;
    }

    pkt->p        = p;
    pkt->offset   = 0;
    pkt->next     = NULL;
    slot->rx_count++;

    if (slot->rx_tail != NULL) {
        slot->rx_tail->next = pkt;
        slot->rx_tail       = pkt;
    } else {
        slot->rx_head       = pkt;
        slot->rx_tail       = pkt;
    }

    return ERR_OK;
}

/* Callback from lwIP when TCP connection handshake succeeds */
static err_t tn_tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    (void)pcb; (void)err;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return ERR_OK;

    slot = &g_sockets[slot_idx];
    if (!slot->in_use) return ERR_OK;

    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    if (slot->pending_connect_msg != NULL) {
        slot->pending_connect_msg->result = 0;
        slot->pending_connect_msg->err_no = 0;
        ReplyMsg((struct Message *)slot->pending_connect_msg);
        slot->pending_connect_msg = NULL;
    }

    return ERR_OK;
}

/* Callback from lwIP on TCP errors (RST, timeout, etc.) */
static void tn_tcp_err_cb(void *arg, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    (void)err;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return;

    slot = &g_sockets[slot_idx];
    slot->tcp_state = TN_TCP_STATE_ERROR;
    slot->tcp_pcb   = NULL; /* lwIP frees PCB before calling err_cb */

    if (slot->pending_connect_msg != NULL) {
        slot->pending_connect_msg->result = -1;
        slot->pending_connect_msg->err_no = ECONNREFUSED;
        ReplyMsg((struct Message *)slot->pending_connect_msg);
        slot->pending_connect_msg = NULL;
    }
}

static void ip_to_str(char *buf, const ip4_addr_t *addr)
{
    ULONG ip = lwip_ntohl(addr->addr);
    char *p = buf;
    int octet;

    for (octet = 3; octet >= 0; octet--) {
        ULONG val = (ip >> (octet * 8)) & 0xFF;
        char tmp[4];
        int i = 0;
        if (val == 0) tmp[i++] = '0';
        while (val > 0) {
            tmp[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
        while (i > 0) *p++ = tmp[--i];
        if (octet > 0) *p++ = '.';
    }
    *p = '\0';
}

static void tn_free_socket_slot(int slot_idx)
{
    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return;
    if (g_sockets[slot_idx].udp_pcb != NULL) {
        udp_remove(g_sockets[slot_idx].udp_pcb);
        g_sockets[slot_idx].udp_pcb = NULL;
    }
    if (g_sockets[slot_idx].tcp_pcb != NULL) {
        tcp_arg(g_sockets[slot_idx].tcp_pcb, NULL);
        tcp_recv(g_sockets[slot_idx].tcp_pcb, NULL);
        tcp_err(g_sockets[slot_idx].tcp_pcb, NULL);
        tcp_close(g_sockets[slot_idx].tcp_pcb);
        g_sockets[slot_idx].tcp_pcb = NULL;
    }
    while (g_sockets[slot_idx].rx_head != NULL) {
        TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
        g_sockets[slot_idx].rx_head = pkt->next;
        if (pkt->p != NULL) pbuf_free(pkt->p);
        FreeVec(pkt);
    }
    g_sockets[slot_idx].rx_tail             = NULL;
    g_sockets[slot_idx].rx_count            = 0;
    g_sockets[slot_idx].ref_count           = 0;
    g_sockets[slot_idx].in_use              = FALSE;
    g_sockets[slot_idx].owner_base          = NULL;
    g_sockets[slot_idx].pending_connect_msg = NULL;
}

/*
 * Handle client IPC requests inside the network task context.
 * Returns TRUE if message should be replied immediately, FALSE if delayed/async.
 */
static BOOL tn_handle_ipc(TnIpcMsg *imsg)
{
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    int i, slot_idx;

    imsg->result = 0;
    imsg->err_no = 0;

    switch (imsg->cmd) {
    case TN_IPC_CMD_OPEN:
        tn_logf(TN_LOG_BASIC, "tolunnet: client task 0x%p opened bsdsocket.library\n",
                imsg->client_task);
        imsg->result = 0;
        return TRUE;

    case TN_IPC_CMD_CLOSE:
        tn_logf(TN_LOG_BASIC, "tolunnet: client task 0x%p closing bsdsocket.library\n",
                imsg->client_task);
        /* Clean up all sockets referenced by this client SocketBase */
        if (base != NULL) {
            for (i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
                slot_idx = base->fd_map[i];
                if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS && g_sockets[slot_idx].in_use) {
                    g_sockets[slot_idx].ref_count--;
                    if (g_sockets[slot_idx].ref_count <= 0) {
                        tn_free_socket_slot(slot_idx);
                    }
                    base->fd_map[i] = -1;
                }
            }
        }
        imsg->result = 0;
        return TRUE;

    case TN_IPC_CMD_SOCKET:
        {
            int domain   = (int)imsg->args[0];
            int type     = (int)imsg->args[1];
            int protocol = (int)imsg->args[2];
            int client_fd = -1;

            if (domain != 2 /* AF_INET */) {
                imsg->result = -1;
                imsg->err_no = EAFNOSUPPORT;
                return TRUE;
            }

            /* Find free client fd */
            if (base != NULL) {
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
                return TRUE;
            }

            /* Find free global socket slot */
            slot_idx = -1;
            for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
                if (!g_sockets[i].in_use) {
                    slot_idx = i;
                    break;
                }
            }

            if (slot_idx < 0) {
                imsg->result = -1;
                imsg->err_no = ENFILE;
                return TRUE;
            }

            g_sockets[slot_idx].in_use              = TRUE;
            g_sockets[slot_idx].owner_base          = base;
            g_sockets[slot_idx].owner_task          = imsg->client_task;
            g_sockets[slot_idx].domain              = domain;
            g_sockets[slot_idx].type                = type;
            g_sockets[slot_idx].protocol            = protocol;
            g_sockets[slot_idx].tcp_state           = TN_TCP_STATE_CLOSED;
            g_sockets[slot_idx].udp_pcb             = NULL;
            g_sockets[slot_idx].tcp_pcb             = NULL;
            g_sockets[slot_idx].rx_head             = NULL;
            g_sockets[slot_idx].rx_tail             = NULL;
            g_sockets[slot_idx].pending_connect_msg = NULL;

            /* UDP socket */
            if (type == 2 /* SOCK_DGRAM */) {
                g_sockets[slot_idx].udp_pcb = udp_new();
                if (g_sockets[slot_idx].udp_pcb == NULL) {
                    g_sockets[slot_idx].in_use = FALSE;
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
                }
                udp_recv(g_sockets[slot_idx].udp_pcb, tn_udp_recv_cb, (void *)(intptr_t)slot_idx);
            }
            /* TCP stream socket */
            else if (type == 1 /* SOCK_STREAM */) {
                g_sockets[slot_idx].tcp_pcb = tcp_new();
                if (g_sockets[slot_idx].tcp_pcb == NULL) {
                    g_sockets[slot_idx].in_use = FALSE;
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
                }
                tcp_arg(g_sockets[slot_idx].tcp_pcb, (void *)(intptr_t)slot_idx);
                tcp_err(g_sockets[slot_idx].tcp_pcb, tn_tcp_err_cb);
            }

            base->fd_map[client_fd] = slot_idx;

            tn_logf(TN_LOG_BASIC, "tolunnet: socket(domain=%d, type=%d, proto=%d) -> fd %d (slot %d)\n",
                    domain, type, protocol, client_fd, slot_idx);

            imsg->result = client_fd;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_CONNECT:
        {
            int client_fd = (int)imsg->args[0];
            struct sockaddr_in *sin = (struct sockaddr_in *)imsg->ptrs[0];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || sin == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (g_sockets[slot_idx].type == 1 /* SOCK_STREAM */ && g_sockets[slot_idx].tcp_pcb != NULL) {
                ip_addr_t dst_ip;
                u16_t dst_port = lwip_ntohs(sin->sin_port);
                err_t cerr;

                ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);

                g_sockets[slot_idx].tcp_state           = TN_TCP_STATE_CONNECTING;
                g_sockets[slot_idx].pending_connect_msg = imsg;

                tcp_recv(g_sockets[slot_idx].tcp_pcb, tn_tcp_recv_cb);
                cerr = tcp_connect(g_sockets[slot_idx].tcp_pcb, &dst_ip, dst_port, tn_tcp_connected_cb);
                if (cerr == ERR_OK) {
                    tcp_output(g_sockets[slot_idx].tcp_pcb);
                }

                if (cerr != ERR_OK) {
                    g_sockets[slot_idx].pending_connect_msg = NULL;
                    g_sockets[slot_idx].tcp_state           = TN_TCP_STATE_CLOSED;
                    imsg->result = -1;
                    imsg->err_no = ECONNREFUSED;
                    return TRUE;
                }

                /* Delayed reply: tn_tcp_connected_cb or tn_tcp_err_cb will call ReplyMsg */
                return FALSE;
            }

            imsg->result = 0;
            return TRUE;
        }

    case TN_IPC_CMD_SEND:
        {
            int client_fd = (int)imsg->args[0];
            const void *buf = (const void *)imsg->ptrs[0];
            LONG len = imsg->args[1];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || buf == NULL || len < 0) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (g_sockets[slot_idx].type == 1 /* SOCK_STREAM */ && g_sockets[slot_idx].tcp_pcb != NULL) {
                err_t werr;
                u16_t send_len;
                u16_t snd_buf;

                if (g_sockets[slot_idx].tcp_state != TN_TCP_STATE_ESTABLISHED) {
                    imsg->result = -1;
                    imsg->err_no = (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_ERROR) ? ECONNRESET : ENOTCONN;
                    return TRUE;
                }

                snd_buf = tcp_sndbuf(g_sockets[slot_idx].tcp_pcb);
                if (snd_buf == 0) {
                    imsg->result = -1;
                    imsg->err_no = EWOULDBLOCK;
                    return TRUE;
                }

                send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
                if (send_len > snd_buf) send_len = snd_buf;

                werr = tcp_write(g_sockets[slot_idx].tcp_pcb, buf, send_len, TCP_WRITE_FLAG_COPY);
                if (werr != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
                }

                tcp_output(g_sockets[slot_idx].tcp_pcb);
                imsg->result = (LONG)send_len;
                imsg->err_no = 0;
                return TRUE;
            }

            imsg->result = -1;
            imsg->err_no = EOPNOTSUPP;
            return TRUE;
        }

    case TN_IPC_CMD_RECV:
        {
            int client_fd = (int)imsg->args[0];
            void *buf = imsg->ptrs[0];
            LONG len = imsg->args[1];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || buf == NULL || len < 0) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (g_sockets[slot_idx].type == 1 /* SOCK_STREAM */) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
                    u16_t avail = pkt->p->tot_len - pkt->offset;
                    u16_t to_copy = (avail < (u16_t)len) ? avail : (u16_t)len;

                    pbuf_copy_partial(pkt->p, buf, to_copy, pkt->offset);
                    pkt->offset += to_copy;

                    if (g_sockets[slot_idx].tcp_pcb != NULL) {
                        tcp_recved(g_sockets[slot_idx].tcp_pcb, to_copy);
                    }

                    if (pkt->offset >= pkt->p->tot_len) {
                        g_sockets[slot_idx].rx_head = pkt->next;
                        if (g_sockets[slot_idx].rx_head == NULL) {
                            g_sockets[slot_idx].rx_tail = NULL;
                        }
                        pbuf_free(pkt->p);
                        FreeVec(pkt);
                        if (g_sockets[slot_idx].rx_count > 0) {
                            g_sockets[slot_idx].rx_count--;
                        }
                    }

                    imsg->result = (LONG)to_copy;
                    imsg->err_no = 0;
                    return TRUE;
                } else if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_PEER_CLOSED) {
                    imsg->result = 0; /* EOF */
                    imsg->err_no = 0;
                    return TRUE;
                } else if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_ERROR) {
                    imsg->result = -1; /* Connection reset (TNET-049) */
                    imsg->err_no = ECONNRESET;
                    return TRUE;
                } else {
                    imsg->result = -1;
                    imsg->err_no = EWOULDBLOCK;
                    return TRUE;
                }
            }

            imsg->result = -1;
            imsg->err_no = EOPNOTSUPP;
            return TRUE;
        }

    case TN_IPC_CMD_SENDTO:
        {
            int client_fd = (int)imsg->args[0];
            const void *buf = (const void *)imsg->ptrs[0];
            LONG len = imsg->args[1];
            const struct sockaddr_in *to = (const struct sockaddr_in *)imsg->ptrs[1];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (g_sockets[slot_idx].type == 2 /* SOCK_DGRAM */ && g_sockets[slot_idx].udp_pcb != NULL) {
                struct pbuf *p;
                ip_addr_t dst_ip;
                u16_t dst_port;
                u16_t send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;

                p = pbuf_alloc(PBUF_TRANSPORT, send_len, PBUF_RAM);
                if (p == NULL) {
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
                }

                pbuf_take(p, buf, send_len);

                if (to != NULL) {
                    ip_addr_set_ip4_u32(&dst_ip, to->sin_addr.s_addr);
                    dst_port = lwip_ntohs(to->sin_port);
                } else {
                    dst_ip = g_sockets[slot_idx].udp_pcb->remote_ip;
                    dst_port = g_sockets[slot_idx].udp_pcb->remote_port;
                }

                udp_sendto(g_sockets[slot_idx].udp_pcb, p, &dst_ip, dst_port);
                pbuf_free(p);

                imsg->result = (LONG)send_len;
                imsg->err_no = 0;
                return TRUE;
            }

            imsg->result = -1;
            imsg->err_no = EOPNOTSUPP;
            return TRUE;
        }

    case TN_IPC_CMD_RECVFROM:
        {
            int client_fd = (int)imsg->args[0];
            void *buf = imsg->ptrs[0];
            LONG len = imsg->args[1];
            struct sockaddr_in *from = (struct sockaddr_in *)imsg->ptrs[1];
            socklen_t *fromlen = (socklen_t *)imsg->ptrs[2];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || buf == NULL || len < 0) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (g_sockets[slot_idx].type == 2 /* SOCK_DGRAM */) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
                    u16_t copied;

                    g_sockets[slot_idx].rx_head = pkt->next;
                    if (g_sockets[slot_idx].rx_head == NULL) {
                        g_sockets[slot_idx].rx_tail = NULL;
                    }

                    copied = pbuf_copy_partial(pkt->p, buf, (u16_t)len, 0);

                    if (from != NULL) {
                        from->sin_len = sizeof(struct sockaddr_in); /* TNET-055 */
                        from->sin_family = 2 /* AF_INET */;
                        from->sin_port   = lwip_htons(pkt->src_port);
                        from->sin_addr.s_addr = ip_addr_get_ip4_u32(&pkt->src_ip);
                        if (fromlen != NULL) *fromlen = sizeof(struct sockaddr_in);
                    }

                    pbuf_free(pkt->p);
                    FreeVec(pkt);
                    if (g_sockets[slot_idx].rx_count > 0) {
                        g_sockets[slot_idx].rx_count--;
                    }

                    imsg->result = (LONG)copied;
                    imsg->err_no = 0;
                    return TRUE;
                } else {
                    imsg->result = -1;
                    imsg->err_no = EWOULDBLOCK;
                    return TRUE;
                }
            }

            imsg->result = -1;
            imsg->err_no = EOPNOTSUPP;
            return TRUE;
        }

    case TN_IPC_CMD_GETHOSTBYNAME:
        {
            const char *hostname = (const char *)imsg->ptrs[0];
            ip4_addr_t resolved;

            if (base == NULL || hostname == NULL || *hostname == '\0') {
                imsg->result = 0;
                imsg->err_no = ENOENT;
                return TRUE;
            }

            if (ip4addr_aton(hostname, &resolved)) {
                int j = 0;
                base->hostent_addr = resolved.addr;
                while (hostname[j] && j < 63) {
                    base->hostent_name[j] = hostname[j];
                    j++;
                }
                base->hostent_name[j] = '\0';
                base->hostent_addrs[0] = (STRPTR)&base->hostent_addr;
                base->hostent_addrs[1] = NULL;
                base->hostent_aliases[0] = NULL;
                base->hostent_data.h_name      = (STRPTR)base->hostent_name;
                base->hostent_data.h_aliases   = base->hostent_aliases;
                base->hostent_data.h_addrtype  = 2 /* AF_INET */;
                base->hostent_data.h_length    = 4;
                base->hostent_data.h_addr_list = (char **)base->hostent_addrs;

                imsg->result = (LONG)(intptr_t)&base->hostent_data;
                imsg->err_no = 0;
            } else {
                ip_addr_t dns_res;
                err_t derr = dns_gethostbyname(hostname, &dns_res, NULL, NULL);
                if (derr == ERR_OK) {
                    int j = 0;
                    base->hostent_addr = ip_addr_get_ip4_u32(&dns_res);
                    while (hostname[j] && j < 63) {
                        base->hostent_name[j] = hostname[j];
                        j++;
                    }
                    base->hostent_name[j] = '\0';
                    base->hostent_addrs[0] = (STRPTR)&base->hostent_addr;
                    base->hostent_addrs[1] = NULL;
                    base->hostent_aliases[0] = NULL;
                    base->hostent_data.h_name      = (STRPTR)base->hostent_name;
                    base->hostent_data.h_aliases   = base->hostent_aliases;
                    base->hostent_data.h_addrtype  = 2 /* AF_INET */;
                    base->hostent_data.h_length    = 4;
                    base->hostent_data.h_addr_list = (char **)base->hostent_addrs;

                    imsg->result = (LONG)(intptr_t)&base->hostent_data;
                    imsg->err_no = 0;
                } else {
                    imsg->result = 0; /* NULL */
                    imsg->err_no = ENOENT;
                }
            }
            return TRUE;
        }

    case TN_IPC_CMD_CLOSESOCKET:
        {
            int client_fd = (int)imsg->args[0];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (g_sockets[slot_idx].udp_pcb != NULL) {
                udp_remove(g_sockets[slot_idx].udp_pcb);
                g_sockets[slot_idx].udp_pcb = NULL;
            }

            if (g_sockets[slot_idx].tcp_pcb != NULL) {
                tcp_arg(g_sockets[slot_idx].tcp_pcb, NULL);
                tcp_recv(g_sockets[slot_idx].tcp_pcb, NULL);
                tcp_err(g_sockets[slot_idx].tcp_pcb, NULL);
                tcp_close(g_sockets[slot_idx].tcp_pcb);
                g_sockets[slot_idx].tcp_pcb = NULL;
            }

            while (g_sockets[slot_idx].rx_head != NULL) {
                TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
                g_sockets[slot_idx].rx_head = pkt->next;
                if (pkt->p != NULL) pbuf_free(pkt->p);
                FreeVec(pkt);
            }
            g_sockets[slot_idx].rx_tail             = NULL;
            g_sockets[slot_idx].rx_count            = 0;
            g_sockets[slot_idx].in_use              = FALSE;
            g_sockets[slot_idx].owner_base          = NULL;
            g_sockets[slot_idx].pending_connect_msg = NULL;
            base->fd_map[client_fd] = -1;

            tn_logf(TN_LOG_BASIC, "tolunnet: CloseSocket(fd=%d, slot=%d) -> ok\n",
                    client_fd, slot_idx);

            imsg->result = 0;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_IOCTL:
        {
            int client_fd = (int)imsg->args[0];
            ULONG req = (ULONG)imsg->args[1];
            APTR argp = imsg->ptrs[0];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || argp == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (req == 0x8004667eUL /* FIONBIO */) {
                g_sockets[slot_idx].is_nonblocking = (*(ULONG *)argp != 0);
                imsg->result = 0;
                imsg->err_no = 0;
            } else if (req == 0x4004667fUL /* FIONREAD */) {
                ULONG total = 0;
                TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
                while (pkt != NULL) {
                    if (pkt->p != NULL) {
                        total += (pkt->p->tot_len - pkt->offset);
                    }
                    pkt = pkt->next;
                }
                *(ULONG *)argp = total;
                imsg->result = 0;
                imsg->err_no = 0;
            } else {
                imsg->result = 0;
                imsg->err_no = 0;
            }
            return TRUE;
        }

    case TN_IPC_CMD_SETSOCKOPT:
        {
            int client_fd = (int)imsg->args[0];
            LONG level = imsg->args[1];
            LONG optname = imsg->args[2];
            const void *optval = (const void *)imsg->ptrs[0];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || optval == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (level == 0xffff /* SOL_SOCKET */) {
                if (optname == 0x0004 /* SO_REUSEADDR */) {
                    g_sockets[slot_idx].opt_reuseaddr = (*(const int *)optval != 0);
                } else if (optname == 0x0008 /* SO_KEEPALIVE */) {
                    g_sockets[slot_idx].opt_keepalive = (*(const int *)optval != 0);
                    if (g_sockets[slot_idx].tcp_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_keepalive) {
                            ip_set_option(g_sockets[slot_idx].tcp_pcb, SOF_KEEPALIVE);
                        } else {
                            ip_reset_option(g_sockets[slot_idx].tcp_pcb, SOF_KEEPALIVE);
                        }
                    }
                }
                imsg->result = 0;
                imsg->err_no = 0;
            } else if (level == 6 /* IPPROTO_TCP */) {
                if (optname == 0x0001 /* TCP_NODELAY */) {
                    g_sockets[slot_idx].opt_nodelay = (*(const int *)optval != 0);
                    if (g_sockets[slot_idx].tcp_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_nodelay) {
                            tcp_nagle_disable(g_sockets[slot_idx].tcp_pcb);
                        } else {
                            tcp_nagle_enable(g_sockets[slot_idx].tcp_pcb);
                        }
                    }
                }
                imsg->result = 0;
                imsg->err_no = 0;
            } else {
                imsg->result = 0;
                imsg->err_no = 0;
            }
            return TRUE;
        }

    case TN_IPC_CMD_GETSOCKOPT:
        {
            int client_fd = (int)imsg->args[0];
            LONG level = imsg->args[1];
            LONG optname = imsg->args[2];
            void *optval = imsg->ptrs[0];
            socklen_t *optlen = (socklen_t *)imsg->ptrs[1];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || optval == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (level == 0xffff /* SOL_SOCKET */) {
                if (optname == 0x0004 /* SO_REUSEADDR */) {
                    *(int *)optval = g_sockets[slot_idx].opt_reuseaddr ? 1 : 0;
                } else if (optname == 0x0008 /* SO_KEEPALIVE */) {
                    *(int *)optval = g_sockets[slot_idx].opt_keepalive ? 1 : 0;
                } else if (optname == 0x1007 /* SO_ERROR */) {
                    *(int *)optval = (int)g_sockets[slot_idx].last_error;
                    g_sockets[slot_idx].last_error = 0;
                } else {
                    *(int *)optval = 0;
                }
                if (optlen != NULL) *optlen = sizeof(int);
                imsg->result = 0;
                imsg->err_no = 0;
            } else if (level == 6 /* IPPROTO_TCP */) {
                if (optname == 0x0001 /* TCP_NODELAY */) {
                    *(int *)optval = g_sockets[slot_idx].opt_nodelay ? 1 : 0;
                } else {
                    *(int *)optval = 0;
                }
                if (optlen != NULL) *optlen = sizeof(int);
                imsg->result = 0;
                imsg->err_no = 0;
            } else {
                if (optlen != NULL) *optlen = sizeof(int);
                *(int *)optval = 0;
                imsg->result = 0;
                imsg->err_no = 0;
            }
            return TRUE;
        }

    case TN_IPC_CMD_WAITSELECT:
        {
            LONG nfds = imsg->args[0];
            ULONG *rfds = (ULONG *)imsg->ptrs[0];
            ULONG *wfds = (ULONG *)imsg->ptrs[1];
            ULONG *efds = (ULONG *)imsg->ptrs[2];
            ULONG in_r = rfds ? *rfds : 0;
            ULONG in_w = wfds ? *wfds : 0;
            ULONG out_r = 0, out_w = 0, out_e = 0;
            LONG ready_cnt = 0;

            if (base == NULL || nfds < 0) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            if (nfds > TN_MAX_FDS_PER_TASK) nfds = TN_MAX_FDS_PER_TASK;

            for (i = 0; i < nfds; i++) {
                slot_idx = base->fd_map[i];
                if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS && g_sockets[slot_idx].in_use) {
                    /* Read readiness */
                    if (in_r & (1UL << i)) {
                        if (g_sockets[slot_idx].rx_head != NULL ||
                            g_sockets[slot_idx].tcp_state == TN_TCP_STATE_PEER_CLOSED) {
                            out_r |= (1UL << i);
                            ready_cnt++;
                        }
                    }
                    /* Write readiness */
                    if (in_w & (1UL << i)) {
                        if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_ESTABLISHED ||
                            g_sockets[slot_idx].type == 2 /* UDP */) {
                            out_w |= (1UL << i);
                            ready_cnt++;
                        }
                    }
                }
            }

            if (rfds) *rfds = out_r;
            if (wfds) *wfds = out_w;
            if (efds) *efds = out_e;

            imsg->result = ready_cnt;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_DUP2:
        {
            int old_fd = (int)imsg->args[0];
            int new_fd = (int)imsg->args[1];

            if (base == NULL || old_fd < 0 || old_fd >= TN_MAX_FDS_PER_TASK ||
                new_fd < 0 || new_fd >= TN_MAX_FDS_PER_TASK) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            int old_slot = base->fd_map[old_fd];
            if (old_slot < 0 || old_slot >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[old_slot].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (old_fd == new_fd) {
                imsg->result = new_fd;
                imsg->err_no = 0;
                return TRUE;
            }

            /* If new_fd was already open, release it cleanly (TNET-048) */
            int existing_new_slot = base->fd_map[new_fd];
            if (existing_new_slot >= 0 && existing_new_slot < TN_MAX_GLOBAL_SOCKETS && g_sockets[existing_new_slot].in_use) {
                g_sockets[existing_new_slot].ref_count--;
                if (g_sockets[existing_new_slot].ref_count <= 0) {
                    tn_free_socket_slot(existing_new_slot);
                }
            }

            base->fd_map[new_fd] = old_slot;
            g_sockets[old_slot].ref_count++;

            imsg->result = new_fd;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_GETSTATUS:
        {
            /* Return live IP configuration and active socket count (TNET-043) */
            imsg->args[0] = (LONG)ip_addr_get_ip4_u32(&g_netif.ip_addr);
            imsg->args[1] = (LONG)ip_addr_get_ip4_u32(&g_netif.netmask);
            imsg->args[2] = (LONG)ip_addr_get_ip4_u32(&g_netif.gw);

            int active_socks = 0;
            for (int s = 0; s < TN_MAX_GLOBAL_SOCKETS; s++) {
                if (g_sockets[s].in_use) active_socks++;
            }
            imsg->args[3] = active_socks;

            imsg->result = 0;
            imsg->err_no = 0;
            return TRUE;
        }

    default:
        imsg->result = -1;
        imsg->err_no = ENOSYS;
        return TRUE;
    }
}

int main(int argc, char *argv[])
{
    struct ExecBase *SysBase;
    struct Library  *DOSBase;
    CONST_STRPTR device = (CONST_STRPTR)"ethernet.device";
    ULONG unit = 0;
    TnS2Result s2res;
    ip4_addr_t ipaddr, netmask, gw, dns_server;
    ULONG s2_sig, timer_sig, ipc_sig, ctrl_c_sig, wait_mask;
    BOOL running = TRUE;
    BOOL use_dhcp = TRUE;
    BOOL dhcp_logged = FALSE;
    BPTR log_fh = (BPTR)0;
    ULONG tick_count = 0;
    int i;

    SysBase = *(struct ExecBase **)4UL;
    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        g_sockets[i].in_use              = FALSE;
        g_sockets[i].owner_base          = NULL;
        g_sockets[i].udp_pcb             = NULL;
        g_sockets[i].tcp_pcb             = NULL;
        g_sockets[i].rx_head             = NULL;
        g_sockets[i].rx_tail             = NULL;
        g_sockets[i].pending_connect_msg = NULL;
    }

    if (argc >= 2) {
        device = (CONST_STRPTR)argv[1];
        if (argc >= 3) {
            LONG parsed = 0;
            LONG consumed = StrToLong((CONST_STRPTR)argv[2], &parsed);
            if (consumed > 0) unit = (ULONG)parsed;
        }
        if (argc >= 4) {
            ip4addr_aton(argv[3], &ipaddr);
            if (argc >= 5) ip4addr_aton(argv[4], &netmask);
            else IP4_ADDR(&netmask, 255, 255, 255, 0);
            if (argc >= 6) ip4addr_aton(argv[5], &gw);
            else IP4_ADDR(&gw, 10, 0, 2, 2);
            use_dhcp = FALSE;
        } else {
            IP4_ADDR(&ipaddr, 0, 0, 0, 0);
            IP4_ADDR(&netmask, 0, 0, 0, 0);
            IP4_ADDR(&gw, 0, 0, 0, 0);
        }
    } else {
        /* No CLI args: load from ENVARC:tolunnet.prefs / ENV:tolunnet.prefs */
        TnPrefs prefs;
        if (tn_prefs_load(&prefs)) {
            device = (CONST_STRPTR)prefs.device;
            unit = prefs.unit;
            use_dhcp = prefs.use_dhcp;
            if (!use_dhcp) {
                ip4addr_aton(prefs.ip_addr, &ipaddr);
                ip4addr_aton(prefs.netmask, &netmask);
                ip4addr_aton(prefs.gateway, &gw);
            } else {
                IP4_ADDR(&ipaddr, 0, 0, 0, 0);
                IP4_ADDR(&netmask, 0, 0, 0, 0);
                IP4_ADDR(&gw, 0, 0, 0, 0);
            }
        } else {
            /* Default fallback */
            IP4_ADDR(&ipaddr, 0, 0, 0, 0);
            IP4_ADDR(&netmask, 0, 0, 0, 0);
            IP4_ADDR(&gw, 0, 0, 0, 0);
        }
    }

    tn_log(TN_LOG_BASIC, "========================================\n");
    tn_log(TN_LOG_BASIC, "tolunnet: network task starting...\n");
    tn_log(TN_LOG_BASIC, "========================================\n");

    log_fh = Open((CONST_STRPTR)"WORK:tolunnet-task.log", MODE_NEWFILE);
    g_log_file = log_fh;

    /* 1. Initialize timer.device */
    if (!tn_timer_init(&g_timer)) {
        tn_log(TN_LOG_BASIC, "tolunnet: failed to initialize timer.device\n");
        CloseLibrary(DOSBase);
        return 20;
    }

    /* Initialize hardware PRNG entropy BEFORE lwip_init (TNET-047) */
    tn_rand_init(&g_timer, NULL);

    /* 2. Initialize lwIP stack core */
    lwip_init();
    tn_log(TN_LOG_BASIC, "tolunnet: lwIP 2.2.0 initialized (NO_SYS=1)\n");

    /* 3. Open SANA-II network device */
    s2res = tn_s2_open(&g_s2if, device, unit);
    if (s2res != TN_S2_OK) {
        tn_logf(TN_LOG_BASIC, "tolunnet: SANA-II open failed (%s, %lu)\n", device, unit);
        tn_timer_fini(&g_timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* 4. Bring SANA-II interface online */
    s2res = tn_s2_online(&g_s2if, NULL);
    if (s2res != TN_S2_OK) {
        tn_log(TN_LOG_BASIC, "tolunnet: SANA-II online failed\n");
        tn_s2_offline_close(&g_s2if);
        tn_timer_fini(&g_timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    tn_logf(TN_LOG_BASIC, "tolunnet: %s:%lu online (MAC %02x:%02x:%02x:%02x:%02x:%02x, MTU %lu)\n",
            device, unit,
            (int)g_s2if.mac[0], (int)g_s2if.mac[1], (int)g_s2if.mac[2],
            (int)g_s2if.mac[3], (int)g_s2if.mac[4], (int)g_s2if.mac[5],
            g_s2if.mtu);

    /* Initialize hardware PRNG entropy (TNET-013) */
    tn_rand_init(&g_timer, g_s2if.mac);

    /* 5. Register SANA-II netif with lwIP */
    if (netif_add(&g_netif, &ipaddr, &netmask, &gw, &g_s2if,
                  tn_sana2_netif_init, ethernet_input) == NULL) {
        tn_log(TN_LOG_BASIC, "tolunnet: netif_add failed\n");
        tn_s2_offline_close(&g_s2if);
        tn_timer_fini(&g_timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    netif_set_default(&g_netif);
    netif_set_up(&g_netif);

    /* 6. Arm async SANA-II receive pump */
    if (tn_s2_arm_reads(&g_s2if) != TN_S2_OK) {
        tn_log(TN_LOG_BASIC, "tolunnet: tn_s2_arm_reads failed\n");
        netif_set_down(&g_netif);
        netif_remove(&g_netif);
        tn_s2_offline_close(&g_s2if);
        tn_timer_fini(&g_timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* Initialize DNS client server */
    IP4_ADDR(&dns_server, 10, 0, 2, 3); /* Standard QEMU/slirp DNS */
    dns_setserver(0, (const ip_addr_t *)&dns_server);

    if (use_dhcp) {
        tn_log(TN_LOG_BASIC, "tolunnet: starting DHCP client...\n");
        dhcp_start(&g_netif);
    } else {
        char str_ip[16], str_nm[16], str_gw[16];
        ip_to_str(str_ip, netif_ip4_addr(&g_netif));
        ip_to_str(str_nm, netif_ip4_netmask(&g_netif));
        ip_to_str(str_gw, netif_ip4_gw(&g_netif));

        tn_log(TN_LOG_BASIC, "----------------------------------------\n");
        tn_logf(TN_LOG_BASIC, "tolunnet: Static IP configured!\n");
        tn_logf(TN_LOG_BASIC, "  IP Address : %s\n", str_ip);
        tn_logf(TN_LOG_BASIC, "  Netmask    : %s\n", str_nm);
        tn_logf(TN_LOG_BASIC, "  Gateway    : %s\n", str_gw);
        tn_log(TN_LOG_BASIC, "----------------------------------------\n");

        etharp_gratuitous(&g_netif);
        etharp_request(&g_netif, &gw);
    }

    /* 7. Create and Register Public IPC Message Port */
    g_ipc_port = CreateMsgPort();
    if (g_ipc_port == NULL) {
        tn_log(TN_LOG_BASIC, "tolunnet: failed to create IPC port\n");
        netif_set_down(&g_netif);
        netif_remove(&g_netif);
        tn_s2_offline_close(&g_s2if);
        tn_timer_fini(&g_timer);
        CloseLibrary(DOSBase);
        return 20;
    }
    g_ipc_port->mp_Node.ln_Name = (STRPTR)TOLUNNET_PORT_NAME;
    g_ipc_port->mp_Node.ln_Pri  = 0;
    g_ipc_port->mp_Node.ln_Type = NT_MSGPORT;
    AddPort(g_ipc_port);

    /* 8. Instantiate and register bsdsocket.library */
    g_bsd_lib = tn_lib_create();
    if (g_bsd_lib == NULL) {
        tn_log(TN_LOG_BASIC, "tolunnet: failed to create bsdsocket.library\n");
        RemPort(g_ipc_port);
        DeleteMsgPort(g_ipc_port);
        netif_set_down(&g_netif);
        netif_remove(&g_netif);
        tn_s2_offline_close(&g_s2if);
        tn_timer_fini(&g_timer);
        CloseLibrary(DOSBase);
        return 20;
    }
    tn_log(TN_LOG_BASIC, "tolunnet: bsdsocket.library v4.1 registered with Exec\n");

    /* 9. Arm 100 ms timer tick */
    tn_timer_arm(&g_timer, 100000);

    s2_sig     = 1UL << g_s2if.rx_port->mp_SigBit;
    timer_sig  = g_timer.sig_mask;
    ipc_sig    = 1UL << g_ipc_port->mp_SigBit;
    ctrl_c_sig = SIGBREAKF_CTRL_C;
    wait_mask  = s2_sig | timer_sig | ipc_sig | ctrl_c_sig;

    tn_log(TN_LOG_BASIC, "tolunnet: network task running (Press Ctrl-C to stop)\n");

    /* 10. Main Task Loop */
    while (running) {
        ULONG sigs = Wait(wait_mask);

        /* Ctrl-C Signal -> Shutdown */
        if (sigs & ctrl_c_sig) {
            tn_log(TN_LOG_BASIC, "\ntolunnet: Ctrl-C received, shutting down...\n");
            running = FALSE;
            break;
        }

        /* Client IPC Message Signal */
        if (sigs & ipc_sig) {
            struct Message *msg;
            while ((msg = GetMsg(g_ipc_port)) != NULL) {
                if (tn_handle_ipc((TnIpcMsg *)msg)) {
                    ReplyMsg(msg);
                }
            }
        }

        /* SANA-II Packet Arrival Signal */
        if (sigs & s2_sig) {
            tn_sana2_poll_input(&g_s2if, &g_netif);
        }

        /* 100 ms Timer Tick Signal */
        if (sigs & timer_sig) {
            tn_timer_ack(&g_timer);
            tick_count++;

            /* Drive lwIP timeouts */
            sys_check_timeouts();

            /* Check DHCP lease progress */
            if (use_dhcp && !dhcp_logged && dhcp_supplied_address(&g_netif)) {
                char str_ip[16], str_nm[16], str_gw[16];
                ip_to_str(str_ip, netif_ip4_addr(&g_netif));
                ip_to_str(str_nm, netif_ip4_netmask(&g_netif));
                ip_to_str(str_gw, netif_ip4_gw(&g_netif));

                tn_log(TN_LOG_BASIC, "----------------------------------------\n");
                tn_logf(TN_LOG_BASIC, "tolunnet: DHCP lease obtained!\n");
                tn_logf(TN_LOG_BASIC, "  IP Address : %s\n", str_ip);
                tn_logf(TN_LOG_BASIC, "  Netmask    : %s\n", str_nm);
                tn_logf(TN_LOG_BASIC, "  Gateway    : %s\n", str_gw);
                tn_log(TN_LOG_BASIC, "----------------------------------------\n");

                dhcp_logged = TRUE;
            }

            /* Re-arm timer */
            tn_timer_arm(&g_timer, 100000);
        }
    }

    /* 11. Clean Shutdown Sequence */
    if (g_bsd_lib != NULL) {
        if (g_bsd_lib->lib_OpenCnt > 0) {
            tn_logf(TN_LOG_BASIC, "tolunnet: warning: bsdsocket.library has %lu open client(s) at shutdown\n",
                    (ULONG)g_bsd_lib->lib_OpenCnt);
        }
        tn_log(TN_LOG_BASIC, "tolunnet: removing bsdsocket.library...\n");
        tn_lib_destroy(g_bsd_lib);
        g_bsd_lib = NULL;
    }

    tn_log(TN_LOG_BASIC, "tolunnet: deleting IPC port...\n");
    RemPort(g_ipc_port);
    DeleteMsgPort(g_ipc_port);

    if (use_dhcp) {
        tn_log(TN_LOG_BASIC, "tolunnet: stopping DHCP...\n");
        dhcp_stop(&g_netif);
    }
    tn_log(TN_LOG_BASIC, "tolunnet: bringing netif down...\n");
    netif_set_down(&g_netif);
    netif_remove(&g_netif);

    tn_log(TN_LOG_BASIC, "tolunnet: closing timer.device...\n");
    tn_timer_fini(&g_timer);

    tn_log(TN_LOG_BASIC, "tolunnet: closing SANA-II device...\n");
    tn_s2_offline_close(&g_s2if);

    if (log_fh != (BPTR)0) {
        Close(log_fh);
    }

    CloseLibrary(DOSBase);
    tn_log(TN_LOG_BASIC, "tolunnet: shutdown complete.\n");
    return 0;
}
