/*
 * tolunnet — pure WaitSelect fd_set / timeout helpers (host-testable).
 */

#include "fdset_util.h"

int tn_fdset_check_nfds(int nfds)
{
    if (nfds < 0 || nfds > TN_FD_SETSIZE) return TN_EINVAL;
    if (nfds > TN_FD_TABLE_SIZE) return TN_EBADF; /* fd >= table size is out of range */
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
