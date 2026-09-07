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

int tn_ip_from_sockaddr(const struct sockaddr *sa, socklen_t salen, tn_ip_addr_t *out_ip, uint16_t *out_port)
{
    if (sa == NULL || salen == 0 || out_ip == NULL) {
        return 0;
    }

    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin;
        if (salen < (socklen_t)sizeof(struct sockaddr_in)) {
            return 0;
        }
        sin = (const struct sockaddr_in *)sa;
        out_ip->family = AF_INET;
        out_ip->u.ip4 = sin->sin_addr.s_addr;
        if (out_port != NULL) {
            *out_port = tn_ntohs(sin->sin_port);
        }
        return 1;
    }

    /* Future AF_INET6 branch: */
    if (sa->sa_family == AF_INET6) {
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
        struct sockaddr_in *sin;
        if (*salen < (socklen_t)sizeof(struct sockaddr_in)) {
            return 0;
        }
        sin = (struct sockaddr_in *)sa;
        memset(sin, 0, sizeof(struct sockaddr_in));
#if defined(__AMIGA__) || defined(SIN_LEN) || defined(HAVE_SOCKADDR_LEN)
        sin->sin_len = sizeof(struct sockaddr_in);
#endif
        sin->sin_family = AF_INET;
        sin->sin_port = tn_htons(port);
        sin->sin_addr.s_addr = ip->u.ip4;
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
        sa->sa_family = AF_INET6;
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
