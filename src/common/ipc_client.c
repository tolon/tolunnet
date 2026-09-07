/*
 * tolunnet — IPC Client Helper (TNET-082)
 *
 * Implements synchronous one-shot IPC communication using MEMF_PUBLIC messages
 * to guarantee compliance with MMU/MuForce constraints and avoid stack allocation.
 */

#include "ipc_client.h"

#if defined(__AMIGA__) || defined(__amigaos__) || defined(TN_AMIGA_BUILD)

#include <proto/exec.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/memory.h>

int tn_ipc_oneshot_ex(TnIpcCmd cmd, const LONG *args, int arg_count, const APTR *ptrs, int ptr_count, TnIpcMsg *out_msg)
{
    struct MsgPort *daemon_port;
    struct MsgPort *reply_port;
    TnIpcMsg *msg;
    int rc = -1;
    int i;

    daemon_port = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
    if (daemon_port == NULL) {
        return -1;
    }

    reply_port = CreateMsgPort();
    if (reply_port == NULL) {
        return -1;
    }

    msg = (TnIpcMsg *)AllocMem(sizeof(TnIpcMsg), MEMF_PUBLIC | MEMF_CLEAR);
    if (msg == NULL) {
        DeleteMsgPort(reply_port);
        return -1;
    }

    msg->msg.mn_Node.ln_Type = NT_MESSAGE;
    msg->msg.mn_ReplyPort    = reply_port;
    msg->msg.mn_Length       = sizeof(TnIpcMsg);
    msg->cmd                 = cmd;
    msg->client_task         = FindTask(NULL);
    msg->socket_base         = NULL;

    if (args != NULL && arg_count > 0) {
        if (arg_count > 6) arg_count = 6;
        for (i = 0; i < arg_count; i++) {
            msg->args[i] = args[i];
        }
    }

    if (ptrs != NULL && ptr_count > 0) {
        if (ptr_count > 4) ptr_count = 4;
        for (i = 0; i < ptr_count; i++) {
            msg->ptrs[i] = ptrs[i];
        }
    }

    PutMsg(daemon_port, (struct Message *)msg);
    WaitPort(reply_port);
    GetMsg(reply_port);

    rc = (int)msg->result;
    if (out_msg != NULL) {
        *out_msg = *msg;
    }

    FreeMem(msg, sizeof(TnIpcMsg));
    DeleteMsgPort(reply_port);
    return rc;
}

int tn_ipc_oneshot(TnIpcCmd cmd, const LONG *args, int arg_count, TnIpcMsg *out_msg)
{
    return tn_ipc_oneshot_ex(cmd, args, arg_count, NULL, 0, out_msg);
}

#else

/* Host test mock support */
static int (*s_mock_ipc_handler)(TnIpcCmd cmd, const LONG *args, int arg_count, TnIpcMsg *out_msg) = NULL;

void tn_ipc_set_mock_handler(int (*handler)(TnIpcCmd, const LONG *, int, TnIpcMsg *))
{
    s_mock_ipc_handler = handler;
}

int tn_ipc_oneshot_ex(TnIpcCmd cmd, const LONG *args, int arg_count, const APTR *ptrs, int ptr_count, TnIpcMsg *out_msg)
{
    (void)ptrs;
    (void)ptr_count;
    if (s_mock_ipc_handler != NULL) {
        return s_mock_ipc_handler(cmd, args, arg_count, out_msg);
    }
    return -1;
}

int tn_ipc_oneshot(TnIpcCmd cmd, const LONG *args, int arg_count, TnIpcMsg *out_msg)
{
    return tn_ipc_oneshot_ex(cmd, args, arg_count, NULL, 0, out_msg);
}

#endif
