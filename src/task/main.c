/*
 * tolunnet — Network Task & lwIP Core Mainloop.
 *
 * Master prompt §M2 (IP Alive), §M3 (bsdsocket.library Skeleton), §M4 (UDP / DNS), & §M5 (TCP).
 * - One Amiga task owns lwIP (NO_SYS=1).
 * - SANA-II driver (ethernet.device) wired to lwIP netif.
 * - Periodic 100 ms timer.device ticks drive sys_check_timeouts().
 * - bsdsocket.library instantiated and registered in Exec library list.
 * - Public IPC port ("tolunnet.port") dispatches client socket requests.
 * - Full UDP & TCP stream transport + DNS client support.
 * - Clean shutdown on Ctrl-C (SIGBREAKF_CTRL_C).
 */

#include "../sana2/sana2_netif.h"
#include "../lib/lib_init.h"
#include "../common/log.h"
#include "../common/prefs.h"
#include "timers.h"
#include "../../include/ipc.h"
#include "../common/fdset_util.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/tasks.h>
#include "../common/ipc_client.h"
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
#include "lwip/raw.h"
#include "lwip/dns.h"
#include "lwip/igmp.h"
#include "netif/ethernet.h"

#undef TCP_MSS
#undef htons
#undef ntohs
#undef htonl
#undef ntohl
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <libraries/bsdsocket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

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

/* Queued pending connection on listening TCP sockets */
typedef struct TnAcceptEntry {
    struct TnAcceptEntry *next;
    struct tcp_pcb       *new_pcb;
} TnAcceptEntry;

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
    /* Level SOL_SOCKET options */
    BOOL            opt_broadcast;
    BOOL            opt_reuseaddr;
    BOOL            opt_keepalive;
    BOOL            opt_oobinline;
    struct linger   opt_linger;
    int             opt_sndbuf;
    int             opt_rcvbuf;
    struct timeval  opt_rcvtimeo;
    struct timeval  opt_sndtimeo;
    /* Level IPPROTO_TCP options */
    BOOL            opt_nodelay;
    int             opt_keepidle;
    int             opt_keepintvl;
    int             opt_keepcnt;
    /* Level IPPROTO_IP options */
    u8_t            opt_tos;
    u8_t            opt_ttl;
    BOOL            opt_hdrincl;
    u8_t            opt_multicast_ttl;
    u8_t            opt_multicast_loop;

    LONG            last_error;
    ULONG           rx_count;
    int             ref_count;
    struct udp_pcb *udp_pcb;
    struct tcp_pcb *tcp_pcb;
    struct raw_pcb *raw_pcb;
    TnRxPacket     *rx_head;
    TnRxPacket     *rx_tail;
    TnAcceptEntry  *accept_head;
    TnAcceptEntry  *accept_tail;
    ULONG           accept_count;
    TnIpcMsg       *pending_connect_msg;
    TnIpcMsg       *pending_accept_msg;
    LONG            park_id;
    BOOL            is_parked;
} TnSocketSlot;

static TnSana2If     g_s2if;
static struct netif  g_netif;
static TnTimer       g_timer;
static struct MsgPort *g_ipc_port = NULL;
static struct Library *g_bsd_lib  = NULL;
static TnSocketSlot  g_sockets[TN_MAX_GLOBAL_SOCKETS];
static LONG          g_next_park_id = 1;
static TnPrefs       g_prefs;                 /* Live config (TNET-063/064) */

/* Plain string equality (no libc) */
static BOOL tn_streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return FALSE;
    while (*a && *a == *b) { a++; b++; }
    return (*a == '\0' && *b == '\0');
}

/*
 * Apply the live-configurable part of g_prefs to the running stack (TNET-063).
 * Used at startup and by TN_IPC_CMD_RECONFIG (TNET-064). Interface-level
 * settings (device/unit/addressing mode) still require a stack restart.
 */
static void tn_apply_live_config(void)
{
    ip4_addr_t dns;

    /* DNS1: configured server, only touched when explicitly set (TNET-078) */
    if (g_prefs.dns_server[0] != '\0' && ip4addr_aton(g_prefs.dns_server, &dns)) {
        dns_setserver(0, (const ip_addr_t *)&dns);
    }

    /* DNS2: secondary resolver, only when configured */
    if (g_prefs.dns2[0] != '\0' && ip4addr_aton(g_prefs.dns2, &dns)) {
        dns_setserver(1, (const ip_addr_t *)&dns);
        tn_logf(TN_LOG_BASIC, "tolunnet: secondary DNS %s\n", g_prefs.dns2);
    }

    /* HOSTNAME: DHCP option 12 + gethostname() for future library openers */
    if (g_prefs.hostname[0] != '\0') {
        netif_set_hostname(&g_netif, g_prefs.hostname);
    }

    /* MTU: clamp the netif below the driver-reported maximum */
    if (g_prefs.mtu >= 576 && g_prefs.mtu <= 1500 && g_netif.mtu != 0 &&
        g_prefs.mtu < g_netif.mtu) {
        g_netif.mtu = (u16_t)g_prefs.mtu;
        tn_logf(TN_LOG_BASIC, "tolunnet: MTU clamped to %lu (driver max %lu)\n",
                g_prefs.mtu, (ULONG)g_s2if.mtu);
    }
}

/* Deliver SIGIO to socket owner task if mask is set (TNET-067) */
static void tn_signal_socket(TnSocketSlot *slot)
{
    if (slot != NULL && slot->in_use && slot->owner_task != NULL && slot->owner_base != NULL) {
        ULONG sig_io = slot->owner_base->sig_io;
        if (sig_io != 0) {
            Signal(slot->owner_task, sig_io);
        }
    }
}

/* Deliver socket events to base->events and signal task if SBTC_SIGEVENTMASK set */
static void tn_record_socket_event(TnSocketSlot *slot, ULONG event_mask)
{
    if (slot != NULL && slot->in_use && slot->owner_base != NULL) {
        TnSocketBase *base = slot->owner_base;
        int slot_idx = (int)(slot - g_sockets);
        int fd;
        BOOL posted = FALSE;

        for (fd = 0; fd < TN_MAX_FDS_PER_TASK; fd++) {
            if (base->fd_map[fd] == slot_idx) {
                base->events[fd] |= event_mask;
                posted = TRUE;
            }
        }
        if (posted && base->sig_event != 0 && slot->owner_task != NULL) {
            Signal(slot->owner_task, base->sig_event);
        }
    }
}

/* Callback from lwIP when data is acknowledged and send buffer space frees up */
static err_t tn_tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    int slot_idx = (int)(intptr_t)arg;
    (void)pcb; (void)len;
    if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS) {
        TnSocketSlot *slot = &g_sockets[slot_idx];
        if (slot->in_use) {
            tn_record_socket_event(slot, FD_WRITE);
        }
    }
    return ERR_OK;
}

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
    tn_signal_socket(slot);
    tn_record_socket_event(slot, FD_READ);
}

/* Callback from lwIP when a RAW packet arrives */
static u8_t tn_raw_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                           const ip_addr_t *addr)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;
    TnRxPacket *pkt;
    struct pbuf *q;
    (void)pcb;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) {
        return 0;
    }

    slot = &g_sockets[slot_idx];
    if (!slot->in_use || p == NULL) {
        return 0;
    }

    if (slot->rx_count >= TN_MAX_RX_QUEUE_PER_SOCKET) {
        return 0;
    }

    /* Clone pbuf so raw socket has independent copy with full IP header */
    q = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
    if (q == NULL) {
        return 0;
    }

    pkt = (TnRxPacket *)AllocVec(sizeof(TnRxPacket), MEMF_CLEAR | MEMF_PUBLIC);
    if (pkt == NULL) {
        pbuf_free(q);
        return 0;
    }

    pkt->p        = q;
    pkt->offset   = 0;
    pkt->src_ip   = *addr;
    pkt->src_port = 0;
    pkt->next     = NULL;

    if (slot->rx_tail != NULL) {
        slot->rx_tail->next = pkt;
        slot->rx_tail       = pkt;
    } else {
        slot->rx_head       = pkt;
        slot->rx_tail       = pkt;
    }
    slot->rx_count++;
    tn_signal_socket(slot);
    tn_record_socket_event(slot, FD_READ);

    /* Return 0: do not eat packet so stack/ICMP echo replier can also process it */
    return 0;
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
        tn_signal_socket(slot);
        tn_record_socket_event(slot, FD_CLOSE | FD_READ);
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
    tn_signal_socket(slot);
    tn_record_socket_event(slot, FD_READ);

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
    tn_signal_socket(slot);
    tn_record_socket_event(slot, FD_CONNECT | FD_WRITE);

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
    tn_signal_socket(slot);
    tn_record_socket_event(slot, FD_ERROR);

    if (slot->pending_connect_msg != NULL) {
        slot->pending_connect_msg->result = -1;
        slot->pending_connect_msg->err_no = ECONNREFUSED;
        ReplyMsg((struct Message *)slot->pending_connect_msg);
        slot->pending_connect_msg = NULL;
    }
}

static void tn_init_socket_slot(int slot_idx, TnSocketBase *base, struct Task *task, int domain, int type, int protocol)
{
    TnSocketSlot *s = &g_sockets[slot_idx];
    s->in_use              = TRUE;
    s->owner_base          = base;
    s->owner_task          = task;
    s->domain              = domain;
    s->type                = type;
    s->protocol            = protocol;
    s->tcp_state           = TN_TCP_STATE_CLOSED;
    s->is_nonblocking      = FALSE;

    /* Level SOL_SOCKET options */
    s->opt_broadcast       = FALSE;
    s->opt_reuseaddr       = FALSE;
    s->opt_keepalive       = FALSE;
    s->opt_oobinline       = FALSE;
    s->opt_linger.l_onoff  = 0;
    s->opt_linger.l_linger = 0;
    s->opt_sndbuf          = TCP_SND_BUF;
    s->opt_rcvbuf          = TCP_WND;
    s->opt_rcvtimeo.tv_secs  = 0;
    s->opt_rcvtimeo.tv_micro = 0;
    s->opt_sndtimeo.tv_secs  = 0;
    s->opt_sndtimeo.tv_micro = 0;

    /* Level IPPROTO_TCP options */
    s->opt_nodelay         = FALSE;
    s->opt_keepidle        = 7200;
    s->opt_keepintvl       = 75;
    s->opt_keepcnt         = 9;

    /* Level IPPROTO_IP options */
    s->opt_tos             = 0;
    s->opt_ttl             = 64;
    s->opt_hdrincl         = (protocol == IPPROTO_RAW);
    s->opt_multicast_ttl   = 1;
    s->opt_multicast_loop  = 1;

    s->last_error          = 0;
    s->rx_count            = 0;
    s->ref_count           = 1;
    s->udp_pcb             = NULL;
    s->tcp_pcb             = NULL;
    s->raw_pcb             = NULL;
    s->rx_head             = NULL;
    s->rx_tail             = NULL;
    s->accept_head         = NULL;
    s->accept_tail         = NULL;
    s->accept_count        = 0;
    s->pending_connect_msg = NULL;
    s->pending_accept_msg  = NULL;
}

/* Callback from lwIP when a listening TCP socket receives an incoming connection */
static err_t tn_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    int slot_idx = (int)(intptr_t)arg;
    TnSocketSlot *slot;

    if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS) return ERR_VAL;
    slot = &g_sockets[slot_idx];
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
        int i;

        slot->pending_accept_msg = NULL;

        if (base != NULL) {
            int pref_fd = (int)imsg->args[3];
            if (pref_fd >= 0 && pref_fd < TN_MAX_FDS_PER_TASK && base->fd_map[pref_fd] == -1) {
                client_fd = pref_fd;
            } else {
                for (i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
                    if (base->fd_map[i] == -1) { client_fd = i; break; }
                }
            }
        }
        for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
            if (!g_sockets[i].in_use) { new_slot_idx = i; break; }
        }

        if (client_fd < 0 || new_slot_idx < 0) {
            tcp_abort(newpcb);
            imsg->result = -1;
            imsg->err_no = (client_fd < 0) ? EMFILE : ENFILE;
            ReplyMsg((struct Message *)imsg);
            return ERR_ABRT;
        }

        tn_init_socket_slot(new_slot_idx, base, imsg->client_task, AF_INET, SOCK_STREAM, 0);
        g_sockets[new_slot_idx].tcp_state = TN_TCP_STATE_ESTABLISHED;
        g_sockets[new_slot_idx].tcp_pcb   = newpcb;

        base->fd_map[client_fd] = new_slot_idx;

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
        tn_record_socket_event(&g_sockets[new_slot_idx], FD_WRITE);
        ReplyMsg((struct Message *)imsg);
        return ERR_OK;
    }

    /* Queue incoming connection into accept queue (bounded to 8) */
    if (slot->accept_count >= 8) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    {
        TnAcceptEntry *entry = (TnAcceptEntry *)AllocVec(sizeof(TnAcceptEntry), MEMF_PUBLIC | MEMF_CLEAR);
        if (entry == NULL) {
            tcp_abort(newpcb);
            return ERR_ABRT;
        }
        entry->new_pcb = newpcb;
        entry->next    = NULL;

        if (slot->accept_tail != NULL) {
            slot->accept_tail->next = entry;
        } else {
            slot->accept_head = entry;
        }
        slot->accept_tail = entry;
        slot->accept_count++;
        tn_signal_socket(slot);
        tn_record_socket_event(slot, FD_ACCEPT);
    }

    return ERR_OK;
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

static void tn_drain_loopback(void);

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
        tcp_accept(g_sockets[slot_idx].tcp_pcb, NULL);
        tcp_close(g_sockets[slot_idx].tcp_pcb);
        g_sockets[slot_idx].tcp_pcb = NULL;
    }
    if (g_sockets[slot_idx].raw_pcb != NULL) {
        raw_remove(g_sockets[slot_idx].raw_pcb);
        g_sockets[slot_idx].raw_pcb = NULL;
    }
    while (g_sockets[slot_idx].rx_head != NULL) {
        TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
        g_sockets[slot_idx].rx_head = pkt->next;
        if (pkt->p != NULL) pbuf_free(pkt->p);
        FreeVec(pkt);
    }
    while (g_sockets[slot_idx].accept_head != NULL) {
        TnAcceptEntry *ent = g_sockets[slot_idx].accept_head;
        g_sockets[slot_idx].accept_head = ent->next;
        if (ent->new_pcb != NULL) {
            tcp_abort(ent->new_pcb);
        }
        FreeVec(ent);
    }
    g_sockets[slot_idx].accept_tail  = NULL;
    g_sockets[slot_idx].accept_count = 0;

    if (g_sockets[slot_idx].pending_connect_msg != NULL) {
        TnIpcMsg *cmsg = g_sockets[slot_idx].pending_connect_msg;
        g_sockets[slot_idx].pending_connect_msg = NULL;
        cmsg->result = -1;
        cmsg->err_no = EBADF;
        ReplyMsg((struct Message *)cmsg);
    }
    if (g_sockets[slot_idx].pending_accept_msg != NULL) {
        TnIpcMsg *amsg = g_sockets[slot_idx].pending_accept_msg;
        g_sockets[slot_idx].pending_accept_msg = NULL;
        amsg->result = -1;
        amsg->err_no = EBADF;
        ReplyMsg((struct Message *)amsg);
    }

    g_sockets[slot_idx].rx_tail             = NULL;
    g_sockets[slot_idx].rx_count            = 0;
    g_sockets[slot_idx].ref_count           = 0;
    g_sockets[slot_idx].park_id             = 0;
    g_sockets[slot_idx].is_parked           = FALSE;
    g_sockets[slot_idx].in_use              = FALSE;
    g_sockets[slot_idx].owner_base          = NULL;
}

static void tn_drain_loopback(void)
{
    struct netif *n;
    int guard = 0;
    BOOL had;
    do {
        had = FALSE;
        NETIF_FOREACH(n) {
            if (n->loop_first != NULL) {
                netif_poll(n);
                had = TRUE;
            }
        }
        guard++;
    } while (had && guard < 64);
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
        /* TNET-063: hand the configured hostname to gethostname() callers */
        if (base != NULL && g_prefs.hostname[0] != '\0') {
            int j = 0;
            while (g_prefs.hostname[j] != '\0' && j < (int)sizeof(base->hostname) - 1) {
                base->hostname[j] = g_prefs.hostname[j];
                j++;
            }
            base->hostname[j] = '\0';
        }
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
                    if (g_sockets[slot_idx].ref_count <= 0 && !g_sockets[slot_idx].is_parked) {
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

            if (domain != AF_INET) {
                imsg->result = -1;
                imsg->err_no = EAFNOSUPPORT;
                return TRUE;
            }

            if (type == SOCK_STREAM) {
                if (protocol != 0 && protocol != IPPROTO_TCP) {
                    imsg->result = -1;
                    imsg->err_no = EPROTONOSUPPORT;
                    return TRUE;
                }
            } else if (type == SOCK_DGRAM) {
                if (protocol != 0 && protocol != IPPROTO_UDP) {
                    imsg->result = -1;
                    imsg->err_no = EPROTONOSUPPORT;
                    return TRUE;
                }
            } else if (type == SOCK_RAW) {
                if (protocol != IPPROTO_ICMP && protocol != IPPROTO_RAW) {
                    imsg->result = -1;
                    imsg->err_no = EPROTONOSUPPORT;
                    return TRUE;
                }
            } else {
                imsg->result = -1;
                imsg->err_no = ESOCKTNOSUPPORT;
                return TRUE;
            }

            /* Find free client fd */
            if (base != NULL) {
                int pref_fd = (int)imsg->args[3];
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

            tn_init_socket_slot(slot_idx, base, imsg->client_task, domain, type, protocol);

            /* UDP socket */
            if (type == SOCK_DGRAM) {
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
            else if (type == SOCK_STREAM) {
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
            /* RAW socket (TNET-070) */
            else if (type == SOCK_RAW) {
                g_sockets[slot_idx].raw_pcb = raw_new((u8_t)protocol);
                if (g_sockets[slot_idx].raw_pcb == NULL) {
                    g_sockets[slot_idx].in_use = FALSE;
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
                }
                raw_recv(g_sockets[slot_idx].raw_pcb, tn_raw_recv_cb, (void *)(intptr_t)slot_idx);
                if (protocol == IPPROTO_RAW) {
                    raw_set_flags(g_sockets[slot_idx].raw_pcb, RAW_FLAGS_HDRINCL);
                }
            }

            base->fd_map[client_fd] = slot_idx;
            if (type == SOCK_DGRAM || type == SOCK_RAW) {
                tn_record_socket_event(&g_sockets[slot_idx], FD_WRITE);
            }

            tn_logf(TN_LOG_BASIC, "tolunnet: socket(domain=%d, type=%d, proto=%d) -> fd %d (slot %d)\n",
                    domain, type, protocol, client_fd, slot_idx);

            imsg->result = client_fd;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_BIND:
        {
            int client_fd = (int)imsg->args[0];
            const struct sockaddr_in *sin = (const struct sockaddr_in *)imsg->ptrs[0];
            socklen_t namelen = (socklen_t)imsg->args[1];
            ip_addr_t bind_ip;
            u16_t port;
            err_t berr;

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || sin == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (namelen < (socklen_t)sizeof(struct sockaddr_in)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            if (sin->sin_family != AF_INET) {
                imsg->result = -1;
                imsg->err_no = EAFNOSUPPORT;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            ip_addr_set_ip4_u32(&bind_ip, sin->sin_addr.s_addr);
            port = lwip_ntohs(sin->sin_port);

            if (g_sockets[slot_idx].type == SOCK_STREAM) {
                if (g_sockets[slot_idx].tcp_pcb == NULL) {
                    imsg->result = -1;
                    imsg->err_no = EBADF;
                    return TRUE;
                }
                if (g_sockets[slot_idx].opt_reuseaddr) {
                    ip_set_option(g_sockets[slot_idx].tcp_pcb, SOF_REUSEADDR);
                }
                berr = tcp_bind(g_sockets[slot_idx].tcp_pcb, &bind_ip, port);
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM) {
                if (g_sockets[slot_idx].udp_pcb == NULL) {
                    imsg->result = -1;
                    imsg->err_no = EBADF;
                    return TRUE;
                }
                if (g_sockets[slot_idx].opt_reuseaddr) {
                    ip_set_option(g_sockets[slot_idx].udp_pcb, SOF_REUSEADDR);
                }
                berr = udp_bind(g_sockets[slot_idx].udp_pcb, &bind_ip, port);
            } else if (g_sockets[slot_idx].type == SOCK_RAW) {
                if (g_sockets[slot_idx].raw_pcb == NULL) {
                    imsg->result = -1;
                    imsg->err_no = EBADF;
                    return TRUE;
                }
                berr = raw_bind(g_sockets[slot_idx].raw_pcb, &bind_ip);
            } else {
                imsg->result = -1;
                imsg->err_no = EOPNOTSUPP;
                return TRUE;
            }

            if (berr != ERR_OK) {
                imsg->result = -1;
                imsg->err_no = (berr == ERR_USE) ? EADDRINUSE : EINVAL;
                return TRUE;
            }

            imsg->result = 0;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_LISTEN:
        {
            int client_fd = (int)imsg->args[0];
            LONG backlog = imsg->args[1];
            struct tcp_pcb *lpcb;
            u8_t bl;

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

            if (g_sockets[slot_idx].type != 1 /* TCP */ || g_sockets[slot_idx].tcp_pcb == NULL) {
                imsg->result = -1;
                imsg->err_no = EOPNOTSUPP;
                return TRUE;
            }

            if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_LISTENING) {
                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            }

            bl = (backlog <= 0) ? 1 : ((backlog > 8) ? 8 : (u8_t)backlog);
            lpcb = tcp_listen_with_backlog(g_sockets[slot_idx].tcp_pcb, bl);
            if (lpcb == NULL) {
                imsg->result = -1;
                imsg->err_no = ENOBUFS;
                return TRUE;
            }

            g_sockets[slot_idx].tcp_pcb   = lpcb;
            g_sockets[slot_idx].tcp_state = TN_TCP_STATE_LISTENING;
            tcp_arg(lpcb, (void *)(intptr_t)slot_idx);
            tcp_accept(lpcb, tn_tcp_accept_cb);

            imsg->result = 0;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_ACCEPT:
        {
            int client_fd = (int)imsg->args[0];
            struct sockaddr_in *addr = (struct sockaddr_in *)imsg->ptrs[0];
            socklen_t *addrlen = (socklen_t *)imsg->ptrs[1];

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

            if (g_sockets[slot_idx].tcp_state != TN_TCP_STATE_LISTENING) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            if (g_sockets[slot_idx].accept_head != NULL) {
                TnAcceptEntry *ent = g_sockets[slot_idx].accept_head;
                int new_fd = -1;
                int new_slot = -1;

                g_sockets[slot_idx].accept_head = ent->next;
                if (g_sockets[slot_idx].accept_head == NULL) {
                    g_sockets[slot_idx].accept_tail = NULL;
                }
                g_sockets[slot_idx].accept_count--;

                int pref_fd = (int)imsg->args[3];
                if (pref_fd >= 0 && pref_fd < TN_MAX_FDS_PER_TASK && base->fd_map[pref_fd] == -1) {
                    new_fd = pref_fd;
                } else {
                    for (i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
                        if (base->fd_map[i] == -1) { new_fd = i; break; }
                    }
                }
                for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
                    if (!g_sockets[i].in_use) { new_slot = i; break; }
                }

                if (new_fd < 0 || new_slot < 0) {
                    tcp_abort(ent->new_pcb);
                    FreeVec(ent);
                    imsg->result = -1;
                    imsg->err_no = (new_fd < 0) ? EMFILE : ENFILE;
                    return TRUE;
                }

                tn_init_socket_slot(new_slot, base, imsg->client_task, AF_INET, SOCK_STREAM, 0);
                g_sockets[new_slot].tcp_state = TN_TCP_STATE_ESTABLISHED;
                g_sockets[new_slot].tcp_pcb   = ent->new_pcb;

                base->fd_map[new_fd] = new_slot;
                tcp_arg(ent->new_pcb, (void *)(intptr_t)new_slot);
                tcp_recv(ent->new_pcb, tn_tcp_recv_cb);
                tcp_sent(ent->new_pcb, tn_tcp_sent_cb);
                tcp_err(ent->new_pcb, tn_tcp_err_cb);

                if (addr != NULL && addrlen != NULL && *addrlen >= sizeof(struct sockaddr_in)) {
                    addr->sin_len    = sizeof(struct sockaddr_in);
                    addr->sin_family = AF_INET;
                    addr->sin_port   = lwip_htons(ent->new_pcb->remote_port);
                    addr->sin_addr.s_addr = ip_2_ip4(&ent->new_pcb->remote_ip)->addr;
                    *addrlen = sizeof(struct sockaddr_in);
                }

                FreeVec(ent);
                tn_record_socket_event(&g_sockets[new_slot], FD_WRITE);
                imsg->result = new_fd;
                imsg->err_no = 0;
                return TRUE;
            }

            if (g_sockets[slot_idx].is_nonblocking) {
                imsg->result = -1;
                imsg->err_no = EWOULDBLOCK;
                return TRUE;
            }

            /* Wait for incoming connection */
            g_sockets[slot_idx].pending_accept_msg = imsg;
            return FALSE;
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

            if (g_sockets[slot_idx].type == SOCK_STREAM && g_sockets[slot_idx].tcp_pcb != NULL) {
                ip_addr_t dst_ip;
                u16_t dst_port = lwip_ntohs(sin->sin_port);
                err_t cerr;

                ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);

                g_sockets[slot_idx].tcp_state           = TN_TCP_STATE_CONNECTING;
                g_sockets[slot_idx].pending_connect_msg = imsg;

                tcp_recv(g_sockets[slot_idx].tcp_pcb, tn_tcp_recv_cb);
                tcp_sent(g_sockets[slot_idx].tcp_pcb, tn_tcp_sent_cb);
                cerr = tcp_connect(g_sockets[slot_idx].tcp_pcb, &dst_ip, dst_port, tn_tcp_connected_cb);
                if (cerr == ERR_OK) {
                    tcp_output(g_sockets[slot_idx].tcp_pcb);
                    tn_drain_loopback();
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
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM && g_sockets[slot_idx].udp_pcb != NULL) {
                ip_addr_t dst_ip;
                u16_t dst_port = lwip_ntohs(sin->sin_port);
                ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);
                udp_connect(g_sockets[slot_idx].udp_pcb, &dst_ip, dst_port);
                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            } else if (g_sockets[slot_idx].type == SOCK_RAW && g_sockets[slot_idx].raw_pcb != NULL) {
                ip_addr_t dst_ip;
                ip_addr_set_ip4_u32(&dst_ip, sin->sin_addr.s_addr);
                raw_connect(g_sockets[slot_idx].raw_pcb, &dst_ip);
                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
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

            if (g_sockets[slot_idx].type == SOCK_STREAM && g_sockets[slot_idx].tcp_pcb != NULL) {
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
                tn_drain_loopback();
                imsg->result = (LONG)send_len;
                imsg->err_no = 0;
                return TRUE;
            } else if (g_sockets[slot_idx].type == SOCK_RAW && g_sockets[slot_idx].raw_pcb != NULL) {
                struct pbuf *p;
                u16_t send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
                err_t serr;

                p = pbuf_alloc(PBUF_IP, send_len, PBUF_RAM);
                if (p == NULL) {
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
                }
                pbuf_take(p, buf, send_len);

                serr = raw_send(g_sockets[slot_idx].raw_pcb, p);
                pbuf_free(p);

                if (serr != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = (serr == ERR_MEM) ? ENOBUFS : EHOSTUNREACH;
                    return TRUE;
                }
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
            LONG flags = imsg->args[2];

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

            if (flags & MSG_OOB) {
                imsg->result = -1;
                imsg->err_no = EOPNOTSUPP;
                return TRUE;
            }

            if (g_sockets[slot_idx].type == SOCK_STREAM) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    if (flags & MSG_PEEK) {
                        TnRxPacket *cur = g_sockets[slot_idx].rx_head;
                        u16_t copied = 0;
                        while (cur != NULL && copied < (u16_t)len) {
                            u16_t off = (cur == g_sockets[slot_idx].rx_head) ? cur->offset : 0;
                            u16_t avail = cur->p->tot_len - off;
                            u16_t chunk = (avail < ((u16_t)len - copied)) ? avail : ((u16_t)len - copied);
                            pbuf_copy_partial(cur->p, (char *)buf + copied, chunk, off);
                            copied += chunk;
                            cur = cur->next;
                        }
                        imsg->result = (LONG)copied;
                        imsg->err_no = 0;
                        return TRUE;
                    } else {
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
                    }
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
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM || g_sockets[slot_idx].type == SOCK_RAW) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
                    u16_t avail = pkt->p->tot_len;
                    u16_t to_copy = (avail < (u16_t)len) ? avail : (u16_t)len;

                    pbuf_copy_partial(pkt->p, buf, to_copy, 0);

                    if (!(flags & MSG_PEEK)) {
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

            if (g_sockets[slot_idx].type == SOCK_STREAM && g_sockets[slot_idx].tcp_pcb != NULL) {
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
                tn_drain_loopback();
                imsg->result = (LONG)send_len;
                imsg->err_no = 0;
                return TRUE;
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM && g_sockets[slot_idx].udp_pcb != NULL) {
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
                tn_drain_loopback();

                imsg->result = (LONG)send_len;
                imsg->err_no = 0;
                return TRUE;
            } else if (g_sockets[slot_idx].type == SOCK_RAW && g_sockets[slot_idx].raw_pcb != NULL) {
                struct pbuf *p;
                ip_addr_t dst_ip;
                u16_t send_len = (len > 0xFFFF) ? 0xFFFF : (u16_t)len;
                err_t serr;

                p = pbuf_alloc(PBUF_IP, send_len, PBUF_RAM);
                if (p == NULL) {
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
                }
                pbuf_take(p, buf, send_len);

                if (to != NULL) {
                    ip_addr_set_ip4_u32(&dst_ip, to->sin_addr.s_addr);
                } else {
                    dst_ip = g_sockets[slot_idx].raw_pcb->remote_ip;
                }

                serr = raw_sendto(g_sockets[slot_idx].raw_pcb, p, &dst_ip);
                pbuf_free(p);

                if (serr != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = (serr == ERR_MEM) ? ENOBUFS : EHOSTUNREACH;
                    return TRUE;
                }

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
            LONG flags = imsg->args[2];
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

            if (flags & MSG_OOB) {
                imsg->result = -1;
                imsg->err_no = EOPNOTSUPP;
                return TRUE;
            }

            if (g_sockets[slot_idx].type == SOCK_DGRAM) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
                    u16_t copied = pbuf_copy_partial(pkt->p, buf, (u16_t)len, 0);

                    if (from != NULL) {
                        from->sin_len = sizeof(struct sockaddr_in); /* TNET-055 */
                        from->sin_family = AF_INET;
                        from->sin_port   = lwip_htons(pkt->src_port);
                        from->sin_addr.s_addr = ip_addr_get_ip4_u32(&pkt->src_ip);
                        if (fromlen != NULL) *fromlen = sizeof(struct sockaddr_in);
                    }

                    if (!(flags & MSG_PEEK)) {
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

                    imsg->result = (LONG)copied;
                    imsg->err_no = 0;
                    return TRUE;
                } else {
                    imsg->result = -1;
                    imsg->err_no = EWOULDBLOCK;
                    return TRUE;
                }
            } else if (g_sockets[slot_idx].type == SOCK_RAW) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
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

    case TN_IPC_CMD_SHUTDOWN:
        {
            int client_fd = (int)imsg->args[0];
            LONG how = imsg->args[1];
            int shut_rx, shut_tx;
            err_t serr;

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

            if (g_sockets[slot_idx].type != 1 /* TCP */ || g_sockets[slot_idx].tcp_pcb == NULL) {
                imsg->result = -1;
                imsg->err_no = ENOTCONN;
                return TRUE;
            }

            shut_rx = (how == 0 || how == 2) ? 1 : 0;
            shut_tx = (how == 1 || how == 2) ? 1 : 0;

            if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_CLOSED) {
                if (shut_tx) {
                    g_sockets[slot_idx].tcp_state = TN_TCP_STATE_PEER_CLOSED;
                }
                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            }

            serr = tcp_shutdown(g_sockets[slot_idx].tcp_pcb, shut_rx, shut_tx);
            if (serr != ERR_OK) {
                imsg->result = -1;
                imsg->err_no = ECONNRESET;
                return TRUE;
            }

            if (shut_tx) {
                g_sockets[slot_idx].tcp_state = TN_TCP_STATE_PEER_CLOSED;
            }

            imsg->result = 0;
            imsg->err_no = 0;
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
                base->hostent_data.h_addrtype  = AF_INET;
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
                    base->hostent_data.h_addrtype  = AF_INET;
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

            base->fd_map[client_fd] = -1;
            g_sockets[slot_idx].ref_count--;
            if (g_sockets[slot_idx].ref_count <= 0 && !g_sockets[slot_idx].is_parked) {
                tn_free_socket_slot(slot_idx);
            }

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

            if (req == FIONBIO) {
                g_sockets[slot_idx].is_nonblocking = (*(ULONG *)argp != 0);
                imsg->result = 0;
                imsg->err_no = 0;
            } else if (req == FIONREAD) {
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
            } else if (req == SIOCATMARK) {
                *(int *)argp = 0; /* Not at out-of-band mark */
                imsg->result = 0;
                imsg->err_no = 0;
            } else if (req == SIOCADDRT || req == SIOCDELRT) {
                /* Routing stubs: honest ENOSYS per C2 specification */
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
                            struct sockaddr_in *sin = (struct sockaddr_in *)&ifr->ifr_addr;
                            for (i = 0; i < (int)sizeof(struct sockaddr_in); i++) ((char *)sin)[i] = 0;
                            sin->sin_len = sizeof(struct sockaddr_in);
                            sin->sin_family = AF_INET;
                            sin->sin_port = 0;
                            sin->sin_addr.s_addr = netif_ip4_addr(netif)->addr;
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

                /* Match interface: handle ethX, etX, loX */
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
                    return TRUE;
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
                    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr->ifr_addr;
                    int i;
                    for (i = 0; i < (int)sizeof(struct sockaddr_in); i++) ((char *)sin)[i] = 0;
                    sin->sin_len = sizeof(struct sockaddr_in);
                    sin->sin_family = AF_INET;
                    sin->sin_port = 0;
                    sin->sin_addr.s_addr = netif_ip4_addr(netif)->addr;
                    imsg->result = 0;
                    imsg->err_no = 0;
                } else if (req == SIOCGIFNETMASK || req == OSIOCGIFNETMASK) {
                    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr->ifr_addr;
                    int i;
                    for (i = 0; i < (int)sizeof(struct sockaddr_in); i++) ((char *)sin)[i] = 0;
                    sin->sin_len = sizeof(struct sockaddr_in);
                    sin->sin_family = AF_INET;
                    sin->sin_port = 0;
                    sin->sin_addr.s_addr = netif_ip4_netmask(netif)->addr;
                    imsg->result = 0;
                    imsg->err_no = 0;
                } else if (req == SIOCGIFBRDADDR || req == OSIOCGIFBRDADDR) {
                    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr->ifr_broadaddr;
                    int i;
                    u32_t ip = netif_ip4_addr(netif)->addr;
                    u32_t nm = netif_ip4_netmask(netif)->addr;
                    for (i = 0; i < (int)sizeof(struct sockaddr_in); i++) ((char *)sin)[i] = 0;
                    sin->sin_len = sizeof(struct sockaddr_in);
                    sin->sin_family = AF_INET;
                    sin->sin_port = 0;
                    sin->sin_addr.s_addr = ip | ~nm;
                    imsg->result = 0;
                    imsg->err_no = 0;
                } else if (req == SIOCGIFMTU) {
                    ifr->ifr_mtu = (LONG)netif->mtu;
                    imsg->result = 0;
                    imsg->err_no = 0;
                }
            } else {
                /* Unknown ioctl -> EINVAL per C2 specification */
                imsg->result = -1;
                imsg->err_no = EINVAL;
            }
            return TRUE;
        }

    case TN_IPC_CMD_SETSOCKOPT:
        {
            int client_fd = (int)imsg->args[0];
            LONG level = imsg->args[1];
            LONG optname = imsg->args[2];
            LONG optlen_arg = imsg->args[3];
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

            if (level == SOL_SOCKET) {
                switch (optname) {
                case SO_BROADCAST:
                    g_sockets[slot_idx].opt_broadcast = (*(const int *)optval != 0);
                    if (g_sockets[slot_idx].udp_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_broadcast) {
                            ip_set_option(g_sockets[slot_idx].udp_pcb, SOF_BROADCAST);
                        } else {
                            ip_reset_option(g_sockets[slot_idx].udp_pcb, SOF_BROADCAST);
                        }
                    }
                    break;

                case SO_KEEPALIVE:
                    g_sockets[slot_idx].opt_keepalive = (*(const int *)optval != 0);
                    if (g_sockets[slot_idx].tcp_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_keepalive) {
                            ip_set_option(g_sockets[slot_idx].tcp_pcb, SOF_KEEPALIVE);
                        } else {
                            ip_reset_option(g_sockets[slot_idx].tcp_pcb, SOF_KEEPALIVE);
                        }
                    }
                    break;

                case SO_REUSEADDR:
                    g_sockets[slot_idx].opt_reuseaddr = (*(const int *)optval != 0);
                    if (g_sockets[slot_idx].tcp_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_reuseaddr) {
                            ip_set_option(g_sockets[slot_idx].tcp_pcb, SOF_REUSEADDR);
                        } else {
                            ip_reset_option(g_sockets[slot_idx].tcp_pcb, SOF_REUSEADDR);
                        }
                    }
                    if (g_sockets[slot_idx].udp_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_reuseaddr) {
                            ip_set_option(g_sockets[slot_idx].udp_pcb, SOF_REUSEADDR);
                        } else {
                            ip_reset_option(g_sockets[slot_idx].udp_pcb, SOF_REUSEADDR);
                        }
                    }
                    break;

                case SO_LINGER:
                    if (optlen_arg < (LONG)sizeof(struct linger)) {
                        imsg->result = -1;
                        imsg->err_no = EINVAL;
                        return TRUE;
                    }
                    {
                        const struct linger *l = (const struct linger *)optval;
                        g_sockets[slot_idx].opt_linger.l_onoff  = l->l_onoff;
                        g_sockets[slot_idx].opt_linger.l_linger = l->l_linger;
                    }
                    break;

                case SO_SNDBUF:
                    g_sockets[slot_idx].opt_sndbuf = *(const int *)optval;
                    break;

                case SO_RCVBUF:
                    g_sockets[slot_idx].opt_rcvbuf = *(const int *)optval;
                    break;

                case SO_OOBINLINE:
                    g_sockets[slot_idx].opt_oobinline = (*(const int *)optval != 0);
                    break;

                case SO_RCVTIMEO:
                    if (optlen_arg >= (LONG)sizeof(struct timeval)) {
                        g_sockets[slot_idx].opt_rcvtimeo = *(const struct timeval *)optval;
                    } else if (optlen_arg >= (LONG)sizeof(int)) {
                        int ms = *(const int *)optval;
                        g_sockets[slot_idx].opt_rcvtimeo.tv_secs  = ms / 1000;
                        g_sockets[slot_idx].opt_rcvtimeo.tv_micro = (ms % 1000) * 1000;
                    } else {
                        imsg->result = -1;
                        imsg->err_no = EINVAL;
                        return TRUE;
                    }
                    break;

                case SO_SNDTIMEO:
                    if (optlen_arg >= (LONG)sizeof(struct timeval)) {
                        g_sockets[slot_idx].opt_sndtimeo = *(const struct timeval *)optval;
                    } else if (optlen_arg >= (LONG)sizeof(int)) {
                        int ms = *(const int *)optval;
                        g_sockets[slot_idx].opt_sndtimeo.tv_secs  = ms / 1000;
                        g_sockets[slot_idx].opt_sndtimeo.tv_micro = (ms % 1000) * 1000;
                    } else {
                        imsg->result = -1;
                        imsg->err_no = EINVAL;
                        return TRUE;
                    }
                    break;

                case SO_ERROR:
                case SO_TYPE:
                    /* Read-only socket options */
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;

                default:
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            } else if (level == IPPROTO_TCP) {
                if (g_sockets[slot_idx].type != SOCK_STREAM) {
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                switch (optname) {
                case TCP_NODELAY:
                    g_sockets[slot_idx].opt_nodelay = (*(const int *)optval != 0);
                    if (g_sockets[slot_idx].tcp_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_nodelay) {
                            tcp_nagle_disable(g_sockets[slot_idx].tcp_pcb);
                        } else {
                            tcp_nagle_enable(g_sockets[slot_idx].tcp_pcb);
                        }
                    }
                    break;

                case TCP_MAXSEG:
                    {
                        int mss = *(const int *)optval;
                        if (mss > 0 && g_sockets[slot_idx].tcp_pcb != NULL) {
                            g_sockets[slot_idx].tcp_pcb->mss = (u16_t)mss;
                        }
                    }
                    break;

                case TCP_KEEPIDLE:
                    {
                        int val = *(const int *)optval;
                        g_sockets[slot_idx].opt_keepidle = val;
                        if (g_sockets[slot_idx].tcp_pcb != NULL) {
                            g_sockets[slot_idx].tcp_pcb->keep_idle = (u32_t)val * 1000UL;
                        }
                    }
                    break;

                case TCP_KEEPINTVL:
                    {
                        int val = *(const int *)optval;
                        g_sockets[slot_idx].opt_keepintvl = val;
                        if (g_sockets[slot_idx].tcp_pcb != NULL) {
                            g_sockets[slot_idx].tcp_pcb->keep_intvl = (u32_t)val * 1000UL;
                        }
                    }
                    break;

                case TCP_KEEPCNT:
                    {
                        int val = *(const int *)optval;
                        g_sockets[slot_idx].opt_keepcnt = val;
                        if (g_sockets[slot_idx].tcp_pcb != NULL) {
                            g_sockets[slot_idx].tcp_pcb->keep_cnt = (u32_t)val;
                        }
                    }
                    break;

                default:
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            } else if (level == IPPROTO_IP) {
                switch (optname) {
                case IP_TOS:
                    {
                        u8_t tos = (u8_t)*(const int *)optval;
                        g_sockets[slot_idx].opt_tos = tos;
                        if (g_sockets[slot_idx].tcp_pcb != NULL) {
                            g_sockets[slot_idx].tcp_pcb->tos = tos;
                        }
                        if (g_sockets[slot_idx].udp_pcb != NULL) {
                            g_sockets[slot_idx].udp_pcb->tos = tos;
                        }
                        if (g_sockets[slot_idx].raw_pcb != NULL) {
                            g_sockets[slot_idx].raw_pcb->tos = tos;
                        }
                    }
                    break;

                case IP_TTL:
                    {
                        u8_t ttl = (u8_t)*(const int *)optval;
                        g_sockets[slot_idx].opt_ttl = ttl;
                        if (g_sockets[slot_idx].tcp_pcb != NULL) {
                            g_sockets[slot_idx].tcp_pcb->ttl = ttl;
                        }
                        if (g_sockets[slot_idx].udp_pcb != NULL) {
                            g_sockets[slot_idx].udp_pcb->ttl = ttl;
                        }
                        if (g_sockets[slot_idx].raw_pcb != NULL) {
                            g_sockets[slot_idx].raw_pcb->ttl = ttl;
                        }
                    }
                    break;

                case IP_HDRINCL:
                    if (g_sockets[slot_idx].type != SOCK_RAW) {
                        imsg->result = -1;
                        imsg->err_no = ENOPROTOOPT;
                        return TRUE;
                    }
                    g_sockets[slot_idx].opt_hdrincl = (*(const int *)optval != 0);
                    if (g_sockets[slot_idx].raw_pcb != NULL) {
                        if (g_sockets[slot_idx].opt_hdrincl) {
                            raw_set_flags(g_sockets[slot_idx].raw_pcb, RAW_FLAGS_HDRINCL);
                        } else {
                            raw_clear_flags(g_sockets[slot_idx].raw_pcb, RAW_FLAGS_HDRINCL);
                        }
                    }
                    break;

                case IP_MULTICAST_TTL:
                    {
                        u8_t mttl = (u8_t)*(const int *)optval;
                        g_sockets[slot_idx].opt_multicast_ttl = mttl;
                        if (g_sockets[slot_idx].udp_pcb != NULL) {
                            g_sockets[slot_idx].udp_pcb->mcast_ttl = mttl;
                        }
                        if (g_sockets[slot_idx].raw_pcb != NULL) {
                            g_sockets[slot_idx].raw_pcb->mcast_ttl = mttl;
                        }
                    }
                    break;

                case IP_MULTICAST_LOOP:
                    {
                        u8_t mloop = (*(const int *)optval != 0) ? 1 : 0;
                        g_sockets[slot_idx].opt_multicast_loop = mloop;
                        if (g_sockets[slot_idx].udp_pcb != NULL) {
                            if (mloop) {
                                udp_set_flags(g_sockets[slot_idx].udp_pcb, UDP_FLAGS_MULTICAST_LOOP);
                            } else {
                                udp_clear_flags(g_sockets[slot_idx].udp_pcb, UDP_FLAGS_MULTICAST_LOOP);
                            }
                        }
                    }
                    break;

                case IP_ADD_MEMBERSHIP:
                    if (optlen_arg < (LONG)sizeof(struct ip_mreq)) {
                        imsg->result = -1;
                        imsg->err_no = EINVAL;
                        return TRUE;
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
                            return TRUE;
                        }
                    }
                    break;

                case IP_DROP_MEMBERSHIP:
                    if (optlen_arg < (LONG)sizeof(struct ip_mreq)) {
                        imsg->result = -1;
                        imsg->err_no = EINVAL;
                        return TRUE;
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
                            return TRUE;
                        }
                    }
                    break;

                default:
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            } else {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }
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

            if (level == SOL_SOCKET) {
                switch (optname) {
                case SO_BROADCAST:
                    *(int *)optval = g_sockets[slot_idx].opt_broadcast ? 1 : 0;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_KEEPALIVE:
                    *(int *)optval = g_sockets[slot_idx].opt_keepalive ? 1 : 0;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_REUSEADDR:
                    *(int *)optval = g_sockets[slot_idx].opt_reuseaddr ? 1 : 0;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_LINGER:
                    if (optlen != NULL && *optlen < sizeof(struct linger)) {
                        imsg->result = -1;
                        imsg->err_no = EINVAL;
                        return TRUE;
                    }
                    *(struct linger *)optval = g_sockets[slot_idx].opt_linger;
                    if (optlen != NULL) *optlen = sizeof(struct linger);
                    break;

                case SO_ERROR:
                    *(int *)optval = (int)g_sockets[slot_idx].last_error;
                    g_sockets[slot_idx].last_error = 0;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_SNDBUF:
                    *(int *)optval = g_sockets[slot_idx].opt_sndbuf ? g_sockets[slot_idx].opt_sndbuf : TCP_SND_BUF;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_RCVBUF:
                    *(int *)optval = g_sockets[slot_idx].opt_rcvbuf ? g_sockets[slot_idx].opt_rcvbuf : TCP_WND;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_TYPE:
                    *(int *)optval = g_sockets[slot_idx].type;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_OOBINLINE:
                    *(int *)optval = g_sockets[slot_idx].opt_oobinline ? 1 : 0;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case SO_RCVTIMEO:
                    if (optlen != NULL && *optlen >= sizeof(struct timeval)) {
                        *(struct timeval *)optval = g_sockets[slot_idx].opt_rcvtimeo;
                        *optlen = sizeof(struct timeval);
                    } else {
                        *(int *)optval = g_sockets[slot_idx].opt_rcvtimeo.tv_secs * 1000 + g_sockets[slot_idx].opt_rcvtimeo.tv_micro / 1000;
                        if (optlen != NULL) *optlen = sizeof(int);
                    }
                    break;

                case SO_SNDTIMEO:
                    if (optlen != NULL && *optlen >= sizeof(struct timeval)) {
                        *(struct timeval *)optval = g_sockets[slot_idx].opt_sndtimeo;
                        *optlen = sizeof(struct timeval);
                    } else {
                        *(int *)optval = g_sockets[slot_idx].opt_sndtimeo.tv_secs * 1000 + g_sockets[slot_idx].opt_sndtimeo.tv_micro / 1000;
                        if (optlen != NULL) *optlen = sizeof(int);
                    }
                    break;

                default:
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            } else if (level == IPPROTO_TCP) {
                if (g_sockets[slot_idx].type != SOCK_STREAM) {
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                switch (optname) {
                case TCP_NODELAY:
                    *(int *)optval = g_sockets[slot_idx].opt_nodelay ? 1 : 0;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case TCP_MAXSEG:
                    *(int *)optval = (g_sockets[slot_idx].tcp_pcb != NULL) ? (int)g_sockets[slot_idx].tcp_pcb->mss : TCP_MSS;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case TCP_KEEPIDLE:
                    *(int *)optval = (g_sockets[slot_idx].tcp_pcb != NULL) ? (int)(g_sockets[slot_idx].tcp_pcb->keep_idle / 1000UL) : g_sockets[slot_idx].opt_keepidle;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case TCP_KEEPINTVL:
                    *(int *)optval = (g_sockets[slot_idx].tcp_pcb != NULL) ? (int)(g_sockets[slot_idx].tcp_pcb->keep_intvl / 1000UL) : g_sockets[slot_idx].opt_keepintvl;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case TCP_KEEPCNT:
                    *(int *)optval = (g_sockets[slot_idx].tcp_pcb != NULL) ? (int)g_sockets[slot_idx].tcp_pcb->keep_cnt : g_sockets[slot_idx].opt_keepcnt;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                default:
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            } else if (level == IPPROTO_IP) {
                switch (optname) {
                case IP_TOS:
                    *(int *)optval = (int)g_sockets[slot_idx].opt_tos;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case IP_TTL:
                    *(int *)optval = (int)g_sockets[slot_idx].opt_ttl;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case IP_HDRINCL:
                    if (g_sockets[slot_idx].type != SOCK_RAW) {
                        imsg->result = -1;
                        imsg->err_no = ENOPROTOOPT;
                        return TRUE;
                    }
                    *(int *)optval = (g_sockets[slot_idx].raw_pcb != NULL) ? (raw_is_flag_set(g_sockets[slot_idx].raw_pcb, RAW_FLAGS_HDRINCL) ? 1 : 0) : (g_sockets[slot_idx].opt_hdrincl ? 1 : 0);
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case IP_MULTICAST_TTL:
                    *(int *)optval = (int)g_sockets[slot_idx].opt_multicast_ttl;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case IP_MULTICAST_LOOP:
                    *(int *)optval = (int)g_sockets[slot_idx].opt_multicast_loop;
                    if (optlen != NULL) *optlen = sizeof(int);
                    break;

                case IP_ADD_MEMBERSHIP:
                case IP_DROP_MEMBERSHIP:
                    /* Write-only socket options */
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;

                default:
                    imsg->result = -1;
                    imsg->err_no = ENOPROTOOPT;
                    return TRUE;
                }

                imsg->result = 0;
                imsg->err_no = 0;
                return TRUE;
            } else {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }
        }

    case TN_IPC_CMD_GETSOCKNAME:
        {
            int client_fd = (int)imsg->args[0];
            struct sockaddr_in *sin = (struct sockaddr_in *)imsg->ptrs[0];
            socklen_t *namelen = (socklen_t *)imsg->ptrs[1];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || sin == NULL || namelen == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (*namelen < (socklen_t)sizeof(struct sockaddr_in)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            sin->sin_len    = sizeof(struct sockaddr_in);
            sin->sin_family = AF_INET;

            if (g_sockets[slot_idx].type == SOCK_STREAM && g_sockets[slot_idx].tcp_pcb != NULL) {
                sin->sin_port        = lwip_htons(g_sockets[slot_idx].tcp_pcb->local_port);
                sin->sin_addr.s_addr = ip_2_ip4(&g_sockets[slot_idx].tcp_pcb->local_ip)->addr;
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM && g_sockets[slot_idx].udp_pcb != NULL) {
                sin->sin_port        = lwip_htons(g_sockets[slot_idx].udp_pcb->local_port);
                sin->sin_addr.s_addr = ip_2_ip4(&g_sockets[slot_idx].udp_pcb->local_ip)->addr;
            } else if (g_sockets[slot_idx].type == SOCK_RAW && g_sockets[slot_idx].raw_pcb != NULL) {
                sin->sin_port        = lwip_htons((u16_t)g_sockets[slot_idx].protocol);
                sin->sin_addr.s_addr = ip_2_ip4(&g_sockets[slot_idx].raw_pcb->local_ip)->addr;
            } else {
                sin->sin_port        = 0;
                sin->sin_addr.s_addr = 0;
            }

            *namelen = sizeof(struct sockaddr_in);
            imsg->result = 0;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_GETPEERNAME:
        {
            int client_fd = (int)imsg->args[0];
            struct sockaddr_in *sin = (struct sockaddr_in *)imsg->ptrs[0];
            socklen_t *namelen = (socklen_t *)imsg->ptrs[1];

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || sin == NULL || namelen == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (*namelen < (socklen_t)sizeof(struct sockaddr_in)) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            sin->sin_len    = sizeof(struct sockaddr_in);
            sin->sin_family = AF_INET;

            if (g_sockets[slot_idx].type == SOCK_STREAM && g_sockets[slot_idx].tcp_pcb != NULL) {
                if (g_sockets[slot_idx].tcp_state != TN_TCP_STATE_ESTABLISHED &&
                    g_sockets[slot_idx].tcp_state != TN_TCP_STATE_CONNECTING) {
                    imsg->result = -1;
                    imsg->err_no = ENOTCONN;
                    return TRUE;
                }
                sin->sin_port        = lwip_htons(g_sockets[slot_idx].tcp_pcb->remote_port);
                sin->sin_addr.s_addr = ip_2_ip4(&g_sockets[slot_idx].tcp_pcb->remote_ip)->addr;
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM && g_sockets[slot_idx].udp_pcb != NULL) {
                if (g_sockets[slot_idx].udp_pcb->remote_port == 0) {
                    imsg->result = -1;
                    imsg->err_no = ENOTCONN;
                    return TRUE;
                }
                sin->sin_port        = lwip_htons(g_sockets[slot_idx].udp_pcb->remote_port);
                sin->sin_addr.s_addr = ip_2_ip4(&g_sockets[slot_idx].udp_pcb->remote_ip)->addr;
            } else if (g_sockets[slot_idx].type == SOCK_RAW && g_sockets[slot_idx].raw_pcb != NULL) {
                sin->sin_port        = 0;
                sin->sin_addr.s_addr = ip_2_ip4(&g_sockets[slot_idx].raw_pcb->remote_ip)->addr;
            } else {
                imsg->result = -1;
                imsg->err_no = ENOTCONN;
                return TRUE;
            }

            *namelen = sizeof(struct sockaddr_in);
            imsg->result = 0;
            imsg->err_no = 0;
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

            int chk;

            if (base == NULL) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            chk = tn_fdset_check_nfds((int)nfds);
            if (chk != 0) {
                imsg->result = -1;
                imsg->err_no = chk;
                return TRUE;
            }

            for (i = 0; i < nfds; i++) {
                slot_idx = base->fd_map[i];
                if (slot_idx >= 0 && slot_idx < TN_MAX_GLOBAL_SOCKETS && g_sockets[slot_idx].in_use) {
                    /* Read readiness */
                    if (in_r & (1UL << i)) {
                        if (g_sockets[slot_idx].rx_head != NULL ||
                            g_sockets[slot_idx].tcp_state == TN_TCP_STATE_PEER_CLOSED ||
                            (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_LISTENING && g_sockets[slot_idx].accept_head != NULL)) {
                            out_r |= (1UL << i);
                            ready_cnt++;
                        }
                    }
                    /* Write readiness */
                    if (in_w & (1UL << i)) {
                        if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_ESTABLISHED ||
                            g_sockets[slot_idx].type == 2 /* UDP */ ||
                            g_sockets[slot_idx].type == 3 /* RAW */) {
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

    case TN_IPC_CMD_RECONFIG:
        {
            /* TNET-064: reload config from the prefs stores and apply the
             * live-configurable subset. Interface-level changes require a
             * stack restart, which is reported honestly in the log. */
            TnPrefs old = g_prefs;
            BOOL loaded = tn_prefs_load(&g_prefs);

            g_log_level = TN_LOG_BASIC + ((g_prefs.debug > 0) ? 1 : 0);
            tn_apply_live_config();

            if (!tn_streq(old.device, g_prefs.device) || old.unit != g_prefs.unit ||
                old.use_dhcp != g_prefs.use_dhcp ||
                !tn_streq(old.ip_addr, g_prefs.ip_addr) ||
                !tn_streq(old.netmask, g_prefs.netmask) ||
                !tn_streq(old.gateway, g_prefs.gateway)) {
                tn_log(TN_LOG_BASIC, "tolunnet: RECONFIG: interface settings changed - stop and start the stack to apply them\n");
            } else {
                tn_log(TN_LOG_BASIC, "tolunnet: RECONFIG applied (DNS, hostname, MTU, debug tier)\n");
            }

            imsg->result = loaded ? 0 : -1;
            imsg->err_no = loaded ? 0 : ENOENT;
            return TRUE;
        }

    case TN_IPC_CMD_ENUMSOCKETS:
        {
            LONG max_entries = imsg->args[0];
            TnSocketInfo *out_info = (TnSocketInfo *)imsg->ptrs[0];
            LONG count = 0;
            int s;

            if (out_info == NULL || max_entries <= 0) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            for (s = 0; s < TN_MAX_GLOBAL_SOCKETS && count < max_entries; s++) {
                if (g_sockets[s].in_use) {
                    TnSocketInfo *ent = &out_info[count];
                    ent->proto = (UBYTE)g_sockets[s].type;
                    ent->state = (UBYTE)g_sockets[s].tcp_state;
                    ent->recv_q = g_sockets[s].rx_count;
                    ent->send_q = 0;

                    if (g_sockets[s].type == 1 /* TCP */ && g_sockets[s].tcp_pcb != NULL) {
                        ent->local_port  = g_sockets[s].tcp_pcb->local_port;
                        ent->remote_port = g_sockets[s].tcp_pcb->remote_port;
                        ent->local_ip    = ip_2_ip4(&g_sockets[s].tcp_pcb->local_ip)->addr;
                        ent->remote_ip   = ip_2_ip4(&g_sockets[s].tcp_pcb->remote_ip)->addr;
                        ent->send_q      = (ULONG)tcp_sndbuf(g_sockets[s].tcp_pcb);
                    } else if (g_sockets[s].type == 2 /* UDP */ && g_sockets[s].udp_pcb != NULL) {
                        ent->local_port  = g_sockets[s].udp_pcb->local_port;
                        ent->remote_port = g_sockets[s].udp_pcb->remote_port;
                        ent->local_ip    = ip_2_ip4(&g_sockets[s].udp_pcb->local_ip)->addr;
                        ent->remote_ip   = ip_2_ip4(&g_sockets[s].udp_pcb->remote_ip)->addr;
                    } else if (g_sockets[s].type == 3 /* RAW */ && g_sockets[s].raw_pcb != NULL) {
                        ent->local_port  = (UWORD)g_sockets[s].protocol;
                        ent->remote_port = 0;
                        ent->local_ip    = ip_2_ip4(&g_sockets[s].raw_pcb->local_ip)->addr;
                        ent->remote_ip   = ip_2_ip4(&g_sockets[s].raw_pcb->remote_ip)->addr;
                    } else {
                        ent->local_port  = 0;
                        ent->remote_port = 0;
                        ent->local_ip    = 0;
                        ent->remote_ip   = 0;
                    }
                    count++;
                }
            }

            imsg->result = count;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_SENDMSG:
        {
            int client_fd = (int)imsg->args[0];
            const struct msghdr *msg = (const struct msghdr *)imsg->ptrs[0];
            LONG flags = imsg->args[1];
            const struct sockaddr_in *to = NULL;
            ULONG total_len = 0;
            ULONG i;

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || msg == NULL) {
                imsg->result = -1;
                imsg->err_no = (msg == NULL) ? EINVAL : EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (flags & MSG_OOB) {
                imsg->result = -1;
                imsg->err_no = EOPNOTSUPP;
                return TRUE;
            }

            if (msg->msg_iov == NULL || msg->msg_iovlen == 0 || msg->msg_iovlen > 1024) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            for (i = 0; i < msg->msg_iovlen; i++) {
                if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base == NULL) {
                    imsg->result = -1;
                    imsg->err_no = EFAULT;
                    return TRUE;
                }
                total_len += msg->msg_iov[i].iov_len;
            }

            if (msg->msg_name != NULL) {
                if (msg->msg_namelen < sizeof(struct sockaddr_in)) {
                    imsg->result = -1;
                    imsg->err_no = EINVAL;
                    return TRUE;
                }
                to = (const struct sockaddr_in *)msg->msg_name;
            }

            if (g_sockets[slot_idx].type == SOCK_STREAM && g_sockets[slot_idx].tcp_pcb != NULL) {
                u16_t snd_buf;
                u16_t to_send;
                u16_t remaining;
                u16_t sent_bytes = 0;

                if (g_sockets[slot_idx].tcp_state != TN_TCP_STATE_ESTABLISHED) {
                    imsg->result = -1;
                    imsg->err_no = (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_ERROR) ? ECONNRESET : ENOTCONN;
                    return TRUE;
                }

                snd_buf = tcp_sndbuf(g_sockets[slot_idx].tcp_pcb);
                if (snd_buf == 0 && total_len > 0) {
                    imsg->result = -1;
                    imsg->err_no = EWOULDBLOCK;
                    return TRUE;
                }

                to_send = (total_len > (ULONG)snd_buf) ? snd_buf : (u16_t)total_len;
                remaining = to_send;

                for (i = 0; i < msg->msg_iovlen && remaining > 0; i++) {
                    u16_t chunk = (msg->msg_iov[i].iov_len > (size_t)remaining) ? remaining : (u16_t)msg->msg_iov[i].iov_len;
                    if (chunk > 0) {
                        err_t werr = tcp_write(g_sockets[slot_idx].tcp_pcb, msg->msg_iov[i].iov_base, chunk, TCP_WRITE_FLAG_COPY);
                        if (werr != ERR_OK) {
                            if (sent_bytes == 0) {
                                imsg->result = -1;
                                imsg->err_no = ENOBUFS;
                                return TRUE;
                            }
                            break;
                        }
                        sent_bytes += chunk;
                        remaining -= chunk;
                    }
                }

                tcp_output(g_sockets[slot_idx].tcp_pcb);
                tn_drain_loopback();
                imsg->result = (LONG)sent_bytes;
                imsg->err_no = 0;
                return TRUE;
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM && g_sockets[slot_idx].udp_pcb != NULL) {
                struct pbuf *p;
                ip_addr_t dst_ip;
                u16_t dst_port;
                u16_t send_len = (total_len > 0xFFFF) ? 0xFFFF : (u16_t)total_len;
                u16_t offset = 0;

                p = pbuf_alloc(PBUF_TRANSPORT, send_len, PBUF_RAM);
                if (p == NULL) {
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
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
                    dst_ip = g_sockets[slot_idx].udp_pcb->remote_ip;
                    dst_port = g_sockets[slot_idx].udp_pcb->remote_port;
                }

                udp_sendto(g_sockets[slot_idx].udp_pcb, p, &dst_ip, dst_port);
                pbuf_free(p);
                tn_drain_loopback();

                imsg->result = (LONG)send_len;
                imsg->err_no = 0;
                return TRUE;
            } else if (g_sockets[slot_idx].type == SOCK_RAW && g_sockets[slot_idx].raw_pcb != NULL) {
                struct pbuf *p;
                ip_addr_t dst_ip;
                u16_t send_len = (total_len > 0xFFFF) ? 0xFFFF : (u16_t)total_len;
                u16_t offset = 0;
                err_t serr;

                p = pbuf_alloc(PBUF_IP, send_len, PBUF_RAM);
                if (p == NULL) {
                    imsg->result = -1;
                    imsg->err_no = ENOBUFS;
                    return TRUE;
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
                    dst_ip = g_sockets[slot_idx].raw_pcb->remote_ip;
                }

                serr = raw_sendto(g_sockets[slot_idx].raw_pcb, p, &dst_ip);
                pbuf_free(p);

                if (serr != ERR_OK) {
                    imsg->result = -1;
                    imsg->err_no = (serr == ERR_MEM) ? ENOBUFS : EHOSTUNREACH;
                    return TRUE;
                }

                imsg->result = (LONG)send_len;
                imsg->err_no = 0;
                return TRUE;
            }

            imsg->result = -1;
            imsg->err_no = EOPNOTSUPP;
            return TRUE;
        }

    case TN_IPC_CMD_RECVMSG:
        {
            int client_fd = (int)imsg->args[0];
            struct msghdr *msg = (struct msghdr *)imsg->ptrs[0];
            LONG flags = imsg->args[1];
            ULONG total_space = 0;
            ULONG i;

            if (base == NULL || client_fd < 0 || client_fd >= TN_MAX_FDS_PER_TASK || msg == NULL) {
                imsg->result = -1;
                imsg->err_no = (msg == NULL) ? EINVAL : EBADF;
                return TRUE;
            }

            slot_idx = base->fd_map[client_fd];
            if (slot_idx < 0 || slot_idx >= TN_MAX_GLOBAL_SOCKETS || !g_sockets[slot_idx].in_use) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return TRUE;
            }

            if (flags & MSG_OOB) {
                imsg->result = -1;
                imsg->err_no = EOPNOTSUPP;
                return TRUE;
            }

            if (msg->msg_iov == NULL || msg->msg_iovlen == 0 || msg->msg_iovlen > 1024) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            for (i = 0; i < msg->msg_iovlen; i++) {
                if (msg->msg_iov[i].iov_len > 0 && msg->msg_iov[i].iov_base == NULL) {
                    imsg->result = -1;
                    imsg->err_no = EFAULT;
                    return TRUE;
                }
                total_space += msg->msg_iov[i].iov_len;
            }

            msg->msg_flags = 0;
            if (msg->msg_control != NULL) {
                msg->msg_controllen = 0;
            }

            if (g_sockets[slot_idx].type == SOCK_STREAM) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    ULONG cur_iov = 0;
                    ULONG iov_offset = 0;
                    ULONG total_copied = 0;

                    if (flags & MSG_PEEK) {
                        TnRxPacket *cur = g_sockets[slot_idx].rx_head;
                        while (cur != NULL && cur_iov < msg->msg_iovlen && total_copied < total_space) {
                            u16_t off = (cur == g_sockets[slot_idx].rx_head) ? cur->offset : 0;
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
                        while (g_sockets[slot_idx].rx_head != NULL && cur_iov < msg->msg_iovlen && total_copied < total_space) {
                            TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
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
                                if (g_sockets[slot_idx].tcp_pcb != NULL) {
                                    tcp_recved(g_sockets[slot_idx].tcp_pcb, chunk);
                                }
                            }
                            if (pkt->offset >= pkt->p->tot_len) {
                                g_sockets[slot_idx].rx_head = pkt->next;
                                if (g_sockets[slot_idx].rx_head == NULL) {
                                    g_sockets[slot_idx].rx_tail = NULL;
                                }
                                pbuf_free(pkt->p);
                                FreeVec(pkt);
                                if (g_sockets[slot_idx].rx_count > 0) g_sockets[slot_idx].rx_count--;
                            }
                        }
                    }

                    if (msg->msg_name != NULL) msg->msg_namelen = 0;
                    imsg->result = (LONG)total_copied;
                    imsg->err_no = 0;
                    return TRUE;
                } else if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_PEER_CLOSED) {
                    if (msg->msg_name != NULL) msg->msg_namelen = 0;
                    imsg->result = 0; /* EOF */
                    imsg->err_no = 0;
                    return TRUE;
                } else if (g_sockets[slot_idx].tcp_state == TN_TCP_STATE_ERROR) {
                    imsg->result = -1;
                    imsg->err_no = ECONNRESET;
                    return TRUE;
                } else {
                    imsg->result = -1;
                    imsg->err_no = EWOULDBLOCK;
                    return TRUE;
                }
            } else if (g_sockets[slot_idx].type == SOCK_DGRAM || g_sockets[slot_idx].type == SOCK_RAW) {
                if (g_sockets[slot_idx].rx_head != NULL) {
                    TnRxPacket *pkt = g_sockets[slot_idx].rx_head;
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
                        from->sin_port   = (g_sockets[slot_idx].type == SOCK_DGRAM) ? lwip_htons(pkt->src_port) : 0;
                        from->sin_addr.s_addr = ip_addr_get_ip4_u32(&pkt->src_ip);
                        msg->msg_namelen = sizeof(struct sockaddr_in);
                    }

                    if (!(flags & MSG_PEEK)) {
                        g_sockets[slot_idx].rx_head = pkt->next;
                        if (g_sockets[slot_idx].rx_head == NULL) {
                            g_sockets[slot_idx].rx_tail = NULL;
                        }
                        pbuf_free(pkt->p);
                        FreeVec(pkt);
                        if (g_sockets[slot_idx].rx_count > 0) g_sockets[slot_idx].rx_count--;
                    }

                    imsg->result = (LONG)total_copied;
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

    case TN_IPC_CMD_RELEASESOCKET:
        {
            int client_fd = (int)imsg->args[0];
            LONG req_id   = imsg->args[1];
            BOOL copy     = (BOOL)imsg->args[2];
            LONG assigned_id;

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

            if (req_id == UNIQUE_ID) {
                do {
                    assigned_id = g_next_park_id++;
                    if (g_next_park_id <= 0) g_next_park_id = 1;
                    BOOL dup = FALSE;
                    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
                        if (g_sockets[i].in_use && g_sockets[i].is_parked && g_sockets[i].park_id == assigned_id) {
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
                    return TRUE;
                }
                for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
                    if (g_sockets[i].in_use && g_sockets[i].is_parked && g_sockets[i].park_id == req_id) {
                        imsg->result = -1;
                        imsg->err_no = EADDRINUSE;
                        return TRUE;
                    }
                }
                assigned_id = req_id;
            }

            g_sockets[slot_idx].is_parked = TRUE;
            g_sockets[slot_idx].park_id   = assigned_id;

            if (copy) {
                g_sockets[slot_idx].ref_count++;
            } else {
                base->fd_map[client_fd] = -1;
            }

            imsg->result = assigned_id;
            imsg->err_no = 0;
            return TRUE;
        }

    case TN_IPC_CMD_OBTAINSOCKET:
        {
            LONG park_id  = imsg->args[0];
            int domain    = (int)imsg->args[1];
            int type      = (int)imsg->args[2];
            int protocol  = (int)imsg->args[3];
            int pref_fd   = (int)imsg->args[4];
            int client_fd = -1;

            if (base == NULL) {
                imsg->result = -1;
                imsg->err_no = EINVAL;
                return TRUE;
            }

            slot_idx = -1;
            for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
                if (g_sockets[i].in_use && g_sockets[i].is_parked && g_sockets[i].park_id == park_id) {
                    slot_idx = i;
                    break;
                }
            }
            if (slot_idx < 0) {
                imsg->result = -1;
                imsg->err_no = ENOENT;
                return TRUE;
            }

            if (domain != 0 && g_sockets[slot_idx].domain != domain) {
                imsg->result = -1;
                imsg->err_no = EPROTOTYPE;
                return TRUE;
            }
            if (type != 0 && g_sockets[slot_idx].type != type) {
                imsg->result = -1;
                imsg->err_no = EPROTOTYPE;
                return TRUE;
            }
            if (protocol != 0 && g_sockets[slot_idx].protocol != 0 && g_sockets[slot_idx].protocol != protocol) {
                imsg->result = -1;
                imsg->err_no = EPROTONOSUPPORT;
                return TRUE;
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
                return TRUE;
            }

            base->fd_map[client_fd] = slot_idx;
            g_sockets[slot_idx].is_parked = FALSE;
            g_sockets[slot_idx].park_id   = 0;
            g_sockets[slot_idx].owner_base = base;
            g_sockets[slot_idx].owner_task = imsg->client_task;

            imsg->result = client_fd;
            imsg->err_no = 0;
            return TRUE;
        }

    default:
        imsg->result = -1;
        imsg->err_no = ENOSYS;
        return TRUE;
    }
}

static BOOL        g_stack_swapped = FALSE;
static ULONG       g_orig_stack_size = 0;
static const char *g_cli_device = NULL;
static const LONG *g_cli_unit = NULL;
static const char *g_cli_ip = NULL;
static const char *g_cli_netmask = NULL;
static const char *g_cli_gateway = NULL;

static int tn_task_real_main(int argc, char *argv[])
{
    struct ExecBase *SysBase;
    struct Library  *DOSBase;
    struct Task     *self_task;
    BYTE             old_pri;
    CONST_STRPTR device = (CONST_STRPTR)"ethernet.device";
    ULONG unit = 0;
    TnS2Result s2res;
    ip4_addr_t ipaddr, netmask, gw;
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
        g_sockets[i].accept_head         = NULL;
        g_sockets[i].accept_tail         = NULL;
        g_sockets[i].accept_count        = 0;
        g_sockets[i].pending_connect_msg = NULL;
        g_sockets[i].pending_accept_msg  = NULL;
    }

    /* Load persistent configuration first (defaults when absent) so every
     * key — including HOSTNAME/DNS2/MTU/DEBUG (TNET-063) — is honoured.
     * CLI arguments override the interface-level keys. */
    tn_prefs_load(&g_prefs);
    g_log_level = TN_LOG_BASIC + ((g_prefs.debug > 0) ? 1 : 0);

    /* TNET-066: Set task priority from prefs (default 5) */
    self_task = FindTask(NULL);
    old_pri = SetTaskPri(self_task, (BYTE)g_prefs.priority);

    device   = (CONST_STRPTR)g_prefs.device;
    unit     = g_prefs.unit;
    use_dhcp = g_prefs.use_dhcp;
    if (!use_dhcp) {
        ip4addr_aton(g_prefs.ip_addr, &ipaddr);
        ip4addr_aton(g_prefs.netmask, &netmask);
        ip4addr_aton(g_prefs.gateway, &gw);
    } else {
        IP4_ADDR(&ipaddr, 0, 0, 0, 0);
        IP4_ADDR(&netmask, 0, 0, 0, 0);
        IP4_ADDR(&gw, 0, 0, 0, 0);
    }

    if (g_cli_device != NULL) {
        device = (CONST_STRPTR)g_cli_device;
    }
    if (g_cli_unit != NULL) {
        unit = (ULONG)*g_cli_unit;
    }
    if (g_cli_ip != NULL) {
        ip4addr_aton(g_cli_ip, &ipaddr);
        use_dhcp = FALSE;
    }
    if (g_cli_netmask != NULL) {
        ip4addr_aton(g_cli_netmask, &netmask);
    }
    if (g_cli_gateway != NULL) {
        ip4addr_aton(g_cli_gateway, &gw);
    }

    if (argc >= 2 && g_cli_device == NULL) {
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
        }
    }

    tn_log(TN_LOG_BASIC, "========================================\n");
    tn_log(TN_LOG_BASIC, "tolunnet: network task starting...\n");
    tn_log(TN_LOG_BASIC, "========================================\n");

    if (g_stack_swapped) {
        tn_logf(TN_LOG_BASIC, "tolunnet: switched to 32 KB stack (was %lu bytes)\n", g_orig_stack_size);
    }
    tn_logf(TN_LOG_BASIC, "tolunnet: task priority set to %ld (was %ld)\n",
            (LONG)g_prefs.priority, (LONG)old_pri);

    log_fh = Open((CONST_STRPTR)"WORK:tolunnet-task.log", MODE_NEWFILE);
    g_log_file = log_fh;

    /* 1. Initialize timer.device */
    if (!tn_timer_init(&g_timer)) {
        tn_log(TN_LOG_BASIC, "tolunnet: failed to initialize timer.device\n");
        SetTaskPri(self_task, old_pri);
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

    /* DNS servers, DHCP hostname (option 12), MTU clamp, debug tier (TNET-063) */
    tn_apply_live_config();

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

        /* Ctrl-C Signal -> Shutdown.
         * TNET-059: per-opener clones embed jump tables that point into this
         * task's code segment. Exiting with lib_OpenCnt > 0 would unload that
         * code under live clients → Guru on their next call. Refuse to exit
         * and keep servicing IPC until every opener has closed. */
        if (sigs & ctrl_c_sig) {
            if (g_bsd_lib != NULL && g_bsd_lib->lib_OpenCnt > 0) {
                tn_logf(TN_LOG_BASIC,
                        "\ntolunnet: Ctrl-C: %lu client(s) still have bsdsocket.library open;\n"
                        "tolunnet: not exiting - close them (or their apps) and press Ctrl-C again\n",
                        (ULONG)g_bsd_lib->lib_OpenCnt);
            } else {
                tn_log(TN_LOG_BASIC, "\ntolunnet: Ctrl-C received, shutting down...\n");
                running = FALSE;
                break;
            }
        }

        /* Client IPC Message Signal */
        if (sigs & ipc_sig) {
            struct Message *msg;
            while ((msg = GetMsg(g_ipc_port)) != NULL) {
                if (tn_handle_ipc((TnIpcMsg *)msg)) {
                    ReplyMsg(msg);
                }
            }
            tn_drain_loopback();
        }

        /* SANA-II Packet Arrival Signal */
        if (sigs & s2_sig) {
            tn_sana2_poll_input(&g_s2if, &g_netif);
            tn_drain_loopback();
        }

        /* 100 ms Timer Tick Signal */
        if (sigs & timer_sig) {
            tn_timer_ack(&g_timer);
            tick_count++;

            /* Drive lwIP timeouts */
            sys_check_timeouts();
            tn_drain_loopback();

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

    /* Restore original task priority */
    tn_log(TN_LOG_BASIC, "tolunnet: restoring task priority...\n");
    SetTaskPri(self_task, old_pri);

    tn_log(TN_LOG_BASIC, "tolunnet: shutdown complete.\n");
    CloseLibrary(DOSBase);
    return 0;
}

enum {
    OPT_START = 0,
    OPT_STOP,
    OPT_STATUS,
    OPT_RECONFIG,
    OPT_DEVICE,
    OPT_UNIT,
    OPT_IP,
    OPT_NETMASK,
    OPT_GATEWAY,
    OPT_COUNT
};

int main(int argc, char *argv[])
{
    struct Library *dos_base = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (dos_base == NULL) return 20;

    if (argc > 0) {
        LONG opts[OPT_COUNT];
        struct RDArgs *rdargs;
        int i;
        for (i = 0; i < OPT_COUNT; i++) opts[i] = 0;

        rdargs = ReadArgs((CONST_STRPTR)"START/S,STOP/S,STATUS/S,RECONFIG/S,DEVICE,UNIT/N,IP,NETMASK,GATEWAY", opts, NULL);
        if (rdargs == NULL) {
            PrintFault(IoErr(), (CONST_STRPTR)"tolunnet");
            CloseLibrary(dos_base);
            return 20;
        }

        if (opts[OPT_STOP]) {
            struct MsgPort *dp = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
            FreeArgs(rdargs);
            if (dp == NULL) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is not running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            if (dp->mp_SigTask != NULL) {
                Signal((struct Task *)dp->mp_SigTask, SIGBREAKF_CTRL_C);
            }
            PutStr((CONST_STRPTR)"tolunnet: stop signal sent to daemon\n");
            CloseLibrary(dos_base);
            return 0;
        }

        if (opts[OPT_RECONFIG]) {
            int res;
            FreeArgs(rdargs);
            res = tn_ipc_oneshot(TN_IPC_CMD_RECONFIG, NULL, 0, NULL);
            if (res != 0) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is not running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            PutStr((CONST_STRPTR)"tolunnet: configuration reloaded\n");
            CloseLibrary(dos_base);
            return 0;
        }

        if (opts[OPT_STATUS]) {
            TnIpcMsg msg;
            int res;
            FreeArgs(rdargs);
            res = tn_ipc_oneshot(TN_IPC_CMD_GETSTATUS, NULL, 0, &msg);
            if (res != 0) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is not running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            {
                ULONG ip = (ULONG)msg.args[0];
                ULONG nm = (ULONG)msg.args[1];
                ULONG gw = (ULONG)msg.args[2];
                LONG socks = msg.args[3];
                char buf[80];
                ULONG ip_parts[4] = { (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF };
                ULONG nm_parts[4] = { (nm >> 24) & 0xFF, (nm >> 16) & 0xFF, (nm >> 8) & 0xFF, nm & 0xFF };
                ULONG gw_parts[4] = { (gw >> 24) & 0xFF, (gw >> 16) & 0xFF, (gw >> 8) & 0xFF, gw & 0xFF };

                PutStr((CONST_STRPTR)"tolunnet daemon status: RUNNING\n");

                RawDoFmt((CONST_STRPTR)"  IP Address : %lu.%lu.%lu.%lu\n", (APTR)ip_parts, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);

                RawDoFmt((CONST_STRPTR)"  Netmask    : %lu.%lu.%lu.%lu\n", (APTR)nm_parts, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);

                RawDoFmt((CONST_STRPTR)"  Gateway    : %lu.%lu.%lu.%lu\n", (APTR)gw_parts, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);

                RawDoFmt((CONST_STRPTR)"  Sockets    : %ld active\n", (APTR)&socks, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);
            }
            CloseLibrary(dos_base);
            return 0;
        }

        if (opts[OPT_START]) {
            FreeArgs(rdargs);
            if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is already running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            SystemTags((CONST_STRPTR)"C:tolunnet",
                       SYS_Asynch, TRUE,
                       SYS_Input, (BPTR)0,
                       SYS_Output, (BPTR)0,
                       NP_StackSize, 32768,
                       TAG_END);
            PutStr((CONST_STRPTR)"tolunnet: daemon started in background (32 KB stack)\n");
            CloseLibrary(dos_base);
            return 0;
        }

        if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
            PutStr((CONST_STRPTR)"tolunnet: daemon is already running\n");
            FreeArgs(rdargs);
            CloseLibrary(dos_base);
            return 5;
        }

        g_cli_device  = (const char *)opts[OPT_DEVICE];
        g_cli_unit    = (const LONG *)opts[OPT_UNIT];
        g_cli_ip      = (const char *)opts[OPT_IP];
        g_cli_netmask = (const char *)opts[OPT_NETMASK];
        g_cli_gateway = (const char *)opts[OPT_GATEWAY];

        FreeArgs(rdargs);
    } else {
        /* Workbench startup */
        if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
            CloseLibrary(dos_base);
            return 5;
        }
    }

    CloseLibrary(dos_base);

    {
        struct Process *proc = (struct Process *)FindTask(NULL);
        ULONG stack_size = 0;

        if (proc != NULL && proc->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
            stack_size = proc->pr_StackSize;
        }

        if (stack_size < 32768) {
            struct StackSwapStruct stk;
            APTR new_stk = AllocMem(32768, MEMF_PUBLIC | MEMF_CLEAR);
            int rc;
            if (new_stk == NULL) {
                return 20;
            }
            g_stack_swapped = TRUE;
            g_orig_stack_size = stack_size;

            stk.stk_Lower   = new_stk;
            stk.stk_Upper   = (ULONG)new_stk + 32768;
            stk.stk_Pointer = (APTR)stk.stk_Upper;

            StackSwap(&stk);
            rc = tn_task_real_main(argc, argv);
            StackSwap(&stk);

            FreeMem(new_stk, 32768);
            return rc;
        }
    }

    return tn_task_real_main(argc, argv);
}
