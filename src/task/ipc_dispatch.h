/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Table-Driven IPC Dispatcher (ipc_dispatch.h).
 *
 * ROUND4b §B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_DISPATCH_H
#define TOLUNNET_IPC_DISPATCH_H

#include "task_ctx.h"

typedef enum TnIpcReplyAction {
    TN_IPC_REPLY_NOW = 0,
    TN_IPC_DEFER     = 1
} TnIpcReplyAction;

typedef int (*TnIpcCmdFunc)(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

typedef struct TnIpcHandler {
    TnIpcCmd        cmd;
    TnIpcCmdFunc    handler;
    BOOL            needs_base;
    BOOL            needs_fd;
    ULONG           flags;
} TnIpcHandler;

const char *tn_ipc_cmd_name(TnIpcCmd cmd);
BOOL tn_handle_ipc(TnDaemon *d, TnIpcMsg *imsg);

#endif /* TOLUNNET_IPC_DISPATCH_H */
