/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Stack Detection & Migration Engine
 *
 * Detects Miami, MiamiDx, Roadshow, AmiTCP, and Genesis.
 * Handles backup, commenting out, renaming, and importing settings.
 */

#ifndef TOLUNNET_STACK_DETECT_H
#define TOLUNNET_STACK_DETECT_H

#include "setup_types.h"

/* 1.1/1.8: the wizard's own undo script (never S:tolunnet-undo, which
 * is the installer's) and the TolunnetSetup argument it runs to put
 * the disabled startup lines back. */
#define TN_UNDO_STACKS_SCRIPT "S:tolunnet-undo-stacks"
#define TN_UNDO_STACKS_ARG    "UNDOSTACKS"

/* Detect any installed or running TCP/IP stacks */
void tn_stack_detect_all(WizardState *ws);

/* Apply replacement: comment startup lines, rename .library & .info, import settings */
BOOL tn_stack_apply_replacement(WizardState *ws);

/* Undo replacement: restore lines, restore renamed files; on full
 * success S:tolunnet-undo-stacks is deleted (nothing left to undo) */
BOOL tn_stack_undo_replacement(void);

/* Ask running stacks to quit gracefully (ARexx QUIT, NetShutdown) */
void tn_stack_request_quit(WizardState *ws);

/* Pure string manipulation helpers (used by host unit tests) */
int tn_parse_startup_script(const char *content, char *out_buf, int out_max, int *disabled_count);
int tn_uncomment_startup_script(const char *content, char *out_buf, int out_max, int *restored_count);

#endif /* TOLUNNET_STACK_DETECT_H */
