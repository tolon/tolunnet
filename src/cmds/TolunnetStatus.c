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
#include "../../include/ipc.h"

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
             (VOID (*)())"\x16\xc0\x4e\x75",
             buf);
}

int main(int argc, char *argv[])
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    struct Library *DOSBase;
    struct Library *SocketBase;
    TnPrefs prefs;
    BOOL is_netstat = FALSE;
    struct MsgPort *reply_port = NULL;
    struct MsgPort *daemon_port = NULL;
    ULONG live_ip = 0, live_nm = 0, live_gw = 0;
    int active_socks = 0;
    char ip_str[24], nm_str[24], gw_str[24];
    (void)argc;

    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    if (argv && argv[0] && str_ends_with(argv[0], "netstat")) {
        is_netstat = TRUE;
    }

    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr((CONST_STRPTR)"tolunnet daemon is not running (bsdsocket.library not found).\n");
        CloseLibrary(DOSBase);
        return 5;
    }

    /* Load persistent configuration */
    tn_prefs_load(&prefs);

    /* Query live daemon status via Exec IPC (TNET-043) */
    reply_port = CreateMsgPort();
    daemon_port = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);

    if (reply_port != NULL && daemon_port != NULL) {
        TnIpcMsg msg;
        msg.msg.mn_Node.ln_Type = NT_MESSAGE;
        msg.msg.mn_Node.ln_Pri  = 0;
        msg.msg.mn_ReplyPort    = reply_port;
        msg.msg.mn_Length       = sizeof(TnIpcMsg);
        msg.cmd                 = TN_IPC_CMD_GETSTATUS;
        msg.client_task         = SysBase->ThisTask;
        msg.socket_base         = (APTR)SocketBase;

        PutMsg(daemon_port, (struct Message *)&msg);
        WaitPort(reply_port);
        GetMsg(reply_port);

        if (msg.result == 0) {
            live_ip      = (ULONG)msg.args[0];
            live_nm      = (ULONG)msg.args[1];
            live_gw      = (ULONG)msg.args[2];
            active_socks = (int)msg.args[3];
        }
        DeleteMsgPort(reply_port);
    }

    if (live_ip != 0) {
        ip_to_str(live_ip, ip_str);
        ip_to_str(live_nm, nm_str);
        ip_to_str(live_gw, gw_str);
    } else {
        int i = 0;
        while (prefs.ip_addr[i]) { ip_str[i] = prefs.ip_addr[i]; i++; } ip_str[i] = '\0';
        i = 0; while (prefs.netmask[i]) { nm_str[i] = prefs.netmask[i]; i++; } nm_str[i] = '\0';
        i = 0; while (prefs.gateway[i]) { gw_str[i] = prefs.gateway[i]; i++; } gw_str[i] = '\0';
    }

    if (is_netstat) {
        PutStr((CONST_STRPTR)"Active Internet Connections (servers and established):\n");
        tn_logf(TN_LOG_BASIC, "Proto Recv-Q Send-Q Local Address           Foreign Address         State\n");
        tn_logf(TN_LOG_BASIC, "active socket descriptors: %d\n", active_socks);
        PutStr((CONST_STRPTR)"\nKernel IP routing table:\n");
        PutStr((CONST_STRPTR)"Destination     Gateway         Genmask         Flags Metric Ref    Use Iface\n");
        tn_logf(TN_LOG_BASIC, "default         %-15s 0.0.0.0         UG    0      0        0 %s%lu\n",
                gw_str[0] ? gw_str : "10.0.2.2", prefs.device, prefs.unit);
        tn_logf(TN_LOG_BASIC, "%-15s *               %-15s U     0      0        0 %s%lu\n",
                ip_str[0] ? ip_str : "10.0.2.0",
                nm_str[0] ? nm_str : "255.255.255.0",
                prefs.device, prefs.unit);
    } else {
        tn_logf(TN_LOG_BASIC, "%s (unit %lu): flags=0x8063<UP,BROADCAST,RUNNING,MULTICAST> mtu 1500\n",
                prefs.device, prefs.unit);
        tn_logf(TN_LOG_BASIC, "        inet %s  netmask %s  gateway %s\n",
                ip_str[0] ? ip_str : "0.0.0.0",
                nm_str[0] ? nm_str : "255.255.255.0",
                gw_str[0] ? gw_str : "0.0.0.0");
        tn_logf(TN_LOG_BASIC, "        nameserver %s  (mode: %s)\n",
                prefs.dns_server[0] ? prefs.dns_server : "none",
                prefs.use_dhcp ? "DHCP" : "STATIC");
        PutStr((CONST_STRPTR)"lo0:    flags=0x8049<UP,LOOPBACK,RUNNING> mtu 16384\n");
        PutStr((CONST_STRPTR)"        inet 127.0.0.1  netmask 255.0.0.0\n");
    }

    CloseLibrary(SocketBase);
    CloseLibrary(DOSBase);
    return 0;
}
