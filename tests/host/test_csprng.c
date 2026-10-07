/*
 * test_csprng.c — audit run-1: keyed, non-invertible RNG for security
 * values (DNS txid/port, TCP ISN, ephemeral ports, DHCP xid).
 *
 * RED first: the SipHash-2-4 reference vector, then the properties the
 * fix must have — a seeded generator is not the old shared xorshift
 * state, two different keys give different streams, and the TCP ISN hook
 * depends on the 4-tuple and a clock.
 */
#include "tn_test.h"
#include "common/tn_csprng.h"
#include <string.h>

/* SipHash-2-4 canonical vector: key = 00 01 .. 0f, input = 00 01 .. 0e
 * (15 bytes) -> 0xa129ca6149be45e5 (reference siphash.c vectors_sip64). */
TN_TEST(siphash24_reference_vector)
{
    uint8_t key[16], in[15];
    int i;
    uint64_t out;
    for (i = 0; i < 16; i++) key[i] = (uint8_t)i;
    for (i = 0; i < 15; i++) in[i] = (uint8_t)i;
    out = tn_siphash24(key, in, sizeof(in));
    TN_ASSERT_TRUE(out == 0xa129ca6149be45e5ULL);
}

/* A keyed counter stream must not repeat its own state as output (the old
 * xorshift returned its full state), and consecutive outputs differ. */
TN_TEST(csprng_stream_differs_per_call)
{
    TnCsprng g;
    uint8_t key[16];
    uint32_t a, b, c;
    memset(key, 0x5a, sizeof(key));
    tn_csprng_init(&g, key);
    a = tn_csprng_next(&g);
    b = tn_csprng_next(&g);
    c = tn_csprng_next(&g);
    TN_ASSERT_TRUE(a != b);
    TN_ASSERT_TRUE(b != c);
}

/* Two different keys produce different streams from the same counter. */
TN_TEST(csprng_key_separates_streams)
{
    TnCsprng g1, g2;
    uint8_t k1[16], k2[16];
    memset(k1, 0x11, sizeof(k1));
    memset(k2, 0x22, sizeof(k2));
    tn_csprng_init(&g1, k1);
    tn_csprng_init(&g2, k2);
    TN_ASSERT_TRUE(tn_csprng_next(&g1) != tn_csprng_next(&g2));
}

/* RFC 6528 ISN: depends on the 4-tuple (a different remote port changes
 * it) and increases with the clock for a fixed tuple. */
TN_TEST(tcp_isn_depends_on_tuple_and_clock)
{
    uint8_t key[16];
    uint32_t a, b, c;
    memset(key, 0x3c, sizeof(key));
    a = tn_tcp_isn_compute(key, 0x0A000001UL, 1234, 0x0A000002UL, 80, 1000);
    b = tn_tcp_isn_compute(key, 0x0A000001UL, 1234, 0x0A000002UL, 81, 1000);
    c = tn_tcp_isn_compute(key, 0x0A000001UL, 1234, 0x0A000002UL, 80, 1064);
    TN_ASSERT_TRUE(a != b);        /* different remote port */
    TN_ASSERT_TRUE(c != a);        /* clock advanced */
    TN_ASSERT_TRUE((uint32_t)(c - a) == (uint32_t)((1064u << 8) - (1000u << 8)));
}

int main(void)
{
    TN_TEST_RUN(siphash24_reference_vector);
    TN_TEST_RUN(csprng_stream_differs_per_call);
    TN_TEST_RUN(csprng_key_separates_streams);
    TN_TEST_RUN(tcp_isn_depends_on_tuple_and_clock);
    TN_TEST_PLAN();
    return tn_test_failures();
}
