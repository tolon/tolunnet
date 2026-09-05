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

/* Write a raw string to current Output() and optional log file. */
void tn_log(int tier, const char *msg);

/* Formatted log to Output() (no libc stdio; supports %s, %d, %u, %x, %X, %02x, %p). */
void tn_logf(int tier, const char *fmt, ...);

#endif /* TOLUNNET_LOG_H */
