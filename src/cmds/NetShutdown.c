/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — NetShutdown command (CLOSE §B.8; z.ai step 9a-2 item 1).
 * ReadArgs: FORCE/S — stops the daemon via TN_IPC_CMD_STOP over the IPC
 * port, exactly like TolunnetControl STOP: no bsdsocket.library is
 * opened, the interface is never taken down, no CTRL_C signal is sent.
 *
 * EBUSY means programs still hold bsdsocket.library: the network stays
 * up, RC 5. Success: the daemon port disappears within 5 s, RC 0.
 * FORCE only skips the extra hint text.
 */
#include <exec/types.h>
#include <exec/ports.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include <errno.h>
#include "../../include/ipc.h"
#include "../common/ipc_client.h"

#define TEMPLATE "FORCE/S"

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    TnIpcMsg msg;
    int waits;
    int rc;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"NetShutdown");
        return 20;
    }

    if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) == NULL) {
        Printf((CONST_STRPTR)"NetShutdown: tolunnet daemon is not running\n");
        FreeArgs(rdargs);
        return 5;
    }

    {
        int res = tn_ipc_oneshot(TN_IPC_CMD_STOP, NULL, 0, &msg);
        if (res != 0 || msg.result != 0) {
            if (msg.err_no == EBUSY) {
                LONG n = -1;
                {
                    static TnSocketInfo socks[32];
                    LONG sargs[1];
                    APTR sptrs[1];
                    TnIpcMsg emsg;
                    sargs[0] = 32;
                    sptrs[0] = (APTR)socks;
                    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ENUMSOCKETS, sargs, 1,
                                          sptrs, 1, &emsg) == 0 &&
                        emsg.result >= 0) {
                        n = emsg.result;
                    }
                }
                if (n >= 0) {
                    Printf((CONST_STRPTR)"NetShutdown: %ld program(s) still use the network; close them first\n",
                           n);
                } else {
                    Printf((CONST_STRPTR)"NetShutdown: programs still use the network; close them first\n");
                }
                if (opts[0] == 0) {
                    Printf((CONST_STRPTR)"NetShutdown: the daemon reaps dead clients automatically; retry after closing them\n");
                }
            } else {
                Printf((CONST_STRPTR)"NetShutdown: daemon stop failed\n");
            }
            FreeArgs(rdargs);
            return 5;
        }
    }

    /* the daemon port must disappear within 5 s */
    for (waits = 0; waits < 20; waits++) {
        if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) == NULL) break;
        Delay(5); /* 0.25 s */
    }
    if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
        Printf((CONST_STRPTR)"NetShutdown: daemon did not exit within 5 s\n");
        FreeArgs(rdargs);
        return 5;
    }

    Printf((CONST_STRPTR)"NetShutdown: stopped\n");
    rc = 0;
    FreeArgs(rdargs);
    return rc;
}
