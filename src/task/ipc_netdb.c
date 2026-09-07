/*
 * tolunnet ? NetDB IPC Handlers Implementation (ipc_netdb.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_netdb.h"

/* DNS callback from lwIP when asynchronous host lookup completes (Round 4 ?C8) */
static void tn_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    TnIpcMsg *imsg = (TnIpcMsg *)callback_arg;
    TnSocketBase *base;

    if (imsg == NULL) return;

    base = (TnSocketBase *)imsg->socket_base;
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
        int j = 0;
        base->hostent_addr = resolved.addr;
        while (hostname[j] && j < 63) {
            base->hostent_name[j] = hostname[j];
            j++;
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
        return 0; /* TN_IPC_REPLY_NOW */
    } else {
        ip_addr_t dns_res;
        err_t derr = dns_gethostbyname(hostname, &dns_res, tn_dns_found_cb, imsg);
        if (derr == ERR_OK) {
            int j = 0;
            base->hostent_addr = ip_addr_get_ip4_u32(&dns_res);
            while (hostname[j] && j < 63) {
                base->hostent_name[j] = hostname[j];
                j++;
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
            return 0; /* TN_IPC_REPLY_NOW */
        } else if (derr == ERR_INPROGRESS) {
            return 1; /* TN_IPC_DEFER: tn_dns_found_cb will ReplyMsg */
        } else {
            imsg->result = 0; /* NULL */
            imsg->err_no = ENOENT;
            return 0; /* TN_IPC_REPLY_NOW */
        }
    }
}