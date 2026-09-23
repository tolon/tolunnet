/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Network Interface Manager.
 *
 * ROUND4b §B & §L (Multi-netif and IPv6-ready architecture).
 */
#ifndef TOLUNNET_NETIF_MGR_H
#define TOLUNNET_NETIF_MGR_H

#include "task_ctx.h"

void tn_apply_live_config(TnDaemon *d);
void tn_drain_loopback(void);
void ip_to_str(char *buf, const ip4_addr_t *addr);

#endif /* TOLUNNET_NETIF_MGR_H */
