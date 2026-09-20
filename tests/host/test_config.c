/*
 * test_config.c — config text grammar round-trips (Round 3 §B.1, TNET-073).
 * Unit: src/common/config_text.c (drives prefs.c parse + DEVS: writer).
 */
#include "tn_test.h"
#include "../../src/common/config_text.h"
#include "../../include/ipc.h"
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
        /* TNET-108 defaults */
        TN_ASSERT_EQ(d.log_level, -1);      /* derive from DEBUG */
        TN_ASSERT_STREQ(d.database_order, "");
        TN_ASSERT_EQ(d.selectors, 0u);      /* TN_MAX_SELECTORS */
        TN_ASSERT_EQ(d.stats, TRUE);
        TN_ASSERT_STREQ(d.syslog_host, "");
    }
}

/* ---- TNET-108: RECONFIG hot-reload keys ---------------------------------- */

TN_TEST(round_trip_tnet108_keys)
{
    TnPrefs in, out;
    char text[TN_CONFIG_TEXT_MAX];

    memset(&in, 0, sizeof(in));
    strcpy(in.device, "ethernet.device");
    in.priority = -3;
    strcpy(in.log_file, "WORK:daemon.log");
    in.log_level = 2;
    strcpy(in.database_order, "local,dns");
    in.selectors = 64;
    in.stats = FALSE;
    strcpy(in.syslog_host, "10.0.2.2");

    TN_ASSERT_TRUE(tn_config_format(&in, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "LOGLEVEL=2") != NULL);
    TN_ASSERT_TRUE(strstr(text, "SELECTORS=64") != NULL);
    TN_ASSERT_TRUE(strstr(text, "STATS=NO") != NULL);
    TN_ASSERT_TRUE(strstr(text, "DATABASE_ORDER=local,dns") != NULL);
    TN_ASSERT_TRUE(strstr(text, "SYSLOG=10.0.2.2") != NULL);

    memset(&out, 0, sizeof(out));
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

    TN_ASSERT_EQ(out.priority, -3);
    TN_ASSERT_STREQ(out.log_file, "WORK:daemon.log");
    TN_ASSERT_EQ(out.log_level, 2);
    TN_ASSERT_STREQ(out.database_order, "local,dns");
    TN_ASSERT_EQ(out.selectors, 64u);
    TN_ASSERT_EQ(out.stats, FALSE);
    TN_ASSERT_STREQ(out.syslog_host, "10.0.2.2");
}

TN_TEST(tnet108_key_bounds_and_validation)
{
    TnPrefs p;
    memset(&p, 0, sizeof(p));

    /* LOGLEVEL bounds: 0..2 accepted, outside ignored */
    tn_config_parse_line(&p, "LOGLEVEL", "2");
    TN_ASSERT_EQ(p.log_level, 2);
    tn_config_parse_line(&p, "LOGLEVEL", "3");
    TN_ASSERT_EQ(p.log_level, 2);
    tn_config_parse_line(&p, "LOGLEVEL", "-1");
    TN_ASSERT_EQ(p.log_level, 2);

    /* SELECTORS bounds: 1..128 accepted, outside ignored */
    tn_config_parse_line(&p, "SELECTORS", "128");
    TN_ASSERT_EQ(p.selectors, 128u);
    tn_config_parse_line(&p, "SELECTORS", "129");
    TN_ASSERT_EQ(p.selectors, 128u);
    tn_config_parse_line(&p, "SELECTORS", "0");
    TN_ASSERT_EQ(p.selectors, 128u);

    /* STATS truth spellings */
    tn_config_parse_line(&p, "STATS", "NO");
    TN_ASSERT_EQ(p.stats, FALSE);
    tn_config_parse_line(&p, "STATS", "YES");
    TN_ASSERT_EQ(p.stats, TRUE);
    tn_config_parse_line(&p, "STATS", "OFF");
    TN_ASSERT_EQ(p.stats, FALSE);
    tn_config_parse_line(&p, "STATS", "TRUE");
    TN_ASSERT_EQ(p.stats, TRUE);

    /* SYSLOG charset: hostname/dotted-quad only — spaces, quotes, semicolons
     * and newlines must be rejected (value keeps its previous state) */
    tn_config_parse_line(&p, "SYSLOG", "loghost.lan");
    TN_ASSERT_STREQ(p.syslog_host, "loghost.lan");
    tn_config_parse_line(&p, "SYSLOG", "bad host; rm -rf");
    TN_ASSERT_STREQ(p.syslog_host, "loghost.lan");
    tn_config_parse_line(&p, "SYSLOG", "quote\"host");
    TN_ASSERT_STREQ(p.syslog_host, "loghost.lan");
    tn_config_parse_line(&p, "SYSLOG", "");
    TN_ASSERT_STREQ(p.syslog_host, "loghost.lan");

    /* DATABASE_ORDER charset */
    tn_config_parse_line(&p, "DATABASE_ORDER", "local,dns");
    TN_ASSERT_STREQ(p.database_order, "local,dns");
    tn_config_parse_line(&p, "DATABASE_ORDER", "local;evil");
    TN_ASSERT_STREQ(p.database_order, "local,dns");
}

TN_TEST(recfg_effective_loglevel_precedence)
{
    TnPrefs p;
    tn_prefs_default(&p);   /* log_level sentinel -1 = "derive from DEBUG" */

    /* LOGLEVEL unset -> historical DEBUG-derived tier */
    TN_ASSERT_EQ(tn_recfg_effective_loglevel(&p), 1); /* BASIC */
    p.debug = 1;
    TN_ASSERT_EQ(tn_recfg_effective_loglevel(&p), 2); /* VERBOSE */
    p.debug = 2;
    TN_ASSERT_EQ(tn_recfg_effective_loglevel(&p), 2);

    /* LOGLEVEL set -> wins over DEBUG */
    p.log_level = 0;
    TN_ASSERT_EQ(tn_recfg_effective_loglevel(&p), 0); /* OFF */
    p.log_level = 1;
    TN_ASSERT_EQ(tn_recfg_effective_loglevel(&p), 1);
}

TN_TEST(recfg_diff_classification)
{
    TnPrefs a, b;
    uint32_t restart = 0xFFFFFFFFu, live = 0xFFFFFFFFu;

    /* identical snapshots -> nothing changed */
    tn_prefs_default(&a);
    tn_prefs_default(&b);
    tn_recfg_diff(&a, &b, &restart, &live);
    TN_ASSERT_EQ_U(restart, 0);
    TN_ASSERT_EQ_U(live, 0);

    /* interface keys are restart-only */
    tn_prefs_default(&a);
    tn_prefs_default(&b);
    strcpy(b.device, "other.device");
    b.unit = 2;
    b.use_dhcp = FALSE;
    strcpy(b.ip_addr, "10.0.0.5");
    strcpy(b.netmask, "255.0.0.0");
    strcpy(b.gateway, "10.0.0.1");
    tn_recfg_diff(&a, &b, &restart, &live);
    TN_ASSERT_EQ_U(restart, TN_RECFG_DEVICE | TN_RECFG_UNIT | TN_RECFG_DHCP |
                             TN_RECFG_IP | TN_RECFG_NETMASK | TN_RECFG_GATEWAY);
    TN_ASSERT_EQ_U(live, 0);

    /* hot-reloadable keys are live, not restart */
    tn_prefs_default(&a);
    tn_prefs_default(&b);
    strcpy(b.dns_server, "1.1.1.1");
    strcpy(b.dns2, "1.0.0.1");
    strcpy(b.hostname, "renamed");
    b.mtu = 1400;
    b.log_level = 2;
    b.priority = 7;
    strcpy(b.log_file, "WORK:new.log");
    strcpy(b.database_order, "local,dns");
    b.selectors = 32;
    b.stats = FALSE;
    strcpy(b.syslog_host, "10.0.2.2");
    tn_recfg_diff(&a, &b, &restart, &live);
    TN_ASSERT_EQ_U(restart, 0);
    TN_ASSERT_EQ_U(live, TN_RECFG_DNS | TN_RECFG_DNS2 | TN_RECFG_HOSTNAME |
                          TN_RECFG_MTU | TN_RECFG_LOGLEVEL | TN_RECFG_PRIORITY |
                          TN_RECFG_LOG | TN_RECFG_DATABASE_ORDER |
                          TN_RECFG_SELECTORS | TN_RECFG_STATS | TN_RECFG_SYSLOG);

    /* DEBUG change without LOGLEVEL still maps onto the LOGLEVEL bit */
    tn_prefs_default(&a);
    tn_prefs_default(&b);
    b.debug = 1;
    tn_recfg_diff(&a, &b, &restart, &live);
    TN_ASSERT_EQ_U(restart, 0);
    TN_ASSERT_EQ_U(live, TN_RECFG_LOGLEVEL);

    /* equal DEBUG+LOGLEVEL -> no LOGLEVEL bit */
    tn_prefs_default(&a);
    tn_prefs_default(&b);
    a.debug = 1; b.debug = 1; a.log_level = 2; b.log_level = 2;
    tn_recfg_diff(&a, &b, &restart, &live);
    TN_ASSERT_EQ_U(live, 0);
}

TN_TEST(recfg_key_name_table)
{
    uint32_t bit;
    int named = 0;

    for (bit = 1; bit <= TN_RECFG_ALL; bit <<= 1) {
        const char *name = tn_recfg_key_name(bit);
        TN_ASSERT_TRUE(name != NULL);
        TN_ASSERT_TRUE(name[0] >= 'A' && name[0] <= 'Z');
        named++;
    }
    TN_ASSERT_EQ(named, 18); /* every defined bit has a printable name */
    TN_ASSERT_STREQ(tn_recfg_key_name(TN_RECFG_S2EVENTS), "S2EVENTS");

    TN_ASSERT_TRUE(tn_recfg_key_name(0) == NULL);
    TN_ASSERT_TRUE(tn_recfg_key_name(TN_RECFG_ALL + 1) == NULL);
    TN_ASSERT_STREQ(tn_recfg_key_name(TN_RECFG_DEVICE), "DEVICE");
    TN_ASSERT_STREQ(tn_recfg_key_name(TN_RECFG_SYSLOG), "SYSLOG");
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

/* TNET-109: S2EVENTS= mask parsing, formatting, and restart-only diff. */
TN_TEST(s2events_key_parsing)
{
    TnPrefs a, b;
    char text[TN_CONFIG_TEXT_MAX];
    uint32_t restart = 1, live = 1;

    memset(&a, 0, sizeof(a));

    tn_config_parse_line(&a, "S2EVENTS", "ONLINE|OFFLINE|ERROR");
    TN_ASSERT_EQ_U(a.s2events, 0x08u | 0x10u | 0x01u);

    tn_config_parse_line(&a, "S2EVENTS", "HARDWARE|SOFTWARE");
    TN_ASSERT_EQ_U(a.s2events, 0x40u | 0x80u);

    tn_config_parse_line(&a, "S2EVENTS", "DEFAULT");
    TN_ASSERT_EQ_U(a.s2events, 0u);

    tn_config_parse_line(&a, "S2EVENTS", "ONLINE");
    TN_ASSERT_EQ_U(a.s2events, 0x08u);

    /* unknown name rejects the whole value */
    tn_config_parse_line(&a, "S2EVENTS", "ONLINE|BOGUS");
    TN_ASSERT_EQ_U(a.s2events, 0x08u);

    /* interior whitespace rejected on the raw value */
    tn_config_parse_line(&a, "S2EVENTS", "ONLINE OFFLINE");
    TN_ASSERT_EQ_U(a.s2events, 0x08u);

    /* empty value rejected */
    tn_config_parse_line(&a, "S2EVENTS", "");
    TN_ASSERT_EQ_U(a.s2events, 0x08u);

    /* format round trip: non-default mask is written and reparses equal */
    a.s2events = 0x08u | 0x01u;
    TN_ASSERT_TRUE(tn_config_format(&a, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "S2EVENTS=ONLINE|ERROR") != NULL);
    memset(&b, 0, sizeof(b));
    tn_config_parse_line(&b, "S2EVENTS", "ONLINE|ERROR");
    TN_ASSERT_EQ_U(b.s2events, a.s2events);

    /* default (0) is not written */
    a.s2events = 0;
    TN_ASSERT_TRUE(tn_config_format(&a, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "S2EVENTS=") == NULL);

    /* changed mask classifies restart-only */
    tn_prefs_default(&a);
    tn_prefs_default(&b);
    b.s2events = 0x08u;
    tn_recfg_diff(&a, &b, &restart, &live);
    TN_ASSERT_EQ_U(restart, TN_RECFG_S2EVENTS);
    TN_ASSERT_EQ_U(live, 0);
}

/* TNET-111: DNS_PORT= key (bench mini_dns) rides on the DNS diff bit. */
TN_TEST(dns_port_key)
{
    TnPrefs a, b;
    char text[TN_CONFIG_TEXT_MAX];
    uint32_t restart = 1, live = 1;

    memset(&a, 0, sizeof(a));

    tn_config_parse_line(&a, "DNS_PORT", "5353");
    TN_ASSERT_EQ(a.dns_port, 5353u);

    /* out of range -> ignored, previous value kept */
    tn_config_parse_line(&a, "DNS_PORT", "0");
    TN_ASSERT_EQ(a.dns_port, 5353u);
    tn_config_parse_line(&a, "DNS_PORT", "70000");
    TN_ASSERT_EQ(a.dns_port, 5353u);
    tn_config_parse_line(&a, "DNS_PORT", "junk");
    TN_ASSERT_EQ(a.dns_port, 5353u);

    /* formatted only when set */
    TN_ASSERT_TRUE(tn_config_format(&a, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "DNS_PORT=5353") != NULL);
    a.dns_port = 0;
    TN_ASSERT_TRUE(tn_config_format(&a, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "DNS_PORT=") == NULL);

    /* a port change classifies onto the live DNS bit */
    tn_prefs_default(&a);
    tn_prefs_default(&b);
    b.dns_port = 5353;
    tn_recfg_diff(&a, &b, &restart, &live);
    TN_ASSERT_EQ_U(restart, 0);
    TN_ASSERT_EQ_U(live, TN_RECFG_DNS);
}

TN_TEST(font_key_tnet110)
{
    TnPrefs a;
    char text[TN_CONFIG_TEXT_MAX];

    memset(&a, 0, sizeof(a));

    /* valid spec parses */
    tn_config_parse_line(&a, "FONT", "topaz/11");
    TN_ASSERT_STREQ(a.font, "topaz/11");

    /* bad values are rejected, previous value kept */
    tn_config_parse_line(&a, "FONT", "topaz/99");
    TN_ASSERT_STREQ(a.font, "topaz/11");
    tn_config_parse_line(&a, "FONT", "topaz");
    TN_ASSERT_STREQ(a.font, "topaz/11");
    tn_config_parse_line(&a, "FONT", "bad name/9");
    TN_ASSERT_STREQ(a.font, "topaz/11");
    tn_config_parse_line(&a, "FONT", "topaz/x");
    TN_ASSERT_STREQ(a.font, "topaz/11");
    tn_config_parse_line(&a, "FONT", "");
    TN_ASSERT_STREQ(a.font, "topaz/11");

    /* formatted only when set; round-trips through the parser */
    TN_ASSERT_TRUE(tn_config_format(&a, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "FONT=topaz/11") != NULL);
    a.font[0] = '\0';
    TN_ASSERT_TRUE(tn_config_format(&a, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "FONT=") == NULL);

    /* default is empty (screen font) */
    tn_prefs_default(&a);
    TN_ASSERT_EQ(a.font[0], '\0');
}

static void diag_key_tnet139(void)
{
    TnPrefs a;
    char text[512];

    tn_prefs_default(&a);
    TN_ASSERT_EQ(a.diag, 0);

    tn_config_parse_line(&a, "DIAG", "YES");
    TN_ASSERT_EQ(a.diag, 1);
    tn_config_parse_line(&a, "DIAG", "no");
    TN_ASSERT_EQ(a.diag, 0);
    tn_config_parse_line(&a, "DIAG", "ON");
    TN_ASSERT_EQ(a.diag, 1);
    tn_config_parse_line(&a, "DIAG", "0");
    TN_ASSERT_EQ(a.diag, 0);

    /* DIAG is a runtime switch: never formatted into the config file */
    TN_ASSERT_TRUE(tn_config_format(&a, text, sizeof(text)) > 0);
    TN_ASSERT_TRUE(strstr(text, "DIAG=") == NULL);

    tn_prefs_default(&a);
    TN_ASSERT_EQ(a.diag, 0);
}


/* CLOSE §B.8: CheckNetConfig vocabulary — single source with the parser. */
TN_TEST(checknetconfig_vocabulary)
{
    /* every key the parser accepts is known */
    TN_ASSERT_TRUE(tn_config_key_known("DEVICE"));
    TN_ASSERT_TRUE(tn_config_key_known("device"));   /* case-insensitive */
    TN_ASSERT_TRUE(tn_config_key_known("USE_DHCP"));
    TN_ASSERT_TRUE(tn_config_key_known("DNS_PORT"));
    TN_ASSERT_TRUE(tn_config_key_known("DNS_PENDING"));
    TN_ASSERT_TRUE(tn_config_key_known("DNS_RETRIES"));
    TN_ASSERT_TRUE(tn_config_key_known("DATABASE_ORDER"));
    TN_ASSERT_TRUE(tn_config_key_known("S2EVENTS"));
    TN_ASSERT_TRUE(tn_config_key_known("VERSION"));
    /* unknown / degenerate */
    TN_ASSERT_TRUE(!tn_config_key_known("DEVICEX"));
    TN_ASSERT_TRUE(!tn_config_key_known(""));
    TN_ASSERT_TRUE(!tn_config_key_known(NULL));
    /* TNET-106 TX_QUEUE key */
    TN_ASSERT_TRUE(tn_config_key_known("TX_QUEUE"));
    TN_ASSERT_EQ(tn_config_value_class("TX_QUEUE"), 2);
    {
        TnPrefs p;
        tn_prefs_default(&p);
        TN_ASSERT_EQ((int)p.tx_queue, 4);          /* absent = 4 */
        tn_config_parse_line(&p, "TX_QUEUE", "0");
        TN_ASSERT_EQ((int)p.tx_queue, 0);          /* explicit sync */
        tn_prefs_default(&p);
        tn_config_parse_line(&p, "TX_QUEUE", "8");
        TN_ASSERT_EQ((int)p.tx_queue, 8);
        tn_prefs_default(&p);
        tn_config_parse_line(&p, "TX_QUEUE", "9"); /* out of range: ignored */
        TN_ASSERT_EQ((int)p.tx_queue, 4);
    }

    /* value classes */
    TN_ASSERT_EQ(tn_config_value_class("NETMASK"), 1);
    TN_ASSERT_EQ(tn_config_value_class("GW"), 1);
    TN_ASSERT_EQ(tn_config_value_class("DNS"), 1);
    TN_ASSERT_EQ(tn_config_value_class("UNIT"), 2);
    TN_ASSERT_EQ(tn_config_value_class("DNS_RETRIES"), 2);
    TN_ASSERT_EQ(tn_config_value_class("DHCP"), 3);
    TN_ASSERT_EQ(tn_config_value_class("HOSTNAME"), 0);  /* free string */
    TN_ASSERT_EQ(tn_config_value_class("BOGUS"), 0);
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
    TN_TEST_RUN(round_trip_tnet108_keys);
    TN_TEST_RUN(tnet108_key_bounds_and_validation);
    TN_TEST_RUN(recfg_effective_loglevel_precedence);
    TN_TEST_RUN(recfg_diff_classification);
    TN_TEST_RUN(recfg_key_name_table);
    TN_TEST_RUN(s2events_key_parsing);
    TN_TEST_RUN(dns_port_key);
    TN_TEST_RUN(font_key_tnet110);
    TN_TEST_RUN(diag_key_tnet139);
    TN_TEST_RUN(checknetconfig_vocabulary);
    TN_TEST_PLAN();
    return tn_test_failures();
}
