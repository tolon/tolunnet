/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — NetDB IPC Handlers Implementation (ipc_netdb.c).
 *
 * TNET-150: a DEFERRED gethostbyname parks the client's TnIpcMsg with
 * lwIP (callback_arg). The daemon tracks every parked message in
 * TnDaemon.dns_pending so that (a) CLOSE can cancel it with
 * ECONNABORTED, and (b) a late dns_found_cb that finds no record —
 * the client watchdog abandoned the call, or the base closed — returns
 * without touching the message instead of writing into freed memory.
 */
#include "ipc_netdb.h"

#include <string.h>

/* TNET-150: cancel every deferred DNS request of a closing base — called
 * from the CLOSE IPC handler BEFORE the reply (after the reply the client
 * frees its message storage, so no late callback may touch these). The
 * lwIP DNS entry itself stays (no removal API in 2.2); its callback finds
 * no pending record and bails. Host-testable. */
void tn_dns_cancel_for_base(TnDaemon *d, TnSocketBase *base)
{
    int p;
    for (p = 0; p < TN_DNS_PENDING_MAX; p++) {
        if (d->dns_pending[p].in_use && d->dns_pending[p].base == base) {
            TnIpcMsg *pending = d->dns_pending[p].imsg;
            char nm[64];
            int j = 0;
            while (d->dns_pending[p].name[j] && j < 63) {
                nm[j] = d->dns_pending[p].name[j];
                j++;
            }
            nm[j] = '\0';
            d->dns_pending[p].in_use = 0;
            d->dns_pending[p].imsg = NULL;
            d->dns_pending[p].base = NULL;
            if (d->dns_pending_count > 0) d->dns_pending_count--;
            tn_logf(TN_LOG_BASIC,
                    "tolunnet: cancelling pending DNS '%s' on close\n", nm);
            if (pending != NULL) {
                pending->result = 0;
                pending->err_no = ECONNABORTED;
                ReplyMsg((struct Message *)pending);
            }
        }
    }
}

/* DNS callback from lwIP when asynchronous host lookup completes.
 * Non-static: the host TNET-150 test invokes it directly. */
void tn_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    TnIpcMsg *imsg = (TnIpcMsg *)callback_arg;
    TnDaemon *d = &g_daemon;
    TnDnsPending *rec = NULL;
    TnSocketBase *base;
    int i;

    if (imsg == NULL) return;

    /* TNET-150: only a tracked request may be completed. An untracked
     * imsg means the client timed out and abandoned it (the message may
     * already be freed) or the base closed and the request was cancelled
     * — touch nothing. */
    for (i = 0; i < TN_DNS_PENDING_MAX; i++) {
        if (d->dns_pending[i].in_use && d->dns_pending[i].imsg == imsg) {
            rec = &d->dns_pending[i];
            break;
        }
    }
    if (rec == NULL) {
        d->dns_late_replies++;
        tn_logf(TN_LOG_BASIC,
                "tolunnet: DNS reply for abandoned request '%s' ignored (late=%lu)\n",
                (name != NULL) ? name : "?", d->dns_late_replies);
        return;
    }
    base = rec->base;
    rec->in_use = 0;
    rec->imsg = NULL;
    rec->base = NULL;
    if (d->dns_pending_count > 0) d->dns_pending_count--;

    if (base == NULL) {
        ReplyMsg((struct Message *)imsg);
        return;
    }

    if (ipaddr != NULL) {
        int j = 0;
        base->hostent_addr = ip_addr_get_ip4_u32(ipaddr);
        if (name != NULL) {
            while (name[j] && j < 63) {
                base->hostent_name[j] = name[j];
                j++;
            }
        }
        base->hostent_name[j] = '\0';
        base->hostent_addrs[0] = (STRPTR)&base->hostent_addr;
        base->hostent_addrs[1] = NULL;
        base->hostent_aliases[0] = NULL;
        base->hostent_data.h_name      = (STRPTR)base->hostent_name;
        base->hostent_data.h_aliases   = (STRPTR *)base->hostent_aliases;
        base->hostent_data.h_addrtype  = AF_INET;
        base->hostent_data.h_length    = 4;
        base->hostent_data.h_addr_list = (APTR)base->hostent_addrs;

        imsg->result = (LONG)(intptr_t)&base->hostent_data;
        imsg->err_no = 0;
    } else {
        imsg->result = 0; /* NULL */
        imsg->err_no = ENOENT;
    }

    ReplyMsg((struct Message *)imsg);
}

static void tn_ipc_fill_hostent_now(TnSocketBase *base, const char *hostname,
                                    uint32_t addr_network)
{
    int j = 0;
    base->hostent_addr = addr_network;
    if (hostname != NULL) {
        while (hostname[j] && j < 63) {
            base->hostent_name[j] = hostname[j];
            j++;
        }
    }
    base->hostent_name[j] = '\0';
    base->hostent_addrs[0] = (STRPTR)&base->hostent_addr;
    base->hostent_addrs[1] = NULL;
    base->hostent_aliases[0] = NULL;
    base->hostent_data.h_name      = (STRPTR)base->hostent_name;
    base->hostent_data.h_aliases   = (STRPTR *)base->hostent_aliases;
    base->hostent_data.h_addrtype  = AF_INET;
    base->hostent_data.h_length    = 4;
    base->hostent_data.h_addr_list = (APTR)base->hostent_addrs;
}

int tn_ipc_cmd_gethostbyname(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnSocketBase *base = (TnSocketBase *)imsg->socket_base;
    const char *hostname = (const char *)imsg->ptrs[0];
    ip4_addr_t resolved;
    (void)d;
    (void)slot;

    if (base == NULL || hostname == NULL || *hostname == '\0') {
        imsg->result = 0;
        imsg->err_no = ENOENT;
        return 0; /* TN_IPC_REPLY_NOW */
    }

    if (ip4addr_aton(hostname, &resolved)) {
        tn_ipc_fill_hostent_now(base, hostname, resolved.addr);
        imsg->result = (LONG)(intptr_t)&base->hostent_data;
        imsg->err_no = 0;
        return 0; /* TN_IPC_REPLY_NOW */
    } else {
        ip_addr_t dns_res;
        err_t derr = dns_gethostbyname(hostname, &dns_res, tn_dns_found_cb, imsg);
        if (derr == ERR_OK) {
            tn_ipc_fill_hostent_now(base, hostname, ip_addr_get_ip4_u32(&dns_res));
            imsg->result = (LONG)(intptr_t)&base->hostent_data;
            imsg->err_no = 0;
            return 0; /* TN_IPC_REPLY_NOW */
        } else if (derr == ERR_INPROGRESS) {
            /* TNET-150: register the deferred request; a full table is a
             * policy EAGAIN right now rather than an untracked deferral.
             * Same-name second waiter also EAGAINs: lwIP keeps one
             * callback per DNS entry, so a stacked waiter would never be
             * called back. */
            int i;
            int eff = (d->prefs.dns_pending_max > 0 &&
                       d->prefs.dns_pending_max <= TN_DNS_PENDING_MAX)
                          ? (int)d->prefs.dns_pending_max
                          : TN_DNS_PENDING_MAX;
            for (i = 0; i < eff; i++) {
                if (d->dns_pending[i].in_use &&
                    strcmp(d->dns_pending[i].name, hostname) == 0) {
                    imsg->result = 0;
                    imsg->err_no = EAGAIN;
                    return 0; /* TN_IPC_REPLY_NOW */
                }
            }
            for (i = 0; i < eff; i++) {
                if (!d->dns_pending[i].in_use) {
                    int j = 0;
                    d->dns_pending[i].in_use = 1;
                    d->dns_pending[i].imsg = imsg;
                    d->dns_pending[i].base = base;
                    while (hostname[j] && j < 63) {
                        d->dns_pending[i].name[j] = hostname[j];
                        j++;
                    }
                    d->dns_pending[i].name[j] = '\0';
                    d->dns_pending[i].tick = d->mainloop_ticks;
                    if (d->dns_pending_count < TN_DNS_PENDING_MAX) d->dns_pending_count++;
                    return 1; /* TN_IPC_DEFER: tn_dns_found_cb will ReplyMsg */
                }
            }
            tn_logf(TN_LOG_BASIC,
                    "tolunnet: DNS pending table full (%d) — EAGAIN for '%s'\n",
                    TN_DNS_PENDING_MAX, hostname);
            imsg->result = 0;
            imsg->err_no = EAGAIN;
            return 0; /* TN_IPC_REPLY_NOW */
        } else {
            imsg->result = 0; /* NULL */
            imsg->err_no = ENOENT;
            return 0; /* TN_IPC_REPLY_NOW */
        }
    }
}
