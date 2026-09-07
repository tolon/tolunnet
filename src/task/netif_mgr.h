/*
 * tolunnet — Network Interface Manager.
 *
 * ROUND4b §B & §L (Multi-netif and IPv6-ready architecture).
 */
#ifndef TOLUNNET_NETIF_MGR_H
#define TOLUNNET_NETIF_MGR_H

#include "task_ctx.h"

#ifndef TN_MAX_NETIF
#define TN_MAX_NETIF 4
#endif

typedef struct TnNetif {
    struct netif   lwip_if;
    TnSana2If      s2if;
    BOOL           in_use;
    char           name[16];
    uint16_t       unit;
    uint8_t        family;
    uint8_t        addr_count;
    ip_addr_t      addrs[4];
    ip_addr_t      netmask;
    ip_addr_t      gw;
    BOOL           is_dhcp;
    BOOL           link_up;
} TnNetif;

void tn_apply_live_config(TnDaemon *d);
void tn_drain_loopback(void);
void ip_to_str(char *buf, const ip4_addr_t *addr);

#endif /* TOLUNNET_NETIF_MGR_H */
