/*
 * tolunnet — Online command (CLOSE §B.7, Roadshow-style).
 * ReadArgs: NAME — brings the named interface up (default: primary).
 * NAME may be "-" or "ALL" for the primary interface.
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
        PrintFault(IoErr(), (CONST_STRPTR)"Online");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }
    if (opts[0] != 0) {
        const char *nm = (const char *)opts[0];
        if (strcmp(nm, "-") != 0 && strcmp(nm, "ALL") != 0 &&
            strcmp(nm, "eth0") != 0 && strcmp(nm, "net0") != 0) {
            tn_cmd_printf("Online: tolunnet runs a single interface; '%s' treated as primary\n", nm);
        }
    }

    rc = ifctl_call(TN_IFCTL_UP, idx, 0, 0, 0, NULL, 0, &msg);
    if (rc != 0 || msg.result != 0) {
        tn_cmd_printf("Online: interface not available\n");
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    tn_cmd_printf("Online: primary interface up\n");

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return TN_CMD_OK;
}
