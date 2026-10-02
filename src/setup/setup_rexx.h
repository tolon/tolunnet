/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — TolunnetSetup ARexx Host Port
 *
 * Exposes the TOLUNNETSETUP public MsgPort for scripted bench automation:
 * Commands: PAGE <n>, NEXT, BACK, SELECT <n>, TEST, FINISH, CANCEL, STATUS
 */

#ifndef TOLUNNET_SETUP_REXX_H
#define TOLUNNET_SETUP_REXX_H

#include "setup_types.h"

#ifdef __AMIGA__
#include <exec/ports.h>

struct MsgPort *tn_setup_rexx_init(void);
void tn_setup_rexx_cleanup(struct MsgPort *port);
void tn_setup_rexx_process(struct MsgPort *port, WizardState *ws, void (*on_refresh)(void),
                           void (*on_test)(void));
/* Reply a port message; a real RexxMsg also gets rc in rm_Result1 */
void tn_setup_rexx_reply(struct Message *msg, LONG rc);

#endif

#endif /* TOLUNNET_SETUP_REXX_H */
