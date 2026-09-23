/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — AddNetRoute command (CLOSE §B.5, Roadshow-style wrapper).
 * ReadArgs: DEST/A,MASK/K,GATEWAY/K — adds a static route via ROUTECTL.
 *   AddNetRoute 10.9.0.0 MASK 255.255.0.0 GATEWAY 10.0.2.1
 */
#include "cmdlib.h"
#include "../common/ipc_client.h"
#include "../../include/ipc.h"
#include <string.h>

#define TEMPLATE "DEST/A,MASK/K,GATEWAY/K"

int main(int argc, char **argv)
{
    LONG opts[3] = { 0, 0, 0 };
    struct RDArgs *rdargs;
    TnIpcMsg msg;
    LONG args[6];
    ULONG dest, mask = 0xFFFFFFFFUL, gw = 0;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"AddNetRoute");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    dest = tn_call_inet_addr((const char *)opts[0]);
    if (dest == INADDR_NONE) {
        tn_cmd_printf("AddNetRoute: bad DEST\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_USAGE;
    }
    if (opts[1] != 0) {
        mask = tn_call_inet_addr((const char *)opts[1]);
        if (mask == INADDR_NONE) {
            tn_cmd_printf("AddNetRoute: bad MASK\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
    }
    if (opts[2] != 0) {
        gw = tn_call_inet_addr((const char *)opts[2]);
        if (gw == INADDR_NONE) {
            tn_cmd_printf("AddNetRoute: bad GATEWAY\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
    }

    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_ROUTECTL_ADD;
    args[1] = (LONG)dest;
    args[2] = (LONG)mask;
    args[3] = (LONG)gw;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, NULL, 0, &msg) != 0 ||
        msg.result != 0) {
        tn_cmd_printf("AddNetRoute: failed\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    tn_cmd_printf("AddNetRoute: ok\n");

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return TN_CMD_OK;
}
