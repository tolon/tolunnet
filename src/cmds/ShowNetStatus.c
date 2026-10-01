/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — ShowNetStatus command (CMD-6). ReadArgs: INTERFACES/S,ROUTES/S,DNS/S,SOCKETS/S,FULL/S
 * z.ai step 9a item 1: INTERFACES/ROUTES/DNS/SOCKETS print real daemon
 * state via the IPC snapshot — fixed columns, no placeholder text.
 */
#include "cmdlib.h"
#include <string.h>
TN_VERSTAG_DEF("ShowNetStatus");

#define TEMPLATE "INTERFACES/S,ROUTES/S,DNS/S,SOCKETS/S,FULL/S"

static void print_interfaces(const TnSnapshot *snap)
{
    LONG i;
    tn_cmd_printf("\nInterfaces:\n");
    tn_cmd_printf("%-8s %-15s %-15s %-15s %s\n", "Name", "Address", "Netmask", "Gateway", "Flags");
    for (i = 0; i < snap->if_count && i < TN_SNAP_MAX_IFS; i++) {
        const TnIfInfo *e = &snap->ifs[i];
        char a[16], m[16], g[16];
        if (!e->in_use) continue;
        tn_cmd_ip_to_str(e->addr, a);
        tn_cmd_ip_to_str(e->mask, m);
        tn_cmd_ip_to_str(e->gw, g);
        tn_cmd_printf("%-8s %-15s %-15s %-15s %s,%s\n",
                      e->name, a, m, g,
                      e->is_up ? "UP" : "DOWN",
                      e->is_dhcp ? "DHCP" : "STATIC");
    }
    if (snap->if_count <= 0)
        tn_cmd_printf("  (none reported)\n");
}

static void print_routes(const TnSnapshot *snap)
{
    LONG i;
    tn_cmd_printf("\nRoutes:\n");
    tn_cmd_printf("%-15s %-15s %-15s %s\n", "Destination", "Gateway", "Genmask", "Flags");
    for (i = 0; i < snap->route_count && i < TN_SNAP_MAX_ROUTES; i++) {
        const TnRouteInfo *r = &snap->routes[i];
        char d[16], g[16], m[16];
        if (!r->in_use) continue;
        tn_cmd_ip_to_str(r->dest, d);
        tn_cmd_ip_to_str(r->gw, g);
        tn_cmd_ip_to_str(r->mask, m);
        tn_cmd_printf("%-15s %-15s %-15s %s\n", d, g, m, (r->gw != 0) ? "UG" : "U");
    }
    if (snap->route_count <= 0)
        tn_cmd_printf("  (none reported)\n");
}

static void print_dns(const TnSnapshot *snap)
{
    char b[16];
    ULONG raw1 = ((ULONG)snap->status.dns1[0] << 24) | ((ULONG)snap->status.dns1[1] << 16) |
                 ((ULONG)snap->status.dns1[2] << 8) | (ULONG)snap->status.dns1[3];
    ULONG raw2 = ((ULONG)snap->status.dns2[0] << 24) | ((ULONG)snap->status.dns2[1] << 16) |
                 ((ULONG)snap->status.dns2[2] << 8) | (ULONG)snap->status.dns2[3];
    tn_cmd_printf("\nDNS:\n");
    if (raw1 != 0) { tn_cmd_ip_to_str(raw1, b); tn_cmd_printf("nameserver %s\n", b); }
    if (raw2 != 0) { tn_cmd_ip_to_str(raw2, b); tn_cmd_printf("nameserver %s\n", b); }
    if (raw1 == 0 && raw2 == 0) tn_cmd_printf("nameserver none\n");
}

static void print_sockets(const TnSnapshot *snap)
{
    tn_cmd_printf("\nSockets:\n");
    tn_cmd_printf("active sockets: %ld\n", snap->socket_count);
}

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG show_all;
    static TnSnapshot snap;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"ShowNetStatus");
        return TN_CMD_USAGE;
    }

    show_all = (opts[4] != 0) ||
               (!opts[0] && !opts[1] && !opts[2] && !opts[3]);

    if (tn_cmd_snapshot(&snap) != 0) {
        tn_cmd_printf("tolunnet: daemon is not running\n");
        FreeArgs(rdargs);
        return TN_CMD_WARN;
    }

    if (show_all || opts[0]) print_interfaces(&snap);
    if (show_all || opts[1]) print_routes(&snap);
    if (show_all || opts[2]) print_dns(&snap);
    if (show_all || opts[3]) print_sockets(&snap);

    FreeArgs(rdargs);
    return rc;
}
