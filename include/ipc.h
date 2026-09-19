/*
 * tolunnet — Public IPC Definitions and per-opener SocketBase.
 *
 * Master prompt §5: Single Exec task owns lwIP; clients communicate via
 * synchronous zero-allocation IPC messages.
 */

#ifndef TOLUNNET_IPC_H
#define TOLUNNET_IPC_H

#if defined(__AMIGA__) || defined(__amigaos__) || defined(TN_AMIGA_BUILD)
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/libraries.h>
#include <netdb.h>
#else
#include <stdint.h>
#include <stddef.h>
typedef uint32_t ULONG;
typedef int32_t  LONG;
typedef uint16_t UWORD;
typedef uint8_t  UBYTE;
typedef int8_t   BYTE;
typedef void    *APTR;
typedef char    *STRPTR;
typedef const char *CONST_STRPTR;
typedef int      BOOL;
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
struct Node {
    void *ln_Succ;
    void *ln_Pred;
    uint8_t ln_Type;
    int8_t ln_Pri;
    char *ln_Name;
};
struct Message {
    struct Node mn_Node;
    void *mn_ReplyPort;
    uint16_t mn_Length;
};
struct Task;
struct MsgPort;
struct Library {
    struct Node lib_Node;
    uint8_t lib_Flags;
    uint8_t lib_pad;
    uint16_t lib_NegSize;
    uint16_t lib_PosSize;
    uint16_t lib_Version;
    uint16_t lib_Revision;
    char *lib_IdString;
    uint32_t lib_Sum;
    uint16_t lib_OpenCnt;
};
struct hostent { char *h_name; char **h_aliases; int h_addrtype; int h_length; char **h_addr_list; };
struct servent { char *s_name; char **s_aliases; int s_port; char *s_proto; };
struct protoent { char *p_name; char **p_aliases; int p_proto; };
struct netent { char *n_name; char **n_aliases; int n_addrtype; uint32_t n_net; };
#endif

#define TOLUNNET_PORT_NAME "tolunnet.port"
#define BSDSOCKET_NAME     "bsdsocket.library"
#define BSDSOCKET_VER      4
#define BSDSOCKET_REV      1

#define TN_MAX_FDS_PER_TASK 128                        /* compiled ceiling */
#define TN_DEFAULT_DTABLESIZE 64                       /* Roadshow default (TN-bugtrack-2 item 4) */

/* IPC Command Codes */
typedef enum TnIpcCmd {
    TN_IPC_CMD_OPEN = 0,        /* OpenLibrary notification */
    TN_IPC_CMD_CLOSE,           /* CloseLibrary cleanup */
    TN_IPC_CMD_SOCKET,          /* socket(domain, type, protocol) */
    TN_IPC_CMD_BIND,            /* bind(sock, name, namelen) */
    TN_IPC_CMD_LISTEN,          /* listen(sock, backlog) */
    TN_IPC_CMD_ACCEPT,          /* accept(sock, addr, addrlen) */
    TN_IPC_CMD_CONNECT,         /* connect(sock, name, namelen) */
    TN_IPC_CMD_SENDTO,          /* sendto(sock, buf, len, flags, to, tolen) */
    TN_IPC_CMD_SEND,            /* send(sock, buf, len, flags) */
    TN_IPC_CMD_RECVFROM,        /* recvfrom(sock, buf, len, flags, addr, addrlen) */
    TN_IPC_CMD_RECV,            /* recv(sock, buf, len, flags) */
    TN_IPC_CMD_SHUTDOWN,        /* shutdown(sock, how) */
    TN_IPC_CMD_SETSOCKOPT,      /* setsockopt(...) */
    TN_IPC_CMD_GETSOCKOPT,      /* getsockopt(...) */
    TN_IPC_CMD_GETSOCKNAME,     /* getsockname(...) */
    TN_IPC_CMD_GETPEERNAME,     /* getpeername(...) */
    TN_IPC_CMD_IOCTL,           /* IoctlSocket(...) */
    TN_IPC_CMD_CLOSESOCKET,     /* CloseSocket(sock) */
    TN_IPC_CMD_GETHOSTBYNAME,   /* gethostbyname(name) */
    TN_IPC_CMD_GETHOSTBYADDR,   /* gethostbyaddr(addr, len, type) */
    TN_IPC_CMD_WAITSELECT,      /* WaitSelect(...) */
    TN_IPC_CMD_DUP2,            /* Dup2Socket(old_fd, new_fd) */
    TN_IPC_CMD_GETSTATUS,       /* Query live daemon interface status and socket count */
    TN_IPC_CMD_RECONFIG,        /* Reload configuration (TNET-064; TolunnetPrefs Save/Use) */
    TN_IPC_CMD_ENUMSOCKETS,     /* Enumerate active sockets (TNET-071; netstat) */
    TN_IPC_CMD_SENDMSG,         /* sendmsg(sock, msg, flags) */
    TN_IPC_CMD_RECVMSG,         /* recvmsg(sock, msg, flags) */
    TN_IPC_CMD_RELEASESOCKET,   /* ReleaseSocket(sock, id, copy) */
    TN_IPC_CMD_OBTAINSOCKET,    /* ObtainSocket(id, domain, type, protocol, pref_fd) */
    TN_IPC_CMD_SELECT_ARM,      /* WaitSelect: arm selector for event-driven wake (§D) */
    TN_IPC_CMD_SELECT_DISARM,   /* WaitSelect: disarm selector (§D) */
    TN_IPC_CMD_GETSTATS,        /* Query stack telemetry and statistics (§F) */
    TN_IPC_CMD_ROUTECTL         /* CLOSE §B.5: static route SHOW/ADD/DELETE (route) */
} TnIpcCmd;

/* Route row for TN_IPC_CMD_ROUTECTL LIST replies (CLOSE §B.5).
 * addresses in network byte order; gw 0 = interface route. */
typedef struct TnRouteInfo {
    uint16_t struct_size;   /* sizeof(TnRouteInfo) */
    uint8_t  in_use;
    uint8_t  pad;
    uint32_t dest;
    uint32_t mask;
    uint32_t gw;
} TnRouteInfo;

/* ROUTECTL ops (args[0]) */
#define TN_ROUTECTL_LIST   0  /* ptrs[0]=TnRouteInfo* out, args[4]=capacity; result=count */
#define TN_ROUTECTL_ADD    1  /* args[1]=dest args[2]=mask args[3]=gw; result 0/-1+errno */
#define TN_ROUTECTL_DELETE 2  /* args[1]=dest args[2]=mask; result 0/-1+errno */

/* Sub-structure for protocol statistics (fixed width for client/daemon portability) */
typedef struct TnProtoStats {
    uint32_t xmit;
    uint32_t recv;
    uint32_t fw;
    uint32_t drop;
    uint32_t chkerr;
    uint32_t lenerr;
    uint32_t memerr;
    uint32_t rterr;
    uint32_t proterr;
    uint32_t opterr;
    uint32_t err;
    uint32_t cachehit;
} TnProtoStats;

/* Memory pool statistics */
typedef struct TnMempStats {
    char     name[16];
    uint32_t used;
    uint32_t max;
    uint32_t avail;
    uint32_t err;
} TnMempStats;

#define TN_STATS_MAX_MEMP 20

/* Daemon operational telemetry */
typedef struct TnDaemonStats {
    uint32_t ipc_calls[32];       /* IPC calls per command code (TnIpcCmd 0..31) */
    uint32_t deferred_replies;    /* Non-blocking/async deferred IPC replies */
    uint32_t sigio_sent;          /* Total SIGIO signals delivered */
    uint32_t selector_wakeups;    /* Event-driven selector wakeups (§D) */
    uint32_t mainloop_ticks;      /* Total main loop iterations / 100ms ticks */
    uint32_t s2_rx_frames;        /* SANA-II frames received */
    uint32_t s2_rx_bytes;         /* SANA-II bytes received */
    uint32_t s2_rx_drops;         /* SANA-II dropped frames (buffer full/OOM) */
    uint32_t s2_tx_frames;        /* SANA-II frames transmitted */
    uint32_t s2_tx_bytes;         /* SANA-II bytes transmitted */
    uint32_t s2_tx_drops;         /* SANA-II transmit errors/drops */
    uint32_t rx_high_water;       /* Peak RX queue depth across all slots */
    uint32_t uptime_secs;         /* Uptime in seconds */
    uint32_t lease_remaining;     /* DHCP lease remaining seconds (0 if static/infinite) */
    uint32_t lease_t1;            /* DHCP renewal time (t1) remaining seconds */
    uint32_t lease_t2;            /* DHCP rebind time (t2) remaining seconds */
} TnDaemonStats;

/* Complete telemetry structure for TN_IPC_CMD_GETSTATS (§F) */
typedef struct TnStats {
    uint16_t struct_size;         /* sizeof(TnStats) */
    uint16_t version;             /* 1 */

    TnProtoStats link;
    TnProtoStats etharp;
    TnProtoStats ip;
    TnProtoStats icmp;
    TnProtoStats udp;
    TnProtoStats tcp;

    uint32_t mem_used;
    uint32_t mem_max;
    uint32_t mem_avail;
    uint32_t mem_err;

    uint16_t num_memp;            /* Number of valid entries in memp[] */
    uint16_t pad;
    TnMempStats memp[TN_STATS_MAX_MEMP];

    TnDaemonStats daemon;
} TnStats;

/* Active socket description for TN_IPC_CMD_ENUMSOCKETS (TNET-071) */
typedef struct TnSocketInfo {
    UBYTE proto;        /* 1=TCP, 2=UDP, 3=RAW */
    UBYTE state;        /* TnTcpState */
    UWORD local_port;   /* host order */
    UWORD remote_port;  /* host order */
    ULONG local_ip;     /* network order */
    ULONG remote_ip;    /* network order */
    ULONG recv_q;       /* queued bytes / packets */
    ULONG send_q;       /* available send buffer */
} TnSocketInfo;

/* Versioned extended socket description for TN_IPC_CMD_ENUMSOCKETS (v2, IPv6-ready) */
typedef struct TnSocketInfoV2 {
    uint16_t struct_size; /* sizeof(TnSocketInfoV2) */
    uint8_t  family;      /* AF_INET=2, AF_INET6=10 */
    uint8_t  proto;       /* 1=TCP, 2=UDP, 3=RAW */
    uint8_t  state;       /* TnTcpState */
    uint8_t  pad;
    uint16_t local_port;  /* host order */
    uint16_t remote_port; /* host order */
    uint8_t  local_addr[16];
    uint8_t  remote_addr[16];
    uint32_t recv_q;      /* queued bytes / packets */
    uint32_t send_q;      /* available send buffer */
} TnSocketInfoV2;

/* Versioned status structure for TN_IPC_CMD_GETSTATUS (v2, IPv6-ready) */
typedef struct TnStatusInfoV2 {
    uint16_t struct_size; /* sizeof(TnStatusInfoV2) */
    uint8_t  family;      /* AF_INET=2, AF_INET6=10 */
    uint8_t  pad;
    uint8_t  ip_addr[16];
    uint8_t  netmask[16];
    uint8_t  gw[16];
    uint8_t  dns1[16];
    uint8_t  dns2[16];
    uint32_t active_sockets;
    uint32_t flags;
} TnStatusInfoV2;

/* TNET-108: RECONFIG reply. The daemon compares the newly loaded TnPrefs
 * against the previous one and classifies every changed key:
 *  - needs_restart: interface-level keys the daemon cannot re-apply live
 *    (stop/start the stack to activate them)
 *  - applied:       keys whose live re-apply succeeded
 *  - failed:        keys the daemon tried to apply live but could not
 *    (unopenable LOG= path, SYSLOG= host unresolvable, SELECTORS= alloc ...)
 * Legacy callers read the same masks from args[0..2]. */
#define TN_RECFG_VERSION 1

#define TN_RECFG_DEVICE         0x0001u
#define TN_RECFG_UNIT           0x0002u
#define TN_RECFG_DHCP           0x0004u
#define TN_RECFG_IP             0x0008u
#define TN_RECFG_NETMASK        0x0010u
#define TN_RECFG_GATEWAY        0x0020u
#define TN_RECFG_DNS            0x0040u
#define TN_RECFG_DNS2           0x0080u
#define TN_RECFG_HOSTNAME       0x0100u
#define TN_RECFG_MTU            0x0200u
#define TN_RECFG_LOGLEVEL       0x0400u
#define TN_RECFG_PRIORITY       0x0800u
#define TN_RECFG_LOG            0x1000u
#define TN_RECFG_DATABASE_ORDER 0x2000u
#define TN_RECFG_SELECTORS      0x4000u
#define TN_RECFG_STATS          0x8000u
#define TN_RECFG_SYSLOG         0x10000u
#define TN_RECFG_S2EVENTS       0x20000u  /* TNET-109: restart-only */
#define TN_RECFG_ALL            0x3FFFFu

typedef struct TnReconfigResponse {
    uint16_t struct_size;   /* sizeof(TnReconfigResponse) */
    uint16_t version;       /* TN_RECFG_VERSION */
    uint32_t applied;
    uint32_t needs_restart;
    uint32_t failed;
} TnReconfigResponse;

/* IPC Message passed via Exec PutMsg/GetMsg/ReplyMsg */
typedef struct TnIpcMsg {
    struct Message msg;         /* Standard Exec Message node */
    TnIpcCmd       cmd;         /* Command code */
    struct Task   *client_task; /* Calling client task */
    APTR           socket_base; /* Calling SocketBase */
    LONG           args[6];     /* Generic integer/register arguments */
    APTR           ptrs[4];     /* Generic pointer arguments */
    LONG           result;      /* Return code from operation */
    LONG           err_no;      /* Posix errno code on error */
} TnIpcMsg;

/*
 * Per-task SocketBase extending AmigaOS struct Library.
 * Allocated on OpenLibrary("bsdsocket.library", ...), freed on CloseLibrary().
 */
typedef struct TnSocketBase {
    struct Library  lib_node;               /* Standard AmigaOS Library header */
    UWORD           pad;
    struct Task    *owner_task;             /* Task owning this base instance */
    struct MsgPort *reply_port;             /* Dedicated private reply port */
    struct MsgPort *tolunnet_port;           /* Reference to tolunnet.port */
    struct MsgPort *timer_port;             /* Per-task timer port for WaitSelect */
    APTR            timer_io;               /* Per-task struct timerequest * for WaitSelect */
    TnIpcMsg        ipc_msg;                /* Embedded zero-allocation IPC message */
    LONG           *errno_ptr;              /* Pointer to client task's errno variable */
    LONG            task_errno;             /* Fallback task errno if no ptr set */
    UBYTE           errno_width;            /* 1 = byte, 2 = word, 4 = long */
    LONG           *herrno_ptr;             /* Pointer to client task's h_errno */
    LONG            task_herrno;            /* Fallback task h_errno */
    ULONG           sig_io;                 /* SIGIO signal bit mask */
    ULONG           sig_urg;                /* SIGURG signal bit mask */
    ULONG           sig_int;                /* SIGINT signal bit mask */
    ULONG           sig_event;              /* SBTC_SIGEVENTMASK signal bit mask */
    ULONG           sig_select;             /* Private signal bit mask for WaitSelect (§D) */
    BYTE            sig_select_bit;         /* Private signal bit index (-1 if none allocated) */
    LONG            dtablesize;             /* Current descriptor table size (TN-bugtrack-2 item 4) */
    ULONG          *events;                 /* Per-fd pending events mask (C4) — dtablesize entries */
    ULONG          *event_masks;            /* TNET-122..127: per-fd SO_EVENTMASK filter (0 = all) */
    LONG           *fd_map;                 /* Client fd -> Network task slot — dtablesize entries */
    APTR            fd_callback;            /* SBTC_FDCALLBACK hook function (C5) */
    LONG            log_stat;               /* SBTC_LOGSTAT (C5) */
    APTR            log_tag_ptr;            /* SBTC_LOGTAGPTR (C5) */
    LONG            log_facility;           /* SBTC_LOGFACILITY (C5) */
    LONG            log_mask;               /* SBTC_LOGMASK (C5) */
    LONG            udp_checksum;           /* SBTC_UDP_CHECKSUM (C5) */
    LONG            ip_default_ttl;         /* SBTC_IP_DEFAULT_TTL (C5) */
    /* TNET-111: IPC reply watchdog. 0 = block forever (default, exact
     * historical behaviour); >0 = a daemon that has not replied within that
     * many milliseconds fails the call with ETIMEDOUT instead of hanging the
     * client process. ipc_timeouts counts the expired replies (drained on
     * the next call). Used by the conformance suite's per-test watchdog. */
    uint32_t        ipc_timeout_ms;
    uint32_t        ipc_timeouts;
    APTR            ipc_orphan;             /* timed-out call awaiting its late reply (drained next call) */
    char            inet_ntoa_buf[16];      /* Per-task static buffer for Inet_NtoA */
    char            hostname[32];           /* Per-task hostname */

    /* Per-task storage for gethostbyname (M4) */
    struct hostent  hostent_data;
    STRPTR          hostent_aliases[2];
    STRPTR          hostent_addrs[2];
    ULONG           hostent_addr;
    char            hostent_name[64];

    /* Per-task storage for getservbyname / getservbyport (COMPAT-2) */
    struct servent  servent_data;
    STRPTR          servent_aliases[2];
    char            servent_name[32];
    char            servent_proto[16];

    /* Per-task storage for getprotobyname / getprotobynumber (COMPAT-2) */
    struct protoent protoent_data;
    STRPTR          protoent_aliases[2];
    char            protoent_name[32];

    /* Per-task storage for domain name and getnetent/getservent/getprotoent iterators (§D.5) */
    char            domain_name[64];
    struct netent   netent_data;
    STRPTR          netent_aliases[2];
    char            netent_name[32];
    int             netent_idx;
    int             servent_idx;
    int             protoent_idx;
} TnSocketBase;

#endif /* TOLUNNET_IPC_H */
