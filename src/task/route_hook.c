/*
 * tolunnet — lwIP routing hooks over the static route table (CLOSE §B.5).
 *
 * lwipopts.h defines LWIP_HOOK_IP4_ROUTE_SRC and LWIP_HOOK_ETHARP_GET_GW
 * to the two functions here. Precedence:
 *   1. on-link destinations never consult the static table (lwIP's own
 *      netif match runs first — the hook returns NULL for them);
 *   2. longest-prefix static route wins: its gateway is the next hop
 *      (route_hook gw) and the netif is the one where the gateway is
 *      on-link (falling back to netif_default);
 *   3. no static match: NULL → lwIP default behaviour (netif_default and
 *      its configured gateway).
 */
#include "task_ctx.h"
#include "route.h"

/* LWIP_HOOK_IP4_ROUTE_SRC(src, dest) — src may be NULL. Returns the netif
 * for dest or NULL to let lwIP route normally. */
struct netif *tn_route_hook_src(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    struct netif *n;
    int idx;
    const TnRoute *r;
    struct netif *gw_netif;
    ip4_addr_t net;

    (void)src;
    if (dest == NULL) return NULL;

    /* on-link on any netif: lwIP's own logic is correct, stay out of it */
    NETIF_FOREACH(n) {
        if (n == NULL || n->name == NULL) continue;
        if (ip4_addr_net_eq(dest, netif_ip4_addr(n), netif_ip4_netmask(n))) {
            return NULL;
        }
    }

    idx = tn_route_lookup(dest->addr);
    if (idx < 0) return NULL;
    r = tn_route_at(idx);
    if (r == NULL) return NULL;

    if (r->gw == 0) {
        /* interface route: first netif whose address matches the route
         * network, else the default netif */
        NETIF_FOREACH(n) {
            if (n == NULL || n->name == NULL) continue;
            net.addr = r->mask & netif_ip4_addr(n)->addr;
            if (net.addr == r->dest) return n;
        }
        return netif_default;
    }

    /* gateway route: netif where the gateway is on-link */
    net.addr = r->gw;
    gw_netif = NULL;
    NETIF_FOREACH(n) {
        if (n == NULL || n->name == NULL) continue;
        if (ip4_addr_net_eq(&net, netif_ip4_addr(n), netif_ip4_netmask(n))) {
            gw_netif = n;
            break;
        }
    }
    return (gw_netif != NULL) ? gw_netif : netif_default;
}

/* LWIP_HOOK_ETHARP_GET_GW(netif, ipaddr) — next-hop for an off-link
 * destination; NULL keeps netif->gw. */
const ip4_addr_t *tn_route_gw_get(struct netif *netif, const ip4_addr_t *ipaddr)
{
    static ip4_addr_t s_gw;
    int idx;
    const TnRoute *r;

    (void)netif;
    if (ipaddr == NULL) return NULL;

    idx = tn_route_lookup(ipaddr->addr);
    if (idx < 0) return NULL;
    r = tn_route_at(idx);
    if (r == NULL || r->gw == 0) return NULL;

    s_gw.addr = r->gw;
    return &s_gw;
}
