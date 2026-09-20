/*
 * tolunnet ? NetDB IPC Handlers (ipc_netdb.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_NETDB_H
#define TOLUNNET_IPC_NETDB_H

#include "task_ctx.h"

int tn_ipc_cmd_gethostbyname(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

/* TNET-150: deferred-DNS lifecycle (host-testable). */
void tn_dns_cancel_for_base(TnDaemon *d, TnSocketBase *base);
void tn_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg);

#endif /* TOLUNNET_IPC_NETDB_H */