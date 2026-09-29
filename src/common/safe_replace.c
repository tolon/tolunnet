/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Safe file replace (z.ai step 11k item 1)
 *
 * Same contract the prefs rewrite proved in 11j: park the current
 * file in bak first (an older bak is deleted before that), move
 * tmp over the now-free path name, and on any failure put the old
 * file back. The target path itself is never Deleted - only the
 * tmp and bak names ever are. dos.library only, so the conformance
 * suite can link this file alone.
 */

#include "safe_replace.h"

#include <proto/dos.h>
#include <dos/dos.h>

BOOL tn_safe_replace(const char *tmp, const char *path, const char *bak)
{
    BOOL had;

    if (!tmp || !path || !bak) return FALSE;

    DeleteFile((CONST_STRPTR)bak);          /* older generation */
    had = (BOOL)(Rename((CONST_STRPTR)path, (CONST_STRPTR)bak) != FALSE);
    if (Rename((CONST_STRPTR)tmp, (CONST_STRPTR)path) == FALSE) {
        if (had) Rename((CONST_STRPTR)bak, (CONST_STRPTR)path);
        DeleteFile((CONST_STRPTR)tmp);
        return FALSE;
    }
    return TRUE;
}
