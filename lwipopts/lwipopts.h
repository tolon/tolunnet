/*
 * tolunnet — lwIP build-time configuration.
 *
 * Hardened Network Stack Configuration:
 * - NO_SYS single-task architecture.
 * - Broadcast/multicast ping suppression (Smurf attack defense).
 * - Comprehensive layer-3 and layer-4 checksum enforcement.
 * - IP fragment reassembly age & buffer limits (Teardrop attack defense).
 * - TCP listen backlog & ISN randomization (SYN flood & hijacking defense).
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* --- Threading model: one Amiga task, raw/callback API. --- */
#define NO_SYS               1
#define LWIP_SOCKET          0
#define LWIP_NETCONN         0
#define SYS_LIGHTWEIGHT_PROT 0

/* --- Protocols enabled. --- */
#define LWIP_DHCP            1
#define LWIP_DNS             1
#define LWIP_TCP             1
#define LWIP_UDP             1
#define LWIP_ARP             1
#define LWIP_ETHERNET        1
#define LWIP_ICMP            1
#define LWIP_RAW             1
#define LWIP_IGMP            1
#define LWIP_TCP_KEEPALIVE   1

/* Loopback support (TNET-071) */
#define LWIP_NETIF_LOOPBACK               1
#define LWIP_HAVE_LOOPIF                  1
#define LWIP_NETIF_LOOPBACK_MULTITHREADING 0

/* TNET-063: HOSTNAME= key → DHCP option 12 + netif hostname */
#define LWIP_NETIF_HOSTNAME  1

/* TNET-109: S2_ONEVENT link tracking → netif_set_link_up/down plumbing */
#define LWIP_NETIF_LINK_CALLBACK 1

/* --- IPv4 only --- */
#define LWIP_IPV6            0
#define LWIP_IPV4            1

/* --- Memory tuning (TNET-069) --- */
#define MEM_ALIGNMENT        4
#define MEM_SIZE             (96 * 1024)
#define PBUF_POOL_SIZE       32
#define MEMP_NUM_TCP_PCB     64  /* TNET-131: dtablesize-1 concurrent sockets */
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_UDP_PCB     16
#define MEMP_NUM_RAW_PCB     8
#define MEMP_NUM_TCP_SEG     64
#define DNS_MAX_SERVERS      2
#define DNS_TABLE_SIZE       8
#define LWIP_DHCP_PROVIDE_DNS_SERVERS 2
#define SO_REUSE             1

/* TNET-085 (v3 contract §3.5): two pad bytes BEFORE the ethernet header so
 * the IP header (eth header + 14) lands on a 4-byte boundary. Without this,
 * lwIP's u32 accesses into the IP header are misaligned — silently tolerated
 * on 68020+, a fatal Address Error (#80000003) on the 68000. Proven on the
 * WinUAE 68000 bench: daemon Guru'd on the first received frame; green after
 * this fix plus the SANA-II RX pad handling (sana2_netif.c). */
#define ETH_PAD_SIZE         2

/* --- TCP tuning --- */
#define TCP_MSS              1460
#define TCP_WND              (8 * TCP_MSS)
#define TCP_SND_BUF          (8 * TCP_MSS)
#define TCP_SND_QUEUELEN     (2 * (TCP_SND_BUF) / (TCP_MSS))
#define TCP_LISTEN_BACKLOG   1
#define TCP_DEFAULT_LISTEN_BACKLOG 8

/* --- Security & Hardening Mitigations --- */

/* 1. ICMP Flood & Smurf Attack Mitigation: Do not respond to broadcast/multicast pings */
#define LWIP_BROADCAST_PING  0
#define LWIP_MULTICAST_PING  0

/* 2. Strict Checksum Verification on all input frames */
#define CHECKSUM_CHECK_IP    1
#define CHECKSUM_CHECK_UDP   1
#define CHECKSUM_CHECK_TCP   1
#define CHECKSUM_CHECK_ICMP  1
#define CHECKSUM_GEN_IP      1
#define CHECKSUM_GEN_UDP     1
#define CHECKSUM_GEN_TCP     1
#define CHECKSUM_GEN_ICMP    1

/* 3. IP Fragment Hardening (Teardrop / Overlap Attack & DoS Defense) */
#define IP_REASSEMBLY        1
#define IP_FRAG              1
#define IP_REASS_MAXAGE      3       /* Drop incomplete fragment chains after 3 seconds */
#define IP_REASS_MAX_PBUFS   8       /* Maximum number of pbufs queued for reassembly */

/* --- Statistics (§F): enabled in release and debug builds --- */
#define LWIP_STATS          1
#define LWIP_STATS_DISPLAY  0
#define MEMP_STATS          1
#define MEM_STATS           1
#define TCP_STATS           1
#define UDP_STATS           1
#define IP_STATS            1
#define ICMP_STATS          1
#define ETHARP_STATS        1
#define LINK_STATS          1

/* --- Byte order: 68k is big-endian = network order. --- */
#define BYTE_ORDER           BIG_ENDIAN

#endif /* LWIP_LWIPOPTS_H */
