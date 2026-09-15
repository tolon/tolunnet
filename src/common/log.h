/*
 * tolunnet logging interface — DOS Write/Output (master prompt §3: no stdio in
 * resident code).
 */
#ifndef TOLUNNET_LOG_H
#define TOLUNNET_LOG_H

#include <exec/types.h>
#include <dos/dos.h>

#define TN_LOG_OFF      0
#define TN_LOG_BASIC    1
#define TN_LOG_VERBOSE  2

/* Global DOSBase pointer, log file handle, and current verbosity level. */
extern struct Library *g_log_dos;
extern BPTR            g_log_file;
extern int             g_log_level;

/* TNET-108: optional per-line sink (daemon registers tn_syslog_sink to
 * forward its log via UDP-514). NULL in every other binary; called after the
 * console/file writes so a throwing sink cannot swallow the local log. */
extern void (*g_log_sink)(const char *msg);

/* Write a raw string to current Output() and optional log file. */
void tn_log(int tier, const char *msg);

/* Formatted log to Output() (no libc stdio; supports %s, %d, %u, %x, %X, %02x, %p). */
void tn_logf(int tier, const char *fmt, ...);

/* TNET-108: live log redirection (RECONFIG LOG=).
 * tn_log_open_file closes any open log file, opens path for append
 * (requester-suppressed: a bad path never pops "insert volume"), seeks to
 * end and installs it as g_log_file. Empty path just closes. Returns TRUE
 * when the file is open on return. */
BOOL tn_log_open_file(const char *path);
void tn_log_close_file(void);

/* TNET-139 DIAG: fixed 16-line ring capture of every logged line
 * (pre-tier-filter). Enabled by tn_log_ring_enable(); read by the crash
 * handler via tn_log_ring_snapshot()/tn_log_ring_count(). */
#define TN_LOG_RING_LINES 16
#define TN_LOG_RING_LEN   96
void tn_log_ring_enable(void);
const char *const *tn_log_ring_snapshot(void);
int tn_log_ring_count(void);

#endif /* TOLUNNET_LOG_H */
