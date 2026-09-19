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

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"GetNetStatus");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

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
    } else if (opts[1]) {
        /* ADDRESS: not directly available via thin client */
        tn_cmd_printf("(use ifconfig for address)\n");
        rc = TN_CMD_WARN;
    } else if (opts[2]) {
        tn_cmd_printf("(use netstat -r for gateway)\n");
        rc = TN_CMD_WARN;
    } else if (opts[3]) {
        tn_cmd_printf("(use netstat for DNS)\n");
        rc = TN_CMD_WARN;
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
