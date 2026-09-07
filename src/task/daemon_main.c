/*
 * tolunnet — Network Task & lwIP Core Mainloop (daemon_main.c).
 *
 * ROUND4b §B (Modular Daemon Refactor).
 * Owns lwIP (NO_SYS=1), SANA-II event pump, 100ms timer ticks, bsdsocket.library
 * registration, and top-level CLI command parsing.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <exec/execbase.h>

#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <lwip/prot/dhcp.h>
#include <lwip/timeouts.h>
#include <lwip/ip4_addr.h>
#include <lwip/etharp.h>
#include <netif/ethernet.h>

#include "task_ctx.h"
#include "slot_table.h"
#include "netif_mgr.h"
#include "ipc_dispatch.h"
#include "sana2/sana2_netif.h"
#include "timers.h"
#include "lib/lib_init.h"
#include "common/log.h"
#include "common/prefs.h"
#include "common/ipc_client.h"

/* Daemon Singleton State */
TnDaemon g_daemon;

static BOOL        g_stack_swapped = FALSE;
static ULONG       g_orig_stack_size = 0;
static const char *g_cli_device = NULL;
static const LONG *g_cli_unit = NULL;
static const char *g_cli_ip = NULL;
static const char *g_cli_netmask = NULL;
static const char *g_cli_gateway = NULL;

static int tn_task_real_main(int argc, char *argv[])
{
    struct Library *DOSBase;
    struct Task    *self_task;
    BYTE            old_pri;
    CONST_STRPTR    device = (CONST_STRPTR)"ethernet.device";
    ULONG           unit = 0;
    TnS2Result      s2res;
    ip4_addr_t      ipaddr, netmask, gw;
    ULONG           s2_sig, timer_sig, ipc_sig, ctrl_c_sig, wait_mask;
    BOOL            use_dhcp = TRUE;
    BOOL            dhcp_logged = FALSE;
    BPTR            log_fh = (BPTR)0;
    ULONG           tick_count = 0;
    int             i;
    TnNetif        *prim = tn_netif_primary(&g_daemon);

    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    tn_slot_table_init(&g_daemon);
    g_daemon.if_count = 1;
    prim->in_use = TRUE;

    /* Load persistent configuration first (defaults when absent) so every
     * key — including HOSTNAME/DNS2/MTU/DEBUG (TNET-063) — is honoured.
     * CLI arguments override the interface-level keys. */
    tn_prefs_load(&g_daemon.prefs);
    g_log_level = TN_LOG_BASIC + ((g_daemon.prefs.debug > 0) ? 1 : 0);

    /* TNET-066: Set task priority from prefs (default 5) */
    self_task = FindTask(NULL);
    old_pri = SetTaskPri(self_task, (BYTE)g_daemon.prefs.priority);

    device   = (CONST_STRPTR)g_daemon.prefs.device;
    unit     = g_daemon.prefs.unit;
    use_dhcp = g_daemon.prefs.use_dhcp;
    if (!use_dhcp) {
        ip4addr_aton(g_daemon.prefs.ip_addr, &ipaddr);
        ip4addr_aton(g_daemon.prefs.netmask, &netmask);
        ip4addr_aton(g_daemon.prefs.gateway, &gw);
    } else {
        IP4_ADDR(&ipaddr, 0, 0, 0, 0);
        IP4_ADDR(&netmask, 0, 0, 0, 0);
        IP4_ADDR(&gw, 0, 0, 0, 0);
    }

    if (g_cli_device != NULL) {
        device = (CONST_STRPTR)g_cli_device;
    }
    if (g_cli_unit != NULL) {
        unit = (ULONG)*g_cli_unit;
    }
    if (g_cli_ip != NULL) {
        ip4addr_aton(g_cli_ip, &ipaddr);
        use_dhcp = FALSE;
    }
    if (g_cli_netmask != NULL) {
        ip4addr_aton(g_cli_netmask, &netmask);
    }
    if (g_cli_gateway != NULL) {
        ip4addr_aton(g_cli_gateway, &gw);
    }

    if (argc >= 2 && g_cli_device == NULL) {
        device = (CONST_STRPTR)argv[1];
        if (argc >= 3) {
            LONG parsed = 0;
            LONG consumed = StrToLong((CONST_STRPTR)argv[2], &parsed);
            if (consumed > 0) unit = (ULONG)parsed;
        }
        if (argc >= 4) {
            ip4addr_aton(argv[3], &ipaddr);
            if (argc >= 5) ip4addr_aton(argv[4], &netmask);
            else IP4_ADDR(&netmask, 255, 255, 255, 0);
            if (argc >= 6) ip4addr_aton(argv[5], &gw);
            else IP4_ADDR(&gw, 10, 0, 2, 2);
            use_dhcp = FALSE;
        }
    }

    tn_log(TN_LOG_BASIC, "========================================\n");
    tn_log(TN_LOG_BASIC, "tolunnet: network task starting...\n");
    tn_log(TN_LOG_BASIC, "========================================\n");

    if (g_stack_swapped) {
        tn_logf(TN_LOG_BASIC, "tolunnet: switched to 32 KB stack (was %lu bytes)\n", g_orig_stack_size);
    }
    tn_logf(TN_LOG_BASIC, "tolunnet: task priority set to %ld (was %ld)\n",
            (LONG)g_daemon.prefs.priority, (LONG)old_pri);

    log_fh = Open((CONST_STRPTR)"WORK:tolunnet-task.log", MODE_NEWFILE);
    g_log_file = log_fh;

    /* 1. Initialize timer.device */
    if (!tn_timer_init(&g_daemon.timer)) {
        tn_log(TN_LOG_BASIC, "tolunnet: failed to initialize timer.device\n");
        SetTaskPri(self_task, old_pri);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* Initialize hardware PRNG entropy BEFORE lwip_init (TNET-047) */
    tn_rand_init(&g_daemon.timer, NULL);

    /* 2. Initialize lwIP stack core */
    lwip_init();
    tn_log(TN_LOG_BASIC, "tolunnet: lwIP 2.2.0 initialized (NO_SYS=1)\n");

    /* 3. Open SANA-II network device */
    s2res = tn_s2_open(&prim->s2if, device, unit);
    if (s2res != TN_S2_OK) {
        tn_logf(TN_LOG_BASIC, "tolunnet: SANA-II open failed (%s, %lu)\n", device, unit);
        tn_timer_fini(&g_daemon.timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* 4. Bring SANA-II interface online */
    s2res = tn_s2_online(&prim->s2if, NULL);
    if (s2res != TN_S2_OK) {
        tn_log(TN_LOG_BASIC, "tolunnet: SANA-II online failed\n");
        tn_s2_offline_close(&prim->s2if);
        tn_timer_fini(&g_daemon.timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    tn_logf(TN_LOG_BASIC, "tolunnet: %s:%lu online (MAC %02x:%02x:%02x:%02x:%02x:%02x, MTU %lu)\n",
            device, unit,
            (int)prim->s2if.mac[0], (int)prim->s2if.mac[1], (int)prim->s2if.mac[2],
            (int)prim->s2if.mac[3], (int)prim->s2if.mac[4], (int)prim->s2if.mac[5],
            prim->s2if.mtu);

    /* Initialize hardware PRNG entropy (TNET-013) */
    tn_rand_init(&g_daemon.timer, prim->s2if.mac);

    /* 5. Register SANA-II netif with lwIP */
    if (netif_add(&prim->lwip_if, &ipaddr, &netmask, &gw, &prim->s2if,
                  tn_sana2_netif_init, ethernet_input) == NULL) {
        tn_log(TN_LOG_BASIC, "tolunnet: netif_add failed\n");
        tn_s2_offline_close(&prim->s2if);
        tn_timer_fini(&g_daemon.timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    netif_set_default(&prim->lwip_if);
    netif_set_up(&prim->lwip_if);

    /* 6. Arm async SANA-II receive pump */
    if (tn_s2_arm_reads(&prim->s2if) != TN_S2_OK) {
        tn_log(TN_LOG_BASIC, "tolunnet: tn_s2_arm_reads failed\n");
        netif_set_down(&prim->lwip_if);
        netif_remove(&prim->lwip_if);
        tn_s2_offline_close(&prim->s2if);
        tn_timer_fini(&g_daemon.timer);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* DNS servers, DHCP hostname (option 12), MTU clamp, debug tier (TNET-063) */
    tn_apply_live_config(&g_daemon);

    if (use_dhcp) {
        tn_log(TN_LOG_BASIC, "tolunnet: starting DHCP client...\n");
        dhcp_start(&prim->lwip_if);
    } else {
        char str_ip[16], str_nm[16], str_gw[16];
        ip_to_str(str_ip, netif_ip4_addr(&prim->lwip_if));
        ip_to_str(str_nm, netif_ip4_netmask(&prim->lwip_if));
        ip_to_str(str_gw, netif_ip4_gw(&prim->lwip_if));

        tn_log(TN_LOG_BASIC, "----------------------------------------\n");
        tn_logf(TN_LOG_BASIC, "tolunnet: Static IP configured!\n");
        tn_logf(TN_LOG_BASIC, "  IP Address : %s\n", str_ip);
        tn_logf(TN_LOG_BASIC, "  Netmask    : %s\n", str_nm);
        tn_logf(TN_LOG_BASIC, "  Gateway    : %s\n", str_gw);
        tn_log(TN_LOG_BASIC, "----------------------------------------\n");

        etharp_gratuitous(&prim->lwip_if);
        etharp_request(&prim->lwip_if, &gw);
    }

    /* 7. Create and Register Public IPC Message Port */
    g_daemon.ipc_port = CreateMsgPort();
    if (g_daemon.ipc_port == NULL) {
        tn_log(TN_LOG_BASIC, "tolunnet: failed to create IPC port\n");
        netif_set_down(&prim->lwip_if);
        netif_remove(&prim->lwip_if);
        tn_s2_offline_close(&prim->s2if);
        tn_timer_fini(&g_daemon.timer);
        CloseLibrary(DOSBase);
        return 20;
    }
    g_daemon.ipc_port->mp_Node.ln_Name = (char *)TOLUNNET_PORT_NAME;
    g_daemon.ipc_port->mp_Node.ln_Pri  = 0;
    g_daemon.ipc_port->mp_Node.ln_Type = NT_MSGPORT;
    AddPort(g_daemon.ipc_port);

    /* 8. Instantiate and register bsdsocket.library */
    g_daemon.bsd_lib = tn_lib_create();
    if (g_daemon.bsd_lib == NULL) {
        tn_log(TN_LOG_BASIC, "tolunnet: failed to create bsdsocket.library\n");
        RemPort(g_daemon.ipc_port);
        DeleteMsgPort(g_daemon.ipc_port);
        netif_set_down(&prim->lwip_if);
        netif_remove(&prim->lwip_if);
        tn_s2_offline_close(&prim->s2if);
        tn_timer_fini(&g_daemon.timer);
        CloseLibrary(DOSBase);
        return 20;
    }
    tn_log(TN_LOG_BASIC, "tolunnet: bsdsocket.library v4.1 registered with Exec\n");

    /* 9. Arm 100 ms timer tick */
    tn_timer_arm(&g_daemon.timer, 100000);

    s2_sig     = 1UL << prim->s2if.rx_port->mp_SigBit;
    timer_sig  = g_daemon.timer.sig_mask;
    ipc_sig    = 1UL << g_daemon.ipc_port->mp_SigBit;
    ctrl_c_sig = SIGBREAKF_CTRL_C;
    wait_mask  = s2_sig | timer_sig | ipc_sig | ctrl_c_sig;

    tn_log(TN_LOG_BASIC, "tolunnet: network task running (Press Ctrl-C to stop)\n");
    g_daemon.running = TRUE;

    /* 10. Main Task Loop */
    while (g_daemon.running) {
        ULONG sigs = Wait(wait_mask);

        /* Ctrl-C Signal -> Shutdown.
         * TNET-059: per-opener clones embed jump tables that point into this
         * task's code segment. Exiting with lib_OpenCnt > 0 would unload that
         * code under live clients → Guru on their next call. Refuse to exit
         * and keep servicing IPC until every opener has closed. */
        if (sigs & ctrl_c_sig) {
            if (g_daemon.bsd_lib != NULL && g_daemon.bsd_lib->lib_OpenCnt > 0) {
                tn_logf(TN_LOG_BASIC,
                        "\ntolunnet: Ctrl-C: %lu client(s) still have bsdsocket.library open;\n"
                        "tolunnet: not exiting - close them (or their apps) and press Ctrl-C again\n",
                        (ULONG)g_daemon.bsd_lib->lib_OpenCnt);
            } else {
                tn_log(TN_LOG_BASIC, "\ntolunnet: Ctrl-C received, shutting down...\n");
                g_daemon.running = FALSE;
                break;
            }
        }

        /* Client IPC Message Signal */
        if (sigs & ipc_sig) {
            struct Message *msg;
            while ((msg = GetMsg(g_daemon.ipc_port)) != NULL) {
                if (tn_handle_ipc(&g_daemon, (TnIpcMsg *)msg)) {
                    ReplyMsg(msg);
                }
            }
            tn_drain_loopback();
        }

        /* SANA-II Packet Arrival Signal */
        if (sigs & s2_sig) {
            tn_sana2_poll_input(&prim->s2if, &prim->lwip_if);
            tn_drain_loopback();
        }

        /* 100 ms Timer Tick Signal */
        if (sigs & timer_sig) {
            tn_timer_ack(&g_daemon.timer);
            tick_count++;

            /* Drive lwIP timeouts */
            sys_check_timeouts();
            tn_drain_loopback();

            /* Check DHCP lease progress */
            if (use_dhcp && !dhcp_logged && dhcp_supplied_address(&prim->lwip_if)) {
                char str_ip[16], str_nm[16], str_gw[16];
                ip_to_str(str_ip, netif_ip4_addr(&prim->lwip_if));
                ip_to_str(str_nm, netif_ip4_netmask(&prim->lwip_if));
                ip_to_str(str_gw, netif_ip4_gw(&prim->lwip_if));

                tn_log(TN_LOG_BASIC, "----------------------------------------\n");
                tn_logf(TN_LOG_BASIC, "tolunnet: DHCP lease obtained!\n");
                tn_logf(TN_LOG_BASIC, "  IP Address : %s\n", str_ip);
                tn_logf(TN_LOG_BASIC, "  Netmask    : %s\n", str_nm);
                tn_logf(TN_LOG_BASIC, "  Gateway    : %s\n", str_gw);
                tn_log(TN_LOG_BASIC, "----------------------------------------\n");

                dhcp_logged = TRUE;
            }

            /* Re-arm timer */
            tn_timer_arm(&g_daemon.timer, 100000);
        }
    }

    /* 11. Clean Shutdown Sequence */
    if (g_daemon.bsd_lib != NULL) {
        if (g_daemon.bsd_lib->lib_OpenCnt > 0) {
            tn_logf(TN_LOG_BASIC, "tolunnet: warning: bsdsocket.library has %lu open client(s) at shutdown\n",
                    (ULONG)g_daemon.bsd_lib->lib_OpenCnt);
        }
        tn_log(TN_LOG_BASIC, "tolunnet: removing bsdsocket.library...\n");
        tn_lib_destroy(g_daemon.bsd_lib);
        g_daemon.bsd_lib = NULL;
    }

    tn_log(TN_LOG_BASIC, "tolunnet: deleting IPC port...\n");
    RemPort(g_daemon.ipc_port);
    DeleteMsgPort(g_daemon.ipc_port);

    /* Free any remaining sockets */
    for (i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        if (g_daemon.sockets[i].in_use) {
            tn_slot_free(&g_daemon, i);
        }
    }

    if (use_dhcp) {
        tn_log(TN_LOG_BASIC, "tolunnet: stopping DHCP client...\n");
        dhcp_stop(&prim->lwip_if);
    }
    tn_log(TN_LOG_BASIC, "tolunnet: bringing netif down...\n");
    netif_set_down(&prim->lwip_if);
    netif_remove(&prim->lwip_if);

    tn_log(TN_LOG_BASIC, "tolunnet: closing timer.device...\n");
    tn_timer_fini(&g_daemon.timer);

    tn_log(TN_LOG_BASIC, "tolunnet: closing SANA-II device...\n");
    tn_s2_offline_close(&prim->s2if);

    if (log_fh != (BPTR)0) {
        Close(log_fh);
    }

    /* Restore original task priority */
    tn_log(TN_LOG_BASIC, "tolunnet: restoring task priority...\n");
    SetTaskPri(self_task, old_pri);

    tn_log(TN_LOG_BASIC, "tolunnet: shutdown complete.\n");
    CloseLibrary(DOSBase);
    return 0;
}

enum {
    OPT_START = 0,
    OPT_STOP,
    OPT_STATUS,
    OPT_RECONFIG,
    OPT_DEVICE,
    OPT_UNIT,
    OPT_IP,
    OPT_NETMASK,
    OPT_GATEWAY,
    OPT_COUNT
};

int main(int argc, char *argv[])
{
    struct Library *dos_base = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (dos_base == NULL) return 20;

    if (argc > 0) {
        LONG opts[OPT_COUNT];
        struct RDArgs *rdargs;
        int i;
        for (i = 0; i < OPT_COUNT; i++) opts[i] = 0;

        rdargs = ReadArgs((CONST_STRPTR)"START/S,STOP/S,STATUS/S,RECONFIG/S,DEVICE,UNIT/N,IP,NETMASK,GATEWAY", opts, NULL);
        if (rdargs == NULL) {
            PrintFault(IoErr(), (CONST_STRPTR)"tolunnet");
            CloseLibrary(dos_base);
            return 20;
        }

        if (opts[OPT_STOP]) {
            struct MsgPort *dp = FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
            FreeArgs(rdargs);
            if (dp == NULL) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is not running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            if (dp->mp_SigTask != NULL) {
                Signal((struct Task *)dp->mp_SigTask, SIGBREAKF_CTRL_C);
            }
            PutStr((CONST_STRPTR)"tolunnet: stop signal sent to daemon\n");
            CloseLibrary(dos_base);
            return 0;
        }

        if (opts[OPT_RECONFIG]) {
            int res;
            FreeArgs(rdargs);
            res = tn_ipc_oneshot(TN_IPC_CMD_RECONFIG, NULL, 0, NULL);
            if (res != 0) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is not running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            PutStr((CONST_STRPTR)"tolunnet: configuration reloaded\n");
            CloseLibrary(dos_base);
            return 0;
        }

        if (opts[OPT_STATUS]) {
            TnIpcMsg msg;
            int res;
            FreeArgs(rdargs);
            res = tn_ipc_oneshot(TN_IPC_CMD_GETSTATUS, NULL, 0, &msg);
            if (res != 0) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is not running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            {
                ULONG ip = (ULONG)msg.args[0];
                ULONG nm = (ULONG)msg.args[1];
                ULONG gw = (ULONG)msg.args[2];
                LONG socks = msg.args[3];
                char buf[80];
                ULONG ip_parts[4] = { (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF };
                ULONG nm_parts[4] = { (nm >> 24) & 0xFF, (nm >> 16) & 0xFF, (nm >> 8) & 0xFF, nm & 0xFF };
                ULONG gw_parts[4] = { (gw >> 24) & 0xFF, (gw >> 16) & 0xFF, (gw >> 8) & 0xFF, gw & 0xFF };

                PutStr((CONST_STRPTR)"tolunnet daemon status: RUNNING\n");

                RawDoFmt((CONST_STRPTR)"  IP Address : %lu.%lu.%lu.%lu\n", (APTR)ip_parts, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);

                RawDoFmt((CONST_STRPTR)"  Netmask    : %lu.%lu.%lu.%lu\n", (APTR)nm_parts, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);

                RawDoFmt((CONST_STRPTR)"  Gateway    : %lu.%lu.%lu.%lu\n", (APTR)gw_parts, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);

                RawDoFmt((CONST_STRPTR)"  Sockets    : %ld active\n", (APTR)&socks, (VOID (*)())"\x16\xc0\x4e\x75", buf);
                PutStr((CONST_STRPTR)buf);
            }
            CloseLibrary(dos_base);
            return 0;
        }

        if (opts[OPT_START]) {
            FreeArgs(rdargs);
            if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
                PutStr((CONST_STRPTR)"tolunnet: daemon is already running\n");
                CloseLibrary(dos_base);
                return 5;
            }
            SystemTags((CONST_STRPTR)"C:tolunnet",
                       SYS_Asynch, TRUE,
                       SYS_Input, (BPTR)0,
                       SYS_Output, (BPTR)0,
                       NP_StackSize, 32768,
                       TAG_END);
            PutStr((CONST_STRPTR)"tolunnet: daemon started in background (32 KB stack)\n");
            CloseLibrary(dos_base);
            return 0;
        }

        if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
            PutStr((CONST_STRPTR)"tolunnet: daemon is already running\n");
            FreeArgs(rdargs);
            CloseLibrary(dos_base);
            return 5;
        }

        g_cli_device  = (const char *)opts[OPT_DEVICE];
        g_cli_unit    = (const LONG *)opts[OPT_UNIT];
        g_cli_ip      = (const char *)opts[OPT_IP];
        g_cli_netmask = (const char *)opts[OPT_NETMASK];
        g_cli_gateway = (const char *)opts[OPT_GATEWAY];

        FreeArgs(rdargs);
    } else {
        /* Workbench startup */
        if (FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME) != NULL) {
            CloseLibrary(dos_base);
            return 5;
        }
    }

    CloseLibrary(dos_base);

    {
        struct Process *proc = (struct Process *)FindTask(NULL);
        ULONG stack_size = 0;

        if (proc != NULL && proc->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
            stack_size = proc->pr_StackSize;
        }

        if (stack_size < 32768) {
            struct StackSwapStruct stk;
            APTR new_stk = AllocMem(32768, MEMF_PUBLIC | MEMF_CLEAR);
            int rc;
            if (new_stk == NULL) {
                return 20;
            }
            g_stack_swapped = TRUE;
            g_orig_stack_size = stack_size;

            stk.stk_Lower   = new_stk;
            stk.stk_Upper   = (ULONG)new_stk + 32768;
            stk.stk_Pointer = (APTR)stk.stk_Upper;

            StackSwap(&stk);
            rc = tn_task_real_main(argc, argv);
            StackSwap(&stk);

            FreeMem(new_stk, 32768);
            return rc;
        }
    }

    return tn_task_real_main(argc, argv);
}
