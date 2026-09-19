/*
 * tolunnet — route command (CLOSE §B.5).
 * ReadArgs: SHOW/S,ADD/S,DEST,NETMASK,GATEWAY,DELETE/S,DEFAULT/S
 *   route                          (same as SHOW)
 *   route SHOW
 *   route ADD 10.0.0.0 NETMASK 255.0.0.0 GATEWAY 10.0.2.1
 *   route DELETE 10.0.0.0 NETMASK 255.0.0.0
 *   route DEFAULT GATEWAY 10.0.2.1
 * Talks TN_IPC_CMD_ROUTECTL to the daemon (route table lives there and
 * feeds the lwIP routing hooks).
 */
#include "cmdlib.h"
#include "../common/ipc_client.h"
#include "../../include/ipc.h"
#include "../task/route.h" /* TN_MAX_ROUTES */
#include <string.h>

#define TEMPLATE "SHOW/S,ADD/S,DEST/K,NETMASK/K,GATEWAY/K,DELETE/S,DEFAULT/S"

static void fmt_ip(char *out, ULONG a_net)
{
    ULONG a = ntohl(a_net);
    ULONG o[4];
    int k, pos = 0;
    o[0] = (a >> 24) & 0xFF; o[1] = (a >> 16) & 0xFF;
    o[2] = (a >> 8) & 0xFF;  o[3] = a & 0xFF;
    for (k = 0; k < 4; k++) {
        if (o[k] >= 100) {
            out[pos++] = (char)('0' + (o[k] / 100) % 10);
            out[pos++] = (char)('0' + (o[k] / 10) % 10);
            out[pos++] = (char)('0' + o[k] % 10);
        } else if (o[k] >= 10) {
            out[pos++] = (char)('0' + (o[k] / 10) % 10);
            out[pos++] = (char)('0' + o[k] % 10);
        } else {
            out[pos++] = (char)('0' + o[k]);
        }
        if (k < 3) out[pos++] = '.';
    }
    out[pos] = '\0';
}

static LONG route_ctl(LONG op, ULONG dest, ULONG mask, ULONG gw,
                      TnRouteInfo *rows, LONG cap)
{
    TnIpcMsg msg;
    LONG args[6];
    APTR ptrs[1];

    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    args[0] = op;
    args[1] = (LONG)dest;
    args[2] = (LONG)mask;
    args[3] = (LONG)gw;
    args[4] = cap;
    ptrs[0] = rows;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, args, 5, ptrs, 1, &msg) != 0) {
        return -100; /* daemon unreachable */
    }
    return msg.result;
}

int main(int argc, char **argv)
{
    LONG opts[7] = { 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    static TnRouteInfo rows[TN_MAX_ROUTES];
    char b1[16], b2[16], b3[16];

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"route");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    if (opts[1]) { /* ADD DEST [NETMASK] GATEWAY */
        const char *dest_s = (const char *)opts[2];
        const char *mask_s = (const char *)opts[3];
        const char *gw_s = (const char *)opts[4];
        ULONG dest, mask = 0xFFFFFFFFUL, gw = 0;

        if (dest_s == NULL) {
            tn_cmd_printf("route: ADD needs DEST\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
        dest = tn_call_inet_addr(dest_s);
        if (dest == INADDR_NONE) {
            tn_cmd_printf("route: bad DEST address\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
        if (mask_s != NULL) {
            mask = tn_call_inet_addr(mask_s);
            if (mask == INADDR_NONE) {
                tn_cmd_printf("route: bad NETMASK\n");
                FreeArgs(rdargs); tn_cmd_fini();
                return TN_CMD_USAGE;
            }
        }
        if (gw_s != NULL) {
            gw = tn_call_inet_addr(gw_s);
            if (gw == INADDR_NONE) {
                tn_cmd_printf("route: bad GATEWAY\n");
                FreeArgs(rdargs); tn_cmd_fini();
                return TN_CMD_USAGE;
            }
        }
        {
            LONG r = route_ctl(TN_ROUTECTL_ADD, dest, mask, gw, NULL, 0);
            if (r != 0) {
                tn_cmd_printf("route: add failed (daemon rc %ld)\n", r);
                rc = TN_CMD_FAIL;
            } else {
                tn_cmd_printf("route: added\n");
            }
        }
    } else if (opts[5]) { /* DELETE DEST [NETMASK] */
        const char *dest_s = (const char *)opts[2];
        const char *mask_s = (const char *)opts[3];
        ULONG dest, mask = 0xFFFFFFFFUL;

        if (dest_s == NULL) {
            tn_cmd_printf("route: DELETE needs DEST\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
        dest = tn_call_inet_addr(dest_s);
        if (dest == INADDR_NONE) {
            tn_cmd_printf("route: bad DEST address\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
        if (mask_s != NULL) mask = tn_call_inet_addr(mask_s);
        {
            LONG r = route_ctl(TN_ROUTECTL_DELETE, dest, mask, 0, NULL, 0);
            if (r != 0) {
                tn_cmd_printf("route: delete failed (not found?)\n");
                rc = TN_CMD_FAIL;
            } else {
                tn_cmd_printf("route: deleted\n");
            }
        }
    } else if (opts[6]) { /* DEFAULT GATEWAY gw */
        const char *gw_s = (const char *)opts[4];
        ULONG gw;
        if (gw_s == NULL) {
            tn_cmd_printf("route: DEFAULT needs GATEWAY\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
        gw = tn_call_inet_addr(gw_s);
        if (gw == INADDR_NONE) {
            tn_cmd_printf("route: bad GATEWAY\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_USAGE;
        }
        {
            LONG r = route_ctl(TN_ROUTECTL_ADD, 0, 0, gw, NULL, 0);
            if (r != 0) {
                tn_cmd_printf("route: default add failed (daemon rc %ld)\n", r);
                rc = TN_CMD_FAIL;
            } else {
                tn_cmd_printf("route: default added\n");
            }
        }
    }

    /* SHOW (also the default action) */
    if (rc == TN_CMD_OK || opts[0]) {
        LONG n = route_ctl(TN_ROUTECTL_LIST, 0, 0, 0, rows, TN_MAX_ROUTES);
        if (n < 0) {
            tn_cmd_printf("route: daemon unreachable\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        if (n == 0) {
            tn_cmd_printf("(no static routes)\n");
        } else {
            LONG i;
            tn_cmd_printf("Destination    Netmask        Gateway\n");
            for (i = 0; i < n; i++) {
                fmt_ip(b1, rows[i].dest);
                fmt_ip(b2, rows[i].mask);
                fmt_ip(b3, rows[i].gw);
                tn_cmd_printf("%-14s %-14s %s\n", b1, b2, b3);
            }
        }
    }

    (void)argc;
    (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
