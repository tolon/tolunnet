/*
 * test_nslookup_parse.c — unit tests for the nslookup answer parser
 * (11x item 2). Real C:nslookup output shapes.
 */
#include "tn_test.h"
#include "../../src/common/nslookup_parse.h"
#include <string.h>

/* normal A answer: Server line first, then Name + Address */
TN_TEST(normal_a_answer)
{
    static const char *out =
        "Server: 10.0.2.2:53\r\n"
        "Name:    tolunbench.test\r\n"
        "Address:  10.0.2.55\r\n";
    char ip[32];
    TN_ASSERT_EQ(tn_parse_nslookup_answer(out, ip, sizeof(ip)), 1);
    TN_ASSERT_STREQ(ip, "10.0.2.55");
}

/* multiple Address1..3 lines: the first answer address wins */
TN_TEST(multiple_addresses)
{
    static const char *out =
        "Server: 10.0.2.2:53\r\n"
        "Name:    www.example.com\r\n"
        "Address1: 192.0.2.10\r\n"
        "Address2: 192.0.2.11\r\n"
        "Address3: 192.0.2.12\r\n";
    char ip[32];
    TN_ASSERT_EQ(tn_parse_nslookup_answer(out, ip, sizeof(ip)), 1);
    TN_ASSERT_STREQ(ip, "192.0.2.10");
}

/* error output: "** name doesn't exist" — no answer */
TN_TEST(error_output)
{
    static const char *out =
        "Server: 10.0.2.2:53\r\n"
        "** nx.invalid doesn't exist\r\n";
    char ip[32];
    TN_ASSERT_EQ(tn_parse_nslookup_answer(out, ip, sizeof(ip)), 0);
}

/* server line only (no answer section) */
TN_TEST(server_line_only)
{
    static const char *out = "Server: 10.0.2.2:53\r\n";
    char ip[32];
    TN_ASSERT_EQ(tn_parse_nslookup_answer(out, ip, sizeof(ip)), 0);
}

/* garbage: no dotted quad anywhere */
TN_TEST(garbage_no_quad)
{
    static const char *out = "hello world, nothing to see here: 999.888\n";
    char ip[32];
    TN_ASSERT_EQ(tn_parse_nslookup_answer(out, ip, sizeof(ip)), 0);
}

/* the Server: line must NOT be taken as the answer even when the
 * answer section is missing — 10.0.2.2 is the server, not a result */
TN_TEST(server_ip_is_not_the_answer)
{
    static const char *out =
        "Server: 10.0.2.2:53\r\n"
        "Name:    tolunbench.test\r\n";
    char ip[32];
    TN_ASSERT_EQ(tn_parse_nslookup_answer(out, ip, sizeof(ip)), 0);
}

/* canonical/name lines between the addresses must not confuse it */
TN_TEST(canonical_between_addresses)
{
    static const char *out =
        "Server: 10.0.2.2:53\r\n"
        "Name:      host.test\r\n"
        "Address:  203.0.113.7\r\n"
        "Canonical: real.host.test\r\n";
    char ip[32];
    TN_ASSERT_EQ(tn_parse_nslookup_answer(out, ip, sizeof(ip)), 1);
    TN_ASSERT_STREQ(ip, "203.0.113.7");
}

/* --- 11x item 3 T2: tn_parse_first_ipv4 (GetNetStatus output) --- */

TN_TEST(first_ipv4_single_quad)
{
    char ip[32];
    TN_ASSERT_EQ(tn_parse_first_ipv4("10.0.2.2\n", ip, sizeof(ip)), 1);
    TN_ASSERT_STREQ(ip, "10.0.2.2");
}

TN_TEST(first_ipv4_zero_quad_is_returned_as_is)
{
    /* 0.0.0.0 parses; the caller decides it is not usable */
    char ip[32];
    TN_ASSERT_EQ(tn_parse_first_ipv4("0.0.0.0\n", ip, sizeof(ip)), 1);
    TN_ASSERT_STREQ(ip, "0.0.0.0");
}

TN_TEST(first_ipv4_none_and_garbage)
{
    char ip[32];
    TN_ASSERT_EQ(tn_parse_first_ipv4("none\n", ip, sizeof(ip)), 0);
    TN_ASSERT_STREQ(ip, "");
    TN_ASSERT_EQ(tn_parse_first_ipv4("", ip, sizeof(ip)), 0);
    TN_ASSERT_EQ(tn_parse_first_ipv4("offline\n", ip, sizeof(ip)), 0);
}

TN_TEST(first_ipv4_two_quads_first_wins)
{
    char ip[32];
    TN_ASSERT_EQ(tn_parse_first_ipv4("10.0.2.2\n10.0.2.3\n", ip, sizeof(ip)), 1);
    TN_ASSERT_STREQ(ip, "10.0.2.2");
}

TN_TEST(first_ipv4_null_and_tiny_buffer)
{
    char ip[32];
    char tiny[3];
    TN_ASSERT_EQ(tn_parse_first_ipv4(NULL, ip, sizeof(ip)), 0);
    TN_ASSERT_EQ(tn_parse_first_ipv4("1.2.3.4", ip, 0), 0);
    TN_ASSERT_EQ(tn_parse_first_ipv4("1.2.3.4", tiny, sizeof(tiny)), 1);
    TN_ASSERT_STREQ(tiny, "1.");
}

int main(void)
{
    TN_TEST_RUN(normal_a_answer);
    TN_TEST_RUN(multiple_addresses);
    TN_TEST_RUN(error_output);
    TN_TEST_RUN(server_line_only);
    TN_TEST_RUN(garbage_no_quad);
    TN_TEST_RUN(server_ip_is_not_the_answer);
    TN_TEST_RUN(canonical_between_addresses);
    TN_TEST_RUN(first_ipv4_single_quad);
    TN_TEST_RUN(first_ipv4_zero_quad_is_returned_as_is);
    TN_TEST_RUN(first_ipv4_none_and_garbage);
    TN_TEST_RUN(first_ipv4_two_quads_first_wins);
    TN_TEST_RUN(first_ipv4_null_and_tiny_buffer);
    TN_TEST_PLAN();
    return tn_test_failures();
}
