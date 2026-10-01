/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * TolunnetStatus — Interface & Network Status Diagnostic Tool for AmigaOS.
 *
 * Implements standard ifconfig, netstat, and TolunnetStatus commands.
 * Queries live daemon interface status and routing parameters (TNET-043).
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/execbase.h>
#include <libraries/bsdsocket.h>
#include <netinet/in.h>

#include "../common/log.h"
#include "../common/prefs.h"
#include "../common/ipc_client.h"
#include "../../include/ipc.h"
#include "../common/rawfmt.h"
#include "../../include/version.h"
static const char tn_verstag[] __attribute__((used)) = TN_VERSTAG("TolunnetStatus");

static int str_ends_with(const char *s, const char *suffix)
{
    int slen = 0, tlen = 0;
    while (s && s[slen]) slen++;
    while (suffix && suffix[tlen]) tlen++;
    if (tlen > slen) return 0;
    for (int i = 0; i < tlen; i++) {
        if (s[slen - tlen + i] != suffix[i]) return 0;
    }
    return 1;
}

static void ip_to_str(ULONG ip, char *buf)
{
    ULONG b0 = (ip >> 24) & 0xFF;
    ULONG b1 = (ip >> 16) & 0xFF;
    ULONG b2 = (ip >> 8)  & 0xFF;
    ULONG b3 = ip & 0xFF;
    ULONG octets[4] = {b0, b1, b2, b3};

    buf[0] = '\0';
    RawDoFmt((CONST_STRPTR)"%lu.%lu.%lu.%lu",
             (APTR)octets,
             TN_RAWFMT_PUTCH,
             buf);
}

static void format_ip_port(ULONG ip, UWORD port, char *buf)
{
    ULONG b0 = (ip >> 24) & 0xFF;
    ULONG b1 = (ip >> 16) & 0xFF;
    ULONG b2 = (ip >> 8)  & 0xFF;
    ULONG b3 = ip & 0xFF;

    if (port == 0) {
        ULONG args[4] = {b0, b1, b2, b3};
        RawDoFmt((CONST_STRPTR)"%lu.%lu.%lu.%lu:*",
                 (APTR)args,
                 TN_RAWFMT_PUTCH,
                 buf);
    } else {
        ULONG args[5] = {b0, b1, b2, b3, (ULONG)port};
        RawDoFmt((CONST_STRPTR)"%lu.%lu.%lu.%lu:%lu",
                 (APTR)args,
                 TN_RAWFMT_PUTCH,
                 buf);
    }
}

int main(int argc, char *argv[])
{
    struct Library *SocketBase;
    static TnIfInfo if_list[8];
    static TnRouteInfo rt_list[16];
    int if_count = 0, rt_count = 0;
    ULONG dns1 = 0, dns2 = 0;
    BOOL is_netstat = FALSE;
    ULONG live_ip = 0, live_nm = 0, live_gw = 0;
    int active_socks = 0;
    char ip_str[24], nm_str[24], gw_str[24];
    (void)argc;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    /* z.ai step 9a item 1: case-insensitive FilePart alias match */
    if (argv && argv[0]) {
        STRPTR base = FilePart((STRPTR)argv[0]);
        char lc[32];
        int i;
        for (i = 0; base[i] && i < 31; i++) {
            char c = base[i];
            if (c >= 65 && c <= 90) c = (char)(c + 32);
            lc[i] = c;
        }
        lc[i] = 0;
        if (strcmp(lc, "netstat") == 0) is_netstat = TRUE;
    }

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr((CONST_STRPTR)"tolunnet daemon is not running (bsdsocket.library not found).\n");
        CloseLibrary(DOSBase);
        return 5;
    }

    /* z.ai step 9a item 1: live daemon state - status V2 (dns), the
    interface list and the route list. No prefs fallback. */
    {
        static TnStatusInfoV2 v2;
        LONG sargs[5];
        APTR sptrs[1];
        TnIpcMsg msg;
        sargs[0] = 0; sargs[1] = 0; sargs[2] = 0; sargs[3] = 0;
        sargs[4] = (LONG)sizeof(TnStatusInfoV2);
        sptrs[0] = (APTR)&v2;
        if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATUS, sargs, 5, sptrs, 1, &msg) == 0) {
            live_ip = ((ULONG)v2.ip_addr[0] << 24) | ((ULONG)v2.ip_addr[1] << 16) |
                      ((ULONG)v2.ip_addr[2] << 8) | (ULONG)v2.ip_addr[3];
            live_nm = ((ULONG)v2.netmask[0] << 24) | ((ULONG)v2.netmask[1] << 16) |
                      ((ULONG)v2.netmask[2] << 8) | (ULONG)v2.netmask[3];
            live_gw = ((ULONG)v2.gw[0] << 24) | ((ULONG)v2.gw[1] << 16) |
                      ((ULONG)v2.gw[2] << 8) | (ULONG)v2.gw[3];
            active_socks = (int)v2.active_sockets;
            dns1 = ((ULONG)v2.dns1[0] << 24) | ((ULONG)v2.dns1[1] << 16) |
                   ((ULONG)v2.dns1[2] << 8) | (ULONG)v2.dns1[3];
            dns2 = ((ULONG)v2.dns2[0] << 24) | ((ULONG)v2.dns2[1] << 16) |
                   ((ULONG)v2.dns2[2] << 8) | (ULONG)v2.dns2[3];
        }
    }
    {
        LONG iargs[5];
        APTR iptrs[1];
        TnIpcMsg msg;
        iargs[0] = TN_IFCTL_LIST; iargs[1] = 0; iargs[2] = 0; iargs[3] = 0;
        iargs[4] = 8;
        iptrs[0] = (APTR)if_list;
        {
            int lrc = tn_ipc_oneshot_ex(TN_IPC_CMD_IFCTL, iargs, 5, iptrs, 1, &msg);
            if (lrc > 0) {
                if_count = lrc;
                live_ip = if_list[0].addr;
                live_nm = if_list[0].mask;
                live_gw = if_list[0].gw;
            }
        }
    {
        LONG rargs[5];
        APTR rptrs[1];
        TnIpcMsg msg;
        rargs[0] = TN_ROUTECTL_LIST; rargs[1] = 0; rargs[2] = 0; rargs[3] = 0;
        rargs[4] = 16;
        rptrs[0] = (APTR)rt_list;
        {
            int rrc = tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, rargs, 5, rptrs, 1, &msg);
            if (rrc > 0) {
            rt_count = rrc;
            }
        }
    }

    if (live_ip != 0) {
        ip_to_str(live_ip, ip_str);
        ip_to_str(live_nm, nm_str);
        ip_to_str(live_gw, gw_str);
    }

    if (is_netstat) {
        TnSocketInfo sock_list[32];
        int count = 0;
        TnIpcMsg emsg;
        emsg.args[0] = 32;
        emsg.ptrs[0] = (APTR)sock_list;

        APTR ptrs[1];
        ptrs[0] = (APTR)sock_list;
        if (tn_ipc_oneshot_ex(TN_IPC_CMD_ENUMSOCKETS, emsg.args, 1, ptrs, 1, &emsg) == 0 && emsg.result >= 0) {
            count = (int)emsg.result;
        }

        PutStr((CONST_STRPTR)"Active Internet Connections (servers and established):\n");
        PutStr((CONST_STRPTR)"Proto Recv-Q Send-Q Local Address           Foreign Address         State\n");
        for (int s = 0; s < count; s++) {
            char l_addr[24], r_addr[24];
            const char *proto_str = "raw";
            const char *state_str = "";

            format_ip_port(sock_list[s].local_ip, sock_list[s].local_port, l_addr);
            format_ip_port(sock_list[s].remote_ip, sock_list[s].remote_port, r_addr);

            if (sock_list[s].proto == 1) {
                proto_str = "tcp";
                switch (sock_list[s].state) {
                case 1:  state_str = "SYN_SENT"; break;
                case 2:  state_str = "ESTABLISHED"; break;
                case 3:  state_str = "LISTEN"; break;
                case 4:  state_str = "CLOSE_WAIT"; break;
                case 5:  state_str = "ERROR"; break;
                default: state_str = "CLOSED"; break;
                }
            } else if (sock_list[s].proto == 2) {
                proto_str = "udp";
            }

            tn_logf(TN_LOG_BASIC, "%-5s %6lu %6lu %-23s %-23s %s\n",
                    proto_str, sock_list[s].recv_q, sock_list[s].send_q,
                    l_addr, r_addr, state_str);
        }
        tn_logf(TN_LOG_BASIC, "active socket descriptors: %ld\n", (LONG)((count > 0) ? count : active_socks));
        PutStr((CONST_STRPTR)"\nKernel IP routing table:\n");
        PutStr((CONST_STRPTR)"Destination     Gateway         Genmask         Flags Metric Ref    Use Iface\n");
        /* z.ai step 9a item 1: the daemon route list, not placeholders */
        for (int ri = 0; ri < rt_count && ri < 16; ri++) {
            char rd[24], rg[24], rm[24];
            const char *gws;
            const char *ifn = (if_count > 0) ? if_list[0].name : "eth0";
            if (!rt_list[ri].in_use) continue;
            ip_to_str(rt_list[ri].dest, rd);
            ip_to_str(rt_list[ri].mask, rm);
            if (rt_list[ri].gw == 0) {
                gws = "*";
            } else {
                ip_to_str(rt_list[ri].gw, rg);
                gws = rg;
            }
            tn_logf(TN_LOG_BASIC, "%-15s %-15s %-15s %-5s 0      0        0 %s\n",
                    rd, gws, rm, (rt_list[ri].gw != 0) ? "UG" : "U", ifn);
        }
    }
 else {
        /* z.ai step 9a item 1: live interface flags, DNS and socket
         * count - no prefs, no fixed UP. */
        char d1[24], d2[24];
        ip_to_str(dns1, d1);
        ip_to_str(dns2, d2);
        tn_logf(TN_LOG_BASIC, "%s (unit %lu): flags=<%s,%s,%s> mtu 1500\n",
                if_count > 0 ? if_list[0].name : "eth0",
                if_count > 0 ? (ULONG)if_list[0].unit : 0,
                (if_count > 0 && if_list[0].is_up) ? "UP" : "DOWN",
                (if_count > 0 && if_list[0].is_dhcp) ? "DHCP" : "STATIC",
                (if_count > 0 && if_list[0].link_up) ? "LINK" : "NOLINK");
        tn_logf(TN_LOG_BASIC, "        inet %s  netmask %s  gateway %s\n",
                ip_str, nm_str, gw_str);
        tn_logf(TN_LOG_BASIC, "        nameserver %s\n", d1[0] ? d1 : "none");
        if (dns2 != 0) tn_logf(TN_LOG_BASIC, "        nameserver %s\n", d2);
        tn_logf(TN_LOG_BASIC, "        active sockets: %ld\n", (LONG)active_socks);
    }

    CloseLibrary(SocketBase);
    CloseLibrary(DOSBase);
    return 0;
}
}
