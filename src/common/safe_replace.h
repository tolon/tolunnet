/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Safe file replace (z.ai step 11k item 1)
 *
 * One helper for every in-place config/startup rewrite: the new
 * content (already written to tmp) is moved over path, and the
 * previous path content survives in bak for one generation. path
 * itself is never Deleted, so a crash or a failed rename can at
 * worst leave the old file in place - it cannot destroy it.
 */

#ifndef TOLUNNET_SAFE_REPLACE_H
#define TOLUNNET_SAFE_REPLACE_H

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

BOOL tn_safe_replace(const char *tmp, const char *path, const char *bak);

#endif /* TOLUNNET_SAFE_REPLACE_H */
