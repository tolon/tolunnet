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

int tn_rx_queue_push(TnSocketSlot *slot, struct pbuf *p, const ip_addr_t *src_ip, u16_t src_port);
TnRxPacket *tn_rx_queue_pop(TnSocketSlot *slot);
void tn_rx_queue_drain(TnSocketSlot *slot);

int tn_accept_queue_push(TnSocketSlot *slot, struct tcp_pcb *new_pcb);
struct tcp_pcb *tn_accept_queue_pop(TnSocketSlot *slot);
void tn_accept_queue_drain(TnSocketSlot *slot);

void tn_record_socket_event(TnDaemon *d, TnSocketSlot *slot, ULONG event_mask);
int tn_slot_live_count(const TnDaemon *d);

#endif /* TOLUNNET_SLOT_TABLE_H */
