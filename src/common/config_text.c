/*
 * tolunnet — pure KEY=VALUE config parse/format (host-testable).
 * Grammar per master prompt §5.2 / README.guide §4 (TNET-032/044/063).
 */

#include "config_text.h"
#include "../../include/ipc.h"
#include <string.h>

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
    prefs->log_file[0] = '\0';
    prefs->log_level = -1;      /* TNET-108: derive from DEBUG when unset */
    prefs->database_order[0] = '\0';
    prefs->selectors = 0;       /* 0 = TN_MAX_SELECTORS */
    prefs->stats = TRUE;
    prefs->syslog_host[0] = '\0';
    prefs->s2events = 0;        /* 0 = ONLINE|OFFLINE|ERROR (TNET-109) */
    prefs->dns_port = 0;        /* 0 = default 53 (TNET-111) */
    prefs->diag = FALSE;       /* TNET-139: crash diagnostics off by default */
    prefs->tx_queue = 4;        /* TNET-106: TX pool slots (0 = sync) */
    prefs->font[0] = '\0';      /* empty = TolunnetSetup uses the screen font */
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

/* Exact string equality for config VALUE comparison (TNET-108 diff masks). */
static int tn_streq_cfg(const char *a, const char *b)
{
    if (a == b) return 1;
    if (a == NULL || b == NULL) return 0;
    while (*a && *a == *b) { a++; b++; }
    return (*a == '\0' && *b == '\0');
}



/* TNET-108: raw config-value token check. TRUE when val consists solely of
 * [A-Za-z0-9] plus the given extra characters, optionally followed by
 * trailing whitespace/CR/LF. Anything else (interior space, quote, semicolon,
 * control char) makes the value invalid so the key is ignored. */
static int tn_val_token(const char *val, const char *extra)
{
    size_t end = 0;
    size_t i;

    if (val == NULL) return 0;
    while (val[end] != '\0') end++;
    while (end > 0 && (val[end - 1] == ' ' || val[end - 1] == '\t' ||
                       val[end - 1] == '\r' || val[end - 1] == '\n')) {
        end--;
    }
    for (i = 0; i < end; i++) {
        char c = val[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!ok && extra != NULL) {
            const char *e = extra;
            while (*e != '\0' && *e != c) e++;
            ok = (*e == c);
        }
        if (!ok) return 0;
    }
    return 1;
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
        if (tn_str_equal_nocase(clean_val, "YES") || tn_str_equal_nocase(clean_val, "1") ||
            tn_str_equal_nocase(clean_val, "TRUE") || tn_str_equal_nocase(clean_val, "DHCP")) {
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
    } else if (tn_str_equal_nocase(clean_key, "DNS_PORT")) {
        /* TNET-111: resolver destination port (bench mini_dns on :5353) */
        LONG dp = 0;
        if (tn_str_to_long(clean_val, &dp) && dp >= 1 && dp <= 65535) {
            prefs->dns_port = (ULONG)dp;
        }
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
    } else if (tn_str_equal_nocase(clean_key, "LOG")) {
        tn_str_copy_clean(prefs->log_file, clean_val, sizeof(prefs->log_file));
    } else if (tn_str_equal_nocase(clean_key, "LOGLEVEL")) {
        /* TNET-108: direct log tier control (0=OFF..2=VERBOSE). Overrides the
         * DEBUG-derived tier while set. */
        LONG lv = 0;
        if (tn_str_to_long(clean_val, &lv) && lv >= 0 && lv <= 2) prefs->log_level = lv;
    } else if (tn_str_equal_nocase(clean_key, "DATABASE_ORDER")) {
        /* TNET-108 (§D1): netdb lookup order, e.g. "local,dns". Validated on
         * the RAW value: interior whitespace is silently truncated by the
         * clean copy, so checking clean_val alone would accept "a b" as "a". */
        if (tn_val_token(val, ",_-") && clean_val[0] != '\0') {
            tn_str_copy_clean(prefs->database_order, clean_val, sizeof(prefs->database_order));
        }
    } else if (tn_str_equal_nocase(clean_key, "SELECTORS")) {
        LONG n = 0;
        if (tn_str_to_long(clean_val, &n) && n >= 1 && n <= 128) prefs->selectors = (ULONG)n;
    } else if (tn_str_equal_nocase(clean_key, "TX_QUEUE")) {
        /* TNET-106 CLOSE S-C: TX pool slots (0 = synchronous DoIO) */
        LONG n = 0;
        if (tn_str_to_long(clean_val, &n) && n >= 0 && n <= 8) {
            prefs->tx_queue = (ULONG)n;
        }
    } else if (tn_str_equal_nocase(clean_key, "DNS_PENDING")) {
        /* TNET-150: deferred gethostbyname tracking slots */
        LONG n = 0;
        if (tn_str_to_long(clean_val, &n) && n >= 1 && n <= TN_DNS_PENDING_MAX) {
            prefs->dns_pending_max = (ULONG)n;
        }
    } else if (tn_str_equal_nocase(clean_key, "DNS_RETRIES")) {
        /* TNET-150: informational — must match the compiled lwIP
         * DNS_MAX_RETRIES; sizes the client watchdog ordering guard */
        LONG n = 0;
        if (tn_str_to_long(clean_val, &n) && n >= 1 && n <= 16) {
            prefs->dns_retries = (ULONG)n;
        }
    } else if (tn_str_equal_nocase(clean_key, "STATS")) {
        if (tn_str_equal_nocase(clean_val, "NO") || tn_str_equal_nocase(clean_val, "0") ||
            tn_str_equal_nocase(clean_val, "FALSE") || tn_str_equal_nocase(clean_val, "OFF")) {
            prefs->stats = FALSE;
        } else {
            prefs->stats = TRUE;
        }
    } else if (tn_str_equal_nocase(clean_key, "SYSLOG")) {
        /* TNET-108 (§D3): UDP-514 forward target — dotted quad or hostname.
         * Raw-value validation, same reasoning as DATABASE_ORDER. */
        if (tn_val_token(val, ".-") && clean_val[0] != '\0' &&
            strlen(clean_val) < sizeof(prefs->syslog_host)) {
            tn_str_copy_clean(prefs->syslog_host, clean_val, sizeof(prefs->syslog_host));
        }
    } else if (tn_str_equal_nocase(clean_key, "S2EVENTS")) {
        /* TNET-109: S2_ONEVENT mask, pipe-separated names (ONLINE|OFFLINE|
         * ERROR|TX|RX|BUFF|HARDWARE|SOFTWARE). Validated on the raw value
         * like the other token keys; one unknown name rejects the value. */
        if (tn_val_token(val, "|") && clean_val[0] != '\0') {
            if (tn_str_equal_nocase(clean_val, "DEFAULT")) {
                prefs->s2events = 0;
            } else {
                ULONG mask = 0;
                const char *p = clean_val;
                int valid = 1;
                while (*p && valid) {
                    char name[16];
                    int n = 0;
                    while (*p && *p != '|' && n < (int)sizeof(name) - 1) {
                        name[n++] = *p++;
                    }
                    name[n] = '\0';
                    if (*p == '|') p++;
                    if (n == 0) { valid = 0; break; }
                    if      (tn_str_equal_nocase(name, "ONLINE"))   mask |= TN_S2EV_ONLINE;
                    else if (tn_str_equal_nocase(name, "OFFLINE"))  mask |= TN_S2EV_OFFLINE;
                    else if (tn_str_equal_nocase(name, "ERROR"))    mask |= TN_S2EV_ERROR;
                    else if (tn_str_equal_nocase(name, "TX"))       mask |= TN_S2EV_TX;
                    else if (tn_str_equal_nocase(name, "RX"))       mask |= TN_S2EV_RX;
                    else if (tn_str_equal_nocase(name, "BUFF"))     mask |= TN_S2EV_BUFF;
                    else if (tn_str_equal_nocase(name, "HARDWARE")) mask |= TN_S2EV_HARDWARE;
                    else if (tn_str_equal_nocase(name, "SOFTWARE")) mask |= TN_S2EV_SOFTWARE;
                    else valid = 0;
                }
                if (valid && mask != 0) prefs->s2events = mask;
            }
        }
    } else if (tn_str_equal_nocase(clean_key, "FONT")) {
        /* TNET-110: TolunnetSetup GUI font override, "name/size" (e.g.
         * topaz/11). Validated on the raw value like the other token keys;
         * size outside 6..24 is rejected (the wizard clamps anyway). */
        if (tn_val_token(val, ".-/") && clean_val[0] != '\0' &&
            strlen(clean_val) < sizeof(prefs->font)) {
            const char *slash = clean_val;
            while (*slash && *slash != '/') slash++;
            if (*slash == '/') {
                LONG size = 0;
                const char *sz = slash + 1;
                int ok = (*sz != '\0');
                const char *d = sz;
                while (*d) {
                    if (*d < '0' || *d > '9') { ok = 0; break; }
                    d++;
                }
                if (ok && tn_str_to_long(sz, &size) && size >= 6 && size <= 24) {
                    tn_str_copy_clean(prefs->font, clean_val, sizeof(prefs->font));
                }
            }
        }
    } else if (tn_str_equal_nocase(clean_key, "DIAG")) {
        /* TNET-139: crash diagnostics — trap handler, crash log, step logs */
        if (tn_str_equal_nocase(clean_val, "YES") || tn_str_equal_nocase(clean_val, "1") ||
            tn_str_equal_nocase(clean_val, "TRUE") || tn_str_equal_nocase(clean_val, "ON")) {
            prefs->diag = TRUE;
        } else {
            prefs->diag = FALSE;
        }
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
    if (prefs->dns_port != 0) {
        tn_cfg_put_kv_int(&o, "DNS_PORT=", prefs->dns_port);
    }
    tn_cfg_put_kv_str(&o, "HOSTNAME=", prefs->hostname);
    tn_cfg_put_kv_int(&o, "MTU=", prefs->mtu);
    tn_cfg_put_kv_int(&o, "DEBUG=", prefs->debug);
    tn_cfg_put_kv_long(&o, "PRIORITY=", prefs->priority);
    if (prefs->log_file[0] != '\0') {
        tn_cfg_put_kv_str(&o, "LOG=", prefs->log_file);
    }
    tn_cfg_put_kv_long(&o, "LOGLEVEL=", prefs->log_level);
    tn_cfg_put_kv_int(&o, "SELECTORS=", prefs->selectors);
    if (prefs->dns_pending_max != 0) tn_cfg_put_kv_int(&o, "DNS_PENDING=", prefs->dns_pending_max);
    if (prefs->dns_retries != 0) tn_cfg_put_kv_int(&o, "DNS_RETRIES=", prefs->dns_retries);
    if (prefs->tx_queue != 0) tn_cfg_put_kv_int(&o, "TX_QUEUE=", prefs->tx_queue);
    tn_cfg_put(&o, "STATS=");
    tn_cfg_put(&o, prefs->stats ? "YES\n" : "NO\n");
    if (prefs->database_order[0] != '\0') {
        tn_cfg_put_kv_str(&o, "DATABASE_ORDER=", prefs->database_order);
    }
    if (prefs->syslog_host[0] != '\0') {
        tn_cfg_put_kv_str(&o, "SYSLOG=", prefs->syslog_host);
    }
    if (prefs->s2events != 0) {
        tn_cfg_put(&o, "S2EVENTS=");
        if (prefs->s2events & TN_S2EV_ONLINE)   tn_cfg_put(&o, "ONLINE|");
        if (prefs->s2events & TN_S2EV_OFFLINE)  tn_cfg_put(&o, "OFFLINE|");
        if (prefs->s2events & TN_S2EV_ERROR)    tn_cfg_put(&o, "ERROR|");
        if (prefs->s2events & TN_S2EV_TX)       tn_cfg_put(&o, "TX|");
        if (prefs->s2events & TN_S2EV_RX)       tn_cfg_put(&o, "RX|");
        if (prefs->s2events & TN_S2EV_BUFF)     tn_cfg_put(&o, "BUFF|");
        if (prefs->s2events & TN_S2EV_HARDWARE) tn_cfg_put(&o, "HARDWARE|");
        if (prefs->s2events & TN_S2EV_SOFTWARE) tn_cfg_put(&o, "SOFTWARE|");
        o.len--; /* drop the trailing pipe */
        tn_cfg_put(&o, "\n");
    }
    if (prefs->font[0] != '\0') {
        tn_cfg_put_kv_str(&o, "FONT=", prefs->font);
    }

    if (o.overflow) {
        return -1; /* buffer too small; TN_CONFIG_TEXT_MAX is always sufficient */
    }
    buf[o.len] = '\0';
    return o.len;
}

/* TNET-108: effective log tier — LOGLEVEL wins when set, else DEBUG-derived
 * (the historical formula the daemon used before LOGLEVEL existed). */
int tn_recfg_effective_loglevel(const TnPrefs *p)
{
    if (p == NULL) return 1; /* TN_LOG_BASIC */
    if (p->log_level >= 0 && p->log_level <= 2) return (int)p->log_level;
    return 1 + ((p->debug > 0) ? 1 : 0);
}

/* TNET-108: classify every changed key between two TnPrefs snapshots into
 * needs_restart (interface-level) and live (hot-reloadable) masks. Pure
 * function — the daemon layer decides whether each live key actually applies
 * (failures are reported in the `failed` mask, not here). */
void tn_recfg_diff(const TnPrefs *oldp, const TnPrefs *newp,
                   uint32_t *needs_restart, uint32_t *live)
{
    uint32_t restart = 0;
    uint32_t livem = 0;

    if (oldp == NULL || newp == NULL) {
        if (needs_restart) *needs_restart = 0;
        if (live) *live = 0;
        return;
    }

    if (!tn_streq_cfg(oldp->device, newp->device))          restart |= TN_RECFG_DEVICE;
    if (oldp->unit != newp->unit)                           restart |= TN_RECFG_UNIT;
    if (oldp->use_dhcp != newp->use_dhcp)                   restart |= TN_RECFG_DHCP;
    if (!tn_streq_cfg(oldp->ip_addr, newp->ip_addr))        restart |= TN_RECFG_IP;
    if (!tn_streq_cfg(oldp->netmask, newp->netmask))        restart |= TN_RECFG_NETMASK;
    if (!tn_streq_cfg(oldp->gateway, newp->gateway))        restart |= TN_RECFG_GATEWAY;

    if (!tn_streq_cfg(oldp->dns_server, newp->dns_server) ||
        oldp->dns_port != newp->dns_port)                     livem |= TN_RECFG_DNS;
    if (!tn_streq_cfg(oldp->dns2, newp->dns2))              livem |= TN_RECFG_DNS2;
    if (!tn_streq_cfg(oldp->hostname, newp->hostname))      livem |= TN_RECFG_HOSTNAME;
    if (oldp->mtu != newp->mtu)                             livem |= TN_RECFG_MTU;
    if (tn_recfg_effective_loglevel(oldp) != tn_recfg_effective_loglevel(newp))
                                                            livem |= TN_RECFG_LOGLEVEL;
    if (oldp->priority != newp->priority)                   livem |= TN_RECFG_PRIORITY;
    if (!tn_streq_cfg(oldp->log_file, newp->log_file))      livem |= TN_RECFG_LOG;
    if (!tn_streq_cfg(oldp->database_order, newp->database_order))
                                                            livem |= TN_RECFG_DATABASE_ORDER;
    if (oldp->selectors != newp->selectors)                 livem |= TN_RECFG_SELECTORS;
    if (oldp->stats != newp->stats)                         livem |= TN_RECFG_STATS;
    if (!tn_streq_cfg(oldp->syslog_host, newp->syslog_host)) livem |= TN_RECFG_SYSLOG;
    if (oldp->s2events != newp->s2events)                   restart |= TN_RECFG_S2EVENTS;

    if (needs_restart) *needs_restart = restart;
    if (live) *live = livem;
}

/* TNET-108: printable name for one TN_RECFG_* bit (NULL for unknown/invalid). */
const char *tn_recfg_key_name(uint32_t bit)
{
    switch (bit) {
    case TN_RECFG_DEVICE:         return "DEVICE";
    case TN_RECFG_UNIT:           return "UNIT";
    case TN_RECFG_DHCP:           return "DHCP";
    case TN_RECFG_IP:             return "IP";
    case TN_RECFG_NETMASK:        return "NETMASK";
    case TN_RECFG_GATEWAY:        return "GATEWAY";
    case TN_RECFG_DNS:            return "DNS";
    case TN_RECFG_DNS2:           return "DNS2";
    case TN_RECFG_HOSTNAME:       return "HOSTNAME";
    case TN_RECFG_MTU:            return "MTU";
    case TN_RECFG_LOGLEVEL:       return "LOGLEVEL";
    case TN_RECFG_PRIORITY:       return "PRIORITY";
    case TN_RECFG_LOG:            return "LOG";
    case TN_RECFG_DATABASE_ORDER: return "DATABASE_ORDER";
    case TN_RECFG_SELECTORS:      return "SELECTORS";
    case TN_RECFG_STATS:          return "STATS";
    case TN_RECFG_SYSLOG:         return "SYSLOG";
    case TN_RECFG_S2EVENTS:       return "S2EVENTS";
    default:                      return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* CLOSE §B.8: CheckNetConfig vocabulary — the SAME keys the parser
 * accepts (single source). Value classes drive the command's checks. */

#include <string.h>

static const char *const g_cfg_keys[] = {
    "DATABASE_ORDER", "DEBUG", "DEVICE", "DIAG", "DHCP", "DNS", "DNS1",
    "DNS2", "DNS_PENDING", "DNS_PORT", "DNS_RETRIES", "FONT", "GATEWAY",
    "GW", "HOSTNAME", "IP", "IP_ADDR", "LOG", "LOGLEVEL", "MASK", "MTU",
    "NAMESERVER", "NETMASK", "PRIORITY", "S2EVENTS", "SELECTORS",
    "STATS", "SYSLOG", "TX_QUEUE", "UNIT", "USE_DHCP", "VERSION",
    NULL
};

int tn_config_key_known(const char *key)
{
    int i;
    if (key == NULL || *key == '\0') return 0;
    for (i = 0; g_cfg_keys[i] != NULL; i++) {
        if (tn_str_equal_nocase(key, g_cfg_keys[i])) return 1;
    }
    return 0;
}

int tn_config_value_class(const char *key)
{
    /* 0 string, 1 dotted-quad, 2 number, 3 boolean-ish */
    static const struct { const char *k; int cls; } cls_map[] = {
        { "IP", 1 }, { "IP_ADDR", 1 }, { "NETMASK", 1 }, { "MASK", 1 },
        { "GATEWAY", 1 }, { "GW", 1 }, { "DNS", 1 }, { "DNS1", 1 },
        { "DNS2", 1 }, { "NAMESERVER", 1 },
        { "UNIT", 2 }, { "MTU", 2 }, { "DNS_PORT", 2 }, { "SELECTORS", 2 },
        { "TX_QUEUE", 2 },
        { "PRIORITY", 2 }, { "DNS_PENDING", 2 }, { "DNS_RETRIES", 2 },
        { "LOGLEVEL", 2 },
        { "DHCP", 3 }, { "USE_DHCP", 3 }, { "STATS", 3 }, { "DIAG", 3 },
        { "DEBUG", 3 },
        { NULL, 0 }
    };
    int i;
    if (key == NULL) return 0;
    for (i = 0; cls_map[i].k != NULL; i++) {
        if (tn_str_equal_nocase(key, cls_map[i].k)) return cls_map[i].cls;
    }
    return 0;
}
