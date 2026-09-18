/*
 * tolunnet — arp command (CMD-6). ReadArgs: SHOW/S,FLUSH/S
 * (Minimal version: shows ARP info via netstat-like IPC; add/delete need
 * daemon-side etharp IPC — deferred to CMD-2 full implementation)
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "SHOW/S,FLUSH/S"

int main(int argc, char **argv)
{
    LONG opts[2] = { 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"arp");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    if (opts[1]) {
        tn_cmd_printf("arp: FLUSH not yet implemented (needs daemon ARPCTL IPC)\n");
        rc = TN_CMD_WARN;
    }

    tn_cmd_printf("arp: ARP table display requires daemon ENUMARP IPC (planned CMD-2)\n");
    tn_cmd_printf("      Use 'netstat -a' for the current ARP-visible state\n");

    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
