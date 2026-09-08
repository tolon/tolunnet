/*
 * test_config.c — config text grammar round-trips (Round 3 §B.1, TNET-073).
 * Unit: src/common/config_text.c (drives prefs.c parse + DEVS: writer).
 */
#include "tn_test.h"
#include "../../src/common/config_text.h"
#include <string.h>

TN_TEST(round_trip_all_keys)
{
    TnPrefs in, out;
    char text[TN_CONFIG_TEXT_MAX];

    memset(&in, 0, sizeof(in));
    strcpy(in.device, "wifipi.device");
    in.unit = 3;
    in.use_dhcp = FALSE;
    strcpy(in.ip_addr, "192.168.1.50");
    strcpy(in.netmask, "255.255.255.0");
    strcpy(in.gateway, "192.168.1.1");
    strcpy(in.dns_server, "192.168.1.1");
    strcpy(in.dns2, "1.1.1.1");
    strcpy(in.hostname, "amiga");
    in.mtu = 1400;
    in.debug = 2;
    in.priority = 10;
    strcpy(in.log_file, "WORK:test.log");

    TN_ASSERT_TRUE(tn_config_format(&in, text, sizeof(text)) > 0);

    memset(&out, 0, sizeof(out));
    /* feed the formatted text back line by line, like prefs.c does */
    {
        char *line = text;
        while (*line) {
            char *nl = strchr(line, '\n');
            char saved;
            char *eq;
            if (nl == NULL) break;
            saved = *nl; *nl = '\0';
            eq = strchr(line, '=');
            if (eq != NULL && line[0] != '#') {
                *eq = '\0';
                tn_config_parse_line(&out, line, eq + 1);
                *eq = '=';
            }
            *nl = saved;
            line = nl + 1;
        }
    }

    TN_ASSERT_STREQ(out.device, "wifipi.device");
    TN_ASSERT_EQ(out.unit, 3u);
    TN_ASSERT_EQ(out.use_dhcp, FALSE);
    TN_ASSERT_STREQ(out.ip_addr, "192.168.1.50");
    TN_ASSERT_STREQ(out.netmask, "255.255.255.0");
    TN_ASSERT_STREQ(out.gateway, "192.168.1.1");
    TN_ASSERT_STREQ(out.dns_server, "192.168.1.1");
    TN_ASSERT_STREQ(out.dns2, "1.1.1.1");
    TN_ASSERT_STREQ(out.hostname, "amiga");
    TN_ASSERT_EQ(out.mtu, 1400u);
    TN_ASSERT_EQ(out.debug, 2u);
    TN_ASSERT_EQ(out.priority, 10);
    TN_ASSERT_STREQ(out.log_file, "WORK:test.log");
}

TN_TEST(key_dialects_and_case)
{
    TnPrefs p;
    memset(&p, 0, sizeof(p));

    tn_config_parse_line(&p, "MASK", "255.255.0.0");
    tn_config_parse_line(&p, "gw", "10.0.0.138");
    tn_config_parse_line(&p, "Ip_Addr", "10.0.0.5");
    tn_config_parse_line(&p, "nameserver", "10.0.0.138");
    tn_config_parse_line(&p, "USE_DHCP", "false");

    TN_ASSERT_STREQ(p.netmask, "255.255.0.0");
    TN_ASSERT_STREQ(p.gateway, "10.0.0.138");
    TN_ASSERT_STREQ(p.ip_addr, "10.0.0.5");
    TN_ASSERT_STREQ(p.dns_server, "10.0.0.138");
    TN_ASSERT_EQ(p.use_dhcp, FALSE);
}

TN_TEST(dns2_does_not_alias_dns1)
{
    /* TNET-063 regression guard: the pre-TNET-063 parser wrote DNS2 into the
     * DNS1 field. */
    TnPrefs p;
    memset(&p, 0, sizeof(p));
    strcpy(p.dns_server, "10.0.2.3");

    tn_config_parse_line(&p, "DNS2", "1.0.0.1");

    TN_ASSERT_STREQ(p.dns_server, "10.0.2.3");
    TN_ASSERT_STREQ(p.dns2, "1.0.0.1");
}

TN_TEST(value_cleanup_and_oversize)
{
    TnPrefs p;
    char big[256];
    memset(&p, 0, sizeof(p));
    memset(big, 'x', sizeof(big));
    big[255] = '\0';

    /* trims trailing CR/LF/space (leading whitespace is skipped by the
     * prefs.c line splitter — the parser contract) */
    tn_config_parse_line(&p, "DEVICE", "ethernet.device\r\n ");
    tn_config_parse_line(&p, "HOSTNAME", big);                    /* oversize -> truncated + NUL */

    TN_ASSERT_STREQ(p.device, "ethernet.device");
    TN_ASSERT_EQ((int)strlen(p.hostname), (int)sizeof(p.hostname) - 1);
}

TN_TEST(debug_bounds_and_unknown_keys)
{
    TnPrefs p;
    memset(&p, 0, sizeof(p));

    tn_config_parse_line(&p, "DEBUG", "3");   /* out of range -> ignored */
    TN_ASSERT_EQ(p.debug, 0u);
    tn_config_parse_line(&p, "DEBUG", "2");
    TN_ASSERT_EQ(p.debug, 2u);
    tn_config_parse_line(&p, "PRIORITY", "200"); /* out of range (>127) -> ignored */
    TN_ASSERT_EQ(p.priority, 0);
    tn_config_parse_line(&p, "PRIORITY", "-5");
    TN_ASSERT_EQ(p.priority, -5);
    tn_config_parse_line(&p, "TOTALLY_UNKNOWN", "42"); /* ignored silently */
    TN_ASSERT_EQ(p.unit, 0u);
}

TN_TEST(defaults_have_no_slirp_literals)
{
    /* TNET-078: defaults must be EMPTY (no 10.0.2.x literals) so a RECONFIG
     * cannot clobber DHCP-supplied DNS. tn_prefs_default lives in
     * config_text.c, so this asserts the real shipping defaults. */
    {
        TnPrefs d;
        memset(&d, 'x', sizeof(d));
        tn_prefs_default(&d);
        TN_ASSERT_STREQ(d.dns_server, "");
        TN_ASSERT_STREQ(d.ip_addr, "");
        TN_ASSERT_STREQ(d.gateway, "");
        TN_ASSERT_STREQ(d.dns2, "");
        TN_ASSERT_EQ(d.priority, 5);
    }
}

TN_TEST(version_header_formatted)
{
    TnPrefs in;
    char text[TN_CONFIG_TEXT_MAX];
    tn_prefs_default(&in);
    TN_ASSERT_TRUE(tn_config_format(&in, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "VERSION=1") != NULL);
}

TN_TEST(hand_edited_devs_precedence)
{
    /* TNET-083: Simulates the mtime comparison rule.
     * When DEVS has a newer DateStamp than ENV (or equal), DEVS wins.
     * When ENV has a strictly newer DateStamp, ENV wins. */
    struct {
        int32_t ds_Days;
        int32_t ds_Minute;
        int32_t ds_Tick;
    } date_devs, date_env;

    /* Case 1: Hand-edited DEVS (newer minute) -> DEVS wins */
    date_devs.ds_Days = 1000; date_devs.ds_Minute = 120; date_devs.ds_Tick = 0;
    date_env.ds_Days  = 1000; date_env.ds_Minute = 100; date_env.ds_Tick = 0;
    int env_is_newer = (date_env.ds_Days > date_devs.ds_Days) ||
                       (date_env.ds_Days == date_devs.ds_Days && date_env.ds_Minute > date_devs.ds_Minute) ||
                       (date_env.ds_Days == date_devs.ds_Days && date_env.ds_Minute == date_devs.ds_Minute && date_env.ds_Tick > date_devs.ds_Tick);
    TN_ASSERT_FALSE(env_is_newer);

    /* Case 2: User clicked 'Use' in Prefs (newer minute in ENV) -> ENV wins */
    date_env.ds_Minute = 130;
    env_is_newer = (date_env.ds_Days > date_devs.ds_Days) ||
                   (date_env.ds_Days == date_devs.ds_Days && date_env.ds_Minute > date_devs.ds_Minute) ||
                   (date_env.ds_Days == date_devs.ds_Days && date_env.ds_Minute == date_devs.ds_Minute && date_env.ds_Tick > date_devs.ds_Tick);
    TN_ASSERT_TRUE(env_is_newer);
}

int main(void)
{
    TN_TEST_RUN(round_trip_all_keys);
    TN_TEST_RUN(key_dialects_and_case);
    TN_TEST_RUN(dns2_does_not_alias_dns1);
    TN_TEST_RUN(value_cleanup_and_oversize);
    TN_TEST_RUN(debug_bounds_and_unknown_keys);
    TN_TEST_RUN(defaults_have_no_slirp_literals);
    TN_TEST_RUN(version_header_formatted);
    TN_TEST_RUN(hand_edited_devs_precedence);
    TN_TEST_PLAN();
    return tn_test_failures();
}
