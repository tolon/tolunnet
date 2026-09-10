/*
 * tolunnet — sockaddr to/from generic IP address marshaling (host-testable).
 *
 * Implements conversion between struct sockaddr / sockaddr_in / sockaddr_in6
 * and tn_ip_addr_t (ROUND4b §L).
 */
#include "sockaddr_util.h"
#include <string.h>

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define tn_htons(x) (x)
#define tn_ntohs(x) (x)
#else
static inline uint16_t tn_swap16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}
#define tn_htons(x) tn_swap16(x)
#define tn_ntohs(x) tn_swap16(x)
#endif

static uint16_t tn_sa_family_load(const struct sockaddr *sa)
{
    /* TNET-139: byte-safe read of sa_family — client buffers may be odd */
    sa_family_t fam;
    memcpy(&fam, &sa->sa_family, sizeof(fam));
    return (uint16_t)fam;
}

static void tn_sa_family_store(struct sockaddr *sa, uint16_t fam)
{
    sa_family_t f = (sa_family_t)fam;
    memcpy(&sa->sa_family, &f, sizeof(f));
}

int tn_ip_from_sockaddr(const struct sockaddr *sa, socklen_t salen, tn_ip_addr_t *out_ip, uint16_t *out_port)
{
    if (sa == NULL || salen == 0 || out_ip == NULL) {
        return 0;
    }

    if (tn_sa_family_load(sa) == AF_INET) {
        uint16_t family;
        uint16_t port;
        uint32_t addr;
        if (salen < (socklen_t)sizeof(struct sockaddr_in)) {
            return 0;
        }
        tn_sockin_load_bytes(sa, &family, &port, &addr);
        out_ip->family = AF_INET;
        out_ip->u.ip4 = addr;
        if (out_port != NULL) {
            *out_port = port;
        }
        return 1;
    }

    /* Future AF_INET6 branch: */
    if (tn_sa_family_load(sa) == AF_INET6) {
        /* If system provides struct sockaddr_in6: */
#ifdef AF_INET6
        /* 28 bytes minimum for sockaddr_in6 */
        if (salen < 28) {
            return 0;
        }
        out_ip->family = AF_INET6;
        /* Copy 16 bytes of IPv6 address from sin6_addr offset (offset 8 in standard POSIX) */
        memcpy(out_ip->u.ip6, ((const uint8_t *)sa) + 8, 16);
        if (out_port != NULL) {
            uint16_t p;
            memcpy(&p, ((const uint8_t *)sa) + 2, 2);
            *out_port = tn_ntohs(p);
        }
        return 1;
#else
        return 0;
#endif
    }

    return 0;
}

int tn_sockaddr_from_ip(struct sockaddr *sa, socklen_t *salen, const tn_ip_addr_t *ip, uint16_t port)
{
    if (sa == NULL || salen == NULL || ip == NULL) {
        return 0;
    }

    if (ip->family == AF_INET) {
        if (*salen < (socklen_t)sizeof(struct sockaddr_in)) {
            return 0;
        }
        memset(sa, 0, sizeof(struct sockaddr_in));
        tn_sockin_store_bytes(sa, AF_INET, port, ip->u.ip4);
        *salen = sizeof(struct sockaddr_in);
        return 1;
    }

    if (ip->family == AF_INET6) {
        if (*salen < 28) {
            return 0;
        }
        memset(sa, 0, 28);
#if defined(__AMIGA__) || defined(SIN_LEN) || defined(HAVE_SOCKADDR_LEN)
        ((uint8_t *)sa)[0] = 28; /* sin6_len */
#endif
        tn_sa_family_store(sa, AF_INET6);
        {
            uint16_t p = tn_htons(port);
            memcpy(((uint8_t *)sa) + 2, &p, 2);
        }
        memcpy(((uint8_t *)sa) + 8, ip->u.ip6, 16);
        *salen = 28;
        return 1;
    }

    return 0;
}

/*
 * TNET-139 byte-wise sockaddr_in access: fill/read an aligned local
 * sockaddr_in and memcpy it to/from the client buffer, so no word/long field
 * access ever happens on memory whose alignment we do not control. Layout
 * and byte order come from the platform's own struct definition, which keeps
 * the host tests honest.
 */
void tn_sockin_store_bytes(void *dst, uint16_t family, uint16_t port_host, uint32_t addr_network)
{
    struct sockaddr_in tmpl;
    if (dst == NULL) return;
    memset(&tmpl, 0, sizeof(tmpl));
#if defined(__AMIGA__) || defined(SIN_LEN) || defined(HAVE_SOCKADDR_LEN)
    tmpl.sin_len = sizeof(struct sockaddr_in);
#endif
    tmpl.sin_family = family;
    tmpl.sin_port = tn_htons(port_host);
    tmpl.sin_addr.s_addr = addr_network;
    memcpy(dst, &tmpl, sizeof(tmpl));
}

void tn_sockin_load_bytes(const void *src, uint16_t *family, uint16_t *port_host, uint32_t *addr_network)
{
    struct sockaddr_in tmpl;
    if (src == NULL) return;
    memcpy(&tmpl, src, sizeof(tmpl));
    if (family != NULL) {
        *family = tmpl.sin_family;
    }
    if (port_host != NULL) {
        *port_host = tn_ntohs(tmpl.sin_port);
    }
    if (addr_network != NULL) {
        *addr_network = tmpl.sin_addr.s_addr;
    }
}
