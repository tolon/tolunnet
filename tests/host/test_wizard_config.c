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

TN_TEST(test_startup_script_robustness)
{
    /* 1. 600-char line must not be split into two lines */
    char long_line_input[1024];
    memset(long_line_input, 'A', 600);
    long_line_input[600] = '\n';
    strcpy(&long_line_input[601], "Run Miami:Miami\n");

    char out_buf[2048];
    int count = 0;
    int len = tn_parse_startup_script(long_line_input, out_buf, sizeof(out_buf), &count);
    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_EQ(count, 1);
    /* Verify long line remains on a single line */
    TN_ASSERT_TRUE(memcmp(out_buf, long_line_input, 601) == 0);
    TN_ASSERT_TRUE(strstr(out_buf, "; tolunnet-disabled: Run Miami:Miami\n") != NULL);

    /* 2. Missing trailing newline */
    const char *no_newline = "Assign Miami: SYS:Miami";
    count = 0;
    len = tn_parse_startup_script(no_newline, out_buf, sizeof(out_buf), &count);
    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_EQ(count, 1);
    TN_ASSERT_STREQ(out_buf, "; tolunnet-disabled: Assign Miami: SYS:Miami");

    /* 3. Empty file */
    count = 0;
    len = tn_parse_startup_script("", out_buf, sizeof(out_buf), &count);
    TN_ASSERT_EQ(len, 0);
    TN_ASSERT_EQ(count, 0);
    TN_ASSERT_STREQ(out_buf, "");

    /* 4. CRLF preservation */
    const char *crlf_input = "; Header\r\nMiamiInit\r\nEcho Done\r\n";
    count = 0;
    len = tn_parse_startup_script(crlf_input, out_buf, sizeof(out_buf), &count);
    TN_ASSERT_TRUE(len > 0);
    TN_ASSERT_EQ(count, 1);
    TN_ASSERT_TRUE(strstr(out_buf, "; tolunnet-disabled: MiamiInit\r\n") != NULL);
    TN_ASSERT_TRUE(strstr(out_buf, "Echo Done\r\n") != NULL);
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

TN_TEST(test_wireless_block_injection_defense)
{
    char buf[512];

    /* 1. SSID with quotes or special characters formats as unquoted hex */
    int len = tn_format_wireless_block("Amiga\"AP\nInjection", "GoodPassword123", buf, sizeof(buf));
    TN_ASSERT_TRUE(len > 0);
    /* Must not contain raw quotes or newline in SSID field */
    TN_ASSERT_TRUE(strstr(buf, "ssid=\"Amiga\"AP") == NULL);
    /* Must emit hex form */
    TN_ASSERT_TRUE(strstr(buf, "ssid=416d696761") != NULL);

    /* 2. Passphrase with quote must be rejected */
    len = tn_format_wireless_block("ValidSSID", "Bad\"Password", buf, sizeof(buf));
    TN_ASSERT_EQ(len, 0);

    /* 3. Passphrase with control character (\n, \r, \t) must be rejected */
    len = tn_format_wireless_block("ValidSSID", "Bad\nPassword", buf, sizeof(buf));
    TN_ASSERT_EQ(len, 0);
    len = tn_format_wireless_block("ValidSSID", "Bad\tPassword", buf, sizeof(buf));
    TN_ASSERT_EQ(len, 0);

    /* 4. Passphrase length bounds: 8..63 chars */
    len = tn_format_wireless_block("ValidSSID", "1234567", buf, sizeof(buf)); /* 7 chars: too short */
    TN_ASSERT_EQ(len, 0);

    len = tn_format_wireless_block("ValidSSID", "12345678", buf, sizeof(buf)); /* 8 chars: min valid */
    TN_ASSERT_TRUE(len > 0);

    char pass63[64];
    memset(pass63, 'x', 63);
    pass63[63] = '\0';
    len = tn_format_wireless_block("ValidSSID", pass63, buf, sizeof(buf)); /* 63 chars: max valid */
    TN_ASSERT_TRUE(len > 0);

    char pass64[65];
    memset(pass64, 'x', 64);
    pass64[64] = '\0';
    len = tn_format_wireless_block("ValidSSID", pass64, buf, sizeof(buf)); /* 64 chars: too long */
    TN_ASSERT_EQ(len, 0);
}

TN_TEST(test_wifi_tagitem_parsing)
{
    const char *fake_ssid = "MyHomeWiFi";
    const UBYTE fake_bssid[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    struct TagItem fake_tags[] = {
        {S2INFO_SSID, (uintptr_t)fake_ssid},
        {S2INFO_BSSID, (uintptr_t)fake_bssid},
        {S2INFO_Channel, 11},
        {S2INFO_Signal, (ULONG)-60}, /* (-60 + 100) * 2 = 80% */
        {S2INFO_Noise, (ULONG)-95},
        {S2INFO_Encryption, 3},
        {TAG_END, 0}
    };

    WifiNetwork net;
    BOOL ok = tn_parse_wifi_tagitem(fake_tags, &net);
    TN_ASSERT_TRUE(ok);
    TN_ASSERT_STREQ(net.ssid, "MyHomeWiFi");
    TN_ASSERT_EQ(memcmp(net.bssid, fake_bssid, 6), 0);
    TN_ASSERT_EQ(net.channel, 11);
    TN_ASSERT_EQ(net.signal_dbm, -60);
    TN_ASSERT_EQ(net.noise_dbm, -95);
    TN_ASSERT_EQ(net.encryption, 3);
    TN_ASSERT_TRUE(strstr(net.display_str, "80%") != NULL);
    TN_ASSERT_TRUE(strstr(net.display_str, "WPA2") != NULL);

    /* Test signal percentage clamping */
    /* -50 dBm -> 100% */
    struct TagItem tags_strong[] = {
        {S2INFO_SSID, (uintptr_t)"StrongAP"},
        {S2INFO_Signal, (ULONG)-40}, /* Clamped to 100% */
        {TAG_END, 0}
    };
    ok = tn_parse_wifi_tagitem(tags_strong, &net);
    TN_ASSERT_TRUE(ok);
    TN_ASSERT_TRUE(strstr(net.display_str, "100%") != NULL);

    /* -100 dBm -> 0% */
    struct TagItem tags_weak[] = {
        {S2INFO_SSID, (uintptr_t)"WeakAP"},
        {S2INFO_Signal, (ULONG)-110}, /* Clamped to 0% */
        {TAG_END, 0}
    };
    ok = tn_parse_wifi_tagitem(tags_weak, &net);
    TN_ASSERT_TRUE(ok);
    TN_ASSERT_TRUE(strstr(net.display_str, "  0%") != NULL);

    /* Test fallback to BSSID[8] when S2INFO_SSID is absent */
    char bssid_buf[64];
    memset(bssid_buf, 0, sizeof(bssid_buf));
    strcpy(&bssid_buf[8], "LegacyAP");
    struct TagItem tags_legacy[] = {
        {S2INFO_BSSID, (uintptr_t)bssid_buf},
        {TAG_END, 0}
    };
    ok = tn_parse_wifi_tagitem(tags_legacy, &net);
    TN_ASSERT_TRUE(ok);
    TN_ASSERT_STREQ(net.ssid, "LegacyAP");

    /* Test empty tags fallback to "Unknown AP" */
    struct TagItem tags_empty[] = {
        {TAG_END, 0}
    };
    ok = tn_parse_wifi_tagitem(tags_empty, &net);
    TN_ASSERT_TRUE(ok);
    TN_ASSERT_STREQ(net.ssid, "Unknown AP");
}

TN_TEST(test_wifi_device_name_validation)
{
    /* 1. Valid device names */
    TN_ASSERT_TRUE(tn_wifi_validate_devname("wifipi.device"));
    TN_ASSERT_TRUE(tn_wifi_validate_devname("prism2.device"));
    TN_ASSERT_TRUE(tn_wifi_validate_devname("3c589.device"));
    TN_ASSERT_TRUE(tn_wifi_validate_devname("a2065.device"));
    TN_ASSERT_TRUE(tn_wifi_validate_devname("test-1_2.dev"));

    /* 2. Invalid device names */
    TN_ASSERT_FALSE(tn_wifi_validate_devname(NULL));
    TN_ASSERT_FALSE(tn_wifi_validate_devname(""));
    TN_ASSERT_FALSE(tn_wifi_validate_devname("../device"));
    TN_ASSERT_FALSE(tn_wifi_validate_devname("wifipi.device;rm"));
    TN_ASSERT_FALSE(tn_wifi_validate_devname("wifipi\"device"));
    TN_ASSERT_FALSE(tn_wifi_validate_devname("dev name"));
    TN_ASSERT_FALSE(tn_wifi_validate_devname("wifipi\ndevice"));

    /* 3. Length boundary: max 31 chars */
    char dev31[32];
    memset(dev31, 'a', 31);
    dev31[31] = '\0';
    TN_ASSERT_TRUE(tn_wifi_validate_devname(dev31));

    char dev32[33];
    memset(dev32, 'a', 32);
    dev32[32] = '\0';
    TN_ASSERT_FALSE(tn_wifi_validate_devname(dev32));

    /* 4. tn_wifi_start_manager respects devname validation */
    TN_ASSERT_TRUE(tn_wifi_start_manager("wifipi.device", 0));
    TN_ASSERT_FALSE(tn_wifi_start_manager("wifipi.device;reboot", 0));
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
    TN_TEST_RUN(test_startup_script_robustness);
    TN_TEST_RUN(test_wireless_block_formatting);
    TN_TEST_RUN(test_wireless_block_injection_defense);
    TN_TEST_RUN(test_wifi_tagitem_parsing);
    TN_TEST_RUN(test_wifi_device_name_validation);
    TN_TEST_RUN(test_roadshow_interface_formatting);
    TN_TEST_PLAN();
    return tn_test_failures();
}
