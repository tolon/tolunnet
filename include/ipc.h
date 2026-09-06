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

#define TN_MAX_FDS_PER_TASK 32

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
    TN_IPC_CMD_ENUMSOCKETS      /* Enumerate active sockets (TNET-071; netstat) */
} TnIpcCmd;

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
    LONG            fd_map[TN_MAX_FDS_PER_TASK]; /* Client fd -> Network task slot */
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
