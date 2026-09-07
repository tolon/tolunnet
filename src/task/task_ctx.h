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

/* Daemon Singleton State */
typedef struct TnDaemon {
    TnSana2If       s2if;
    struct netif    netif;
    TnTimer         timer;
    struct MsgPort *ipc_port;
    struct Library *bsd_lib;
    TnSocketSlot    sockets[TN_MAX_GLOBAL_SOCKETS];
    LONG            next_park_id;
    TnPrefs         prefs;
    BOOL            running;
} TnDaemon;

extern TnDaemon g_daemon;

#endif /* TOLUNNET_TASK_CTX_H */
