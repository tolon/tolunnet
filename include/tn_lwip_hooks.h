/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — lwIP hook declarations (audit run-1).
 *
 * Included by lwIP through LWIP_HOOK_FILENAME (see lwipopts.h), which pulls
 * it in only after the lwIP arch and ip_addr types are defined — lwipopts.h
 * itself is processed before u32_t/ip_addr_t exist, so the TCP-ISN binding
 * cannot live there.
 */
#ifndef TOLUNNET_LWIP_HOOKS_H
#define TOLUNNET_LWIP_HOOKS_H

#include "lwip/arch.h"
#include "lwip/ip_addr.h"

/* RFC 6528 initial sequence number (src/task/timers.c, src/common/tn_csprng.c).
 * Keyed hash of the connection 4-tuple plus a coarse clock; replaces lwIP's
 * predictable default counter. Addresses are passed host-order. */
u32_t tn_tcp_isn(u32_t laddr, u16_t lport, u32_t raddr, u16_t rport);

#define LWIP_HOOK_TCP_ISN(laddr, lport, raddr, rport)                 \
    tn_tcp_isn(lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(laddr))), (lport), \
               lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(raddr))), (rport))

#endif /* TOLUNNET_LWIP_HOOKS_H */
