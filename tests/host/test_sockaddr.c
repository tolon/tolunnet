#include "sockaddr_util.h"
#include <stdio.h>
#include <string.h>

#define TAP_TEST(desc, cond) do { \
    test_num++; \
    if (cond) { \
        printf("ok %d - %s\n", test_num, desc); \
    } else { \
        printf("not ok %d - %s (line %d)\n", test_num, desc, __LINE__); \
        failed++; \
    } \
} while(0)

int main(void)
{
    int test_num = 0;
    int failed = 0;

    /* Test 1: IPv4 marshaling from struct sockaddr_in */
    {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = 0x3412; /* raw bytes in memory */
        sin.sin_addr.s_addr = 0x0100007f; /* 127.0.0.1 */

        tn_ip_addr_t ip;
        uint16_t port = 0;
        int ok = tn_ip_from_sockaddr((struct sockaddr *)&sin, sizeof(sin), &ip, &port);
        TAP_TEST("ipv4_extract_valid", ok == 1 && ip.family == AF_INET && ip.u.ip4 == 0x0100007f);
    }

    /* Test 2: IPv4 marshaling into struct sockaddr_in */
    {
        tn_ip_addr_t ip;
        ip.family = AF_INET;
        ip.u.ip4 = 0x01020304;

        char buf[64];
        socklen_t salen = sizeof(buf);
        int ok = tn_sockaddr_from_ip((struct sockaddr *)buf, &salen, &ip, 8080);
        struct sockaddr_in *sin = (struct sockaddr_in *)buf;

        TAP_TEST("ipv4_marshal_valid", ok == 1 && salen == sizeof(struct sockaddr_in) &&
                 sin->sin_family == AF_INET && sin->sin_addr.s_addr == 0x01020304);
    }

    /* Test 3: Insufficient buffer handling */
    {
        tn_ip_addr_t ip;
        ip.family = AF_INET;
        ip.u.ip4 = 0x01020304;

        char buf[64];
        socklen_t salen = 4; /* Too small */
        int ok = tn_sockaddr_from_ip((struct sockaddr *)buf, &salen, &ip, 80);
        TAP_TEST("buffer_too_small_rejected", ok == 0);
    }

    /* Test 4: NULL and invalid parameters */
    {
        int ok1 = tn_ip_from_sockaddr(NULL, 16, NULL, NULL);
        int ok2 = tn_sockaddr_from_ip(NULL, NULL, NULL, 0);
        TAP_TEST("null_parameters_rejected", ok1 == 0 && ok2 == 0);
    }

    printf("1..%d\n", test_num);
    return failed;
}
