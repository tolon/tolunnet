/*
 * tolunet memory helpers — placeholder for M2.
 *
 * Master prompt §2: src/common/mem.c. In M2 the network task wires lwIP's
 * memory callbacks (lwip_malloc/free → Amiga AllocVec/FreeVec under NO_SYS=1)
 * here. For M0 this file is a stub so the build tree is complete and the
 * interface is captured early.
 *
 * Nothing here is called by the M0 hello-task.
 */
#ifndef TOLUNET_MEM_H
#define TOLUNET_MEM_H

#include <exec/types.h>

/* Returns resident RAM used (task + library + pools) in bytes, or 0 if
 * unknown. M-later: implemented via AvailMem accounting for the
 * `TolunetStatus MEM` budget print (§9). */
ULONG tn_mem_resident_bytes(void);

#endif /* TOLUNET_MEM_H */
