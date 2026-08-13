/*
 * tolunet logging — DOS-based, no stdio (master prompt §3).
 *
 * Used by the network task and (later) the library for diagnostic output.
 * Tiers per §9: 0 = off (default), 1 = basic, 2 = verbose. The ENV variable
 * TOLUNET_DEBUG mirrors the config DEBUG key (§5.2).
 *
 * M0: interface defined, a thin implementation provided. The task wires its
 * own DOSBase in M2; until then this compiles standalone but is not called by
 * the hello-task (which has its own minimal DOS use).
 */
#ifndef TOLUNET_LOG_H
#define TOLUNET_LOG_H

#include <exec/types.h>

/* Debug tiers (mirror TN_DEBUG_* in include/tolunet/config.h). */
#define TN_LOG_OFF    0
#define TN_LOG_BASIC  1
#define TN_LOG_VERBOSE 2

/*
 * Initialised by the task at startup. The task opens dos.library into
 * g_log_dos and sets g_log_level from config/ENV before any logging happens.
 */
extern struct Library *g_log_dos;
extern int             g_log_level;

/* tn_log: emit a line at the given tier (no-op if tier > g_log_level). */
void tn_log(int tier, const char *msg);

#endif /* TOLUNET_LOG_H */
