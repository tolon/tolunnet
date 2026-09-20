/*
 * test_dns_pending.c — TNET-150: deferred gethostbyname lifecycle.
 *
 * The daemon parks the client's TnIpcMsg with lwIP on ERR_INPROGRESS.
 * Scenarios proven here (ASan watches every write):
 *  1. DEFER registers a pending record.
 *  2. CLOSE cancels it: exactly one ECONNABORTED reply, record gone.
 *  3. A late dns_found_cb after cancel touches NOTHING — the test frees
 *     the message first, exactly like tn_lib_close does after the CLOSE
 *     reply.
 *  4. A late dns_found_cb for a never-registered (foreign/freed) message
 *     is ignored and counted in dns_late_replies.
 *  5. Normal completion fills the hostent on the recorded base.
 *  6. Same-name second waiter and a full table both reply EAGAIN now
 *     (lwIP keeps one callback per entry; a stacked waiter would hang).
 */
#include "tn_test.h"
#include "task/task_ctx.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* ---- lwIP / log shims, declared BEFORE the unit under test ------------ */

#ifndef TN_LOG_BASIC
#define TN_LOG_BASIC 1
#endif

void tn_logf(int tier, const char *fmt, ...);

typedef void (*dns_found_callback)(const char *name, const ip_addr_t *ipaddr,
                                   void *callback_arg);

err_t dns_gethostbyname(const char *hostname, ip_addr_t *addr,
                        dns_found_callback found, void *callback_arg);
unsigned char ip4addr_aton(const char *cp, ip4_addr_t *addr);

/* real handler + callback under test */
#include "../../src/task/ipc_netdb.c"

/* ---- shim implementations (mock_lwip.c provides ReplyMsg etc.) --------- */

static err_t s_dns_ret = 0;
static dns_found_callback s_dns_cb = NULL;
static void *s_dns_cb_arg = NULL;

void tn_logf(int tier, const char *fmt, ...)
{
    (void)tier;
    (void)fmt;
}

err_t dns_gethostbyname(const char *hostname, ip_addr_t *addr,
                        dns_found_callback found, void *callback_arg)
{
    (void)hostname;
    if (s_dns_ret == 0) {
        addr->addr = 0x0A000202UL; /* 10.0.2.2, network order */
        return 0; /* ERR_OK */
    }
    s_dns_cb = found;
    s_dns_cb_arg = callback_arg;
    return s_dns_ret;
}

unsigned char ip4addr_aton(const char *cp, ip4_addr_t *addr)
{
    /* only digits/dots parse; test names are letters -> not an IP */
    const char *p = cp;
    if (*p == '\0') return 0;
    while (*p) {
        if ((*p < '0' || *p > '9') && *p != '.') return 0;
        p++;
    }
    addr->addr = 0x7F000001UL;
    return 1;
}

/* ---- helpers ------------------------------------------------------------- */

static void reset_daemon(void)
{
    memset(&g_daemon, 0, sizeof(g_daemon));
    mock_lwip_reset();
    s_dns_ret = 0;
    s_dns_cb = NULL;
    s_dns_cb_arg = NULL;
}

static int reply_count(void)
{
    return mock_lwip_call_count(MOCK_CALL_REPLY_MSG);
}

TN_TEST(dns_defer_registers_and_cancel_replies)
{
    TnSocketBase base;
    TnIpcMsg *imsg = (TnIpcMsg *)calloc(1, sizeof(TnIpcMsg));
    int rc;

    reset_daemon();
    memset(&base, 0, sizeof(base));
    imsg->socket_base = &base;
    imsg->ptrs[0] = (APTR)"test.tolunnet.lan";
    s_dns_ret = ERR_INPROGRESS;

    rc = tn_ipc_cmd_gethostbyname(&g_daemon, imsg, NULL);
    TN_ASSERT_EQ(rc, 1);              /* TN_IPC_DEFER */
    TN_ASSERT_EQ((int)g_daemon.dns_pending_count, 1);
    TN_ASSERT_TRUE(g_daemon.dns_pending[0].in_use);
    TN_ASSERT_TRUE(g_daemon.dns_pending[0].imsg == imsg);
    TN_ASSERT_TRUE(g_daemon.dns_pending[0].base == &base);
    TN_ASSERT_STREQ(g_daemon.dns_pending[0].name, "test.tolunnet.lan");
    TN_ASSERT_EQ(reply_count(), 0);   /* deferred: no reply yet */

    /* CLOSE cancels: exactly one reply, ECONNABORTED */
    tn_dns_cancel_for_base(&g_daemon, &base);
    TN_ASSERT_EQ(reply_count(), 1);
    TN_ASSERT_EQ(imsg->result, 0);
    TN_ASSERT_EQ(imsg->err_no, ECONNABORTED);
    TN_ASSERT_EQ((int)g_daemon.dns_pending_count, 0);
    TN_ASSERT_TRUE(!g_daemon.dns_pending[0].in_use);

    /* the client now frees the message (tn_lib_close, after the CLOSE
     * reply) — then the daemon's late callback fires: it must not touch
     * the freed block. The uintptr_t hop keeps gcc's static use-after-free
     * check quiet: the property under test is the RUNTIME one (ASan). */
    {
        uintptr_t freed_val = (uintptr_t)imsg;
        free(imsg);
        tn_dns_found_cb("test.tolunnet.lan", NULL, (void *)freed_val);
    }
    TN_ASSERT_EQ((int)g_daemon.dns_late_replies, 1);
    TN_ASSERT_EQ(reply_count(), 1); /* no additional reply */
}

TN_TEST(dns_late_callback_foreign_message_ignored)
{
    TnIpcMsg *ghost = (TnIpcMsg *)calloc(1, sizeof(TnIpcMsg));
    ip_addr_t ip;

    reset_daemon();
    ip.addr = 0x0A000202UL;

    /* never registered; freed immediately — ASan flags any write */
    {
        uintptr_t freed_val = (uintptr_t)ghost;
        free(ghost);
        tn_dns_found_cb("x.example", &ip, (void *)freed_val);
    }
    TN_ASSERT_EQ((int)g_daemon.dns_late_replies, 1);
    TN_ASSERT_EQ(reply_count(), 0);
}

TN_TEST(dns_defer_completion_fills_hostent)
{
    TnSocketBase base;
    TnIpcMsg *imsg = (TnIpcMsg *)calloc(1, sizeof(TnIpcMsg));
    ip_addr_t ip;
    int rc;

    reset_daemon();
    memset(&base, 0, sizeof(base));
    imsg->socket_base = &base;
    imsg->ptrs[0] = (APTR)"test.tolunnet.lan";
    s_dns_ret = ERR_INPROGRESS;

    rc = tn_ipc_cmd_gethostbyname(&g_daemon, imsg, NULL);
    TN_ASSERT_EQ(rc, 1);
    TN_ASSERT_TRUE(s_dns_cb != NULL);

    ip.addr = 0x0A000202UL;
    s_dns_cb("test.tolunnet.lan", &ip, s_dns_cb_arg);

    TN_ASSERT_EQ(reply_count(), 1);
    TN_ASSERT_EQ(imsg->err_no, 0);
    /* host LONG is 32-bit: the hostent pointer travels truncated, compare
     * the truncated representations (exact on the 32-bit target) */
    TN_ASSERT_EQ((long)(uint32_t)imsg->result,
                 (long)(uint32_t)(uintptr_t)&base.hostent_data);
    TN_ASSERT_EQ((int)base.hostent_data.h_length, 4);
    TN_ASSERT_EQ((int)base.hostent_addr, 0x0A000202);
    TN_ASSERT_STREQ(base.hostent_name, "test.tolunnet.lan");
    TN_ASSERT_EQ((int)g_daemon.dns_pending_count, 0);

    free(imsg);
}

TN_TEST(dns_pending_table_full_and_same_name_eagain)
{
    TnSocketBase base;
    TnIpcMsg *imsg;
    TnIpcMsg *reg[TN_DNS_PENDING_MAX];
    static char names[TN_DNS_PENDING_MAX][24];
    int i, rc;

    reset_daemon();
    memset(&base, 0, sizeof(base));
    s_dns_ret = ERR_INPROGRESS;

    for (i = 0; i < TN_DNS_PENDING_MAX; i++) {
        snprintf(names[i], sizeof(names[i]), "h%d.tolunnet.lan", i);
        imsg = (TnIpcMsg *)calloc(1, sizeof(TnIpcMsg));
        reg[i] = imsg;
        imsg->socket_base = &base;
        imsg->ptrs[0] = (APTR)names[i];
        rc = tn_ipc_cmd_gethostbyname(&g_daemon, imsg, NULL);
        TN_ASSERT_EQ(rc, 1);
        TN_ASSERT_EQ(imsg->err_no, 0); /* deferred, not EAGAIN */
    }
    TN_ASSERT_EQ((int)g_daemon.dns_pending_count, TN_DNS_PENDING_MAX);

    /* same-name second waiter: immediate EAGAIN, not a stacked deferral */
    imsg = (TnIpcMsg *)calloc(1, sizeof(TnIpcMsg));
    imsg->socket_base = &base;
    imsg->ptrs[0] = (APTR)"h0.tolunnet.lan";
    rc = tn_ipc_cmd_gethostbyname(&g_daemon, imsg, NULL);
    TN_ASSERT_EQ(rc, 0);
    TN_ASSERT_EQ(imsg->result, 0);
    TN_ASSERT_EQ(imsg->err_no, EAGAIN);
    free(imsg);

    /* full-table different name: EAGAIN as well */
    imsg = (TnIpcMsg *)calloc(1, sizeof(TnIpcMsg));
    imsg->socket_base = &base;
    imsg->ptrs[0] = (APTR)"fresh.tolunnet.lan";
    rc = tn_ipc_cmd_gethostbyname(&g_daemon, imsg, NULL);
    TN_ASSERT_EQ(rc, 0);
    TN_ASSERT_EQ(imsg->err_no, EAGAIN);
    free(imsg);

    /* registrations reply ECONNABORTED exactly TN_DNS_PENDING_MAX times on
     * cancel-all; the client-side (test) frees them afterwards, mirroring
     * tn_lib_close ordering */
    TN_ASSERT_EQ(reply_count(), 0);
    tn_dns_cancel_for_base(&g_daemon, &base);
    TN_ASSERT_EQ(reply_count(), TN_DNS_PENDING_MAX);
    for (i = 0; i < TN_DNS_PENDING_MAX; i++) {
        TN_ASSERT_EQ(reg[i]->err_no, ECONNABORTED);
        free(reg[i]);
    }
}

int main(void)
{
    TN_TEST_RUN(dns_defer_registers_and_cancel_replies);
    TN_TEST_RUN(dns_late_callback_foreign_message_ignored);
    TN_TEST_RUN(dns_defer_completion_fills_hostent);
    TN_TEST_RUN(dns_pending_table_full_and_same_name_eagain);
    TN_TEST_PLAN();
    return tn_test_failures();
}
