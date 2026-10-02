/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — pure WaitSelect fd_set / timeout helpers (host-testable).
 */

#include "fdset_util.h"

int tn_fdset_check_nfds(int nfds)
{
    if (nfds < 0) return TN_EINVAL;
    /* 4.4BSD select: the client LVO clamps nfds to min(nfds, dtablesize, 64)
     * and the daemon (ipc_select.c) clamps again before scanning */
    return 0;
}

uint32_t tn_waitselect_timeout_ms(uint32_t secs, uint32_t micros)
{
    /* secs * 1000 with saturation, then + micros/1000 (capped) */
    if (secs > 0xFFFFFFFFu / 1000u) return 0xFFFFFFFFu;
    uint64_t ms = (uint64_t)secs * 1000u + (uint64_t)(micros / 1000u);
    if (ms > 0xFFFFFFFFu) return 0xFFFFFFFFu;
    return (uint32_t)ms;
}
