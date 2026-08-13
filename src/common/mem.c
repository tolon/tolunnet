/*
 * tolunet memory helpers — M0 stub.
 *
 * Real accounting arrives with the network task (M2): the task will track
 * AllocVec allocations for pbuf pools and resident structures and report via
 * TolunetStatus MEM (§9, budget ≤ 250 KB). For now this returns 0 so any
 * caller compiles; no caller exists in M0.
 */

#include "mem.h"

ULONG tn_mem_resident_bytes(void)
{
    return 0;   /* not measured yet — see STATUS.md Budgets table */
}
