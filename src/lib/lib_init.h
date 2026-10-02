/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — bsdsocket.library initialization and lifecycle management.
 */
#ifndef TOLUNNET_LIB_INIT_H
#define TOLUNNET_LIB_INIT_H

#include <exec/types.h>
#include <exec/libraries.h>

/* Create and add bsdsocket.library to Exec's library list */
struct Library *tn_lib_create(void);

/* Remove and free bsdsocket.library from Exec's library list */
void tn_lib_destroy(struct Library *lib);

/* TN-bugtrack 5.1: forward one preformatted log line from a library client
 * task to the daemon log (TN_IPC_CMD_SYSLOG); gated on g_log_level. */
struct TnSocketBase;
void tn_lib_log(struct TnSocketBase *base, int tier, const char *line);

#endif /* TOLUNNET_LIB_INIT_H */
