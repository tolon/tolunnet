/*
 * tolunnet — TolunnetSetup ARexx Host Port
 *
 * Exposes the TOLUNNETSETUP public MsgPort for scripted bench automation:
 * Commands: PAGE <n>, NEXT, BACK, SELECT <n>, FINISH, CANCEL, STATUS
 */

#include "setup_rexx.h"

#ifdef __AMIGA__
#include <proto/exec.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define REXX_PORT_NAME "TOLUNNETSETUP"

struct MsgPort *tn_setup_rexx_init(void)
{
    Forbid();
    if (FindPort((CONST_STRPTR)REXX_PORT_NAME)) {
        Permit();
        return NULL;
    }
    struct MsgPort *port = CreateMsgPort();
    if (port) {
        port->mp_Node.ln_Name = (char *)REXX_PORT_NAME;
        port->mp_Node.ln_Pri = 0;
        AddPort(port);
    }
    Permit();
    return port;
}

void tn_setup_rexx_cleanup(struct MsgPort *port)
{
    if (!port) return;
    Forbid();
    RemPort(port);
    Permit();

    /* Drain any pending messages */
    struct Message *msg;
    while ((msg = GetMsg(port))) {
        ReplyMsg(msg);
    }
    DeleteMsgPort(port);
}

void tn_setup_rexx_process(struct MsgPort *port, WizardState *ws, void (*on_refresh)(void))
{
    if (!port || !ws) return;

    struct Message *msg;
    while ((msg = GetMsg(port))) {
        /*
         * Extract command string. Could be RexxMsg (ARG0) or plain Message with ln_Name.
         */
        const char *cmd = NULL;

        /* If ln_Name is set, use it */
        if (msg->mn_Node.ln_Name && msg->mn_Node.ln_Name[0] != '\0') {
            cmd = msg->mn_Node.ln_Name;
        } else {
            /* Check if rm_Args[0] is present (RexxMsg structure layout) */
            char **args = (char **)((char *)msg + sizeof(struct Message) + 8);
            if (args && args[0]) {
                cmd = args[0];
            }
        }

        if (cmd) {
            char verb[32];
            int arg_num = 0;
            if (sscanf(cmd, "%31s %d", verb, &arg_num) >= 1) {
                if (strcasecmp(verb, "NEXT") == 0) {
                    if (ws->current_page < WIZARD_PAGE_COUNT - 1) {
                        ws->current_page++;
                        /* If advancing to WiFi page but hardware is wired, skip to Address */
                        if (ws->current_page == WIZARD_PAGE_WIFI) {
                            if (ws->selected_hw_idx >= 0 && ws->selected_hw_idx < ws->hw_count) {
                                if (!ws->hw[ws->selected_hw_idx].is_wireless) {
                                    ws->current_page = WIZARD_PAGE_ADDRESS;
                                }
                            }
                        }
                    }
                    if (on_refresh) on_refresh();
                } else if (strcasecmp(verb, "BACK") == 0) {
                    if (ws->current_page > 0) {
                        ws->current_page--;
                        if (ws->current_page == WIZARD_PAGE_WIFI) {
                            if (ws->selected_hw_idx >= 0 && ws->selected_hw_idx < ws->hw_count) {
                                if (!ws->hw[ws->selected_hw_idx].is_wireless) {
                                    ws->current_page = WIZARD_PAGE_HW;
                                }
                            }
                        }
                    }
                    if (on_refresh) on_refresh();
                } else if (strcasecmp(verb, "PAGE") == 0) {
                    if (arg_num >= 0 && arg_num < WIZARD_PAGE_COUNT) {
                        ws->current_page = arg_num;
                    }
                    if (on_refresh) on_refresh();
                } else if (strcasecmp(verb, "SELECT") == 0) {
                    if (ws->current_page == WIZARD_PAGE_HW) {
                        if (arg_num >= 0 && arg_num < ws->hw_count) {
                            ws->selected_hw_idx = arg_num;
                        }
                    } else if (ws->current_page == WIZARD_PAGE_WIFI) {
                        if (arg_num >= 0 && arg_num < ws->wifi_count) {
                            ws->selected_wifi_idx = arg_num;
                        }
                    }
                    if (on_refresh) on_refresh();
                } else if (strcasecmp(verb, "FINISH") == 0) {
                    ws->rexx_done = TRUE;
                    ws->rexx_finish_msg = msg;
                    continue; /* ReplyMsg will be sent after apply_wizard_finish() completes */
                } else if (strcasecmp(verb, "CANCEL") == 0 || strcasecmp(verb, "QUIT") == 0) {
                    ws->rexx_cancel = TRUE;
                } else if (strcasecmp(verb, "STATUS") == 0) {
                    /* Handled */
                }
            }
        }

        ReplyMsg(msg);
    }
}

#endif
