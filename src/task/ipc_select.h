/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet ? Select & Async Signaling (ipc_select.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_SELECT_H
#define TOLUNNET_IPC_SELECT_H

#include "task_ctx.h"

void tn_signal_socket(TnDaemon *d, TnSocketSlot *slot);
BOOL tn_select_can_read(const TnSocketSlot *slot);
BOOL tn_select_can_write(const TnSocketSlot *slot);
BOOL tn_select_has_except(const TnSocketSlot *slot);

int tn_ipc_cmd_waitselect(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_select_arm(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_select_disarm(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
void tn_selector_disarm_all_for_base(TnDaemon *d, const TnSocketBase *base);

#endif /* TOLUNNET_IPC_SELECT_H */