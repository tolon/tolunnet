/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — ARP ioctl shared definition (TNET-141, CLOSE §B).
 *
 * The toolchain netinclude sys/sockio.h carries no ARP constants, so the
 * value lives here once and is used by the daemon handler, the arp command
 * and the conformance suite. Numbering follows NetBSD (SIOCGARP =
 * _IOWR('i', 38, struct arpreq)); with the netinclude struct arpreq
 * (16+16+4 = 36 bytes) that is 0xC0000000 | (36<<16) | ('i'<<8) | 38.
 * struct arpreq itself comes from <net/if_arp.h>.
 */
#ifndef TOLUNNET_TN_ARP_H
#define TOLUNNET_TN_ARP_H

#define TN_SIOCGARP 0xC2696926UL

#endif /* TOLUNNET_TN_ARP_H */
