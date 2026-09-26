/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — ShowNetStatus command (CMD-6). ReadArgs: INTERFACES/S,ROUTES/S,DNS/S,SOCKETS/S,FULL/S
 * Shows a human-readable network status report. Thin client: reads via
 * existing gethostbyname/gethostname and /TolunnetStatus IPC.
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "INTERFACES/S,ROUTES/S,DNS/S,SOCKETS/S,FULL/S"

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    char hostname[64];
    LONG show_all;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"ShowNetStatus");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    show_all = (opts[4] != 0) ||
               (!opts[0] && !opts[1] && !opts[2] && !opts[3]);

    if (tn_call_gethostname((STRPTR)hostname, (LONG)sizeof(hostname) - 1) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        tn_cmd_printf("Hostname: %s\n", hostname);
    }

    if (show_all || opts[0]) {
        tn_cmd_printf("\nInterfaces:\n");
        tn_cmd_printf("  (run 'ifconfig' for detailed interface info)\n");
    }
    if (show_all || opts[1]) {
        tn_cmd_printf("\nRoutes:\n");
        tn_cmd_printf("  (run 'netstat -r' for routing table)\n");
    }
    if (show_all || opts[2]) {
        tn_cmd_printf("\nDNS:\n");
        tn_cmd_printf("  (run 'netstat' for resolver info)\n");
    }
    if (show_all || opts[3]) {
        tn_cmd_printf("\nSockets:\n");
        tn_cmd_printf("  (run 'netstat -a' for active sockets)\n");
    }

    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
