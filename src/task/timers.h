/*
 * tolunet — timer.device management for periodic lwIP ticks and precise time.
 *
 * Master prompt §6 & AUDIT-2 TNET-013/TNET-014.
 */

#ifndef TOLUNET_TIMERS_H
#define TOLUNET_TIMERS_H

#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <stdint.h>

typedef struct TnTimer {
    struct MsgPort       *port;
    struct timerequest   *io;
    struct MsgPort       *time_port;        /* Dedicated port for GETSYSTIME queries (TNET-026) */
    struct timerequest   *time_io;         /* Dedicated IORequest for sys_now() queries */
    struct timeval        boot_time;
    BOOL                  open;
    BOOL                  armed;
    ULONG                 sig_mask;
} TnTimer;

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

/* lwIP sys_now() function returning real elapsed time in milliseconds (TNET-014) */
uint32_t sys_now(void);

/* Fast 32-bit PRNG seeded from hardware entropy (TNET-013) */
uint32_t tn_rand(void);

#endif /* TOLUNET_TIMERS_H */
