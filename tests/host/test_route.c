/*
 * test_route.c — static route table: longest-prefix, default, delete,
 * validation (CLOSE §B.5; the former §D/T3 SKIP rows are now real tests).
 *
 * All addresses go through htonl() (via inet_pton) so the table is
 * exercised exactly as the daemon feeds it — network byte order — on the
 * little-endian host as on the 68k target.
 */
#include "tn_test.h"
#include "task/route.h"
#include <errno.h>
#include <string.h>

/* host helper: integer value → memory bytes in big-endian (network) order,
 * read back as the host's uint32; identity on a big-endian machine */
static uint32_t net32(uint32_t v)
{
    uint8_t b[4];
    uint32_t r;
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
    memcpy(&r, b, 4);
    return r;
}

/* dotted quad → network byte order (no POSIX feature macros needed) */
static uint32_t ip(const char *s)
{
    uint32_t o[4] = { 0, 0, 0, 0 };
    int part = 0, digits = 0;
    const char *p = s;
    while (*p && part < 4) {
        if (*p >= '0' && *p <= '9') {
            o[part] = o[part] * 10 + (uint32_t)(*p - '0');
            digits++;
            p++;
        } else if (*p == '.' && digits > 0 && part < 3) {
            part++;
            digits = 0;
            p++;
        } else {
            return 0;
        }
    }
    if (part != 3 || digits == 0) return 0;
    {
        int i;
        uint32_t v = 0;
        for (i = 0; i < 4; i++) {
            if (o[i] > 255) return 0;
            v = (v << 8) | o[i];
        }
        return net32(v);
    }
}

TN_TEST(route_longest_prefix)
{
    int eno = 0;
    int idx;
    const TnRoute *r;

    tn_route_init();
    TN_ASSERT_EQ(tn_route_count(), 0);

    /* default route via gw1 */
    TN_ASSERT_EQ(tn_route_add(ip("0.0.0.0"), ip("0.0.0.0"), ip("10.0.2.1"), &eno), 0);
    /* /16 via gw2 */
    TN_ASSERT_EQ(tn_route_add(ip("10.9.0.0"), ip("255.255.0.0"), ip("10.0.2.2"), &eno), 0);
    /* /24 via gw3 */
    TN_ASSERT_EQ(tn_route_add(ip("10.9.9.0"), ip("255.255.255.0"), ip("10.0.2.3"), &eno), 0);
    TN_ASSERT_EQ(tn_route_count(), 3);

    /* 10.9.9.7 → most specific /24 */
    idx = tn_route_lookup(ip("10.9.9.7"));
    TN_ASSERT_TRUE(idx >= 0);
    r = tn_route_at(idx);
    TN_ASSERT_TRUE(r != NULL);
    TN_ASSERT_EQ(r->gw, ip("10.0.2.3"));

    /* 10.9.4.1 → /16 */
    idx = tn_route_lookup(ip("10.9.4.1"));
    TN_ASSERT_TRUE(idx >= 0);
    r = tn_route_at(idx);
    TN_ASSERT_TRUE(r != NULL);
    TN_ASSERT_EQ(r->gw, ip("10.0.2.2"));

    /* 10.8.1.1 → default */
    idx = tn_route_lookup(ip("10.8.1.1"));
    TN_ASSERT_TRUE(idx >= 0);
    r = tn_route_at(idx);
    TN_ASSERT_TRUE(r != NULL);
    TN_ASSERT_EQ(r->gw, ip("10.0.2.1"));
}

TN_TEST(route_default_and_delete)
{
    int eno = 0;
    int idx;

    tn_route_init();

    /* validation: non-contiguous mask rejected */
    TN_ASSERT_EQ(tn_route_add(ip("10.0.0.0"), ip("255.0.255.0"), ip("10.0.2.1"), &eno), -1);
    TN_ASSERT_EQ(eno, EINVAL);

    /* host bits set under the mask rejected */
    TN_ASSERT_EQ(tn_route_add(ip("10.9.0.7"), ip("255.255.0.0"), ip("10.0.2.1"), &eno), -1);
    TN_ASSERT_EQ(eno, EINVAL);

    /* add + duplicate */
    TN_ASSERT_EQ(tn_route_add(ip("10.9.0.0"), ip("255.255.0.0"), ip("10.0.2.2"), &eno), 0);
    TN_ASSERT_EQ(tn_route_add(ip("10.9.0.0"), ip("255.255.0.0"), ip("10.0.2.9"), &eno), -1);
    TN_ASSERT_EQ(eno, EEXIST);

    /* default route */
    TN_ASSERT_EQ(tn_route_add(0, 0, ip("10.0.2.1"), &eno), 0);
    idx = tn_route_lookup(ip("192.168.1.1"));
    TN_ASSERT_TRUE(idx >= 0);
    TN_ASSERT_TRUE(tn_route_at(idx) != NULL);
    TN_ASSERT_EQ(tn_route_at(idx)->gw, ip("10.0.2.1"));

    /* delete /16 → default still catches 10.9.x */
    TN_ASSERT_EQ(tn_route_delete(ip("10.9.0.0"), ip("255.255.0.0"), &eno), 0);
    idx = tn_route_lookup(ip("10.9.4.1"));
    TN_ASSERT_TRUE(idx >= 0);
    TN_ASSERT_TRUE(tn_route_at(idx) != NULL);
    TN_ASSERT_EQ(tn_route_at(idx)->gw, ip("10.0.2.1"));

    /* delete absent */
    TN_ASSERT_EQ(tn_route_delete(ip("10.9.0.0"), ip("255.255.0.0"), &eno), -1);
    TN_ASSERT_EQ(eno, ENOENT);

    /* capacity: fill to TN_MAX_ROUTES, next add is ENOSPC */
    tn_route_init();
    {
        int i;
        char net[16];
        for (i = 0; i < TN_MAX_ROUTES; i++) {
            snprintf(net, sizeof(net), "10.%d.0.0", 100 + i);
            TN_ASSERT_EQ(tn_route_add(ip(net), ip("255.255.0.0"), ip("10.0.2.1"), &eno), 0);
        }
        TN_ASSERT_EQ(tn_route_add(ip("10.200.0.0"), ip("255.255.0.0"), ip("10.0.2.1"), &eno), -1);
        TN_ASSERT_EQ(eno, ENOSPC);
    }
}

int main(void)
{
    TN_TEST_RUN(route_longest_prefix);
    TN_TEST_RUN(route_default_and_delete);
    TN_TEST_PLAN();
    return tn_test_failures();
}
