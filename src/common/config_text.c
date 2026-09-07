/*
 * tolunnet — pure KEY=VALUE config parse/format (host-testable).
 * Grammar per master prompt §5.2 / README.guide §4 (TNET-032/044/063).
 */

#include "config_text.h"

/* Factory defaults. NOTE (TNET-078, Round 3 §C3): the 10.0.2.x literals below
 * are bench/slirp values and are scheduled to become empty strings so a
 * RECONFIG can never clobber DHCP-supplied settings; test_config.c pins the
 * target with a TODO-marked assertion. Lives here (not prefs.c) so the host
 * tests exercise the very code the daemon uses. */
void tn_prefs_default(TnPrefs *prefs)
{
    if (prefs == NULL) return;

    tn_str_copy_clean(prefs->device, "ethernet.device", sizeof(prefs->device));
    prefs->unit = 0;
    prefs->use_dhcp = TRUE;
    prefs->ip_addr[0] = '\0';
    prefs->netmask[0] = '\0';
    prefs->gateway[0] = '\0';
    prefs->dns_server[0] = '\0';
    tn_str_copy_clean(prefs->hostname, "amiga", sizeof(prefs->hostname));
    prefs->dns2[0] = 0;
    prefs->mtu  = 0;   /* 0 = use driver-reported MTU */
    prefs->debug = 0;
    prefs->priority = 5; /* TNET-066 */
}


int tn_str_equal_nocase(const char *s1, const char *s2)
{
    if (!s1 || !s2) return 0;
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return 0;
        s1++; s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

void tn_str_copy_clean(char *dst, const char *src, int max_len)
{
    int i = 0;
    while (src && *src && *src != '\r' && *src != '\n' && *src != ' ' && *src != '\t' && i < max_len - 1) {
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

/* Plain decimal parse (replaces DOS StrToLong so the unit is host-buildable) */
static LONG tn_str_to_long(const char *s, LONG *out)
{
    LONG v = 0;
    int digits = 0;
    int neg = 0;

    if (s == NULL) return 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        digits++;
        s++;
    }
    if (digits == 0) return 0;
    *out = neg ? -v : v;
    return 1; /* consumed */
}

void tn_config_parse_line(TnPrefs *prefs, const char *key, const char *val)
{
    char clean_key[32];
    char clean_val[64];

    if (prefs == NULL || key == NULL || val == NULL) return;

    tn_str_copy_clean(clean_key, key, sizeof(clean_key));
    tn_str_copy_clean(clean_val, val, sizeof(clean_val));

    if (tn_str_equal_nocase(clean_key, "DEVICE")) {
        tn_str_copy_clean(prefs->device, clean_val, sizeof(prefs->device));
    } else if (tn_str_equal_nocase(clean_key, "UNIT")) {
        LONG u = 0;
        if (tn_str_to_long(clean_val, &u)) prefs->unit = (ULONG)u;
    } else if (tn_str_equal_nocase(clean_key, "DHCP") || tn_str_equal_nocase(clean_key, "USE_DHCP")) {
        if (tn_str_equal_nocase(clean_val, "YES") || tn_str_equal_nocase(clean_val, "1") || tn_str_equal_nocase(clean_val, "TRUE")) {
            prefs->use_dhcp = TRUE;
        } else {
            prefs->use_dhcp = FALSE;
        }
    } else if (tn_str_equal_nocase(clean_key, "IP") || tn_str_equal_nocase(clean_key, "IP_ADDR")) {
        tn_str_copy_clean(prefs->ip_addr, clean_val, sizeof(prefs->ip_addr));
    } else if (tn_str_equal_nocase(clean_key, "NETMASK") || tn_str_equal_nocase(clean_key, "MASK")) {
        tn_str_copy_clean(prefs->netmask, clean_val, sizeof(prefs->netmask));
    } else if (tn_str_equal_nocase(clean_key, "GATEWAY") || tn_str_equal_nocase(clean_key, "GW")) {
        tn_str_copy_clean(prefs->gateway, clean_val, sizeof(prefs->gateway));
    } else if (tn_str_equal_nocase(clean_key, "DNS2")) {
        /* TNET-063: secondary nameserver is its own key, never aliases DNS1 */
        tn_str_copy_clean(prefs->dns2, clean_val, sizeof(prefs->dns2));
    } else if (tn_str_equal_nocase(clean_key, "DNS") || tn_str_equal_nocase(clean_key, "DNS1") ||
               tn_str_equal_nocase(clean_key, "NAMESERVER")) {
        tn_str_copy_clean(prefs->dns_server, clean_val, sizeof(prefs->dns_server));
    } else if (tn_str_equal_nocase(clean_key, "HOSTNAME")) {
        tn_str_copy_clean(prefs->hostname, clean_val, sizeof(prefs->hostname));
    } else if (tn_str_equal_nocase(clean_key, "MTU")) {
        LONG m = 0;
        if (tn_str_to_long(clean_val, &m)) prefs->mtu = (ULONG)m;
    } else if (tn_str_equal_nocase(clean_key, "DEBUG")) {
        LONG d = 0;
        if (tn_str_to_long(clean_val, &d) && d >= 0 && d <= 2) prefs->debug = (ULONG)d;
    } else if (tn_str_equal_nocase(clean_key, "PRIORITY")) {
        LONG p = 5;
        if (tn_str_to_long(clean_val, &p) && p >= -128 && p <= 127) prefs->priority = p;
    } else if (tn_str_equal_nocase(clean_key, "VERSION")) {
        /* Config format version recognised */
    }
}

/* Append helper used by tn_config_format */
struct tn_cfg_out {
    char *buf;
    int   size;
    int   len;
    int   overflow;
};

static void tn_cfg_put(struct tn_cfg_out *o, const char *s)
{
    while (*s) {
        if (o->len + 1 < o->size) {
            o->buf[o->len++] = *s;
        } else {
            o->overflow = 1;
        }
        s++;
    }
}

static void tn_cfg_put_kv_int(struct tn_cfg_out *o, const char *key, ULONG v)
{
    char num[16];
    int i = 0, j;
    if (v == 0) { num[0] = '0'; num[1] = '\0'; }
    else {
        char tmp[16];
        while (v > 0 && i < 15) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
        for (j = 0; j < i; j++) num[j] = tmp[i - 1 - j];
        num[i] = '\0';
    }
    tn_cfg_put(o, key);
    tn_cfg_put(o, num);
    tn_cfg_put(o, "\n");
}

static void tn_cfg_put_kv_long(struct tn_cfg_out *o, const char *key, LONG v)
{
    char num[16];
    int i = 0, j;
    ULONG uv;
    if (v < 0) {
        num[i++] = '-';
        uv = (ULONG)(-v);
    } else {
        uv = (ULONG)v;
    }
    if (uv == 0) {
        num[i++] = '0';
        num[i] = '\0';
    } else {
        char tmp[16];
        int ti = 0;
        while (uv > 0 && ti < 15) { tmp[ti++] = (char)('0' + (uv % 10)); uv /= 10; }
        for (j = 0; j < ti; j++) num[i++] = tmp[ti - 1 - j];
        num[i] = '\0';
    }
    tn_cfg_put(o, key);
    tn_cfg_put(o, num);
    tn_cfg_put(o, "\n");
}

static void tn_cfg_put_kv_str(struct tn_cfg_out *o, const char *key, const char *val)
{
    tn_cfg_put(o, key);
    tn_cfg_put(o, val ? val : "");
    tn_cfg_put(o, "\n");
}

int tn_config_format(const TnPrefs *prefs, char *buf, int buf_size)
{
    struct tn_cfg_out o = { buf, buf_size, 0, 0 };

    tn_cfg_put(&o, "# tolunnet configuration file\n");
    tn_cfg_put(&o, "VERSION=1\n");
    tn_cfg_put_kv_str(&o, "DEVICE=", prefs->device);
    tn_cfg_put_kv_int(&o, "UNIT=", prefs->unit);
    tn_cfg_put(&o, "DHCP=");
    tn_cfg_put(&o, prefs->use_dhcp ? "YES\n" : "NO\n");
    tn_cfg_put_kv_str(&o, "IP=", prefs->ip_addr);
    tn_cfg_put_kv_str(&o, "NETMASK=", prefs->netmask);
    tn_cfg_put_kv_str(&o, "GATEWAY=", prefs->gateway);
    tn_cfg_put_kv_str(&o, "DNS=", prefs->dns_server);
    tn_cfg_put_kv_str(&o, "DNS2=", prefs->dns2);
    tn_cfg_put_kv_str(&o, "HOSTNAME=", prefs->hostname);
    tn_cfg_put_kv_int(&o, "MTU=", prefs->mtu);
    tn_cfg_put_kv_int(&o, "DEBUG=", prefs->debug);
    tn_cfg_put_kv_long(&o, "PRIORITY=", prefs->priority);

    if (o.overflow) {
        return -1; /* buffer too small; TN_CONFIG_TEXT_MAX is always sufficient */
    }
    buf[o.len] = '\0';
    return o.len;
}
