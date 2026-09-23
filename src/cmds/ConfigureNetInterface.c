/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — ConfigureNetInterface command (CLOSE §B.7, Roadshow-style).
 * ReadArgs: NAME/M,ADDRESS/K,NETMASK/K,GATEWAY/K,DHCP/K
 * Applies live address changes to the primary interface via IFCTL SET.
 * DHCP=YES and DEVICE changes need a daemon restart (reported, RC 5).
 */
#include "ifctl_cmd.h"
#include <string.h>

#define TEMPLATE "NAME/M,ADDRESS/K,NETMASK/K,GATEWAY/K,DHCP/K"

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    TnIpcMsg msg;
    ULONG addr = 0, mask = 0, gw = 0;
    int rc = TN_CMD_OK;
    LONG r;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"ConfigureNetInterface");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    if (opts[1] != 0) {
        addr = tn_call_inet_addr((const char *)opts[1]);
        if (addr == INADDR_NONE) {
            tn_cmd_printf("ConfigureNetInterface: bad ADDRESS\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
    }
    if (opts[2] != 0) {
        mask = tn_call_inet_addr((const char *)opts[2]);
        if (mask == INADDR_NONE) {
            tn_cmd_printf("ConfigureNetInterface: bad NETMASK\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
    }
    if (opts[3] != 0) {
        gw = tn_call_inet_addr((const char *)opts[3]);
        if (gw == INADDR_NONE) {
            tn_cmd_printf("ConfigureNetInterface: bad GATEWAY\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
    }
    if (opts[4] != 0) {
        const char *d = (const char *)opts[4];
        tn_cmd_printf("ConfigureNetInterface: DHCP changes need a daemon restart\n");
        rc = TN_CMD_WARN;
    }
    if (addr == 0 && mask == 0 && gw == 0) {
        tn_cmd_printf("ConfigureNetInterface: nothing to set (ADDRESS/NETMASK/GATEWAY)\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    r = ifctl_call(TN_IFCTL_SET, -1, addr, mask, gw, NULL, 0, &msg);
    if (r != 0 || msg.result != 0) {
        tn_cmd_printf("ConfigureNetInterface: SET failed\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    tn_cmd_printf("ConfigureNetInterface: applied\n");

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
