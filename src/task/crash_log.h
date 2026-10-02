/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — DIAG crash capture (TN-bugtrack-2 item 1 / TNET-139).
 *
 * When the daemon runs with DIAG=YES it arms a task-local CPU trap handler
 * (tc_TrapCode) for the CPU exception vectors. On a fault, before the
 * Guru appears, the handler formats PC, SR, fault address, the raw exception
 * frame, the register set, the recorded segment bases (SegTracker-style)
 * and the last 16 log lines into the static buffer tn_crash_report, then
 * chains to the previous handler so the machine still shows its normal
 * Software Failure. No DOS from trap context: the log file is written only
 * by tn_crash_flush_pending() from task context, i.e. only if the task
 * survives the chained handler. Otherwise the report stays in memory at
 * tn_crash_report (starts "tolunnet DIAG crash report") for a monitor.
 */
#ifndef TOLUNNET_CRASH_LOG_H
#define TOLUNNET_CRASH_LOG_H

#include "../../include/ipc.h"

void tn_crash_arm(void);          /* install handler + record segments */
void tn_crash_arm_path(const char *path); /* arm with a custom log path */
const char *tn_crash_last(void);  /* path of the crash log (diagnostics) */
void tn_crash_flush_pending(void); /* task context: write a pending report */

extern char tn_crash_report[];    /* last formatted report (NUL-terminated) */

#endif /* TOLUNNET_CRASH_LOG_H */
