/*
 * tolunnet ? NetDB IPC Handlers (ipc_netdb.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_NETDB_H
#define TOLUNNET_IPC_NETDB_H

#include "task_ctx.h"

int tn_ipc_cmd_gethostbyname(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

#endif /* TOLUNNET_IPC_NETDB_H */