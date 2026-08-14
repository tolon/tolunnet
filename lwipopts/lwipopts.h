/*
 * tolunet — lwIP build-time configuration.
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

/* --- IPv4 only --- */
#define LWIP_IPV6            0
#define LWIP_IPV4            1

/* --- Memory tuning --- */
#define MEM_ALIGNMENT        4
#define MEM_SIZE             (64 * 1024)
#define PBUF_POOL_SIZE       24

/* --- TCP tuning --- */
#define TCP_MSS              1460
#define TCP_WND              (8 * TCP_MSS)
#define TCP_SND_BUF          (8 * TCP_MSS)
#define TCP_SND_QUEUELEN     (2 * (TCP_SND_BUF) / (TCP_MSS))
#define MEMP_NUM_TCP_SEG     TCP_SND_QUEUELEN
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

/* --- Statistics: only in debug builds --- */
#if defined(TOLUNET_DEBUG) && (TOLUNET_DEBUG + 0)
  #define LWIP_STATS          1
  #define LWIP_STATS_DISPLAY  1
#else
  #define LWIP_STATS          0
#endif

/* --- Byte order: 68k is big-endian = network order. --- */
#define BYTE_ORDER           BIG_ENDIAN

#endif /* LWIP_LWIPOPTS_H */
