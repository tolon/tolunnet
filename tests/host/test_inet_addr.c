/*
 * test_inet_addr.c — TNET-051 parser conformance (Round 3 §B.1).
 * Unit: src/common/inet_parse.c (drives the bsdsocket inet_addr LVO).
 */
#include "tn_test.h"
#include "../../src/common/inet_parse.h"

#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                        ((uint32_t)(c) << 8) | (uint32_t)(d))

TN_TEST(four_part_decimal)
{
    TN_ASSERT_EQ_U(tn_inet_addr_parse("10.0.2.15"), IP(10, 0, 2, 15));
    TN_ASSERT_EQ_U(tn_inet_addr_parse("192.168.1.1"), IP(192, 168, 1, 1));
    TN_ASSERT_EQ_U(tn_inet_addr_parse("0.0.0.0"), 0u);
}

TN_TEST(broadcast_is_valid_not_error)
{
    /* The classic BSD wart: 255.255.255.255 == the error sentinel in the
     * legacy-shaped API, but the _ex form must report it as VALID. */
    uint32_t out = 0;
    TN_ASSERT_EQ(tn_inet_addr_parse_ex("255.255.255.255", &out), 1);
    TN_ASSERT_EQ_U(out, 0xFFFFFFFFu);
    TN_ASSERT_EQ_U(tn_inet_addr_parse("255.255.255.255"), TN_INADDR_NONE);
}

TN_TEST(one_to_three_part_classful)
{
    TN_ASSERT_EQ_U(tn_inet_addr_parse("10.1"), IP(10, 0, 0, 1));
    TN_ASSERT_EQ_U(tn_inet_addr_parse("10.1.2"), IP(10, 1, 0, 2));
    TN_ASSERT_EQ_U(tn_inet_addr_parse("1"), IP(0, 0, 0, 1));
}

TN_TEST(octal_and_hex_parts)
{
    TN_ASSERT_EQ_U(tn_inet_addr_parse("0x0a.0.2.15"), IP(10, 0, 2, 15));
    TN_ASSERT_EQ_U(tn_inet_addr_parse("012.0.2.15"), IP(10, 0, 2, 15));
    TN_ASSERT_EQ_U(tn_inet_addr_parse("0x7f.0.0.1"), IP(127, 0, 0, 1));
}

TN_TEST(range_and_junk_rejected)
{
    TN_ASSERT_EQ_U(tn_inet_addr_parse("256.1.1.1"), TN_INADDR_NONE);
    TN_ASSERT_EQ_U(tn_inet_addr_parse("1.2.3.4.5"), TN_INADDR_NONE); /* 5 parts */
    TN_ASSERT_EQ_U(tn_inet_addr_parse("1.2.3.300"), TN_INADDR_NONE);
    TN_ASSERT_EQ_U(tn_inet_addr_parse("1.2.3.4junk"), TN_INADDR_NONE);
    TN_ASSERT_EQ_U(tn_inet_addr_parse("junk"), TN_INADDR_NONE);
    TN_ASSERT_EQ_U(tn_inet_addr_parse(""), TN_INADDR_NONE);
    TN_ASSERT_EQ_U(tn_inet_addr_parse(NULL), TN_INADDR_NONE);
    TN_ASSERT_EQ_U(tn_inet_addr_parse("1.2.3."), TN_INADDR_NONE); /* trailing dot */
    TN_ASSERT_EQ_U(tn_inet_addr_parse("1..2.3"), TN_INADDR_NONE); /* empty part */
}

TN_TEST(delimiters_rejected)
{
    TN_ASSERT_EQ_U(tn_inet_addr_parse(" 10.0.2.15"), TN_INADDR_NONE); /* leading space */
    TN_ASSERT_EQ_U(tn_inet_addr_parse("10.0.2.15 "), TN_INADDR_NONE); /* trailing space */
    TN_ASSERT_EQ_U(tn_inet_addr_parse("10,0,2,15"), TN_INADDR_NONE);
    TN_ASSERT_EQ_U(tn_inet_addr_parse("10.0.2"), IP(10, 0, 0, 2));
}

int main(void)
{
    TN_TEST_RUN(four_part_decimal);
    TN_TEST_RUN(broadcast_is_valid_not_error);
    TN_TEST_RUN(one_to_three_part_classful);
    TN_TEST_RUN(octal_and_hex_parts);
    TN_TEST_RUN(range_and_junk_rejected);
    TN_TEST_RUN(delimiters_rejected);
    TN_TEST_PLAN();
    return tn_test_failures();
}
