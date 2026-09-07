/*
 * tolunnet — Host Unit Tests for Wizard Configuration & Migration
 *
 * TAP-compliant test suite under ASan/UBSan.
 */

#include "tn_test.h"
#include "../../src/setup/stack_detect.h"
#include "../../src/setup/wifi_mgr.h"
#include "../../src/setup/net_test.h"

#include <stdio.h>
#include <string.h>

TN_TEST(test_startup_script_comment_and_uncomment)
{
    const char *orig =
        "; User-Startup header\n"
        "Assign Miami: SYS:Miami\n"
        "Run <>NIL: Miami:Miami\n"
        "MiamiInit\n"
        "AmiTCP:bin/startnet\n"
        "AmiTCP:bin/stopnet\n"
        "AddNetInterface DEVS:NetInterfaces/WiFiPi\n"
        "ConfigureNetInterface DEVS:NetInterfaces/WiFiPi\n"
        "C:NetShutdown\n"
        "Echo \"Network initialized\"\n";

    char disabled_buf[2048];
    int dis_count = 0;
    int len = tn_parse_startup_script(orig, disabled_buf, sizeof(disabled_buf), &dis_count);

    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_EQ(dis_count, 8);
    TN_ASSERT_TRUE(strstr(disabled_buf, "; tolunnet-disabled: Run <>NIL: Miami:Miami") != NULL);
    TN_ASSERT_TRUE(strstr(disabled_buf, "; tolunnet-disabled: MiamiInit") != NULL);
    TN_ASSERT_TRUE(strstr(disabled_buf, "; tolunnet-disabled: AmiTCP:bin/startnet") != NULL);
    TN_ASSERT_TRUE(strstr(disabled_buf, "; tolunnet-disabled: AmiTCP:bin/stopnet") != NULL);
    TN_ASSERT_TRUE(strstr(disabled_buf, "; tolunnet-disabled: AddNetInterface DEVS:NetInterfaces/WiFiPi") != NULL);
    TN_ASSERT_TRUE(strstr(disabled_buf, "; tolunnet-disabled: ConfigureNetInterface DEVS:NetInterfaces/WiFiPi") != NULL);
    TN_ASSERT_TRUE(strstr(disabled_buf, "; tolunnet-disabled: C:NetShutdown") != NULL);
    TN_ASSERT_TRUE(strstr(disabled_buf, "\nEcho \"Network initialized\"\n") != NULL);

    /* Test undo / uncomment */
    char restored_buf[2048];
    int res_count = 0;
    int rlen = tn_uncomment_startup_script(disabled_buf, restored_buf, sizeof(restored_buf), &res_count);

    TN_ASSERT_TRUE(rlen > 0);
    TN_ASSERT_EQ(res_count, 8);
    TN_ASSERT_TRUE(strstr(restored_buf, "; tolunnet-disabled:") == NULL);
    TN_ASSERT_TRUE(strstr(restored_buf, "Run <>NIL: Miami:Miami") != NULL);
    TN_ASSERT_TRUE(strstr(restored_buf, "MiamiInit") != NULL);
    TN_ASSERT_TRUE(strstr(restored_buf, "AmiTCP:bin/startnet") != NULL);
    TN_ASSERT_TRUE(strstr(restored_buf, "ConfigureNetInterface DEVS:NetInterfaces/WiFiPi") != NULL);
}

TN_TEST(test_wireless_block_formatting)
{
    char buf[512];

    /* 1. WPA/WPA2 with passphrase */
    int len = tn_format_wireless_block("MyAmigaAP", "SecretPass123", buf, sizeof(buf));
    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_TRUE(strstr(buf, "network={\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "    ssid=\"MyAmigaAP\"\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "    psk=\"SecretPass123\"\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "    scan_ssid=1\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "key_mgmt") == NULL);

    /* 2. Open network without passphrase */
    len = tn_format_wireless_block("OpenCafe", "", buf, sizeof(buf));
    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_TRUE(strstr(buf, "    ssid=\"OpenCafe\"\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "    key_mgmt=NONE\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "    scan_ssid=1\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "psk=") == NULL);
}

TN_TEST(test_roadshow_interface_formatting)
{
    char buf[512];

    /* 1. DHCP */
    int len = tn_format_roadshow_interface("wifipi.device", 0, 1, NULL, NULL, buf, sizeof(buf));
    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_TRUE(strstr(buf, "device=wifipi.device\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "unit=0\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "configure=dhcp\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "iprequests=128\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "requiresinitdelay=no\n") != NULL);

    /* 2. Static */
    len = tn_format_roadshow_interface("uaenet.device", 0, 0, "192.168.1.50", "255.255.255.0", buf, sizeof(buf));
    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_TRUE(strstr(buf, "device=uaenet.device\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "address=192.168.1.50\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "netmask=255.255.255.0\n") != NULL);
    TN_ASSERT_TRUE(strstr(buf, "configure=dhcp") == NULL);
}

int main(void)
{
    TN_TEST_RUN(test_startup_script_comment_and_uncomment);
    TN_TEST_RUN(test_wireless_block_formatting);
    TN_TEST_RUN(test_roadshow_interface_formatting);
    TN_TEST_PLAN();
    return tn_test_failures();
}
