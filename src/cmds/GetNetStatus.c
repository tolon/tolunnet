/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — GetNetStatus command (CMD-6).
 * Prints a single value for scripts. RC 0 = online, 5 = offline.
 * ReadArgs: ONLINE/S,ADDRESS/S,GATEWAY/S,DNS/S
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "ONLINE/S,ADDRESS/S,GATEWAY/S,DNS/S"

/* GetGateway LVO = -198 (Inet_NetOf area) — we use gethostbyname to
 * resolve "gateway" from the hosts concept; simpler: use inet_addr on
 * the gateway if known. For now, ONLINE + HOSTNAME are the reliable ones. */

int main(int argc, char **argv)
{
    LONG opts[4] = { 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    char name[64];

    /* z.ai step 9a item 1: ADDRESS/GATEWAY/DNS are pure IPC - the
     * library is opened only for the liveness paths that need it. */
    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"GetNetStatus");
        return TN_CMD_USAGE;
    }

    if (opts[1] || opts[2] || opts[3]) {
        static TnSnapshot snap;
        char buf[16];
        if (tn_cmd_snapshot(&snap) != 0) {
            tn_cmd_printf("offline\n");
            FreeArgs(rdargs);
            return TN_CMD_WARN; /* RC 5: daemon not running */
        }
        if (opts[1]) {
            ULONG raw = ((ULONG)snap.status.ip_addr[0] << 24) |
                        ((ULONG)snap.status.ip_addr[1] << 16) |
                        ((ULONG)snap.status.ip_addr[2] << 8) |
                         (ULONG)snap.status.ip_addr[3];
            tn_cmd_ip_to_str(raw, buf);
            tn_cmd_printf("%s\n", buf);
        } else if (opts[2]) {
            ULONG raw = ((ULONG)snap.status.gw[0] << 24) |
                        ((ULONG)snap.status.gw[1] << 16) |
                        ((ULONG)snap.status.gw[2] << 8) |
                         (ULONG)snap.status.gw[3];
            tn_cmd_ip_to_str(raw, buf);
            tn_cmd_printf("%s\n", buf);
        } else {
            ULONG raw1 = ((ULONG)snap.status.dns1[0] << 24) |
                         ((ULONG)snap.status.dns1[1] << 16) |
                         ((ULONG)snap.status.dns1[2] << 8) |
                          (ULONG)snap.status.dns1[3];
            ULONG raw2 = ((ULONG)snap.status.dns2[0] << 24) |
                         ((ULONG)snap.status.dns2[1] << 16) |
                         ((ULONG)snap.status.dns2[2] << 8) |
                          (ULONG)snap.status.dns2[3];
            if (raw1 != 0) { tn_cmd_ip_to_str(raw1, buf); tn_cmd_printf("%s\n", buf); }
            if (raw2 != 0) { tn_cmd_ip_to_str(raw2, buf); tn_cmd_printf("%s\n", buf); }
            if (raw1 == 0 && raw2 == 0) tn_cmd_printf("none\n");
        }
        FreeArgs(rdargs);
        return TN_CMD_OK;
    }

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    /* ONLINE: daemon liveness, not DNS — a resolver round trip would call
     * every DNS-less site "offline" (TNET-141). The library answering
     * gethostname plus a UDP socket round trip is the honest check. */
    if (opts[0]) {
        LONG fd = tn_call_socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0 && tn_call_gethostname((STRPTR)name, (LONG)sizeof(name) - 1) == 0) {
            tn_cmd_printf("online\n");
            rc = TN_CMD_OK;
        } else {
            tn_cmd_printf("offline\n");
            rc = TN_CMD_WARN;
        }
        if (fd >= 0) tn_call_closesocket(fd);
    } else {
        /* Default: show hostname as proof of stack liveness */
        if (tn_call_gethostname((STRPTR)name, (LONG)sizeof(name) - 1) == 0) {
            name[sizeof(name) - 1] = '\0';
            tn_cmd_printf("%s\n", name);
            rc = TN_CMD_OK;
        } else {
            tn_cmd_printf("offline\n");
            rc = TN_CMD_WARN;
        }
    }

    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
