/*
 * tolunnet ? TCP Stream IPC Handlers & lwIP Callbacks (ipc_tcp.h).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#ifndef TOLUNNET_IPC_TCP_H
#define TOLUNNET_IPC_TCP_H

#include "task_ctx.h"

err_t tn_tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len);
err_t tn_tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
err_t tn_tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err);
void tn_tcp_err_cb(void *arg, err_t err);
err_t tn_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err);

int tn_ipc_cmd_listen(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_accept(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_connect(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_send(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_recv(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);
int tn_ipc_cmd_shutdown(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot);

#endif /* TOLUNNET_IPC_TCP_H */