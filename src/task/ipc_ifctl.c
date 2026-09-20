/*
 * tolunnet — IFCTL IPC handler (CLOSE §B.7).
 *
 * Interface control for AddNetInterface / ConfigureNetInterface /
 * Online / Offline: LIST the daemon's netifs, bring one up (lwIP
 * netif_set_up + S2_ONLINE, idempotent) or down (netif_set_down + a
 * light S2_OFFLINE — no device close), and apply address/netmask/gateway
 * live via netif_set_addr.
 */
#include "ipc_ifctl.h"
#include "../common/sockaddr_util.h"
#include <string.h>

static TnNetif *tn_ifctl_pick(TnDaemon *d, LONG idx)
{
    if (idx >= 0 && idx < TN_MAX_NETIF) {
        if (d->ifs[idx].in_use) return &d->ifs[idx];
        return NULL;
    }
    if (idx == -1) return tn_netif_primary(d);
    return NULL;
}

int tn_ipc_cmd_ifctl(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG op = imsg->args[0];
    (void)slot;

    if (op == TN_IFCTL_LIST) {
        TnIfInfo *out = (TnIfInfo *)imsg->ptrs[0];
        LONG cap = imsg->args[4];
        LONG count = 0;
        int i;

        if (out == NULL || cap <= 0) {
            imsg->result = -1;
            imsg->err_no = EINVAL;
            return 0;
        }
        for (i = 0; i < TN_MAX_NETIF && count < cap; i++) {
            TnNetif *n = &d->ifs[i];
            if (!n->in_use) continue;
            {
                TnIfInfo *ent = &out[count];
                memset(ent, 0, sizeof(TnIfInfo));
                ent->struct_size = (uint16_t)sizeof(TnIfInfo);
                ent->in_use = 1;
                ent->is_up = (n->lwip_if.flags & NETIF_FLAG_UP) ? 1 : 0;
                ent->is_dhcp = n->is_dhcp ? 1 : 0;
                ent->link_up = n->link_up ? 1 : 0;
                {
                    int j;
                    for (j = 0; j < 15 && n->name[j]; j++) ent->name[j] = n->name[j];
                    ent->name[j] = '\0';
                }
                {
                    int j;
                    const char *dev = n->s2if.device_name;
                    for (j = 0; j < 39 && dev && dev[j]; j++) ent->device[j] = dev[j];
                    ent->device[j] = '\0';
                }
                ent->unit = n->unit;
                ent->addr = netif_ip4_addr(&n->lwip_if)->addr;
                ent->mask = netif_ip4_netmask(&n->lwip_if)->addr;
                ent->gw = netif_ip4_gw(&n->lwip_if)->addr;
                count++;
            }
        }
        imsg->result = count;
        imsg->err_no = 0;
        return 0;
    } else if (op == TN_IFCTL_UP) {
        TnNetif *n = tn_ifctl_pick(d, imsg->args[1]);
        if (n == NULL) {
            imsg->result = -1;
            imsg->err_no = ENXIO;
            return 0;
        }
        netif_set_up(&n->lwip_if);
        /* S2_ONLINE is idempotent ("already online" treated as ok) */
        if (tn_s2_online(&n->s2if, NULL) != TN_S2_OK) {
            tn_log(TN_LOG_BASIC, "tolunnet: IFCTL UP: S2_ONLINE failed\n");
        }
        n->link_up = TRUE;
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    } else if (op == TN_IFCTL_DOWN) {
        TnNetif *n = tn_ifctl_pick(d, imsg->args[1]);
        if (n == NULL) {
            imsg->result = -1;
            imsg->err_no = ENXIO;
            return 0;
        }
        netif_set_down(&n->lwip_if);
        {
            /* light S2_OFFLINE — no device close/teardown */
            struct IOSana2Req *io = n->s2if.io;
            if (io != NULL) {
                io->ios2_Req.io_Command = S2_OFFLINE;
                io->ios2_Req.io_Error = 0;
                DoIO((struct IORequest *)io);
                if (io->ios2_Req.io_Error != 0) {
                    tn_log_s2err("S2_OFFLINE", io->ios2_Req.io_Error,
                                 io->ios2_WireError);
                }
            }
            n->s2if.online = FALSE;
        }
        n->link_up = FALSE;
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    } else if (op == TN_IFCTL_SET) {
        TnNetif *n = tn_ifctl_pick(d, imsg->args[1]);
        ip4_addr_t ip, nm, gw;
        if (n == NULL) {
            imsg->result = -1;
            imsg->err_no = ENXIO;
            return 0;
        }
        ip.addr = netif_ip4_addr(&n->lwip_if)->addr;
        nm.addr = netif_ip4_netmask(&n->lwip_if)->addr;
        gw.addr = netif_ip4_gw(&n->lwip_if)->addr;
        if ((uint32_t)imsg->args[2] != 0) ip.addr = (uint32_t)imsg->args[2];
        if ((uint32_t)imsg->args[3] != 0) nm.addr = (uint32_t)imsg->args[3];
        if ((uint32_t)imsg->args[4] != 0) gw.addr = (uint32_t)imsg->args[4];
        netif_set_addr(&n->lwip_if, &ip, &nm, &gw);
        if (gw.addr != 0) {
            /* keep lwIP's default gateway in step with the interface */
            netif_set_gw(&n->lwip_if, &gw);
        }
        n->is_dhcp = FALSE; /* explicit addressing overrides DHCP */
        tn_log(TN_LOG_BASIC, "tolunnet: IFCTL SET applied\n");
        imsg->result = 0;
        imsg->err_no = 0;
        return 0;
    }

    imsg->result = -1;
    imsg->err_no = EINVAL;
    return 0;
}
