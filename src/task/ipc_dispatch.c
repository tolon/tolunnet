/*
 * tolunnet — Table-Driven IPC Dispatcher Implementation (ipc_dispatch.c).
 *
 * ROUND4b §B (Modular Daemon Refactor).
 *
 * Architectural Invariants (§B.2):
 * 1. A slot is touched only from the daemon task. Client pointers in imsg->ptrs[]
 *    are read/written only before ReplyMsg; deferred handlers keep the TnIpcMsg *
 *    in the slot (pending_*_msg) and reply from a callback.
 * 2. ref_count semantics: fd_map entry = +1, parked socket = +1, accept-queue entry = +1;
 *    free only at 0.
 * 3. Every lwIP callback that touches a slot checks slot->in_use && slot->tcp_pcb == pcb
 *    first (pcb reuse after abort).
 * 4. Reply always sets both result and err_no; err_no values only from
 *    include/netinclude/sys/errno.h.
 */
#include "ipc_dispatch.h"
#include "ipc_socket.h"
#include "ipc_tcp.h"
#include "ipc_dgram.h"
#include "ipc_msg.h"
#include "ipc_select.h"
#include "ipc_netdb.h"
#include "ipc_status.h"
#include "slot_table.h"

static const char * const g_ipc_cmd_names[] = {
    [TN_IPC_CMD_OPEN]          = "OPEN",
    [TN_IPC_CMD_CLOSE]         = "CLOSE",
    [TN_IPC_CMD_SOCKET]        = "SOCKET",
    [TN_IPC_CMD_BIND]          = "BIND",
    [TN_IPC_CMD_LISTEN]        = "LISTEN",
    [TN_IPC_CMD_ACCEPT]        = "ACCEPT",
    [TN_IPC_CMD_CONNECT]       = "CONNECT",
    [TN_IPC_CMD_SENDTO]        = "SENDTO",
    [TN_IPC_CMD_SEND]          = "SEND",
    [TN_IPC_CMD_RECVFROM]      = "RECVFROM",
    [TN_IPC_CMD_RECV]          = "RECV",
    [TN_IPC_CMD_SHUTDOWN]      = "SHUTDOWN",
    [TN_IPC_CMD_SETSOCKOPT]    = "SETSOCKOPT",
    [TN_IPC_CMD_GETSOCKOPT]    = "GETSOCKOPT",
    [TN_IPC_CMD_GETSOCKNAME]   = "GETSOCKNAME",
    [TN_IPC_CMD_GETPEERNAME]   = "GETPEERNAME",
    [TN_IPC_CMD_IOCTL]         = "IOCTL",
    [TN_IPC_CMD_CLOSESOCKET]   = "CLOSESOCKET",
    [TN_IPC_CMD_GETHOSTBYNAME] = "GETHOSTBYNAME",
    [TN_IPC_CMD_GETHOSTBYADDR] = "GETHOSTBYADDR",
    [TN_IPC_CMD_WAITSELECT]    = "WAITSELECT",
    [TN_IPC_CMD_DUP2]          = "DUP2",
    [TN_IPC_CMD_GETSTATUS]     = "GETSTATUS",
    [TN_IPC_CMD_RECONFIG]      = "RECONFIG",
    [TN_IPC_CMD_ENUMSOCKETS]   = "ENUMSOCKETS",
    [TN_IPC_CMD_SENDMSG]       = "SENDMSG",
    [TN_IPC_CMD_RECVMSG]       = "RECVMSG",
    [TN_IPC_CMD_RELEASESOCKET] = "RELEASESOCKET",
    [TN_IPC_CMD_OBTAINSOCKET]  = "OBTAINSOCKET",
    [TN_IPC_CMD_SELECT_ARM]    = "SELECT_ARM",
    [TN_IPC_CMD_SELECT_DISARM] = "SELECT_DISARM"
};

const char *tn_ipc_cmd_name(TnIpcCmd cmd)
{
    if ((size_t)cmd < sizeof(g_ipc_cmd_names) / sizeof(g_ipc_cmd_names[0]) && g_ipc_cmd_names[cmd] != NULL) {
        return g_ipc_cmd_names[cmd];
    }
    return "UNKNOWN";
}

static const TnIpcHandler g_ipc_table[] = {
    [TN_IPC_CMD_OPEN]          = { TN_IPC_CMD_OPEN,          tn_ipc_cmd_open,          TRUE,  FALSE, 0 },
    [TN_IPC_CMD_CLOSE]         = { TN_IPC_CMD_CLOSE,         tn_ipc_cmd_close,         TRUE,  FALSE, 0 },
    [TN_IPC_CMD_SOCKET]        = { TN_IPC_CMD_SOCKET,        tn_ipc_cmd_socket,        TRUE,  FALSE, 0 },
    [TN_IPC_CMD_BIND]          = { TN_IPC_CMD_BIND,          tn_ipc_cmd_bind,          TRUE,  TRUE,  0 },
    [TN_IPC_CMD_LISTEN]        = { TN_IPC_CMD_LISTEN,        tn_ipc_cmd_listen,        TRUE,  TRUE,  0 },
    [TN_IPC_CMD_ACCEPT]        = { TN_IPC_CMD_ACCEPT,        tn_ipc_cmd_accept,        TRUE,  TRUE,  0 },
    [TN_IPC_CMD_CONNECT]       = { TN_IPC_CMD_CONNECT,       tn_ipc_cmd_connect,       TRUE,  TRUE,  0 },
    [TN_IPC_CMD_SENDTO]        = { TN_IPC_CMD_SENDTO,        tn_ipc_cmd_sendto,        TRUE,  TRUE,  0 },
    [TN_IPC_CMD_SEND]          = { TN_IPC_CMD_SEND,          tn_ipc_cmd_send,          TRUE,  TRUE,  0 },
    [TN_IPC_CMD_RECVFROM]      = { TN_IPC_CMD_RECVFROM,      tn_ipc_cmd_recvfrom,      TRUE,  TRUE,  0 },
    [TN_IPC_CMD_RECV]          = { TN_IPC_CMD_RECV,          tn_ipc_cmd_recv,          TRUE,  TRUE,  0 },
    [TN_IPC_CMD_SHUTDOWN]      = { TN_IPC_CMD_SHUTDOWN,      tn_ipc_cmd_shutdown,      TRUE,  TRUE,  0 },
    [TN_IPC_CMD_SETSOCKOPT]    = { TN_IPC_CMD_SETSOCKOPT,    tn_ipc_cmd_setsockopt,    TRUE,  TRUE,  0 },
    [TN_IPC_CMD_GETSOCKOPT]    = { TN_IPC_CMD_GETSOCKOPT,    tn_ipc_cmd_getsockopt,    TRUE,  TRUE,  0 },
    [TN_IPC_CMD_GETSOCKNAME]   = { TN_IPC_CMD_GETSOCKNAME,   tn_ipc_cmd_getsockname,   TRUE,  TRUE,  0 },
    [TN_IPC_CMD_GETPEERNAME]   = { TN_IPC_CMD_GETPEERNAME,   tn_ipc_cmd_getpeername,   TRUE,  TRUE,  0 },
    [TN_IPC_CMD_IOCTL]         = { TN_IPC_CMD_IOCTL,         tn_ipc_cmd_ioctl,         TRUE,  TRUE,  0 },
    [TN_IPC_CMD_CLOSESOCKET]   = { TN_IPC_CMD_CLOSESOCKET,   tn_ipc_cmd_closesocket,   TRUE,  TRUE,  0 },
    [TN_IPC_CMD_GETHOSTBYNAME] = { TN_IPC_CMD_GETHOSTBYNAME, tn_ipc_cmd_gethostbyname, FALSE, FALSE, 0 },
    [TN_IPC_CMD_GETHOSTBYADDR] = { TN_IPC_CMD_GETHOSTBYADDR, NULL,                    FALSE, FALSE, 0 },
    [TN_IPC_CMD_WAITSELECT]    = { TN_IPC_CMD_WAITSELECT,    tn_ipc_cmd_waitselect,    TRUE,  FALSE, 0 },
    [TN_IPC_CMD_DUP2]          = { TN_IPC_CMD_DUP2,          tn_ipc_cmd_dup2,          TRUE,  TRUE,  0 },
    [TN_IPC_CMD_GETSTATUS]     = { TN_IPC_CMD_GETSTATUS,     tn_ipc_cmd_getstatus,     FALSE, FALSE, 0 },
    [TN_IPC_CMD_RECONFIG]      = { TN_IPC_CMD_RECONFIG,      tn_ipc_cmd_reconfig,      FALSE, FALSE, 0 },
    [TN_IPC_CMD_ENUMSOCKETS]   = { TN_IPC_CMD_ENUMSOCKETS,   tn_ipc_cmd_enumsockets,   FALSE, FALSE, 0 },
    [TN_IPC_CMD_SENDMSG]       = { TN_IPC_CMD_SENDMSG,       tn_ipc_cmd_sendmsg,       TRUE,  TRUE,  0 },
    [TN_IPC_CMD_RECVMSG]       = { TN_IPC_CMD_RECVMSG,       tn_ipc_cmd_recvmsg,       TRUE,  TRUE,  0 },
    [TN_IPC_CMD_RELEASESOCKET] = { TN_IPC_CMD_RELEASESOCKET, tn_ipc_cmd_releasesocket, TRUE,  TRUE,  0 },
    [TN_IPC_CMD_OBTAINSOCKET]  = { TN_IPC_CMD_OBTAINSOCKET,  tn_ipc_cmd_obtainsocket,  TRUE,  FALSE, 0 },
    [TN_IPC_CMD_SELECT_ARM]    = { TN_IPC_CMD_SELECT_ARM,    tn_ipc_cmd_select_arm,    TRUE,  FALSE, 0 },
    [TN_IPC_CMD_SELECT_DISARM] = { TN_IPC_CMD_SELECT_DISARM, tn_ipc_cmd_select_disarm, TRUE,  FALSE, 0 }
};

BOOL tn_handle_ipc(TnDaemon *d, TnIpcMsg *imsg)
{
    const TnIpcHandler *h;
    TnSocketSlot *slot = NULL;
    int action;

    if (imsg == NULL) return FALSE;

    imsg->result = 0;
    imsg->err_no = 0;

    if (imsg->cmd < 0 || (size_t)imsg->cmd >= sizeof(g_ipc_table) / sizeof(g_ipc_table[0])) {
        imsg->result = -1;
        imsg->err_no = ENOSYS;
        return TRUE;
    }

    h = &g_ipc_table[imsg->cmd];
    if (h->handler == NULL) {
        imsg->result = -1;
        imsg->err_no = ENOSYS;
        return TRUE;
    }

    if (h->needs_base) {
        if (imsg->socket_base == NULL) {
            imsg->result = -1;
            imsg->err_no = EINVAL;
            return TRUE;
        }
    }

    if (h->needs_fd) {
        int fd = (int)imsg->args[0];
        slot = tn_slot_lookup(d, (const TnSocketBase *)imsg->socket_base, fd, NULL);
        if (slot == NULL) {
            imsg->result = -1;
            imsg->err_no = EBADF;
            return TRUE;
        }
    }

    action = h->handler(d, imsg, slot);
    return (action == TN_IPC_REPLY_NOW);
}
