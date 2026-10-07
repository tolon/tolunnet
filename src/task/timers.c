/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — timer.device management and real hardware time/random helpers for lwIP.
 *
 * Master prompt §6, AUDIT-2 TNET-013 (Entropy/PRNG), TNET-014 (Precise sys_now),
 * TNET-026 (Dedicated IORequest for sys_now to prevent collisions with in-flight ticks),
 * and Item 10 (Monotonic sys_now via ReadEClock).
 */

#include "timers.h"
#include "../common/tn_csprng.h"

#ifdef __AMIGA__
#include "../common/log.h"
#include <proto/exec.h>
#include <exec/memory.h>
#include <proto/timer.h>

struct Device *TimerBase = NULL;
static TnTimer *g_active_timer = NULL;
#endif

/* Audit run-1: keyed, non-invertible generator replaces the old shared
 * xorshift32 whose output was its full recoverable state. g_isn_key drives
 * the RFC 6528 TCP ISN (tn_tcp_isn), keyed independently of the stream. */
static TnCsprng g_rng;
static uint8_t  g_isn_key[16];

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

    /* Audit run-1: spread the hardware entropy across a 32-byte key pool
     * (one 16-byte key for the random stream, one for the ISN) instead of
     * folding everything into a single recoverable 32-bit word. The key
     * strength is still bounded by what this platform can gather — see
     * the entropy words below — but one observed output no longer reveals
     * the key or any future value. */
    {
        uint32_t w[8];
        uint8_t key[32];
        int i;
        uint32_t macw0 = 0, macw1 = 0;

        if (mac != NULL) {
            macw0 = ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) |
                    ((uint32_t)mac[2] << 8)  | (uint32_t)mac[3];
            macw1 = ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5];
        }
        w[0] = hash32(cur.tv_secs);
        w[1] = hash32(cur.tv_micro ^ 0x9e3779b9UL);
        w[2] = hash32(ev.ev_lo);
        w[3] = hash32(ev.ev_hi ^ 0x85ebca6bUL);
        w[4] = hash32(chip_mem);
        w[5] = hash32(fast_mem);
        w[6] = hash32(macw0);
        w[7] = hash32(macw1 ^ 0xc2b2ae35UL);
        for (i = 0; i < 8; i++) {
            key[i * 4 + 0] = (uint8_t)(w[i] >> 24);
            key[i * 4 + 1] = (uint8_t)(w[i] >> 16);
            key[i * 4 + 2] = (uint8_t)(w[i] >> 8);
            key[i * 4 + 3] = (uint8_t)(w[i]);
        }
        tn_csprng_init(&g_rng, key);
        for (i = 0; i < 16; i++) {
            g_isn_key[i] = key[16 + i];
        }
    }
    /* Do not log key material. */
    tn_logf(TN_LOG_VERBOSE, "tolunnet: PRNG entropy pool initialized\n");
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

/* Audit run-1: keyed, non-invertible stream (SipHash over a counter).
 * Replaces the xorshift32 whose output equalled its recoverable state. */
uint32_t tn_rand(void)
{
    return tn_csprng_next(&g_rng);
}

/* RFC 6528 TCP ISN hook (LWIP_HOOK_TCP_ISN via include/tn_lwip_hooks.h).
 * Keyed hash of the connection 4-tuple plus a coarse monotonic clock. */
uint32_t tn_tcp_isn(uint32_t laddr, uint16_t lport, uint32_t raddr, uint16_t rport)
{
    return tn_tcp_isn_compute(g_isn_key, laddr, lport, raddr, rport, sys_now());
}
