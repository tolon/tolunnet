/*
 * tolunnet — RFC3164 syslog forwarding implementation (Round 4 §D3, TNET-108).
 *
 * Runs entirely inside the daemon task (the only lwIP owner). Frames are
 * "<PRI>tolunnet: <line>" capped at 512 bytes, facility 1 (user), severity 6
 * (informational) → PRI 14, matching the classic BSD syslog UDP convention.
 */
#include "syslog.h"
#include "../common/config_text.h"

#define TN_SYSLOG_PORT     514
#define TN_SYSLOG_MAX_FRAME    512

static struct udp_pcb *s_pcb;
static ip_addr_t       s_dst;
static BOOL            s_armed;
static BOOL            s_in_sink;   /* reentrancy latch: we run inside tn_log */
static char            s_host[48];

static void tn_syslog_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    (void)arg;
    if (ipaddr != NULL && s_pcb != NULL) {
        ip_addr_copy(s_dst, *ipaddr);
        s_armed = TRUE;
        tn_logf(TN_LOG_BASIC, "tolunnet: syslog forwarding armed to %s:514\n", s_host);
    } else {
        tn_logf(TN_LOG_BASIC, "tolunnet: syslog host '%s' could not be resolved\n", s_host);
    }
}

void tn_syslog_shutdown(void)
{
    if (s_pcb != NULL) {
        udp_remove(s_pcb);
        s_pcb = NULL;
    }
    s_armed = FALSE;
    s_host[0] = '\0';
}

BOOL tn_syslog_apply(const char *host)
{
    err_t derr;

    if (host == NULL || host[0] == '\0') {
        if (s_armed || s_pcb != NULL) {
            tn_log(TN_LOG_BASIC, "tolunnet: syslog forwarding disabled\n");
        }
        tn_syslog_shutdown();
        return TRUE;
    }

    if (s_pcb == NULL) {
        s_pcb = udp_new();
        if (s_pcb == NULL) {
            return FALSE;
        }
    }

    tn_str_copy_clean(s_host, host, (int)sizeof(s_host));

    /* Dotted quad: arm at once */
    if (ip4addr_aton(s_host, ip_2_ip4(&s_dst))) {
        s_armed = TRUE;
        tn_logf(TN_LOG_BASIC, "tolunnet: syslog forwarding armed to %s:514\n", s_host);
        return TRUE;
    }

    /* Hostname: resolve (cached copy returns immediately, else DNS callback).
     * Stop forwarding to any previous target while the new one resolves. */
    s_armed = FALSE;
    derr = dns_gethostbyname(s_host, &s_dst, tn_syslog_dns_cb, NULL);
    if (derr == ERR_OK) {
        s_armed = TRUE;
        tn_logf(TN_LOG_BASIC, "tolunnet: syslog forwarding armed to %s:514\n", s_host);
        return TRUE;
    }
    if (derr == ERR_INPROGRESS) {
        s_armed = FALSE;
        tn_logf(TN_LOG_BASIC, "tolunnet: syslog host '%s' resolving...\n", s_host);
        return TRUE;
    }

    tn_logf(TN_LOG_BASIC, "tolunnet: syslog host '%s' rejected by resolver\n", s_host);
    return FALSE;
}

void tn_syslog_sink(const char *msg)
{
    static const char prefix[] = "<14>tolunnet: ";
    char frame[TN_SYSLOG_MAX_FRAME];
    int len;
    struct pbuf *p;
    const char *src;
    char *dst;

    if (!s_armed || s_pcb == NULL || msg == NULL || s_in_sink) return;

    /* RFC3164 frame: "<PRI>tag: message" without trailing newline. */
    dst = frame;
    for (src = prefix; *src && (dst - frame) < (TN_SYSLOG_MAX_FRAME - 1); ) {
        *dst++ = *src++;
    }
    while (*msg == '\n') msg++;                     /* leading blank lines */
    while (*msg && (dst - frame) < (TN_SYSLOG_MAX_FRAME - 1)) {
        *dst++ = *msg++;
    }
    if (dst > frame && dst[-1] == '\n') dst--;      /* strip trailing newline */
    len = (int)(dst - frame);
    if (len <= (int)(sizeof(prefix) - 1)) return;    /* empty message */

    s_in_sink = TRUE;

    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (p != NULL) {
        if (pbuf_take(p, frame, (u16_t)len) == ERR_OK) {
            udp_sendto(s_pcb, p, &s_dst, TN_SYSLOG_PORT);
        }
        pbuf_free(p);
    }
    /* Failures are silent: this runs inside tn_log and must not log. */

    s_in_sink = FALSE;
}
