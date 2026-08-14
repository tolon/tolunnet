/*
 * tolunnet — Public IPC Definitions and per-opener SocketBase.
 *
 * Master prompt §5: Single Exec task owns lwIP; clients communicate via
 * synchronous zero-allocation IPC messages.
 */

#ifndef TOLUNNET_IPC_H
#define TOLUNNET_IPC_H

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/libraries.h>
#include <netdb.h>

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
    TN_IPC_CMD_GETSTATUS        /* Query live daemon interface status and socket count */
} TnIpcCmd;

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
    struct MsgPort *tolunet_port;           /* Reference to tolunet.port */
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
} TnSocketBase;

#endif /* TOLUNNET_IPC_H */
