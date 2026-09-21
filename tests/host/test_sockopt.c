/*
 * test_sockopt.c — Host unit tests for getsockopt optlen bounds and Roadshow semantics (SEC item 7).
 *
 * Verifies that:
 * 1. slot == NULL or optval == NULL returns EBADF.
 * 2. optlen == NULL returns EINVAL.
 * 3. *optlen < sizeof(int) returns EINVAL without modifying optval (Roadshow semantics).
 * 4. *optlen == sizeof(int) writes the option and updates *optlen.
 * 5. *optlen > sizeof(int) writes the option and clamps *optlen to sizeof(int).
 * 6. SO_ERROR preserves slot->last_error when *optlen < sizeof(int).
 * 7. SO_LINGER requires *optlen >= sizeof(struct linger).
 * 8. SO_RCVTIMEO / SO_SNDTIMEO requires *optlen >= sizeof(int).
 * 9. IPPROTO_TCP and IPPROTO_IP options enforce *optlen >= sizeof(int).
 */
#define DEVICES_TIMER_H 1
#include "tn_test.h"
#include "mock_lwip.h"
#include "task/slot_table.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>

#ifndef RAW_FLAGS_HDRINCL
#define RAW_FLAGS_HDRINCL 0x01
#endif
#ifndef raw_is_flag_set
#define raw_is_flag_set(pcb, flag) 0
#endif

#include "../../src/task/ipc_getsockopt.c"

TN_TEST(getsockopt_null_checks)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_TYPE;

    int val = 0;
    socklen_t len = sizeof(val);

    /* slot == NULL -> EBADF */
    msg.ptrs[0] = &val;
    msg.ptrs[1] = &len;
    tn_ipc_cmd_getsockopt(NULL, &msg, NULL);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EBADF);

    /* optval == NULL -> EBADF */
    msg.ptrs[0] = NULL;
    msg.ptrs[1] = &len;
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EBADF);

    /* optlen == NULL -> EINVAL */
    msg.ptrs[0] = &val;
    msg.ptrs[1] = NULL;
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
}

TN_TEST(getsockopt_sol_socket_int_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;
    slot.opt_reuseaddr = 1;
    slot.opt_keepalive = 1;
    slot.opt_broadcast = 1;
    slot.opt_oobinline = 1;
    slot.opt_sndbuf = 8192;
    slot.opt_rcvbuf = 16384;

    LONG opts[] = { SO_TYPE, SO_REUSEADDR, SO_KEEPALIVE, SO_BROADCAST,
                    SO_OOBINLINE, SO_SNDBUF, SO_RCVBUF };
    int num_opts = (int)(sizeof(opts) / sizeof(opts[0]));

    for (int i = 0; i < num_opts; i++) {
        TnIpcMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.args[1] = SOL_SOCKET;
        msg.args[2] = opts[i];

        int val = 0x12345678;
        socklen_t len;

        /* len = 0 < sizeof(int) -> EINVAL, optval untouched */
        len = 0;
        msg.ptrs[0] = &val;
        msg.ptrs[1] = &len;
        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, -1);
        TN_ASSERT_EQ(msg.err_no, EINVAL);
        TN_ASSERT_EQ(val, 0x12345678);
        TN_ASSERT_EQ(len, 0);

        /* len = 2 < sizeof(int) -> EINVAL, optval untouched */
        len = 2;
        val = 0x12345678;
        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, -1);
        TN_ASSERT_EQ(msg.err_no, EINVAL);
        TN_ASSERT_EQ(val, 0x12345678);
        TN_ASSERT_EQ(len, 2);

        /* len = 4 == sizeof(int) -> success, *optlen == sizeof(int) */
        len = sizeof(int);
        val = 0;
        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, 0);
        TN_ASSERT_EQ(msg.err_no, 0);
        TN_ASSERT_EQ(len, sizeof(int));

        /* len = 8 > sizeof(int) -> success, clamped to sizeof(int) */
        len = 8;
        val = 0;
        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, 0);
        TN_ASSERT_EQ(msg.err_no, 0);
        TN_ASSERT_EQ(len, sizeof(int));
    }
}

TN_TEST(getsockopt_so_error_preserves_error_on_einval)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;
    slot.last_error = 61; /* ECONNREFUSED */

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_ERROR;

    int val = 0;
    socklen_t len = 2; /* too small */
    msg.ptrs[0] = &val;
    msg.ptrs[1] = &len;

    /* Should fail with EINVAL and NOT clear slot.last_error */
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
    TN_ASSERT_EQ(slot.last_error, 61);

    /* Now provide adequate buffer */
    len = sizeof(int);
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(val, 61);
    TN_ASSERT_EQ(len, sizeof(int));
    TN_ASSERT_EQ(slot.last_error, 0); /* cleared after successful read */
}

TN_TEST(getsockopt_so_linger_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;
    slot.opt_linger.l_onoff = 1;
    slot.opt_linger.l_linger = 30;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_LINGER;

    struct linger ling;
    memset(&ling, 0, sizeof(ling));
    socklen_t len = sizeof(int); /* smaller than sizeof(struct linger) */
    msg.ptrs[0] = &ling;
    msg.ptrs[1] = &len;

    /* len < sizeof(struct linger) -> EINVAL */
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* len == sizeof(struct linger) -> success */
    len = sizeof(struct linger);
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(ling.l_onoff, 1);
    TN_ASSERT_EQ(ling.l_linger, 30);
    TN_ASSERT_EQ(len, sizeof(struct linger));
}

TN_TEST(getsockopt_timeo_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;
    slot.opt_rcvtimeo.tv_secs = 5;
    slot.opt_rcvtimeo.tv_micro = 500000;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_RCVTIMEO;

    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    socklen_t len = 2; /* too small (< sizeof(int)) */
    msg.ptrs[0] = &tv;
    msg.ptrs[1] = &len;

    /* len < sizeof(int) -> EINVAL */
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* len == sizeof(int) -> returns ms */
    int ms = 0;
    len = sizeof(int);
    msg.ptrs[0] = &ms;
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(ms, 5500);
    TN_ASSERT_EQ(len, sizeof(int));

    /* len == sizeof(struct timeval) -> returns struct timeval */
    len = sizeof(struct timeval);
    msg.ptrs[0] = &tv;
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ((int)tv.tv_secs, 5);
    TN_ASSERT_EQ((int)tv.tv_micro, 500000);
    TN_ASSERT_EQ(len, sizeof(struct timeval));
}

TN_TEST(getsockopt_ipproto_tcp_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;
    slot.opt_nodelay = 1;
    slot.opt_keepidle = 7200;
    slot.opt_keepintvl = 75;
    slot.opt_keepcnt = 9;

    LONG tcp_opts[] = { TCP_NODELAY, TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT };
    int count = (int)(sizeof(tcp_opts) / sizeof(tcp_opts[0]));

    for (int i = 0; i < count; i++) {
        TnIpcMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.args[1] = IPPROTO_TCP;
        msg.args[2] = tcp_opts[i];

        int val = 0;
        socklen_t len = 2; /* too small */
        msg.ptrs[0] = &val;
        msg.ptrs[1] = &len;

        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, -1);
        TN_ASSERT_EQ(msg.err_no, EINVAL);

        len = sizeof(int);
        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, 0);
        TN_ASSERT_EQ(msg.err_no, 0);
        TN_ASSERT_EQ(len, sizeof(int));
    }
}

TN_TEST(getsockopt_ipproto_ip_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;
    slot.opt_ttl = 64;
    slot.opt_tos = 0x10;
    slot.opt_multicast_ttl = 1;
    slot.opt_multicast_loop = 1;

    LONG ip_opts[] = { IP_TTL, IP_TOS, IP_MULTICAST_TTL, IP_MULTICAST_LOOP };
    int count = (int)(sizeof(ip_opts) / sizeof(ip_opts[0]));

    for (int i = 0; i < count; i++) {
        TnIpcMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.args[1] = IPPROTO_IP;
        msg.args[2] = ip_opts[i];

        int val = 0;
        socklen_t len = 3; /* too small */
        msg.ptrs[0] = &val;
        msg.ptrs[1] = &len;

        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, -1);
        TN_ASSERT_EQ(msg.err_no, EINVAL);

        len = sizeof(int);
        tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, 0);
        TN_ASSERT_EQ(msg.err_no, 0);
        TN_ASSERT_EQ(len, sizeof(int));
    }
}

int main(void)
{
    TN_TEST_RUN(getsockopt_null_checks);
    TN_TEST_RUN(getsockopt_sol_socket_int_bounds);
    TN_TEST_RUN(getsockopt_so_error_preserves_error_on_einval);
    TN_TEST_RUN(getsockopt_so_linger_bounds);
    TN_TEST_RUN(getsockopt_timeo_bounds);
    TN_TEST_RUN(getsockopt_ipproto_tcp_bounds);
    TN_TEST_RUN(getsockopt_ipproto_ip_bounds);
    TN_TEST_PLAN();
    return tn_test_failures();
}
