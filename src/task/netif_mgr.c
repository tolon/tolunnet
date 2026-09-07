/*
 * tolunnet ? Network Interface Manager Implementation.
 *
 * ROUND4b ?B & ?L.
 */
#include "netif_mgr.h"

void ip_to_str(char *buf, const ip4_addr_t *addr)
{
    ULONG ip;
    char *p;
    int octet;

    if (buf == NULL || addr == NULL) return;
    ip = lwip_ntohl(addr->addr);
    p = buf;

    for (octet = 3; octet >= 0; octet--) {
        ULONG val = (ip >> (octet * 8)) & 0xFF;
        char tmp[4];
        int i = 0;
        if (val == 0) tmp[i++] = '0';
        while (val > 0) {
            tmp[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
        while (i > 0) *p++ = tmp[--i];
        if (octet > 0) *p++ = '.';
    }
    *p = '\0';
}

void tn_drain_loopback(void)
{
    struct netif *n;
    int guard = 0;
    BOOL had;
    do {
        had = FALSE;
        NETIF_FOREACH(n) {
            if (n->loop_first != NULL) {
                netif_poll(n);
                had = TRUE;
            }
        }
        guard++;
    } while (had && guard < 64);
}

void tn_apply_live_config(TnDaemon *d)
{
    ip4_addr_t dns;
    if (d == NULL) return;

    /* DNS1: configured server, only touched when explicitly set (TNET-078) */
    if (d->prefs.dns_server[0] != '\0' && ip4addr_aton(d->prefs.dns_server, &dns)) {
        dns_setserver(0, (const ip_addr_t *)&dns);
    }

    /* DNS2: secondary resolver, only when configured */
    if (d->prefs.dns2[0] != '\0' && ip4addr_aton(d->prefs.dns2, &dns)) {
        dns_setserver(1, (const ip_addr_t *)&dns);
        tn_logf(TN_LOG_BASIC, "tolunnet: secondary DNS %s\n", d->prefs.dns2);
    }

    /* HOSTNAME: DHCP option 12 + gethostname() for future library openers */
    if (d->prefs.hostname[0] != '\0') {
        netif_set_hostname(&d->netif, d->prefs.hostname);
    }

    /* MTU: clamp the netif below the driver-reported maximum */
    if (d->prefs.mtu >= 576 && d->prefs.mtu <= 1500 && d->netif.mtu != 0 &&
        d->prefs.mtu < d->netif.mtu) {
        d->netif.mtu = (u16_t)d->prefs.mtu;
        tn_logf(TN_LOG_BASIC, "tolunnet: MTU clamped to %lu (driver max %lu)\n",
                d->prefs.mtu, (ULONG)d->s2if.mtu);
    }
}