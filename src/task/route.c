/*
 * tolunnet — Static IPv4 route table implementation (CLOSE §B.5).
 *
 * Host-testable: no AmigaOS or lwIP headers (route.h documents the
 * contract). Entries keep the caller's network-order bytes untouched; all
 * bit logic (mask contiguity, prefix match, prefix length) runs on the
 * canonical big-endian integer value via be32() — identity on the 68k
 * target, an un-swap of htonl()-fed arguments on the little-endian host
 * test rig, so both ends behave identically.
 */
#include "route.h"

#include <errno.h>

static TnRoute g_routes[TN_MAX_ROUTES]; /* fields stored in network order */

static uint32_t be32(uint32_t v)
{
    /* reinterpret the 4 bytes as a big-endian integer */
    uint8_t *p = (uint8_t *)&v;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void tn_route_init(void)
{
    int i;
    for (i = 0; i < TN_MAX_ROUTES; i++) g_routes[i].in_use = 0;
}

static int mask_is_contiguous(uint32_t cmask)
{
    /* ones on the left, zeros on the right: ~cmask is 0..01..1, which has
     * no zero below a one iff (~cmask & (~cmask + 1)) == 0 */
    uint32_t inv = ~cmask;
    return (inv & (inv + 1)) == 0;
}

int tn_route_add(uint32_t dest, uint32_t mask, uint32_t gw, int *errno_out)
{
    int i;
    if (errno_out) *errno_out = 0;

    if (!mask_is_contiguous(be32(mask)) || (be32(dest) & ~be32(mask)) != 0) {
        if (errno_out) *errno_out = EINVAL;
        return -1;
    }
    for (i = 0; i < TN_MAX_ROUTES; i++) {
        if (g_routes[i].in_use && g_routes[i].dest == dest &&
            g_routes[i].mask == mask) {
            if (errno_out) *errno_out = EEXIST;
            return -1;
        }
    }
    for (i = 0; i < TN_MAX_ROUTES; i++) {
        if (!g_routes[i].in_use) {
            g_routes[i].dest = dest;
            g_routes[i].mask = mask;
            g_routes[i].gw = gw;
            g_routes[i].in_use = 1;
            return 0;
        }
    }
    if (errno_out) *errno_out = ENOSPC;
    return -1;
}

int tn_route_delete(uint32_t dest, uint32_t mask, int *errno_out)
{
    int i;
    if (errno_out) *errno_out = 0;
    for (i = 0; i < TN_MAX_ROUTES; i++) {
        if (g_routes[i].in_use && g_routes[i].dest == dest &&
            g_routes[i].mask == mask) {
            g_routes[i].in_use = 0;
            return 0;
        }
    }
    if (errno_out) *errno_out = ENOENT;
    return -1;
}

static int mask_prefix_len(uint32_t cmask)
{
    int n = 0;
    while (cmask & 0x80000000u) { n++; cmask <<= 1; }
    return n;
}

int tn_route_lookup(uint32_t dest)
{
    uint32_t cdest = be32(dest);
    int i, best = -1, best_len = -1;
    for (i = 0; i < TN_MAX_ROUTES; i++) {
        if (g_routes[i].in_use) {
            uint32_t cmask = be32(g_routes[i].mask);
            if ((cdest & cmask) == be32(g_routes[i].dest)) {
                int len = mask_prefix_len(cmask);
                /* strict > keeps the first-installed route on equal length */
                if (len > best_len) {
                    best_len = len;
                    best = i;
                }
            }
        }
    }
    return best;
}

static TnRoute g_ret;

const TnRoute *tn_route_at(int idx)
{
    if (idx < 0 || idx >= TN_MAX_ROUTES || !g_routes[idx].in_use) return NULL;
    g_ret = g_routes[idx];
    return &g_ret;
}

int tn_route_count(void)
{
    int i, n = 0;
    for (i = 0; i < TN_MAX_ROUTES; i++) {
        if (g_routes[i].in_use) n++;
    }
    return n;
}
