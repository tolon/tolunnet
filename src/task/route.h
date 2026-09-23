/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Static IPv4 route table (CLOSE §B.5).
 *
 * Pure longest-prefix-match table, no lwIP/exec includes: the same unit
 * compiles into the daemon (behind the LWIP_HOOK_IP4_ROUTE_SRC and
 * LWIP_HOOK_ETHARP_GET_GW hooks, see route_hook.c) and the host unit
 * tests. All addresses are network byte order ULONGs.
 *
 * Lookup precedence (BSD-like): an on-link destination never reaches this
 * table — the hook layer checks the netif list first and only consults the
 * static routes for off-link destinations, so a careless default route
 * cannot hijack local traffic.
 */
#ifndef TOLUNNET_ROUTE_H
#define TOLUNNET_ROUTE_H

#include <stdint.h>
#include <stddef.h> /* NULL — keeps the unit host-compilable standalone */

#define TN_MAX_ROUTES 16

typedef struct TnRoute {
    uint32_t dest;   /* network address, network byte order */
    uint32_t mask;   /* contiguous netmask, network byte order */
    uint32_t gw;     /* gateway, network byte order (0 = interface route) */
    uint8_t  in_use;
} TnRoute;

void tn_route_init(void);

/* Add dest/mask via gw. Returns 0 on success, -1 + errno (EEXIST, ENOSPC,
 * EINVAL for a non-contiguous mask or network+host-bits mismatch). */
int tn_route_add(uint32_t dest, uint32_t mask, uint32_t gw, int *errno_out);

/* Delete the route matching dest/mask exactly. 0 ok, -1 + ENOENT. */
int tn_route_delete(uint32_t dest, uint32_t mask, int *errno_out);

/* Longest-prefix match. Returns the entry index (>= 0) or -1. */
int tn_route_lookup(uint32_t dest);

/* Entry access for SHOW; NULL when idx out of range or unused. */
const TnRoute *tn_route_at(int idx);

/* Number of routes in use. */
int tn_route_count(void);

#endif /* TOLUNNET_ROUTE_H */
