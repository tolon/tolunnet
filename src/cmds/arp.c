/*
 * tolunnet — arp command (CMD-6, TNET-141). ReadArgs: SHOW/S
 * SHOW scans the primary interface's /24 with SIOCGARP and prints every
 * completed entry.
 */
#include "cmdlib.h"
#include "../common/tn_arp.h"
#include <net/if.h>
#include <net/if_arp.h>
#include <sys/sockio.h>
#include <string.h>

#define TEMPLATE "SHOW/S"

static void fmt_ip_mac(char *line, ULONG addr_network, const UBYTE *mac)
{
    const char *hex = "0123456789abcdef";
    ULONG a = ntohl(addr_network);
    int k, pos = 0;

    {
        ULONG o[4];
        o[0] = (a >> 24) & 0xFF; o[1] = (a >> 16) & 0xFF;
        o[2] = (a >> 8) & 0xFF;  o[3] = a & 0xFF;
        for (k = 0; k < 4; k++) {
            if (o[k] >= 100) {
                line[pos++] = (char)('0' + (o[k] / 100) % 10);
                line[pos++] = (char)('0' + (o[k] / 10) % 10);
                line[pos++] = (char)('0' + o[k] % 10);
            } else if (o[k] >= 10) {
                line[pos++] = (char)('0' + (o[k] / 10) % 10);
                line[pos++] = (char)('0' + o[k] % 10);
            } else {
                line[pos++] = (char)('0' + o[k]);
            }
            if (k < 3) line[pos++] = '.';
        }
    }
    line[pos++] = ' '; line[pos++] = ' '; line[pos++] = ' ';
    for (k = 0; k < 6; k++) {
        UBYTE m = mac[k];
        if (k > 0) line[pos++] = ':';
        line[pos++] = hex[(m >> 4) & 0xF];
        line[pos++] = hex[m & 0xF];
    }
    line[pos] = '\0';
}

static void arp_set_pa(struct arpreq *ar, ULONG a_net)
{
    /* sockaddr_in inside arpreq, byte-wise: no struct cast on a
     * short-aligned field (68000-safe, -Wcast-align clean). */
    UBYTE *p = (UBYTE *)&ar->arp_pa;
    int i;
    for (i = 0; i < 16; i++) p[i] = 0;
    p[0] = 16;
    p[1] = (UBYTE)AF_INET;
    p[4] = (UBYTE)(a_net >> 24);
    p[5] = (UBYTE)(a_net >> 16);
    p[6] = (UBYTE)(a_net >> 8);
    p[7] = (UBYTE)a_net;
}

static ULONG arp_if_addr(const struct ifreq *ifr)
{
    /* sin_addr sits at byte offset 4 of the ifr_addr sockaddr */
    const UBYTE *p = (const UBYTE *)&ifr->ifr_addr;
    return ((ULONG)p[4] << 24) | ((ULONG)p[5] << 16) |
           ((ULONG)p[6] << 8) | (ULONG)p[7];
}

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    struct ifconf ifc;
    struct ifreq ifr;
    struct arpreq ar;
    ULONG own_addr, mask, base, probe;
    int i, found = 0;
    char line[48];
    static char ifbuf[sizeof(struct ifreq) * 4];

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"arp");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    fd = tn_call_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        tn_cmd_printf("arp: cannot create socket\n");
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    /* Primary interface address */
    memset(&ifc, 0, sizeof(ifc));
    ifc.ifc_len = (LONG)sizeof(ifbuf);
    ifc.ifc_buf = ifbuf;
    if (tn_call_ioctl(fd, SIOCGIFCONF, &ifc) != 0 || ifc.ifc_len < (LONG)sizeof(struct ifreq)) {
        tn_cmd_printf("arp: SIOCGIFCONF failed\n");
        tn_call_closesocket(fd);
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    memset(&ifr, 0, sizeof(ifr));
    memcpy(ifr.ifr_name, ifbuf, IFNAMSIZ); /* ifr_name is at offset 0 */
    if (tn_call_ioctl(fd, SIOCGIFADDR, &ifr) != 0) {
        tn_cmd_printf("arp: SIOCGIFADDR failed\n");
        tn_call_closesocket(fd);
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    own_addr = arp_if_addr(&ifr);

    memset(&ifr, 0, sizeof(ifr));
    memcpy(ifr.ifr_name, ifbuf, IFNAMSIZ); /* ifr_name is at offset 0 */
    if (tn_call_ioctl(fd, SIOCGIFNETMASK, &ifr) != 0) {
        tn_cmd_printf("arp: SIOCGIFNETMASK failed\n");
        tn_call_closesocket(fd);
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    mask = arp_if_addr(&ifr);

    tn_cmd_printf("Address        HWaddress\n");

    base = own_addr & mask;
    for (i = 1; i <= 254; i++) {
        probe = base | htonl((ULONG)i);
        if (probe == own_addr) continue;
        memset(&ar, 0, sizeof(ar));
        arp_set_pa(&ar, probe);
        if (tn_call_ioctl(fd, TN_SIOCGARP, &ar) == 0) {
            fmt_ip_mac(line, probe, (const UBYTE *)ar.arp_ha.sa_data);
            tn_cmd_printf("%s\n", line);
            found++;
        }
    }

    if (found == 0) {
        tn_cmd_printf("(no completed ARP entries)\n");
    }

    tn_call_closesocket(fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
