/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — task-owned copy ring interface (M1, UNPROVEN).
 *
 * See buffers.c for the rationale (master prompt §5: large payloads are copied
 * into task-owned memory so the task holds no pointers into library memory
 * after ReplyMsg). Small payloads travel inline in the TnRequest.
 */
#ifndef TOLUNNET_BUFFERS_H
#define TOLUNNET_BUFFERS_H

#include <exec/types.h>

/* Opaque handle to a task-owned ring slot. */
typedef struct TnRingSlot *TnRingHandle;

/* One-time init/fini of the global ring. Returns FALSE on allocation failure. */
BOOL tn_ring_init(void);
void tn_ring_fini(void);

/* TRUE if a payload of this length must go via the ring (>4 KB, §5). */
BOOL tn_ring_needed(ULONG len);

/* Copy src into a free slot and return a handle, or NULL if the payload does
 * not need the ring or no slot is free. The task owns the copy until release. */
TnRingHandle tn_ring_acquire(const void *src, ULONG len);

/* Accessors. */
const void *tn_ring_data(TnRingHandle h);
ULONG      tn_ring_length(TnRingHandle h);

/* Mark a slot free. */
void tn_ring_release(TnRingHandle h);

#endif /* TOLUNNET_BUFFERS_H */
