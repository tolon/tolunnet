/*
 * tolunnet — IFCTL IPC handler (ipc_ifctl.h, CLOSE §B.7).
 */
#ifndef TOLUNNET_IPC_IFCTL_H
#define TOLUNNET_IPC_IFCTL_H

#include "task_ctx.h"

int tn_ipc_cmd_ifctl(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

#endif /* TOLUNNET_IPC_IFCTL_H */
