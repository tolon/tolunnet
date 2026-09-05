/*
 * tolunnet — IPC Client Helper (TNET-082)
 *
 * Provides safe one-shot IPC communication using MEMF_PUBLIC messages
 * to satisfy MMU/MuForce constraints and prevent stack corruption.
 */

#ifndef TOLUNNET_IPC_CLIENT_H
#define TOLUNNET_IPC_CLIENT_H

#include "../../include/ipc.h"

/*
 * Synchronous one-shot IPC request to the tolunnet daemon.
 * Allocates TnIpcMsg from MEMF_PUBLIC, creates reply port, sends to TOLUNNET_PORT_NAME,
 * waits for reply, copies result/args to out_msg (if out_msg != NULL),
 * and frees all allocated resources.
 *
 * Returns 0 on success, or negative errno / -1 on failure.
 */
int tn_ipc_oneshot(TnIpcCmd cmd, const LONG *args, int arg_count, TnIpcMsg *out_msg);

#endif /* TOLUNNET_IPC_CLIENT_H */
