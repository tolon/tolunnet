/*
 * tolunnet — sockaddr to/from generic IP address marshaling (host-testable).
 *
 * Prepared for dual-stack IPv4 / IPv6 migration (ROUND4b §L).
 * No AmigaOS or lwIP dependencies required.
 */
#ifndef TOLUNNET_SOCKADDR_UTIL_H
#define TOLUNNET_SOCKADDR_UTIL_H

#include <stdint.h>
#include <stddef.h>

#ifndef __AMIGA__
#ifndef DEVICES_TIMER_H
struct timeval;
#define DEVICES_TIMER_H 1
#endif
#endif

#include <sys/socket.h>
#include <netinet/in.h>

#ifndef AF_INET
#define AF_INET 2
#endif

#ifndef AF_INET6
#define AF_INET6 10
#endif

typedef struct tn_ip_addr {
    uint8_t family;      /* AF_INET or AF_INET6 */
    union {
        uint32_t ip4;    /* Network byte order */
        uint8_t  ip6[16];
    } u;
} tn_ip_addr_t;

/*
 * Extract IP address and port (host byte order) from struct sockaddr.
 * Returns 1 on success, 0 on invalid parameters or unsupported address family.
 */
int tn_ip_from_sockaddr(const struct sockaddr *sa, socklen_t salen, tn_ip_addr_t *out_ip, uint16_t *out_port);

/*
 * Marshal IP address and port (host byte order) into struct sockaddr.
 * On entry, *salen is the capacity of sa buffer.
 * On exit, *salen is updated with the written sockaddr structure length.
 * Returns 1 on success, 0 if buffer too small or unsupported family.
 */
int tn_sockaddr_from_ip(struct sockaddr *sa, socklen_t *salen, const tn_ip_addr_t *ip, uint16_t port);

#endif /* TOLUNNET_SOCKADDR_UTIL_H */
