/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet logging implementation — DOS Write/Output (master prompt §3: no
 * stdio in resident code).
 *
 * Provides tn_log() and tn_logf() without libc printf/vprintf dependencies.
 */

#include "log.h"
#include "log_format.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <dos/dosextens.h>
#include <stdarg.h>

struct Library *g_log_dos   = NULL;
BPTR            g_log_file  = (BPTR)0;
int             g_log_level = TN_LOG_OFF;
void          (*g_log_sink)(const char *msg) = NULL;

/* 5.1: the task that owns the log file and the sink (the daemon). tn_log
 * also runs on library client tasks (syslog(), VERBOSE LVO traces); those
 * must not enter lwIP through the sink, share the daemon's file handle, or
 * call Output() on a plain Task. NULL = single-task binary, no restriction. */
static struct Task *g_log_owner = NULL;

void tn_log_set_owner(void)
{
    g_log_owner = FindTask(NULL);
}

/* TNET-139 DIAG ring: static storage, NUL-terminated slots, trap-safe. */
static int  g_ring_on = 0;
static int  g_ring_next = 0;
static int  g_ring_used = 0;
static char g_ring[TN_LOG_RING_LINES][TN_LOG_RING_LEN];

void tn_log_ring_enable(void)
{
    g_ring_on = 1;
}

const char *const *tn_log_ring_snapshot(void)
{
    /* g_ring rows are 96-byte aligned slots; the void* hop records that */
    return (const char *const *)(const void *)g_ring;
}

int tn_log_ring_count(void)
{
    return g_ring_used;
}

static void ring_capture(const char *msg)
{
    int i = 0;
    if (!g_ring_on) return;
    /* 5.1: shared ring, written from any task - a <=96-byte copy */
    Forbid();
    while (msg[i] != '\0' && i < TN_LOG_RING_LEN - 1) {
        g_ring[g_ring_next][i] = msg[i];
        i++;
    }
    g_ring[g_ring_next][i] = '\0';
    g_ring_next = (g_ring_next + 1) % TN_LOG_RING_LINES;
    if (g_ring_used < TN_LOG_RING_LINES) g_ring_used++;
    Permit();
}

void tn_log(int tier, const char *msg)
{
    LONG len;
    BPTR out;
    struct Task *self;

    if (msg == NULL) return;
    ring_capture(msg);
    if (tier > g_log_level) return;
    if (g_log_dos == NULL) return;

    /* 5.1: a foreign task (library client) only gets the ring above */
    self = FindTask(NULL);
    if (g_log_owner != NULL && self != g_log_owner) return;

    len = 0;
    while (msg[len] != '\0') len++;

    if (self->tc_Node.ln_Type == NT_PROCESS) {   /* Output() needs a Process */
        out = Output();
        if (out != (BPTR)0) {
            Write(out, (CONST APTR)msg, len);
        }
    }

    if (g_log_file != (BPTR)0) {
        Write(g_log_file, (CONST APTR)msg, len);
        Flush(g_log_file);
    }

    /* the sink sends through lwIP: only on the owner task that armed it */
    if (g_log_sink != NULL && g_log_owner != NULL) {
        g_log_sink(msg);
    }
}

BOOL tn_log_open_file(const char *path)
{
    struct Process *pr = (struct Process *)FindTask(NULL);
    APTR old_wp = NULL;
    BPTR fh = (BPTR)0;

    tn_log_set_owner();   /* 5.1: the file belongs to the opening (daemon) task */
    if (g_log_file != (BPTR)0) {
        Close(g_log_file);
        g_log_file = (BPTR)0;
    }
    if (path == NULL || path[0] == '\0') return FALSE;

    /* A bad path must never raise an "insert volume" requester (W3 item 6). */
    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        old_wp = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;
    }
    fh = Open((CONST_STRPTR)path, MODE_READWRITE);
    if (fh == (BPTR)0 && IoErr() == ERROR_OBJECT_NOT_FOUND) {
        fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    }
    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        pr->pr_WindowPtr = old_wp;
    }
    if (fh == (BPTR)0) return FALSE;

    Seek(fh, 0, OFFSET_END);
    g_log_file = fh;
    return TRUE;
}

void tn_log_close_file(void)
{
    if (g_log_file != (BPTR)0) {
        Close(g_log_file);
        g_log_file = (BPTR)0;
    }
}

/* the formatter lives in the pure, host-testable log_format.c */


void tn_logf(int tier, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    if (fmt == NULL) return;
    if (tier > g_log_level) return;
    if (g_log_dos == NULL) return;

    va_start(ap, fmt);
    tn_logf_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    tn_log(tier, buf);
}
