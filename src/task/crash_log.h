/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — DIAG crash capture (TN-bugtrack-2 item 1 / TNET-139).
 *
 * When the daemon runs with DIAG=YES it arms a task-local CPU trap handler
 * (tc_TrapCode) for the bus/address/illegal vectors. On a fault, before the
 * Guru appears, the handler writes PC, SR, fault address, the raw exception
 * frame, the register set, the recorded segment bases (SegTracker-style)
 * and the last 16 log lines to RAM:tolunnet-crash.log, then chains to the
 * previous handler so the machine still shows its normal Software Failure.
 */
#ifndef TOLUNNET_CRASH_LOG_H
#define TOLUNNET_CRASH_LOG_H

#include "../../include/ipc.h"

void tn_crash_arm(void);          /* install handler + record segments */
const char *tn_crash_last(void);  /* path of the crash log (diagnostics) */

#endif /* TOLUNNET_CRASH_LOG_H */
