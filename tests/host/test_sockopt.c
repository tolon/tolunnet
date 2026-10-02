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
#include "../../src/task/ipc_setsockopt.c"

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

TN_TEST(setsockopt_null_checks)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_REUSEADDR;
    msg.args[3] = sizeof(int);

    int val = 1;

    /* slot == NULL -> EBADF */
    msg.ptrs[0] = &val;
    tn_ipc_cmd_setsockopt(NULL, &msg, NULL);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EBADF);

    /* optval == NULL -> EBADF */
    msg.ptrs[0] = NULL;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EBADF);
}

TN_TEST(setsockopt_sol_socket_int_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;

    LONG opts[] = { SO_REUSEADDR, SO_KEEPALIVE, SO_BROADCAST,
                    SO_OOBINLINE, SO_SNDBUF, SO_RCVBUF };
    int num_opts = (int)(sizeof(opts) / sizeof(opts[0]));

    for (int i = 0; i < num_opts; i++) {
        TnIpcMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.args[1] = SOL_SOCKET;
        msg.args[2] = opts[i];

        int val = 1;

        /* optlen < sizeof(int) -> EINVAL */
        msg.ptrs[0] = &val;
        msg.args[3] = sizeof(int) - 1;
        tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, -1);
        TN_ASSERT_EQ(msg.err_no, EINVAL);

        /* optlen >= sizeof(int) -> 0 */
        msg.args[3] = sizeof(int);
        tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
        TN_ASSERT_EQ(msg.result, 0);
        TN_ASSERT_EQ(msg.err_no, 0);
    }
}

TN_TEST(setsockopt_so_linger_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_LINGER;

    struct linger l;
    l.l_onoff = 1;
    l.l_linger = 10;
    msg.ptrs[0] = &l;

    /* optlen < sizeof(struct linger) -> EINVAL */
    msg.args[3] = sizeof(struct linger) - 1;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* optlen == sizeof(struct linger) -> 0, verifies slot updated */
    msg.args[3] = sizeof(struct linger);
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(slot.opt_linger.l_onoff, 1);
    TN_ASSERT_EQ(slot.opt_linger.l_linger, 10);
}

TN_TEST(setsockopt_timeo_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_RCVTIMEO;

    int ms = 2500;
    msg.ptrs[0] = &ms;

    /* optlen < sizeof(int) -> EINVAL */
    msg.args[3] = sizeof(int) - 1;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* optlen == sizeof(int) (integer ms form) -> 0 */
    msg.args[3] = sizeof(int);
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(slot.opt_rcvtimeo.tv_secs, 2u);
    TN_ASSERT_EQ(slot.opt_rcvtimeo.tv_micro, 500000u);

    /* optlen == sizeof(struct timeval) -> 0 */
    struct timeval tv;
    tv.tv_secs = 5;
    tv.tv_micro = 12345;
    msg.ptrs[0] = &tv;
    msg.args[3] = sizeof(tv);
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(slot.opt_rcvtimeo.tv_secs, 5u);
    TN_ASSERT_EQ(slot.opt_rcvtimeo.tv_micro, 12345u);
}

TN_TEST(setsockopt_ipproto_tcp_ip_bounds)
{
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;

    TnIpcMsg msg;
    int val = 1;

    /* TCP_NODELAY with optlen < sizeof(int) -> EINVAL */
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = IPPROTO_TCP;
    msg.args[2] = TCP_NODELAY;
    msg.args[3] = sizeof(int) - 1;
    msg.ptrs[0] = &val;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* IP_TTL with optlen < sizeof(int) -> EINVAL */
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = IPPROTO_IP;
    msg.args[2] = IP_TTL;
    msg.args[3] = sizeof(int) - 1;
    msg.ptrs[0] = &val;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* IP_MULTICAST_TTL with optlen < 1 -> EINVAL */
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = IPPROTO_IP;
    msg.args[2] = IP_MULTICAST_TTL;
    msg.args[3] = 0;
    msg.ptrs[0] = &val;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* 1-byte optlen works for IP_MULTICAST_TTL */
    unsigned char mttl = 42;
    msg.args[3] = 1;
    msg.ptrs[0] = &mttl;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(slot.opt_multicast_ttl, 42);
}

TN_TEST(setsockopt_unaligned_buffer)
{
    /* TNET-139 / Item 8: Verify that unaligned client buffer never causes
     * address misalignment fault under ASan/UBSan or 68000. */
    TnSocketSlot slot;
    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;

    char raw_buf[32];
    void *odd_ptr = &raw_buf[1]; /* Guaranteed unaligned */
    int test_val = 0x12345678;
    memcpy(odd_ptr, &test_val, sizeof(int));

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_SNDBUF;
    msg.args[3] = sizeof(int);
    msg.ptrs[0] = odd_ptr;

    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(slot.opt_sndbuf, test_val);

    /* Unaligned SO_LINGER */
    void *odd_linger_ptr = &raw_buf[3];
    struct linger l;
    l.l_onoff = 1;
    l.l_linger = 300;
    memcpy(odd_linger_ptr, &l, sizeof(struct linger));

    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_LINGER;
    msg.args[3] = sizeof(struct linger);
    msg.ptrs[0] = odd_linger_ptr;

    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(slot.opt_linger.l_onoff, 1);
    TN_ASSERT_EQ(slot.opt_linger.l_linger, 300);
}

/* bugtrack 4.9: getsockopt must not do typed stores/loads through odd
 * client optval/optlen pointers (68000 address error; UBSan here). */
TN_TEST(getsockopt_unaligned_buffer)
{
    TnSocketSlot slot;
    TnIpcMsg msg;
    char raw_buf[32];
    char raw_len[16];
    void *odd_val = &raw_buf[1];
    void *odd_len = &raw_len[1];
    socklen_t len = sizeof(int);
    int got = 0;
    struct timeval tv;

    memset(&slot, 0, sizeof(slot));
    slot.type = SOCK_STREAM;
    slot.opt_sndbuf = 0x1234;
    slot.opt_rcvtimeo.tv_secs = 7;
    slot.opt_rcvtimeo.tv_micro = 5000;

    memcpy(odd_len, &len, sizeof(len));
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = SOL_SOCKET;
    msg.args[2] = SO_SNDBUF;
    msg.ptrs[0] = odd_val;
    msg.ptrs[1] = odd_len;
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    memcpy(&got, odd_val, sizeof(int));
    TN_ASSERT_EQ(got, 0x1234);
    memcpy(&len, odd_len, sizeof(len));
    TN_ASSERT_EQ(len, sizeof(int));

    /* timeval form */
    len = sizeof(struct timeval);
    memcpy(odd_len, &len, sizeof(len));
    msg.args[2] = SO_RCVTIMEO;
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    memcpy(&tv, odd_val, sizeof(tv));
    TN_ASSERT_EQ(tv.tv_secs, 7u);
    TN_ASSERT_EQ(tv.tv_micro, 5000u);

    /* SO_LINGER */
    slot.opt_linger.l_onoff = 1;
    slot.opt_linger.l_linger = 9;
    len = sizeof(struct linger);
    memcpy(odd_len, &len, sizeof(len));
    msg.args[2] = SO_LINGER;
    tn_ipc_cmd_getsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    {
        struct linger l;
        memcpy(&l, odd_val, sizeof(l));
        TN_ASSERT_EQ(l.l_onoff, 1);
        TN_ASSERT_EQ(l.l_linger, 9);
    }
}

/* bugtrack 4.10: TCP_MAXSEG may only lower the mss; keepalive timers > 0
 * and overflow-safe in milliseconds. */
TN_TEST(setsockopt_tcp_maxseg_keepalive_validation)
{
    TnSocketSlot slot;
    struct tcp_pcb pcb;
    TnIpcMsg msg;
    int val;

    memset(&slot, 0, sizeof(slot));
    memset(&pcb, 0, sizeof(pcb));
    slot.type = SOCK_STREAM;
    slot.tcp_state = TN_TCP_STATE_ESTABLISHED;
    slot.tcp_pcb = &pcb;
    pcb.mss = 1000;
    pcb.keep_idle = 7200000UL;

    memset(&msg, 0, sizeof(msg));
    msg.args[1] = IPPROTO_TCP;
    msg.args[2] = TCP_MAXSEG;
    msg.args[3] = sizeof(int);
    msg.ptrs[0] = &val;

    val = 0;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
    val = -1;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
    val = 1460; /* above the negotiated 1000 */
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
    TN_ASSERT_EQ(pcb.mss, 1000);
    TN_ASSERT_EQ(slot.opt_mss, 0);
    val = 0x10000 + 536; /* would truncate to 536 as u16 */
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
    TN_ASSERT_EQ(pcb.mss, 1000);
    val = 536;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(pcb.mss, 536);
    TN_ASSERT_EQ(slot.opt_mss, 536);

    /* unconnected pcb: lwIP's INITIAL_MSS placeholder is not the bound */
    slot.tcp_state = TN_TCP_STATE_CLOSED;
    slot.opt_mss = 0;
    pcb.mss = 536;
    val = 1400;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(pcb.mss, 1400);
    pcb.mss = 536;

    /* LISTENING socket: bound is opt_mss / TCP_MSS, pcb untouched */
    slot.tcp_state = TN_TCP_STATE_LISTENING;
    slot.opt_mss = 0;
    val = TCP_MSS + 1;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
    val = 1200;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(slot.opt_mss, 1200);
    TN_ASSERT_EQ(pcb.mss, 536);
    slot.tcp_state = TN_TCP_STATE_ESTABLISHED;

    /* keepalive knobs */
    {
        static const int names[3] = { TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_KEEPCNT };
        int k;
        for (k = 0; k < 3; k++) {
            msg.args[2] = names[k];
            val = 0;
            tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
            TN_ASSERT_EQ(msg.result, -1);
            TN_ASSERT_EQ(msg.err_no, EINVAL);
            val = -5;
            tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
            TN_ASSERT_EQ(msg.err_no, EINVAL);
        }
    }
    TN_ASSERT_EQ(pcb.keep_idle, 7200000UL);

    msg.args[2] = TCP_KEEPIDLE;
    val = 4294968; /* * 1000 overflows u32 */
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
    TN_ASSERT_EQ(pcb.keep_idle, 7200000UL);
    val = 4294967; /* largest that fits */
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(pcb.keep_idle, 4294967000UL);

    msg.args[2] = TCP_KEEPCNT;
    val = 5;
    tn_ipc_cmd_setsockopt(NULL, &msg, &slot);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(pcb.keep_cnt, 5u);
}

/* bugtrack 4.10: IP_ADD_MEMBERSHIP is recorded per slot and left on free */
TN_TEST(setsockopt_mcast_membership_tracking)
{
    static TnDaemon d;
    TnSocketSlot *slot;
    TnIpcMsg msg;
    struct ip_mreq mreq;
    int i, used;

    memset(&d, 0, sizeof(d));
    slot = &d.sockets[5];
    slot->in_use = TRUE;
    slot->type = SOCK_DGRAM;

    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = 0xE00000FBUL;
    memset(&msg, 0, sizeof(msg));
    msg.args[1] = IPPROTO_IP;
    msg.args[2] = IP_ADD_MEMBERSHIP;
    msg.args[3] = sizeof(mreq);
    msg.ptrs[0] = &mreq;
    tn_ipc_cmd_setsockopt(&d, &msg, slot);
    TN_ASSERT_EQ(msg.result, 0);

    used = 0;
    for (i = 0; i < TN_MCAST_JOINS_MAX; i++) {
        if (d.mcast_joins[i].in_use) {
            used++;
            TN_ASSERT_EQ(d.mcast_joins[i].slot, 5);
            TN_ASSERT_EQ_U(d.mcast_joins[i].grp, mreq.imr_multiaddr.s_addr);
        }
    }
    TN_ASSERT_EQ(used, 1);

    /* explicit drop clears the record */
    msg.args[2] = IP_DROP_MEMBERSHIP;
    tn_ipc_cmd_setsockopt(&d, &msg, slot);
    TN_ASSERT_EQ(msg.result, 0);
    for (i = 0; i < TN_MCAST_JOINS_MAX; i++) TN_ASSERT_FALSE(d.mcast_joins[i].in_use);

    /* table full -> ENOBUFS, then leave-all frees the slot's records */
    msg.args[2] = IP_ADD_MEMBERSHIP;
    for (i = 0; i < TN_MCAST_JOINS_MAX; i++) {
        mreq.imr_multiaddr.s_addr = 0xE0000100UL + (uint32_t)i;
        tn_ipc_cmd_setsockopt(&d, &msg, slot);
        TN_ASSERT_EQ(msg.result, 0);
    }
    mreq.imr_multiaddr.s_addr = 0xE0000200UL;
    tn_ipc_cmd_setsockopt(&d, &msg, slot);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, ENOBUFS);

    tn_mcast_leave_all(&d, 5);
    for (i = 0; i < TN_MCAST_JOINS_MAX; i++) TN_ASSERT_FALSE(d.mcast_joins[i].in_use);
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
    TN_TEST_RUN(setsockopt_null_checks);
    TN_TEST_RUN(setsockopt_sol_socket_int_bounds);
    TN_TEST_RUN(setsockopt_so_linger_bounds);
    TN_TEST_RUN(setsockopt_timeo_bounds);
    TN_TEST_RUN(setsockopt_ipproto_tcp_ip_bounds);
    TN_TEST_RUN(setsockopt_unaligned_buffer);
    TN_TEST_RUN(getsockopt_unaligned_buffer);
    TN_TEST_RUN(setsockopt_tcp_maxseg_keepalive_validation);
    TN_TEST_RUN(setsockopt_mcast_membership_tracking);
    TN_TEST_PLAN();
    return tn_test_failures();
}

