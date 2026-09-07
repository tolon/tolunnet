/*
 * tolunnet ? Select & Async Signaling (ipc_select.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_SELECT_H
#define TOLUNNET_IPC_SELECT_H

#include "task_ctx.h"

void tn_signal_socket(TnSocketSlot *slot);
BOOL tn_select_can_read(const TnSocketSlot *slot);
BOOL tn_select_can_write(const TnSocketSlot *slot);
BOOL tn_select_has_except(const TnSocketSlot *slot);

int tn_ipc_cmd_waitselect(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

#endif /* TOLUNNET_IPC_SELECT_H */