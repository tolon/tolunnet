/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — hostname command (CMD-1). ReadArgs: HOSTNAME,SAVE/S
 */
#include "cmdlib.h"

#define TEMPLATE "HOSTNAME,SAVE/S"

int main(int argc, char **argv)
{
    LONG opts[2] = { 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    char name[64];

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"hostname");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    if (opts[1] != 0 && opts[0] == 0) {
        tn_cmd_printf("hostname: SAVE requires a NAME\n");
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    if (tn_call_gethostname((STRPTR)name, (LONG)sizeof(name) - 1) == 0) {
        name[sizeof(name) - 1] = '\0';
        tn_cmd_printf("%s\n", name);
    } else {
        tn_cmd_printf("hostname: unable to read hostname\n");
        rc = TN_CMD_FAIL;
    }

    if (opts[0] != 0) {
        tn_cmd_printf("(To change: edit HOSTNAME= in DEVS:tolunnet.config and RECONFIG)\n");
    }

    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
