/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — timer.device management for periodic lwIP ticks and precise time.
 *
 * Master prompt §6 & AUDIT-2 TNET-013/TNET-014.
 * Item 10: Monotonic sys_now via ReadEClock.
 */

#ifndef TOLUNNET_TIMERS_H
#define TOLUNNET_TIMERS_H

#include <stdint.h>

#ifdef __AMIGA__
#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#else
#include "ipc.h"
struct timerequest;
struct MsgPort;
#ifndef _ECLOCKVAL_DEFINED
#define _ECLOCKVAL_DEFINED
struct EClockVal {
    uint32_t ev_hi;
    uint32_t ev_lo;
};
#endif
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
    uint32_t tv_secs;
    uint32_t tv_micro;
};
#endif
typedef uint32_t ULONG;
typedef uint8_t  UBYTE;
#endif

typedef struct TnTimer {
    struct MsgPort       *port;
    struct timerequest   *io;
    struct MsgPort       *time_port;        /* Dedicated port for GETSYSTIME queries (TNET-026) */
    struct timerequest   *time_io;         /* Dedicated IORequest for sys_now() queries */
    struct timeval        boot_time;
    struct EClockVal      boot_eclock;      /* Baseline E-Clock for monotonic sys_now() (Item 10) */
    ULONG                 eclock_freq;      /* E-Clock frequency (ticks/sec) */
    BOOL                  open;
    BOOL                  armed;
    ULONG                 sig_mask;
} TnTimer;

#ifdef __AMIGA__
/* Initialize and open timer.device (UNIT_MICROHZ). */
BOOL tn_timer_init(TnTimer *tm);

/* Initialize hardware entropy pool for PRNG (TNET-013) */
void tn_rand_init(TnTimer *tm, const UBYTE mac[6]);

/* Arm a periodic tick (microsecs: 100000 = 100 ms). */
void tn_timer_arm(TnTimer *tm, ULONG microsecs);

/* Acknowledge a fired timer tick (clears reply from port). */
void tn_timer_ack(TnTimer *tm);

/* Cancel any pending timer request and close timer.device. */
void tn_timer_fini(TnTimer *tm);
#endif

/* Pure calculation helper: E-Clock ticks to elapsed milliseconds (Item 10) */
uint32_t tn_eclock_to_ms(const struct EClockVal *cur, const struct EClockVal *boot, uint32_t freq);

/* lwIP sys_now() function returning real elapsed time in milliseconds (TNET-014) */
uint32_t sys_now(void);

#ifndef __AMIGA__
/* Host mock control for sys_now */
void mock_set_sys_now(uint32_t ms);
#endif

/* Fast 32-bit PRNG seeded from hardware entropy (TNET-013) */
uint32_t tn_rand(void);

#endif /* TOLUNNET_TIMERS_H */
