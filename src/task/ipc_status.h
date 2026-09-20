/*
 * tolunnet ? Operational & Telemetry IPC Handlers (ipc_status.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_STATUS_H
#define TOLUNNET_IPC_STATUS_H

#include "task_ctx.h"

int tn_ipc_cmd_getstatus(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_reconfig(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_enumsockets(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_getstats(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_stop(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot); /* TNET-152 */

#endif /* TOLUNNET_IPC_STATUS_H */