/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — RFC3164 syslog forwarding (Round 4 §D3, TNET-108).
 *
 * SYSLOG=host in DEVS:tolunnet.config arms the daemon to mirror every log
 * line it emits as a UDP-514 datagram to that host (dotted quad resolves
 * immediately; a hostname resolves via DNS and arms on completion). Empty
 * value disarms. The forwarding itself lives in tn_syslog_sink, registered
 * as the g_log_sink by the daemon only — client binaries never link this in.
 */
#ifndef TOLUNNET_SYSLOG_H
#define TOLUNNET_SYSLOG_H

#include "task_ctx.h"

/* Arm/disarm forwarding. Returns FALSE only on definite failure (unusable
 * pcb, hostname rejected by the resolver); asynchronous resolution counts
 * as success and arms from the DNS callback. */
BOOL tn_syslog_apply(const char *host);

/* Log sink: forward one already-formatted daemon log line. Silent on every
 * failure by design — it runs inside the logging path and must not recurse. */
void tn_syslog_sink(const char *msg);

/* Release the UDP pcb (daemon shutdown / RECONFIG disable). */
void tn_syslog_shutdown(void);

#endif /* TOLUNNET_SYSLOG_H */
