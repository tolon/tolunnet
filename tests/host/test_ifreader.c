/*
 * test_ifreader.c — Roadshow interfaces-file parser (CLOSE §B.7).
 */
#include "tn_test.h"
#include "common/ifreader.h"

#include <string.h>

TN_TEST(ifreader_basic_block_and_continuation)
{
    static const char text[] =
        "; Roadshow-style interfaces\n"
        "# another comment\n"
        "\n"
        "eth0 DEVICE=ethernet.device UNIT=0 ADDRESS=10.0.2.15/24\n"
        "    GATEWAY=10.0.2.2\n"
        "    DHCP=NO\n"
        "lo0 ADDRESS=127.0.0.1 NETMASK=255.0.0.0\n";
    TnIfEntry e[TN_IF_MAX];
    int n = tn_if_parse_lines(text, e, TN_IF_MAX);

    TN_ASSERT_EQ(n, 2);
    TN_ASSERT_STREQ(e[0].name, "eth0");
    TN_ASSERT_STREQ(e[0].device, "ethernet.device");
    TN_ASSERT_EQ(e[0].unit, 0);
    TN_ASSERT_STREQ(e[0].address, "10.0.2.15/24");
    TN_ASSERT_STREQ(e[0].gateway, "10.0.2.2");
    TN_ASSERT_EQ(e[0].dhcp, 0);
    TN_ASSERT_STREQ(e[1].name, "lo0");
    TN_ASSERT_STREQ(e[1].address, "127.0.0.1");
    TN_ASSERT_STREQ(e[1].netmask, "255.0.0.0");
    TN_ASSERT_EQ(e[1].unit, -1);
}

TN_TEST(ifreader_unknown_keys_and_crlf_tolerated)
{
    static const char text[] =
        "net0 DEVICE=wifipi.device UNIT=1 UP DEBUG=YES ADDRESS=192.168.1.50\r\n"
        "    DHCP=YES BOGUS=whatever\r\n";
    TnIfEntry e[TN_IF_MAX];
    int n = tn_if_parse_lines(text, e, TN_IF_MAX);

    TN_ASSERT_EQ(n, 1);
    TN_ASSERT_STREQ(e[0].name, "net0");
    TN_ASSERT_STREQ(e[0].device, "wifipi.device");
    TN_ASSERT_EQ(e[0].unit, 1);
    TN_ASSERT_STREQ(e[0].address, "192.168.1.50");
    TN_ASSERT_EQ(e[0].dhcp, 1);
}

TN_TEST(ifreader_capacity_and_empty)
{
    TnIfEntry e[TN_IF_MAX];
    TN_ASSERT_EQ(tn_if_parse_lines("", e, TN_IF_MAX), 0);
    TN_ASSERT_EQ(tn_if_parse_lines("; only comments\n", e, TN_IF_MAX), 0);
    TN_ASSERT_EQ(tn_if_parse_lines(NULL, e, TN_IF_MAX), -1);
    {
        static const char text[] =
            "a0 ADDRESS=1.1.1.1\n"
            "b0 ADDRESS=2.2.2.2\n"
            "c0 ADDRESS=3.3.3.3\n"
            "d0 ADDRESS=4.4.4.4\n"
            "e0 ADDRESS=5.5.5.5\n";
        TN_ASSERT_EQ(tn_if_parse_lines(text, e, TN_IF_MAX), TN_IF_MAX);
        TN_ASSERT_STREQ(e[TN_IF_MAX - 1].name, "d0");
    }
}

int main(void)
{
    TN_TEST_RUN(ifreader_basic_block_and_continuation);
    TN_TEST_RUN(ifreader_unknown_keys_and_crlf_tolerated);
    TN_TEST_RUN(ifreader_capacity_and_empty);
    TN_TEST_PLAN();
    return tn_test_failures();
}
