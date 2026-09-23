/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet ? Operational & Telemetry IPC Handlers Implementation (ipc_status.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_status.h"
#include "netif_mgr.h"
#include "slot_table.h"
#include "syslog.h"
#include "ipc_dispatch.h"
#include "../common/config_text.h"
#include <string.h>

#if !defined(TN_HOST_BUILD)
#include <lwip/stats.h>
#include <lwip/memp.h>
#include <lwip/dhcp.h>
#include <lwip/prot/dhcp.h>
#endif

#if defined(TN_HOST_BUILD)
/* Host harness: tn_logf is stubbed by the test, Exec/DOS/lwIP calls below
 * are compiled out; the mask classification itself stays covered. */
#define tn_logf(tier, ...) do { (void)(tier); } while (0)
#endif

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

/* TNET-108: list every set bit of a mask as " KEY KEY ..." (names from
 * tn_recfg_key_name). Writes "(none)" when the mask is empty. */
static void tn_recfg_format_mask(uint32_t mask, char *buf, int size)
{
    int o = 0;
    uint32_t bit;

    if (mask == 0) {
        tn_str_copy_clean(buf, "(none)", size);
        return;
    }
    buf[0] = '\0';
    for (bit = 1; bit <= TN_RECFG_ALL; bit <<= 1) {
        if (mask & bit) {
            const char *name = tn_recfg_key_name(bit);
            if (name != NULL) {
                int n = 0;
                while (name[n] && o + n + 2 < size) n++;
                if (o > 0 && o + 1 < size) buf[o++] = ' ';
                while (n-- > 0 && o + 1 < size) buf[o++] = *name++;
                buf[o] = '\0';
            }
        }
    }
}

int tn_ipc_cmd_reconfig(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnPrefs old;
    uint32_t restart = 0;
    uint32_t live = 0;
    uint32_t failed = 0;
    uint32_t applied;
    BOOL loaded;
    char mask_buf[192];
    (void)slot;

    old = d->prefs;
    loaded = tn_prefs_load(&d->prefs);

    /* Classify the delta: restart-only interface keys vs hot-reloadable */
    tn_recfg_diff(&old, &d->prefs, &restart, &live);

    /* LOGLEVEL: apply first so the summary lines below follow the new tier */
    if (live & TN_RECFG_LOGLEVEL) {
        g_log_level = tn_recfg_effective_loglevel(&d->prefs);
        tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: log tier -> %d\n", g_log_level);
    }

    /* PRIORITY: live SetTaskPri on the daemon task (this handler runs in it) */
#if !defined(TN_HOST_BUILD)
    if (live & TN_RECFG_PRIORITY) {
        struct Task *self = FindTask(NULL);
        BYTE prev = SetTaskPri(self, (BYTE)d->prefs.priority);
        tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: task priority %ld -> %ld\n",
                (LONG)prev, (LONG)d->prefs.priority);
    }
#endif

    /* LOG: close old handle, open new path for append (requester-suppressed) */
#if !defined(TN_HOST_BUILD)
    if (live & TN_RECFG_LOG) {
        if (d->prefs.log_file[0] == '\0') {
            tn_log_close_file();
            tn_log(TN_LOG_BASIC, "tolunnet: RECONFIG: log file closed\n");
        } else if (tn_log_open_file(d->prefs.log_file)) {
            tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: log file -> %s\n", d->prefs.log_file);
        } else {
            failed |= TN_RECFG_LOG;
            tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: cannot open log file '%s'\n",
                    d->prefs.log_file);
        }
    }
#endif

    /* DATABASE_ORDER: recorded for the netdb layer (§D1 reload flag) */
    if (live & TN_RECFG_DATABASE_ORDER) {
        tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: netdb database order -> %s\n",
                d->prefs.database_order[0] ? d->prefs.database_order : "(default)");
    }

    /* SELECTORS: grow-only live resize of the selector table */
    if (live & TN_RECFG_SELECTORS) {
        uint32_t want = (d->prefs.selectors > 0) ? d->prefs.selectors : TN_MAX_SELECTORS;
        if (want > d->max_selectors) {
            if (tn_selector_table_grow(d, want)) {
                tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: selector table grown to %lu entries\n",
                        (ULONG)d->max_selectors);
            } else {
                failed |= TN_RECFG_SELECTORS;
                tn_log(TN_LOG_BASIC, "tolunnet: RECONFIG: selector table grow failed (no memory)\n");
            }
        }
    }

    /* STATS: any change resets the counters; NO freezes reports at zero.
     * Only increment-only counters are zeroed — lwip_stats.mem.used and the
     * per-pool used counters are alloc/free-paired and would underflow under
     * live allocations, so they keep running. */
    if (live & TN_RECFG_STATS) {
        d->stats_enabled = d->prefs.stats;
        memset(d->ipc_calls, 0, sizeof(d->ipc_calls));
        d->deferred_replies = 0;
        d->sigio_sent = 0;
        d->selector_wakeups = 0;
        d->mainloop_ticks = 0;
        d->s2_rx_frames = 0;
        d->s2_rx_bytes = 0;
        d->s2_rx_drops = 0;
        d->s2_tx_frames = 0;
        d->s2_tx_bytes = 0;
        d->s2_tx_drops = 0;
        d->rx_high_water = 0;
#if !defined(TN_HOST_BUILD) && LWIP_STATS
        memset(&lwip_stats.link,   0, sizeof(lwip_stats.link));
        memset(&lwip_stats.etharp, 0, sizeof(lwip_stats.etharp));
        memset(&lwip_stats.ip,     0, sizeof(lwip_stats.ip));
        memset(&lwip_stats.icmp,   0, sizeof(lwip_stats.icmp));
        memset(&lwip_stats.udp,    0, sizeof(lwip_stats.udp));
        memset(&lwip_stats.tcp,    0, sizeof(lwip_stats.tcp));
        lwip_stats.mem.err  = 0;
        lwip_stats.mem.max  = 0;
#endif
        tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: statistics counters reset (%s)\n",
                d->stats_enabled ? "reporting on" : "reporting off");
    }

    /* SYSLOG: arm/disarm UDP-514 forwarding of the daemon log */
#if !defined(TN_HOST_BUILD)
    if (live & TN_RECFG_SYSLOG) {
        if (!tn_syslog_apply(d->prefs.syslog_host)) {
            failed |= TN_RECFG_SYSLOG;
        }
    }
#endif

    /* DNS/DNS2/HOSTNAME/MTU: pre-existing live keys (TNET-063/064) */
    tn_apply_live_config(d);

    applied = live & ~failed;

    if (restart != 0) {
        tn_recfg_format_mask(restart, mask_buf, sizeof(mask_buf));
        tn_logf(TN_LOG_BASIC, "tolunnet: RECONFIG: needs restart: %s\n", mask_buf);
    } else {
        tn_log(TN_LOG_BASIC, "tolunnet: RECONFIG applied\n");
    }

    /* Reply: legacy masks in args[0..2] + versioned struct when a buffer of
     * sufficient size was supplied in ptrs[0] / args[4] (GETSTATUS convention). */
    imsg->args[0] = (LONG)applied;
    imsg->args[1] = (LONG)restart;
    imsg->args[2] = (LONG)failed;
    if (imsg->ptrs[0] != NULL && imsg->args[4] >= (LONG)sizeof(TnReconfigResponse)) {
        TnReconfigResponse *resp = (TnReconfigResponse *)imsg->ptrs[0];
        memset(resp, 0, sizeof(TnReconfigResponse));
        resp->struct_size = sizeof(TnReconfigResponse);
        resp->version = TN_RECFG_VERSION;
        resp->applied = applied;
        resp->needs_restart = restart;
        resp->failed = failed;
    }

    imsg->result = loaded ? 0 : -1;
    imsg->err_no = loaded ? 0 : ENOENT;
    return 0; /* TN_IPC_REPLY_NOW */
}

/* TNET-152 (RC3): robust stop. Signal(SIGBREAKF_CTRL_C) never reaches the
 * daemon task in the bench environment (alive-but-deaf, evidence
 * 20260920-194631) while IPC provably works — so the stop travels the
 * proven channel. The handler only clears d->running; the main loop
 * performs the shutdown sequence after replying. */
int tn_ipc_cmd_stop(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    (void)slot;
    if (d == NULL || imsg == NULL) return TN_IPC_REPLY_NOW;
    imsg->result = 0;
    imsg->err_no = 0;
    if (d->bsd_lib != NULL && d->bsd_lib->lib_OpenCnt > 0) {
        /* TNET-059 semantics preserved: refuse while clients are open.
         * The refusal is reported so callers can reap/close first. */
        imsg->result = -1;
        imsg->err_no = EBUSY;
        return TN_IPC_REPLY_NOW;
    }
    /* TNET-152: defer the reply until after RemPort and SANA-II CloseDevice,
     * so the caller only wakes up when hardware and ports are 100% released. */
    d->stop_msg = imsg;
    d->running = FALSE;
    return TN_IPC_DEFER;
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

int tn_ipc_cmd_getstats(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnStats *out;
    LONG buf_size;
    (void)slot;

    if (imsg == NULL || imsg->ptrs[0] == NULL) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0; /* TN_IPC_REPLY_NOW */
    }

    buf_size = imsg->args[0];
    if (buf_size < (LONG)sizeof(TnStats)) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0; /* TN_IPC_REPLY_NOW */
    }

    out = (TnStats *)imsg->ptrs[0];
    memset(out, 0, sizeof(TnStats));
    out->struct_size = sizeof(TnStats);
    out->version = 1;

#if !defined(TN_HOST_BUILD)
#if LINK_STATS
    out->link.xmit     = lwip_stats.link.xmit;
    out->link.recv     = lwip_stats.link.recv;
    out->link.fw       = lwip_stats.link.fw;
    out->link.drop     = lwip_stats.link.drop;
    out->link.chkerr   = lwip_stats.link.chkerr;
    out->link.lenerr   = lwip_stats.link.lenerr;
    out->link.memerr   = lwip_stats.link.memerr;
    out->link.rterr    = lwip_stats.link.rterr;
    out->link.proterr  = lwip_stats.link.proterr;
    out->link.opterr   = lwip_stats.link.opterr;
    out->link.err      = lwip_stats.link.err;
    out->link.cachehit = lwip_stats.link.cachehit;
#endif

#if ETHARP_STATS
    out->etharp.xmit     = lwip_stats.etharp.xmit;
    out->etharp.recv     = lwip_stats.etharp.recv;
    out->etharp.fw       = lwip_stats.etharp.fw;
    out->etharp.drop     = lwip_stats.etharp.drop;
    out->etharp.chkerr   = lwip_stats.etharp.chkerr;
    out->etharp.lenerr   = lwip_stats.etharp.lenerr;
    out->etharp.memerr   = lwip_stats.etharp.memerr;
    out->etharp.rterr    = lwip_stats.etharp.rterr;
    out->etharp.proterr  = lwip_stats.etharp.proterr;
    out->etharp.opterr   = lwip_stats.etharp.opterr;
    out->etharp.err      = lwip_stats.etharp.err;
    out->etharp.cachehit = lwip_stats.etharp.cachehit;
#endif

#if IP_STATS
    out->ip.xmit     = lwip_stats.ip.xmit;
    out->ip.recv     = lwip_stats.ip.recv;
    out->ip.fw       = lwip_stats.ip.fw;
    out->ip.drop     = lwip_stats.ip.drop;
    out->ip.chkerr   = lwip_stats.ip.chkerr;
    out->ip.lenerr   = lwip_stats.ip.lenerr;
    out->ip.memerr   = lwip_stats.ip.memerr;
    out->ip.rterr    = lwip_stats.ip.rterr;
    out->ip.proterr  = lwip_stats.ip.proterr;
    out->ip.opterr   = lwip_stats.ip.opterr;
    out->ip.err      = lwip_stats.ip.err;
    out->ip.cachehit = lwip_stats.ip.cachehit;
#endif

#if ICMP_STATS
    out->icmp.xmit     = lwip_stats.icmp.xmit;
    out->icmp.recv     = lwip_stats.icmp.recv;
    out->icmp.fw       = lwip_stats.icmp.fw;
    out->icmp.drop     = lwip_stats.icmp.drop;
    out->icmp.chkerr   = lwip_stats.icmp.chkerr;
    out->icmp.lenerr   = lwip_stats.icmp.lenerr;
    out->icmp.memerr   = lwip_stats.icmp.memerr;
    out->icmp.rterr    = lwip_stats.icmp.rterr;
    out->icmp.proterr  = lwip_stats.icmp.proterr;
    out->icmp.opterr   = lwip_stats.icmp.opterr;
    out->icmp.err      = lwip_stats.icmp.err;
    out->icmp.cachehit = lwip_stats.icmp.cachehit;
#endif

#if UDP_STATS
    out->udp.xmit     = lwip_stats.udp.xmit;
    out->udp.recv     = lwip_stats.udp.recv;
    out->udp.fw       = lwip_stats.udp.fw;
    out->udp.drop     = lwip_stats.udp.drop;
    out->udp.chkerr   = lwip_stats.udp.chkerr;
    out->udp.lenerr   = lwip_stats.udp.lenerr;
    out->udp.memerr   = lwip_stats.udp.memerr;
    out->udp.rterr    = lwip_stats.udp.rterr;
    out->udp.proterr  = lwip_stats.udp.proterr;
    out->udp.opterr   = lwip_stats.udp.opterr;
    out->udp.err      = lwip_stats.udp.err;
    out->udp.cachehit = lwip_stats.udp.cachehit;
#endif

#if TCP_STATS
    out->tcp.xmit     = lwip_stats.tcp.xmit;
    out->tcp.recv     = lwip_stats.tcp.recv;
    out->tcp.fw       = lwip_stats.tcp.fw;
    out->tcp.drop     = lwip_stats.tcp.drop;
    out->tcp.chkerr   = lwip_stats.tcp.chkerr;
    out->tcp.lenerr   = lwip_stats.tcp.lenerr;
    out->tcp.memerr   = lwip_stats.tcp.memerr;
    out->tcp.rterr    = lwip_stats.tcp.rterr;
    out->tcp.proterr  = lwip_stats.tcp.proterr;
    out->tcp.opterr   = lwip_stats.tcp.opterr;
    out->tcp.err      = lwip_stats.tcp.err;
    out->tcp.cachehit = lwip_stats.tcp.cachehit;
#endif

#if MEM_STATS
    out->mem_used  = (uint32_t)lwip_stats.mem.used;
    out->mem_max   = (uint32_t)lwip_stats.mem.max;
    out->mem_avail = (uint32_t)lwip_stats.mem.avail;
    out->mem_err   = (uint32_t)lwip_stats.mem.err;
#endif

#if MEMP_STATS
    {
        static const char * const s_memp_names[MEMP_MAX] = {
#define LWIP_MEMPOOL(name,num,size,desc) desc,
#include "lwip/priv/memp_std.h"
        };
        int p;
        int count = 0;
        for (p = 0; p < MEMP_MAX && count < TN_STATS_MAX_MEMP; p++) {
            if (memp_pools[p] != NULL && memp_pools[p]->stats != NULL) {
                const char *desc = s_memp_names[p];
                if (desc != NULL) {
                    strncpy(out->memp[count].name, desc, sizeof(out->memp[count].name) - 1);
                }
                out->memp[count].used  = (uint32_t)memp_pools[p]->stats->used;
                out->memp[count].max   = (uint32_t)memp_pools[p]->stats->max;
                out->memp[count].avail = (uint32_t)memp_pools[p]->stats->avail;
                out->memp[count].err   = (uint32_t)memp_pools[p]->stats->err;
                count++;
            }
        }
        out->num_memp = (uint16_t)count;
    }
#endif

#if LWIP_DHCP
    {
        TnNetif *prim = tn_netif_primary(d);
        struct dhcp *dhcp = netif_dhcp_data(&prim->lwip_if);
        if (dhcp != NULL && (dhcp->state == DHCP_STATE_BOUND ||
                             dhcp->state == DHCP_STATE_RENEWING ||
                             dhcp->state == DHCP_STATE_REBINDING)) {
            if (dhcp->t0_timeout > dhcp->lease_used) {
                out->daemon.lease_remaining = (uint32_t)(dhcp->t0_timeout - dhcp->lease_used) * DHCP_COARSE_TIMER_SECS;
            }
            if (dhcp->t1_timeout > dhcp->lease_used) {
                out->daemon.lease_t1 = (uint32_t)(dhcp->t1_timeout - dhcp->lease_used) * DHCP_COARSE_TIMER_SECS;
            }
            if (dhcp->t2_timeout > dhcp->lease_used) {
                out->daemon.lease_t2 = (uint32_t)(dhcp->t2_timeout - dhcp->lease_used) * DHCP_COARSE_TIMER_SECS;
            }
        }
    }
#endif
#endif /* !TN_HOST_BUILD */

    /* Daemon operational metrics */
    {
        int i;
        for (i = 0; i < 32; i++) {
            out->daemon.ipc_calls[i] = d->ipc_calls[i];
        }
        out->daemon.deferred_replies = d->deferred_replies;
        out->daemon.sigio_sent       = d->sigio_sent;
        out->daemon.selector_wakeups = d->selector_wakeups;
        out->daemon.mainloop_ticks   = d->mainloop_ticks;
        out->daemon.dns_late_replies = d->dns_late_replies;
        out->daemon.s2_rx_frames     = d->s2_rx_frames;
        out->daemon.s2_rx_bytes      = d->s2_rx_bytes;
        out->daemon.s2_rx_drops      = d->s2_rx_drops;
        out->daemon.s2_tx_frames     = d->s2_tx_frames;
        out->daemon.s2_tx_bytes      = d->s2_tx_bytes;
        out->daemon.s2_tx_drops      = d->s2_tx_drops;
        out->daemon.rx_high_water    = d->rx_high_water;
        out->daemon.uptime_secs      = d->mainloop_ticks / 10;
    }

    /* TNET-108: STATS=NO — reporting off. Counters were zeroed when the key
     * was applied; report zeroed tables so a disabled stack cannot leak
     * telemetry through GETSTATS. */
    if (!d->stats_enabled) {
        uint16_t keep_size = out->struct_size;
        uint16_t keep_ver  = out->version;
        memset(out, 0, sizeof(TnStats));
        out->struct_size = keep_size;
        out->version = keep_ver;
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0; /* TN_IPC_REPLY_NOW */
}