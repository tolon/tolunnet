/*
 * tolunnet — Offline command (CLOSE §B.7, Roadshow-style).
 * ReadArgs: NAME — takes the named interface down (default: primary).
 */
#include "ifctl_cmd.h"
#include <string.h>

#define TEMPLATE "NAME"

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    TnIpcMsg msg;
    LONG idx = -1;
    LONG rc;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"Offline");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    rc = ifctl_call(TN_IFCTL_DOWN, idx, 0, 0, 0, NULL, 0, &msg);
    if (rc != 0 || msg.result != 0) {
        tn_cmd_printf("Offline: interface not available\n");
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    tn_cmd_printf("Offline: primary interface down\n");

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return TN_CMD_OK;
}
