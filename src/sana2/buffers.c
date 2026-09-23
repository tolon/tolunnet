/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — task-owned copy ring (M1, UNPROVEN skeleton).
 *
 * Master prompt §5: "buffers >4 KB via task-owned copy ring (buffers.c)" and
 * "task keeps no pointers into library memory after ReplyMsg". The rule: the
 * network task never holds a pointer to caller (library) memory once it has
 * replied. For small payloads the TnRequest itself carries the data; for large
 * ones (>4 KB — e.g. big recv/send) the task copies into its own ring buffer
 * and hands the library a task-owned handle.
 *
 * M1 scope: define the interface and a minimal single-slot implementation
 * sufficient for the raw frame logger. The real ring (multiple slots, lifo/fifo
 * selection by measurement, §5 "measure before optimising") arrives with M3+
 * when real sockets move bulk data.
 *
 * UNPROVEN: not yet called by any built target. The 4 KB threshold and the
 * slot count are start values; tune only with measurements pasted in STATUS.md.
 */

#include "buffers.h"

#include <proto/exec.h>
#include <exec/memory.h>

#define TN_RING_THRESHOLD  4096UL   /* §5: ">4 KB" */
#define TN_RING_SLOTS      8        /* start value; measure before changing */
#define TN_RING_SLOT_BYTES (16 * 1024)

typedef struct TnRingSlot {
    UBYTE *base;
    ULONG  cap;
    ULONG  len;       /* used bytes */
    BOOL   in_use;
} TnRingSlot;

struct TnRing {
    TnRingSlot slot[TN_RING_SLOTS];
};

/* One global ring for M1. M3+ may make this per-task if contention appears. */
static struct TnRing g_ring;

BOOL tn_ring_init(void)
{
    ULONG i;
    for (i = 0; i < TN_RING_SLOTS; i++) {
        g_ring.slot[i].base = (UBYTE *)AllocVec(TN_RING_SLOT_BYTES,
                                                MEMF_CLEAR | MEMF_ANY);
        if (g_ring.slot[i].base == NULL) {
            /* Roll back what we got. */
            ULONG j;
            for (j = 0; j < i; j++) {
                FreeVec(g_ring.slot[j].base);
                g_ring.slot[j].base = NULL;
            }
            return FALSE;
        }
        g_ring.slot[i].cap = TN_RING_SLOT_BYTES;
        g_ring.slot[i].len = 0;
        g_ring.slot[i].in_use = FALSE;
    }
    return TRUE;
}

void tn_ring_fini(void)
{
    ULONG i;
    for (i = 0; i < TN_RING_SLOTS; i++) {
        FreeVec(g_ring.slot[i].base);
        g_ring.slot[i].base = NULL;
        g_ring.slot[i].cap = 0;
        g_ring.slot[i].len = 0;
        g_ring.slot[i].in_use = FALSE;
    }
}

/* Decide whether a payload needs the ring (> threshold) or fits inline. */
BOOL tn_ring_needed(ULONG len)
{
    return len > TN_RING_THRESHOLD;
}

TnRingHandle tn_ring_acquire(const void *src, ULONG len)
{
    ULONG i;
    if (!tn_ring_needed(len)) return NULL;     /* caller keeps it inline */
    if (len > TN_RING_SLOT_BYTES) return NULL; /* too big for one slot; M3
                                                  splits or grows slots */

    for (i = 0; i < TN_RING_SLOTS; i++) {
        if (!g_ring.slot[i].in_use) {
            UBYTE *d = g_ring.slot[i].base;
            const UBYTE *s = (const UBYTE *)src;
            ULONG k;
            for (k = 0; k < len; k++) d[k] = s[k];
            g_ring.slot[i].len = len;
            g_ring.slot[i].in_use = TRUE;
            return (TnRingHandle)&g_ring.slot[i];
        }
    }
    return NULL;   /* ring full; M3 will wait/park the request */
}

const void *tn_ring_data(TnRingHandle h)
{
    if (h == NULL) return NULL;
    return ((TnRingSlot *)h)->base;
}

ULONG tn_ring_length(TnRingHandle h)
{
    if (h == NULL) return 0;
    return ((TnRingSlot *)h)->len;
}

void tn_ring_release(TnRingHandle h)
{
    if (h == NULL) return;
    ((TnRingSlot *)h)->in_use = FALSE;
    ((TnRingSlot *)h)->len = 0;
}
