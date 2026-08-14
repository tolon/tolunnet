/*
 * tolunnet — timer.device management and real hardware time/random helpers for lwIP.
 *
 * Master prompt §6, AUDIT-2 TNET-013 (Entropy/PRNG), TNET-014 (Precise sys_now),
 * and TNET-026 (Dedicated IORequest for sys_now to prevent collisions with in-flight ticks).
 */

#include "timers.h"
#include "../common/log.h"

#include <proto/exec.h>
#include <exec/memory.h>

static TnTimer *g_active_timer = NULL;
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

    /* Record baseline boot timestamp for precision uptime calculations */
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
    ULONG chip_mem, fast_mem;
    uint32_t seed = 0;

    cur.tv_secs = 0;
    cur.tv_micro = 0;

    if (tm != NULL && tm->open) {
        struct timerequest *req = (tm->time_io != NULL) ? tm->time_io : tm->io;
        if (req != NULL) {
            req->tr_node.io_Command = TR_GETSYSTIME;
            DoIO((struct IORequest *)req);
            cur = req->tr_time;
        }
    }

    chip_mem = AvailMem(MEMF_CHIP);
    fast_mem = AvailMem(MEMF_FAST | MEMF_PUBLIC);

    /* Fold hardware entropy into seed */
    seed ^= hash32(cur.tv_secs);
    seed ^= hash32(cur.tv_micro ^ 0x9e3779b9UL);
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
}

uint32_t sys_now(void)
{
    struct timeval cur;
    int32_t d_sec, d_micro;
    struct timerequest *req;

    if (g_active_timer == NULL || !g_active_timer->open) {
        return 0;
    }

    req = (g_active_timer->time_io != NULL) ? g_active_timer->time_io : g_active_timer->io;
    if (req == NULL) return 0;

    /* Issue TR_GETSYSTIME synchronously on dedicated channel */
    req->tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *)req);
    cur = req->tr_time;

    d_sec = (int32_t)(cur.tv_secs - g_active_timer->boot_time.tv_secs);
    d_micro = (int32_t)(cur.tv_micro - g_active_timer->boot_time.tv_micro);

    if (d_micro < 0) {
        d_sec -= 1;
        d_micro += 1000000;
    }

    if (d_sec < 0) return 0;

    return (uint32_t)(d_sec * 1000 + d_micro / 1000);
}

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
