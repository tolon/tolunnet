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

    /* TNET-139: byte-wise helpers on ODD-address client buffers. A
     * sockaddr_in at an odd base is legal client memory on m68k; the
     * helpers must read/write it without any struct-typed access. */
    {
        /* raw[1] is intentionally odd */
        unsigned char raw[32];
        unsigned char *odd = raw + 1;
        uint16_t fam = 0, port = 0;
        uint32_t addr = 0;

        memset(raw, 0xAA, sizeof(raw));
        tn_sockin_store_bytes(odd, AF_INET, 8080, 0x7F000001u);
        TAP_TEST("store_odd_bounds_exact",
                 odd[sizeof(struct sockaddr_in)] == 0xAA &&
                 odd[sizeof(struct sockaddr_in) + 1] == 0xAA);

        tn_sockin_load_bytes(odd, &fam, &port, &addr);
        TAP_TEST("odd_roundtrip_family_port_addr",
                 fam == AF_INET && port == 8080 && addr == 0x7F000001u);

        tn_sockin_load_bytes(NULL, NULL, NULL, NULL); /* must not crash */
        tn_sockin_store_bytes(NULL, 0, 0, 0);         /* must not crash */
        TAP_TEST("null_helper_args_safe", 1);
    }

    /* TNET-139: tn_ip_from_sockaddr / tn_sockaddr_from_ip on odd bases */
    {
        unsigned char raw[64];
        struct sockaddr *odd_sa = (struct sockaddr *)(raw + 1);
        socklen_t salen = sizeof(struct sockaddr_in);
        tn_ip_addr_t ip;
        uint16_t port = 0;
        int ok;

        memset(raw, 0, sizeof(raw));
        ip.family = AF_INET;
        ip.u.ip4 = 0x0A000002u;
        ok = tn_sockaddr_from_ip(odd_sa, &salen, &ip, 53);
        TAP_TEST("marshal_into_odd_buffer", ok == 1 && salen == sizeof(struct sockaddr_in));

        memset(&ip, 0, sizeof(ip));
        ok = tn_ip_from_sockaddr(odd_sa, salen, &ip, &port);
        TAP_TEST("extract_from_odd_buffer",
                 ok == 1 && ip.family == AF_INET && ip.u.ip4 == 0x0A000002u && port == 53);
    }

    printf("1..%d\n", test_num);
    return failed;
}
