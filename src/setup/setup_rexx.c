/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — TolunnetSetup ARexx Host Port
 *
 * Exposes the TOLUNNETSETUP public MsgPort for scripted bench automation:
 * Commands: PAGE <n>, NEXT, BACK, SELECT <n>, FINISH, CANCEL, STATUS
 *
 * 7.7: real ARexx RexxMsgs (IsRexxMsg, command in ARG0, rc in
 * rm_Result1) and the bench's plain Messages (command in ln_Name,
 * only sizeof(struct Message) allocated - never touch rm_* there)
 * are both accepted.
 */

#include "setup_rexx.h"

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#include <rexx/errors.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define REXX_PORT_NAME "TOLUNNETSETUP"

/* opened by tn_setup_rexx_init; NULL = fall back to the name check */
struct RxsLib *RexxSysBase = NULL;

static BOOL is_rexx_msg(struct Message *msg)
{
    if (RexxSysBase != NULL) {
        return IsRexxMsg((struct RexxMsg *)msg) ? TRUE : FALSE;
    }
    /* no rexxsyslib: rexxsyslib-built messages are named "REXX" and
     * are a full RexxMsg long */
    return (msg->mn_Node.ln_Name != NULL &&
            strcmp(msg->mn_Node.ln_Name, "REXX") == 0 &&
            msg->mn_Length >= sizeof(struct RexxMsg)) ? TRUE : FALSE;
}

void tn_setup_rexx_reply(struct Message *msg, LONG rc)
{
    if (msg == NULL) return;
    if (is_rexx_msg(msg)) {
        struct RexxMsg *rm = (struct RexxMsg *)msg;
        rm->rm_Result1 = rc;
        rm->rm_Result2 = 0;   /* no result string */
    }
    ReplyMsg(msg);
}

struct MsgPort *tn_setup_rexx_init(void)
{
    /* allocate outside Forbid; only the name check + AddPort are atomic */
    struct MsgPort *port = CreateMsgPort();
    BOOL taken;
    if (port == NULL) return NULL;
    port->mp_Node.ln_Name = (char *)REXX_PORT_NAME;
    port->mp_Node.ln_Pri = 0;
    Forbid();
    taken = (FindPort((CONST_STRPTR)REXX_PORT_NAME) != NULL) ? TRUE : FALSE;
    if (!taken) AddPort(port);
    Permit();
    if (taken) {
        DeleteMsgPort(port);
        return NULL;
    }
    if (port && RexxSysBase == NULL) {
        RexxSysBase = (struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library", 0);
    }
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
        tn_setup_rexx_reply(msg, RC_ERROR);
    }
    DeleteMsgPort(port);

    if (RexxSysBase != NULL) {
        CloseLibrary((struct Library *)RexxSysBase);
        RexxSysBase = NULL;
    }
}

void tn_setup_rexx_process(struct MsgPort *port, WizardState *ws, void (*on_refresh)(void),
                           void (*on_test)(void))
{
    if (!port || !ws) return;

    struct Message *msg;
    while ((msg = GetMsg(port))) {
        const char *cmd = NULL;
        LONG rc = RC_ERROR;     /* unknown / unparsable command */

        if (is_rexx_msg(msg)) {
            cmd = (const char *)ARG0((struct RexxMsg *)msg);
        } else if (msg->mn_Node.ln_Name && msg->mn_Node.ln_Name[0] != '\0') {
            cmd = msg->mn_Node.ln_Name;   /* bench: hand-built Message */
        }

        if (cmd) {
            char verb[32];
            int arg_num = 0;
            if (sscanf(cmd, "%31s %d", verb, &arg_num) >= 1) {
                rc = RC_OK;
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
                } else if (strcasecmp(verb, "IPMODE") == 0) {
                    if (arg_num == 0 || arg_num == 1) {
                        ws->ip_mode = arg_num;
                    }
                    if (on_refresh) on_refresh();
                } else if (strcasecmp(verb, "SELECT") == 0) {
                    if (ws->current_page == WIZARD_PAGE_HW) {
                        if (arg_num >= 0 && arg_num < ws->hw_count) {
                            ws->selected_hw_idx = arg_num;
                        }
                    } else if (ws->current_page == WIZARD_PAGE_WIFI) {
                        if (arg_num >= 0 && arg_num < ws->wifi_count) {
                            /* 7.1: the SSID field is the one truth */
                            ws->selected_wifi_idx = arg_num;
                            strncpy(ws->wifi_ssid_str, ws->wifi[arg_num].ssid,
                                    sizeof(ws->wifi_ssid_str) - 1);
                            ws->wifi_ssid_str[sizeof(ws->wifi_ssid_str) - 1] = '\0';
                        }
                    }
                    if (on_refresh) on_refresh();
                } else if (strcasecmp(verb, "FINISH") == 0) {
                    if (ws->rexx_finish_msg != NULL) {
                        /* 7.7: one FINISH at a time - the parked one
                         * keeps its reply, this one fails at once */
                        tn_setup_rexx_reply(msg, RC_ERROR);
                        continue;
                    }
                    ws->rexx_done = TRUE;
                    ws->rexx_finish_msg = msg;
                    continue; /* replied after apply_wizard_finish() completes */
                } else if (strcasecmp(verb, "CANCEL") == 0 || strcasecmp(verb, "QUIT") == 0) {
                    ws->rexx_cancel = TRUE;
                } else if (strcasecmp(verb, "TEST") == 0) {
                    /* 11q item 2: run the Test-page checks on demand -
                     * blocks until they finish, then the reply goes out. */
                    if (on_test) on_test();
                } else if (strcasecmp(verb, "STATUS") == 0) {
                    /* Handled */
                } else {
                    rc = RC_ERROR;
                }
            }
        }

        tn_setup_rexx_reply(msg, rc);
    }
}

#endif
