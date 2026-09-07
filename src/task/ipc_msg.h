/*
 * tolunnet ? Scatter-Gather Message Handlers (ipc_msg.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_MSG_H
#define TOLUNNET_IPC_MSG_H

#include "task_ctx.h"

int tn_ipc_cmd_sendmsg(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_recvmsg(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

#endif /* TOLUNNET_IPC_MSG_H */