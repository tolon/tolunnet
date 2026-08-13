/*
 * tolunet — lwIP build-time configuration.
 *
 * Binding source: TOLUNET-master-prompt.md §6 (lwipopts.h START VALUES). These
 * are the agreed start values; tune ONLY with measurements pasted in STATUS.md
 * (§6, §9). 68k is big-endian = network order, so the byte-swap macros are
 * identity, but lwIP still wants the symbols defined (§3 last line).
 *
 * This file is on the include path BEFORE lwIP's own headers (see Makefile,
 * -I lwipopts), so it overrides the defaults in vendor/lwip/src/include/lwip/opt.h.
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* --- Threading model (§1.1): one Amiga task, raw/callback API. --- */
#define NO_SYS               1
#define LWIP_SOCKET          0
#define LWIP_NETCONN         0
/* tolunet provides its own blocking layer on top of the raw API (§6). */

/* --- Protocols enabled. --- */
#define LWIP_DHCP            1
#define LWIP_DNS             1
#define LWIP_TCP             1
#define LWIP_UDP             1
/* IPv6 is NOT v1 (§1). Keep it off so the ipv6/ core files can be excluded. */
#define LWIP_IPV6            0
#define LWIP_IPV4            1

/* --- Memory (§6 start values). Measure before changing (§9, ≤250 KB). --- */
#define MEM_SIZE             (64 * 1024)
#define PBUF_POOL_SIZE       24

/* --- TCP tuning (§6). 68k + low RAM: conservative windows. --- */
#define TCP_MSS              1460
#define TCP_WND              (8 * TCP_MSS)
#define TCP_SND_BUF          (8 * TCP_MSS)

/* --- Statistics: only in debug builds (§6, §9). Off by default. --- */
#if defined(TOLUNET_DEBUG) && (TOLUNET_DEBUG + 0)
  #define LWIP_STATS          1
  #define LWIP_STATS_DISPLAY  1
#else
  #define LWIP_STATS          0
#endif

/* --- Byte order: 68k is big-endian = network order. Keep the macros. --- */
#define BYTE_ORDER           BIG_ENDIAN

/*
 * Timers (§6): the task ticks lwIP at 100 ms granularity via timer.device and
 * calls sys_check_timeouts() on each tick. lwIP's own OS-timer hooks are unused
 * under NO_SYS=1; the task drives timeouts.c explicitly.
 */

#endif /* LWIP_LWIPOPTS_H */
