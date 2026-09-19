/*
 * tolunnet — ROUTECTL IPC handler (ipc_route.h, CLOSE §B.5).
 */
#ifndef TOLUNNET_IPC_ROUTE_H
#define TOLUNNET_IPC_ROUTE_H

#include "task_ctx.h"

int tn_ipc_cmd_routectl(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

#endif /* TOLUNNET_IPC_ROUTE_H */
