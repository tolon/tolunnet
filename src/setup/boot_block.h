/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Boot block editor for the user startup script
 * (z.ai step 11l item 2)
 *
 * Byte-exact editor: the whole file is read (size from ExamineFH,
 * no cap), every line outside a "; BEGIN tolunnet ... ; END
 * tolunnet" block is kept verbatim (no line-length limit, a missing
 * final newline stays missing), old blocks are removed, and the new
 * content is installed through tn_safe_replace so the original can
 * never be destroyed.
 */

#ifndef TOLUNNET_BOOT_BLOCK_H
#define TOLUNNET_BOOT_BLOCK_H

#if defined(__AMIGA__) || defined(__amigaos__) || defined(TN_AMIGA_BUILD)
#include <exec/types.h>
#else
#include <stdint.h>
#include <stddef.h>
typedef uint32_t ULONG;
typedef int32_t  LONG;
typedef int      BOOL;
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#endif

BOOL tn_boot_block_apply(const char *path, BOOL enable);

#endif /* TOLUNNET_BOOT_BLOCK_H */
