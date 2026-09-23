/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — timer.device management and real hardware time/random helpers for lwIP.
 *
 * Master prompt §6, AUDIT-2 TNET-013 (Entropy/PRNG), TNET-014 (Precise sys_now),
 * TNET-026 (Dedicated IORequest for sys_now to prevent collisions with in-flight ticks),
 * and Item 10 (Monotonic sys_now via ReadEClock).
 */

#include "timers.h"

#ifdef __AMIGA__
#include "../common/log.h"
#include <proto/exec.h>
#include <exec/memory.h>
#include <proto/timer.h>

struct Device *TimerBase = NULL;
static TnTimer *g_active_timer = NULL;
#endif

static uint32_t g_rand_state   = 0;

/* 32-bit Integer Hash (Murmur3 finalizer) */
static inline uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x85ebca6bUL;
    x ^= x >> 13;
    x *= 0xc2b2ae35UL;
    x ^= x >> 16;
    return x;
}

#ifdef __AMIGA__
BOOL tn_timer_init(TnTimer *tm)
{
    BYTE err;

    if (tm == NULL) return FALSE;

    tm->port = NULL;
    tm->io = NULL;
    tm->time_port = NULL;
    tm->time_io = NULL;
    tm->boot_time.tv_secs = 0;
    tm->boot_time.tv_micro = 0;
    tm->boot_eclock.ev_hi = 0;
    tm->boot_eclock.ev_lo = 0;
    tm->eclock_freq = 0;
    tm->open = FALSE;
    tm->armed = FALSE;
    tm->sig_mask = 0;

    /* 1. Periodic Tick Port & IORequest (TR_ADDREQUEST) */
    tm->port = CreateMsgPort();
    if (tm->port == NULL) return FALSE;

    tm->io = (struct timerequest *)AllocVec(sizeof(struct timerequest),
                                            MEMF_CLEAR | MEMF_PUBLIC);
    if (tm->io == NULL) {
        DeleteMsgPort(tm->port);
        tm->port = NULL;
        return FALSE;
    }

    tm->io->tr_node.io_Message.mn_ReplyPort = tm->port;
    tm->io->tr_node.io_Message.mn_Length    = sizeof(struct timerequest);
    tm->io->tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;

    err = OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
                     (struct IORequest *)tm->io, 0UL);
    if (err != 0) {
        tn_logf(TN_LOG_BASIC, "tolunnet: OpenDevice(timer.device) failed err=%d\n", (int)err);
        FreeVec(tm->io);
        tm->io = NULL;
        DeleteMsgPort(tm->port);
        tm->port = NULL;
        return FALSE;
    }

    /* Assign TimerBase from opened device node */
    TimerBase = (struct Device *)tm->io->tr_node.io_Device;

    /* Read baseline monotonic E-Clock (Item 10) */
    tm->eclock_freq = ReadEClock(&tm->boot_eclock);
    if (tm->eclock_freq == 0) {
        tm->eclock_freq = 709379UL; /* Standard PAL E-Clock fallback */
    }

    /* 2. Dedicated Time Query Port & IORequest for sys_now() (TNET-026) */
    tm->time_port = CreateMsgPort();
    if (tm->time_port != NULL) {
        tm->time_io = (struct timerequest *)AllocVec(sizeof(struct timerequest),
                                                     MEMF_CLEAR | MEMF_PUBLIC);
        if (tm->time_io != NULL) {
            tm->time_io->tr_node.io_Message.mn_ReplyPort = tm->time_port;
            tm->time_io->tr_node.io_Message.mn_Length    = sizeof(struct timerequest);
            tm->time_io->tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;

            if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
                           (struct IORequest *)tm->time_io, 0UL) != 0) {
                FreeVec(tm->time_io);
                tm->time_io = NULL;
                DeleteMsgPort(tm->time_port);
                tm->time_port = NULL;
            }
        }
    }

    /* Record baseline boot timestamp for wall-clock start time reporting */
    if (tm->time_io != NULL) {
        tm->time_io->tr_node.io_Command = TR_GETSYSTIME;
        DoIO((struct IORequest *)tm->time_io);
        tm->boot_time = tm->time_io->tr_time;
    } else {
        tm->io->tr_node.io_Command = TR_GETSYSTIME;
        DoIO((struct IORequest *)tm->io);
        tm->boot_time = tm->io->tr_time;
    }

    tm->open = TRUE;
    tm->sig_mask = 1UL << tm->port->mp_SigBit;
    g_active_timer = tm;

    return TRUE;
}

void tn_rand_init(TnTimer *tm, const UBYTE mac[6])
{
    struct timeval cur;
    struct EClockVal ev;
    ULONG chip_mem, fast_mem;
    uint32_t seed = 0;

    cur.tv_secs = 0;
    cur.tv_micro = 0;
    ev.ev_hi = 0;
    ev.ev_lo = 0;

    if (tm != NULL && tm->open) {
        struct timerequest *req = (tm->time_io != NULL) ? tm->time_io : tm->io;
        if (req != NULL) {
            req->tr_node.io_Command = TR_GETSYSTIME;
            DoIO((struct IORequest *)req);
            cur = req->tr_time;
        }
        if (TimerBase != NULL) {
            ReadEClock(&ev);
        }
    }

    chip_mem = AvailMem(MEMF_CHIP);
    fast_mem = AvailMem(MEMF_FAST | MEMF_PUBLIC);

    /* Fold hardware entropy into seed */
    seed ^= hash32(cur.tv_secs);
    seed ^= hash32(cur.tv_micro ^ 0x9e3779b9UL);
    seed ^= hash32(ev.ev_lo);
    seed ^= hash32(chip_mem);
    seed ^= hash32(fast_mem);

    if (mac != NULL) {
        uint32_t mac_hi = ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) |
                          ((uint32_t)mac[2] << 8)  | (uint32_t)mac[3];
        uint32_t mac_lo = ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5];
        seed ^= hash32(mac_hi);
        seed ^= hash32(mac_lo);
    }

    if (seed == 0) {
        seed = 0xa5a5a5a5UL;
    }

    g_rand_state = seed;
    tn_logf(TN_LOG_VERBOSE, "tolunnet: PRNG entropy pool initialized (seed: 0x%08lx)\n", (ULONG)g_rand_state);
}

void tn_timer_arm(TnTimer *tm, ULONG microsecs)
{
    if (tm == NULL || !tm->open || tm->armed) return;

    tm->io->tr_node.io_Command = TR_ADDREQUEST;
    tm->io->tr_node.io_Error   = 0;
    tm->io->tr_time.tv_secs    = microsecs / 1000000UL;
    tm->io->tr_time.tv_micro   = microsecs % 1000000UL;

    SendIO((struct IORequest *)tm->io);
    tm->armed = TRUE;
}

void tn_timer_ack(TnTimer *tm)
{
    if (tm == NULL || !tm->open || !tm->armed) return;

    WaitIO((struct IORequest *)tm->io);
    tm->armed = FALSE;
}

void tn_timer_fini(TnTimer *tm)
{
    if (tm == NULL) return;

    if (tm->open && tm->io != NULL) {
        if (tm->armed) {
            if (!CheckIO((struct IORequest *)tm->io)) {
                AbortIO((struct IORequest *)tm->io);
            }
            WaitIO((struct IORequest *)tm->io);
            tm->armed = FALSE;
        }
        CloseDevice((struct IORequest *)tm->io);
        tm->open = FALSE;
    }

    if (tm->io != NULL) {
        FreeVec(tm->io);
        tm->io = NULL;
    }

    if (tm->port != NULL) {
        DeleteMsgPort(tm->port);
        tm->port = NULL;
    }

    /* Clean up dedicated time query channel (TNET-026) */
    if (tm->time_io != NULL) {
        CloseDevice((struct IORequest *)tm->time_io);
        FreeVec(tm->time_io);
        tm->time_io = NULL;
    }

    if (tm->time_port != NULL) {
        DeleteMsgPort(tm->time_port);
        tm->time_port = NULL;
    }

    tm->sig_mask = 0;
    if (g_active_timer == tm) {
        g_active_timer = NULL;
    }
    TimerBase = NULL;
}
#endif /* __AMIGA__ */

/* Pure calculation helper: E-Clock ticks to elapsed milliseconds (Item 10) */
uint32_t tn_eclock_to_ms(const struct EClockVal *cur, const struct EClockVal *boot, uint32_t freq)
{
    uint64_t cur_ticks, boot_ticks, diff_ticks;

    if (cur == NULL || boot == NULL) {
        return 0;
    }
    if (freq == 0) {
        freq = 709379UL;
    }

    cur_ticks = ((uint64_t)cur->ev_hi << 32) | cur->ev_lo;
    boot_ticks = ((uint64_t)boot->ev_hi << 32) | boot->ev_lo;

    if (cur_ticks < boot_ticks) {
        return 0;
    }

    diff_ticks = cur_ticks - boot_ticks;

    return (uint32_t)((diff_ticks * 1000ULL) / freq);
}

#ifdef __AMIGA__
uint32_t sys_now(void)
{
    struct EClockVal cur;
    ULONG freq;

    if (g_active_timer == NULL || !g_active_timer->open || TimerBase == NULL) {
        return 0;
    }

    freq = ReadEClock(&cur);
    if (freq == 0) {
        freq = g_active_timer->eclock_freq ? g_active_timer->eclock_freq : 709379UL;
    }

    return tn_eclock_to_ms(&cur, &g_active_timer->boot_eclock, freq);
}
#else
static uint32_t g_host_now_ms = 0;

void mock_set_sys_now(uint32_t ms)
{
    g_host_now_ms = ms;
}

uint32_t sys_now(void)
{
    return g_host_now_ms;
}
#endif

/* Fast 32-bit xorshift PRNG */
uint32_t tn_rand(void)
{
    uint32_t x = g_rand_state;
    if (x == 0) x = 0xdeadbeefUL;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rand_state = x;
    return x;
}
