/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — AddNetInterface command (CLOSE §B.7, Roadshow-style).
 * ReadArgs: FILE — reads a Roadshow-format interfaces file (default
 * DEVS:Internet/interfaces) and applies each entry to the primary
 * interface via IFCTL SET + UP. Entries whose DEVICE differs from the
 * running one are reported as needing a daemon restart (tolunnet runs
 * one interface live).
 */
#include "ifctl_cmd.h"
#include "../common/ifreader.h"
#include <proto/dos.h>
#include <string.h>

#define TEMPLATE "FILE"

static char g_ifbuf[4096];

static int read_file(const char *path, char *buf, int size)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    LONG n;
    if (fh == (BPTR)0) return -1;
    n = Read(fh, (APTR)buf, size - 1);
    Close(fh);
    if (n < 0) return -1;
    buf[n] = '\0';
    return (int)n;
}

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    static TnIfEntry entries[TN_IF_MAX];
    int n, i;
    int rc = TN_CMD_OK;
    const char *path = "DEVS:Internet/interfaces";

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"AddNetInterface");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }
    if (opts[0] != 0) path = (const char *)opts[0];

    if (read_file(path, g_ifbuf, sizeof(g_ifbuf)) < 0) {
        tn_cmd_printf("AddNetInterface: cannot read %s\n", path);
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    n = tn_if_parse_lines(g_ifbuf, entries, TN_IF_MAX);
    if (n <= 0) {
        tn_cmd_printf("AddNetInterface: no interface entries in %s\n", path);
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    for (i = 0; i < n; i++) {
        TnIfEntry *e = &entries[i];
        ULONG addr = 0, mask = 0, gw = 0;
        char ipbuf[24];
        const char *addr_s = e->address;
        char cidr_addr[24];
        const char *slash = NULL;
        int k;

        /* split CIDR a.b.c.d/n into address + netmask */
        for (k = 0; e->address[k]; k++) {
            if (e->address[k] == '/') { slash = &e->address[k]; break; }
        }
        if (slash != NULL) {
            int prefix = 0;
            int alen = (int)(slash - e->address);
            const char *p = slash + 1;
            for (k = 0; k < alen && k < 23; k++) cidr_addr[k] = e->address[k];
            cidr_addr[alen] = '\0';
            while (*p >= '0' && *p <= '9') { prefix = prefix * 10 + (*p - '0'); p++; }
            if (prefix < 0 || prefix > 32) prefix = 24;
            mask = 0xFFFFFFFFUL << (32 - prefix);
            if (prefix == 0) mask = 0;
            mask = htonl(mask);
            addr_s = cidr_addr;
        }

        if (addr_s[0] != '\0') {
            addr = tn_call_inet_addr(addr_s);
            if (addr == INADDR_NONE) {
                tn_cmd_printf("%s: bad ADDRESS '%s' — skipped\n", e->name, addr_s);
                rc = TN_CMD_FAIL;
                continue;
            }
        }
        if (e->netmask[0] != '\0') {
            mask = tn_call_inet_addr(e->netmask);
        }
        if (e->gateway[0] != '\0') {
            gw = tn_call_inet_addr(e->gateway);
            if (gw == INADDR_NONE) gw = 0;
        }

        {
            TnIpcMsg msg;
            if (ifctl_call(TN_IFCTL_SET, -1, addr, mask, gw, NULL, 0, &msg) != 0 ||
                msg.result != 0) {
                tn_cmd_printf("%s: SET failed\n", e->name);
                rc = TN_CMD_FAIL;
                continue;
            }
            ifctl_call(TN_IFCTL_UP, -1, 0, 0, 0, NULL, 0, &msg);
        }
        ifctl_fmt_ip(ipbuf, addr);
        tn_cmd_printf("%s: applied address %s%s\n", e->name,
                      (addr != 0) ? ipbuf : "(unchanged)",
                      e->dhcp ? " (DHCP needs restart)" : "");
        if (e->dhcp) rc = TN_CMD_WARN;
    }

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
