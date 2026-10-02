/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Socket Slot Table & Queue Management.
 *
 * ROUND4b §B (Modular Daemon Refactor).
 * Provides slot allocation, lookup, reference counting, and queue operations.
 */
#ifndef TOLUNNET_SLOT_TABLE_H
#define TOLUNNET_SLOT_TABLE_H

#include "task_ctx.h"

void tn_slot_table_init(TnDaemon *d);
/* TNET-108: grow the heap selector table to new_max entries (never shrinks;
 * existing entries are preserved, new ones zeroed). Returns TRUE on success
 * (including "already big enough"), FALSE on allocation failure or bad args. */
BOOL tn_selector_table_grow(TnDaemon *d, uint32_t new_max);
void tn_selector_table_free(TnDaemon *d);
TnSocketSlot *tn_slot_alloc(TnDaemon *d, TnSocketBase *base, struct Task *task,
                           int domain, int type, int protocol, int *out_slot_idx);
void tn_slot_free(TnDaemon *d, int slot_idx);
TnSocketSlot *tn_slot_lookup(TnDaemon *d, const TnSocketBase *base, int fd, int *out_slot_idx);
int tn_fd_alloc(TnSocketBase *base, int slot_idx);
void tn_slot_ref(TnSocketSlot *slot);
void tn_slot_unref(TnDaemon *d, int slot_idx);

/* TNET-107: TnRxPacket freelist — avoids per-packet AllocVec/FreeVec churn. */
TnRxPacket *tn_rxpkt_get(void);
void tn_rxpkt_put(TnRxPacket *pkt);
void tn_rxpkt_fini(void);

int tn_rx_queue_push(TnSocketSlot *slot, struct pbuf *p, const ip_addr_t *src_ip, u16_t src_port);
int tn_rx_queue_push_nocap(TnSocketSlot *slot, struct pbuf *p, const ip_addr_t *src_ip, u16_t src_port); /* TNET-156 */
TnRxPacket *tn_rx_queue_pop(TnSocketSlot *slot);
void tn_rx_queue_drain(TnSocketSlot *slot);
void tn_rx_queue_drain_with_recved(TnSocketSlot *slot); /* z.ai step 4 item 2 */

int tn_accept_queue_push(TnSocketSlot *slot, struct tcp_pcb *new_pcb);
struct tcp_pcb *tn_accept_queue_pop(TnSocketSlot *slot);
TnAcceptEntry *tn_accept_queue_pop_entry(TnSocketSlot *slot);
void tn_accept_queue_remove(TnSocketSlot *slot, TnAcceptEntry *ent);
void tn_accept_entry_free(TnAcceptEntry *ent, BOOL abort_pcb);
void tn_accept_queue_drain(TnSocketSlot *slot);

void tn_record_socket_event(TnDaemon *d, TnSocketSlot *slot, ULONG event_mask);
int tn_slot_live_count(const TnDaemon *d);

int tn_slot_park_recv(TnDaemon *d, TnSocketSlot *slot, TnIpcMsg *imsg);
void tn_slot_check_recv_timeouts(TnDaemon *d); /* also SO_SNDTIMEO of parked sends */
void tn_slot_reply_send(TnSocketSlot *slot, LONG err_no, int reply); /* 4.2 */
BOOL tn_slot_cancel_parked_send(TnDaemon *d, TnIpcMsg *target);     /* 4.2: CANCEL hook */
BOOL tn_slot_owner_alive(const TnDaemon *d, const TnSocketBase *base); /* 3.7 */
err_t tn_tcp_queued_recv_cb_test(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err); /* z.ai step 7 item 3 host entry */
void tn_recv_cancel_for_base(TnDaemon *d, TnSocketBase *base);
void tn_recv_cancel_for_base2(TnDaemon *d, TnSocketBase *base, int reply); /* z.ai step 5 item 2 */
void tn_slot_clear_owner_base(TnDaemon *d, const TnSocketBase *base);

#endif /* TOLUNNET_SLOT_TABLE_H */
