/*
 * tolunnet — DeleteNetRoute command (CLOSE §B.5, Roadshow-style wrapper).
 * ReadArgs: DEST/A,MASK/K — deletes the matching static route via ROUTECTL.
 *   DeleteNetRoute 10.9.0.0 MASK 255.255.0.0
 */
#include "cmdlib.h"
#include "../common/ipc_client.h"
#include "../../include/ipc.h"
#include <string.h>

#define TEMPLATE "DEST/A,MASK/K"

int main(int argc, char **argv)
{
    LONG opts[2] = { 0, 0 };
    struct RDArgs *rdargs;
    TnIpcMsg msg;
    LONG args[6];
    ULONG dest, mask = 0xFFFFFFFFUL;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"DeleteNetRoute");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    dest = tn_call_inet_addr((const char *)opts[0]);
    if (dest == INADDR_NONE) {
        tn_cmd_printf("DeleteNetRoute: bad DEST\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_USAGE;
    }
    if (opts[1] != 0) {
        mask = tn_call_inet_addr((const char *)opts[1]);
        if (mask == INADDR_NONE) {
            tn_cmd_printf("DeleteNetRoute: bad MASK\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
    }

    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = TN_ROUTECTL_DELETE;
    args[1] = (LONG)dest;
    args[2] = (LONG)mask;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, NULL, 0, &msg) != 0 ||
        msg.result != 0) {
        tn_cmd_printf("DeleteNetRoute: failed (not found?)\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    tn_cmd_printf("DeleteNetRoute: ok\n");

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return TN_CMD_OK;
}
