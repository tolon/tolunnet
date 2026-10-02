/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet ? Datagram & Raw Socket Handlers (ipc_dgram.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_DGRAM_H
#define TOLUNNET_IPC_DGRAM_H

#include "task_ctx.h"

void tn_udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
u8_t tn_raw_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);

int tn_ipc_cmd_bind(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_sendto(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_recvfrom(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
/* 4.5: BSD-truncating sockaddr_in store into client addr/addrlen (memcpy only) */
void tn_store_client_sockaddr(void *addr, socklen_t *addrlen, u16_t port_host, u32_t addr_net);

#endif /* TOLUNNET_IPC_DGRAM_H */