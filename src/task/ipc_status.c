/*
 * tolunnet ? Operational & Telemetry IPC Handlers Implementation (ipc_status.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_status.h"
#include "netif_mgr.h"
#include <string.h>

static BOOL tn_streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return FALSE;
    while (*a && *a == *b) { a++; b++; }
    return (*a == '\0' && *b == '\0');
}

int tn_ipc_cmd_getstatus(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    int active_socks = 0;
    int s;
    TnNetif *prim = tn_netif_primary(d);
    (void)slot;

    /* Legacy return format in args[0..3] (TNET-043) */
    imsg->args[0] = (LONG)ip_addr_get_ip4_u32(&prim->lwip_if.ip_addr);
    imsg->args[1] = (LONG)ip_addr_get_ip4_u32(&prim->lwip_if.netmask);
    imsg->args[2] = (LONG)ip_addr_get_ip4_u32(&prim->lwip_if.gw);

    for (s = 0; s < TN_MAX_GLOBAL_SOCKETS; s++) {
        if (d->sockets[s].in_use) active_socks++;
    }
    imsg->args[3] = active_socks;

    /* Extended IPv6-ready return struct if caller supplied buffer (ROUND4b §L) */
    if (imsg->ptrs[0] != NULL && imsg->args[4] >= (LONG)sizeof(TnStatusInfoV2)) {
        TnStatusInfoV2 *v2 = (TnStatusInfoV2 *)imsg->ptrs[0];
        memset(v2, 0, sizeof(TnStatusInfoV2));
        v2->struct_size = sizeof(TnStatusInfoV2);
        v2->family = AF_INET;
        memcpy(v2->ip_addr, &prim->lwip_if.ip_addr, 4);
        memcpy(v2->netmask, &prim->lwip_if.netmask, 4);
        memcpy(v2->gw, &prim->lwip_if.gw, 4);
        v2->active_sockets = active_socks;
        v2->flags = (netif_is_link_up(&prim->lwip_if) ? 1 : 0) | (d->prefs.use_dhcp ? 2 : 0);
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0; /* TN_IPC_REPLY_NOW */
}

int tn_ipc_cmd_reconfig(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnPrefs old;
    BOOL loaded;
    (void)slot;

    old = d->prefs;
    loaded = tn_prefs_load(&d->prefs);

    g_log_level = TN_LOG_BASIC + ((d->prefs.debug > 0) ? 1 : 0);
    tn_apply_live_config(d);

    if (!tn_streq(old.device, d->prefs.device) || old.unit != d->prefs.unit ||
        old.use_dhcp != d->prefs.use_dhcp ||
        !tn_streq(old.ip_addr, d->prefs.ip_addr) ||
        !tn_streq(old.netmask, d->prefs.netmask) ||
        !tn_streq(old.gateway, d->prefs.gateway)) {
        tn_log(TN_LOG_BASIC, "tolunnet: RECONFIG: interface settings changed - stop and start the stack to apply them\n");
    } else {
        tn_log(TN_LOG_BASIC, "tolunnet: RECONFIG applied (DNS, hostname, MTU, debug tier)\n");
    }

    imsg->result = loaded ? 0 : -1;
    imsg->err_no = loaded ? 0 : ENOENT;
    return 0; /* TN_IPC_REPLY_NOW */
}

int tn_ipc_cmd_enumsockets(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG max_entries = imsg->args[0];
    void *out_ptr = imsg->ptrs[0];
    LONG entry_size = imsg->args[2];
    LONG count = 0;
    int s;
    (void)slot;

    if (out_ptr == NULL || max_entries <= 0) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0; /* TN_IPC_REPLY_NOW */
    }

    if (entry_size == (LONG)sizeof(TnSocketInfoV2)) {
        /* V2 versioned enumeration */
        TnSocketInfoV2 *out_v2 = (TnSocketInfoV2 *)out_ptr;
        for (s = 0; s < TN_MAX_GLOBAL_SOCKETS && count < max_entries; s++) {
            if (d->sockets[s].in_use) {
                TnSocketInfoV2 *ent = &out_v2[count];
                memset(ent, 0, sizeof(TnSocketInfoV2));
                ent->struct_size = sizeof(TnSocketInfoV2);
                ent->family = AF_INET;
                ent->proto = (uint8_t)d->sockets[s].type;
                ent->state = (uint8_t)d->sockets[s].tcp_state;
                ent->recv_q = d->sockets[s].rx_count;

                if (d->sockets[s].type == 1 /* TCP */ && d->sockets[s].tcp_pcb != NULL) {
                    ent->local_port  = d->sockets[s].tcp_pcb->local_port;
                    ent->remote_port = d->sockets[s].tcp_pcb->remote_port;
                    memcpy(ent->local_addr, &ip_2_ip4(&d->sockets[s].tcp_pcb->local_ip)->addr, 4);
                    memcpy(ent->remote_addr, &ip_2_ip4(&d->sockets[s].tcp_pcb->remote_ip)->addr, 4);
                    ent->send_q      = (uint32_t)tcp_sndbuf(d->sockets[s].tcp_pcb);
                } else if (d->sockets[s].type == 2 /* UDP */ && d->sockets[s].udp_pcb != NULL) {
                    ent->local_port  = d->sockets[s].udp_pcb->local_port;
                    ent->remote_port = d->sockets[s].udp_pcb->remote_port;
                    memcpy(ent->local_addr, &ip_2_ip4(&d->sockets[s].udp_pcb->local_ip)->addr, 4);
                    memcpy(ent->remote_addr, &ip_2_ip4(&d->sockets[s].udp_pcb->remote_ip)->addr, 4);
                } else if (d->sockets[s].type == 3 /* RAW */ && d->sockets[s].raw_pcb != NULL) {
                    ent->local_port  = (uint16_t)d->sockets[s].protocol;
                    memcpy(ent->local_addr, &ip_2_ip4(&d->sockets[s].raw_pcb->local_ip)->addr, 4);
                    memcpy(ent->remote_addr, &ip_2_ip4(&d->sockets[s].raw_pcb->remote_ip)->addr, 4);
                }
                count++;
            }
        }
    } else {
        /* Legacy TnSocketInfo enumeration */
        TnSocketInfo *out_info = (TnSocketInfo *)out_ptr;
        for (s = 0; s < TN_MAX_GLOBAL_SOCKETS && count < max_entries; s++) {
            if (d->sockets[s].in_use) {
                TnSocketInfo *ent = &out_info[count];
                ent->proto = (UBYTE)d->sockets[s].type;
                ent->state = (UBYTE)d->sockets[s].tcp_state;
                ent->recv_q = d->sockets[s].rx_count;
                ent->send_q = 0;

                if (d->sockets[s].type == 1 /* TCP */ && d->sockets[s].tcp_pcb != NULL) {
                    ent->local_port  = d->sockets[s].tcp_pcb->local_port;
                    ent->remote_port = d->sockets[s].tcp_pcb->remote_port;
                    ent->local_ip    = ip_2_ip4(&d->sockets[s].tcp_pcb->local_ip)->addr;
                    ent->remote_ip   = ip_2_ip4(&d->sockets[s].tcp_pcb->remote_ip)->addr;
                    ent->send_q      = (ULONG)tcp_sndbuf(d->sockets[s].tcp_pcb);
                } else if (d->sockets[s].type == 2 /* UDP */ && d->sockets[s].udp_pcb != NULL) {
                    ent->local_port  = d->sockets[s].udp_pcb->local_port;
                    ent->remote_port = d->sockets[s].udp_pcb->remote_port;
                    ent->local_ip    = ip_2_ip4(&d->sockets[s].udp_pcb->local_ip)->addr;
                    ent->remote_ip   = ip_2_ip4(&d->sockets[s].udp_pcb->remote_ip)->addr;
                } else if (d->sockets[s].type == 3 /* RAW */ && d->sockets[s].raw_pcb != NULL) {
                    ent->local_port  = (UWORD)d->sockets[s].protocol;
                    ent->remote_port = 0;
                    ent->local_ip    = ip_2_ip4(&d->sockets[s].raw_pcb->local_ip)->addr;
                    ent->remote_ip   = ip_2_ip4(&d->sockets[s].raw_pcb->remote_ip)->addr;
                } else {
                    ent->local_port  = 0;
                    ent->remote_port = 0;
                    ent->local_ip    = 0;
                    ent->remote_ip   = 0;
                }
                count++;
            }
        }
    }

    imsg->result = count;
    imsg->err_no = 0;
    return 0; /* TN_IPC_REPLY_NOW */
}