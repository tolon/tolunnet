/*
 * tolunnet — pure WaitSelect fd_set / timeout helpers (host-testable).
 *
 * Round 3 §B.1: target semantics for the WaitSelect path. The daemon adopts
 * these in §C11 (TNET-067); until then the unit is validated on the host
 * (test_fdset.c) so the adoption cannot change behaviour unnoticed.
 *
 * NDK netinclude uses a 64-bit fd_set for bsdsocket (FD_SETSIZE 64); tolunnet
 * supports 32 descriptors per opener (TN_MAX_FDS_PER_TASK).
 */
#ifndef TOLUNNET_FDSET_UTIL_H
#define TOLUNNET_FDSET_UTIL_H

#include <stdint.h>

#define TN_FD_SETSIZE      64   /* NDK bsdsocket fd_set width in bits */
#define TN_FD_TABLE_SIZE   32   /* tolunnet descriptors per opener */

/* errno-style result codes (values per netinclude/sys/errno.h) */
#define TN_EBADF   9
#define TN_EINVAL 22

/* Bit operations on a 64-bit fd_set image (bit n = descriptor n). */
static inline void tn_fd_set(uint64_t *set, int fd)      { *set |=  (uint64_t)1 << fd; }
static inline void tn_fd_clr(uint64_t *set, int fd)      { *set &= ~((uint64_t)1 << fd); }
static inline int  tn_fd_isset(uint64_t set, int fd)     { return (int)((set >> fd) & 1u); }

/*
 * Validate an nfds argument: 0..TN_FD_TABLE_SIZE is valid; descriptors at or
 * above the per-opener table size fail with EBADF (target semantics for
 * WaitSelect/socket calls — the current daemon clamps instead; adoption in
 * §C11). Returns 0 when valid, else the errno code.
 */
int tn_fdset_check_nfds(int nfds);

/*
 * Convert a bsdsocket timeval to milliseconds, saturating at UINT32_MAX so
 * huge timeouts behave as "forever" instead of wrapping to 0.
 */
uint32_t tn_waitselect_timeout_ms(uint32_t secs, uint32_t micros);

#endif /* TOLUNNET_FDSET_UTIL_H */
