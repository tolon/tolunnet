/*
 * tolunnet — Network Task Context & Shared Data Structures.
 *
 * Master prompt §M2–§M5; ROUND4b §B (Daemon Modularization).
 * All task modules receive TnDaemon *d as first parameter.
 */
#ifndef TOLUNNET_TASK_CTX_H
#define TOLUNNET_TASK_CTX_H

#include <stdint.h>
#include <stddef.h>

#if defined(__AMIGA__) || defined(__amigaos__) || defined(TN_AMIGA_BUILD)
#include "../sana2/sana2_netif.h"
#include "../lib/lib_init.h"
#include "../common/log.h"
#include "../common/prefs.h"
#include "timers.h"
#include "../../include/ipc.h"
#include "../common/fdset_util.h"
#include "../common/sockaddr_util.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/tasks.h>
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
#else
/* Host test environment */
#include "mock_lwip.h"
#include "prefs.h"
#include "fdset_util.h"
#include "sockaddr_util.h"
#include <sys/errno.h>

struct timeval {
    unsigned int tv_secs;
    unsigned int tv_micro;
};
#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H 1
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#undef TCP_MSS
#undef htons
#undef ntohs
#undef htonl
#undef ntohl
#include <netinet/tcp.h>
#undef TCP_MSS
#define TCP_MSS 1460
#endif

#define TN_MAX_GLOBAL_SOCKETS 64
#define TN_MAX_RX_QUEUE_PER_SOCKET 32

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

#ifndef TN_MAX_NETIF
#define TN_MAX_NETIF 4
#endif

typedef struct TnNetif {
    struct netif   lwip_if;
    TnSana2If      s2if;
    BOOL           in_use;
    char           name[16];
    uint16_t       unit;
    uint8_t        family;
    uint8_t        addr_count;
    ip_addr_t      addrs[4];
    ip_addr_t      netmask;
    ip_addr_t      gw;
    BOOL           is_dhcp;
    BOOL           link_up;
} TnNetif;

#ifndef TN_MAX_SELECTORS
#define TN_MAX_SELECTORS 16
#endif

typedef struct TnSelector {
    BOOL          in_use;
    TnSocketBase *base;
    struct Task  *task;
    ULONG         sig_select;
    LONG          nfds;
    ULONG         read_mask;
    ULONG         write_mask;
    ULONG         except_mask;
} TnSelector;

/* Daemon Singleton State */
/* TNET-150: a DEFERRED DNS request parks the client's TnIpcMsg with lwIP
 * (callback_arg); the daemon keeps its own record so CLOSE can cancel it
 * and a late callback can never touch an abandoned/freed message.
 * TN_DNS_PENDING_MAX lives in prefs.h (shared with the config parser). */
typedef struct TnDnsPending {
    TnIpcMsg       *imsg;   /* client message parked with lwIP */
    TnSocketBase   *base;   /* recorded base (survives imsg lifetime checks) */
    char            name[64];
    uint32_t        tick;   /* registered at (diagnostics) */
    uint8_t         in_use;
} TnDnsPending;

typedef struct TnDaemon {
    TnNetif         ifs[TN_MAX_NETIF];
    uint8_t         if_count;
    TnTimer         timer;
    struct MsgPort *ipc_port;
    struct Library *bsd_lib;
    TnSocketSlot    sockets[TN_MAX_GLOBAL_SOCKETS];
    LONG            next_park_id;
    TnPrefs         prefs;
    BOOL            running;
    TnIpcMsg       *stop_msg;       /* TNET-152: parked stop message replied after RemPort & SANA-II teardown */
    /* TNET-108: selector table is heap-allocated so SELECTORS= can grow it
     * at runtime via RECONFIG (tn_selector_table_grow). */
    TnSelector     *selectors;
    uint32_t        max_selectors;
    uint8_t         selector_count;

    /* Operational Telemetry (§F) */
    BOOL            stats_enabled;   /* TNET-108: STATS=NO freezes reports at zero */
    uint32_t        ipc_calls[32];
    uint32_t        deferred_replies;
    uint32_t        sigio_sent;
    uint32_t        selector_wakeups;
    uint32_t        mainloop_ticks;
    uint32_t        s2_rx_frames;
    uint32_t        s2_rx_bytes;
    uint32_t        s2_rx_drops;
    uint32_t        s2_tx_frames;
    uint32_t        s2_tx_bytes;
    uint32_t        s2_tx_drops;
    uint32_t        s2_link_errors;  /* S2EVENT_ERROR-class events (TNET-109) */
    uint32_t        rx_high_water;
    uint32_t        dns_late_replies;   /* TNET-150: found_cb with no pending record */
    ULONG           start_sec;

    /* TNET-150: deferred gethostbyname tracking (TnDnsPending array) */
    TnDnsPending    dns_pending[TN_DNS_PENDING_MAX];
    uint8_t         dns_pending_count;

    /* TNET-150 item 6: registry of open client bases so the Ctrl-C path
     * can name every holder and reap bases whose task died without
     * CloseLibrary (crashed client => stop would refuse forever). */
#define TN_CLIENT_BASES_MAX 16
    TnSocketBase   *open_bases[TN_CLIENT_BASES_MAX];
    uint8_t         open_base_count;
} TnDaemon;

static inline TnNetif *tn_netif_primary(TnDaemon *d)
{
    return &d->ifs[0];
}

extern TnDaemon g_daemon;

#endif /* TOLUNNET_TASK_CTX_H */
