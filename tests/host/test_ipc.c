/*
 * test_ipc.c — Host unit tests for tn_ipc_oneshot client helper (TNET-082).
 */

#include "tn_test.h"
#include "../../src/common/ipc_client.h"

#if !defined(__AMIGA__) && !defined(__amigaos__) && !defined(TN_AMIGA_BUILD)

extern void tn_ipc_set_mock_handler(int (*handler)(TnIpcCmd, const LONG *, int, TnIpcMsg *));

static int mock_handler_echo(TnIpcCmd cmd, const LONG *args, int arg_count, TnIpcMsg *out_msg)
{
    if (out_msg != NULL) {
        out_msg->cmd = cmd;
        out_msg->result = 0;
        out_msg->err_no = 0;
        for (int i = 0; i < arg_count && i < 6; i++) {
            out_msg->args[i] = args[i] * 2;
        }
    }
    return 0;
}

static int mock_handler_error(TnIpcCmd cmd, const LONG *args, int arg_count, TnIpcMsg *out_msg)
{
    (void)cmd; (void)args; (void)arg_count;
    if (out_msg != NULL) {
        out_msg->result = -1;
        out_msg->err_no = 2; /* ENOENT */
    }
    return -1;
}

TN_TEST(unhandled_ipc_returns_error)
{
    tn_ipc_set_mock_handler(NULL);
    TN_ASSERT_EQ(tn_ipc_oneshot(TN_IPC_CMD_GETSTATUS, NULL, 0, NULL), -1);
}

TN_TEST(mock_echo_args_marshaling)
{
    LONG in_args[4] = { 10, 20, 30, 40 };
    TnIpcMsg out;
    int rc;

    tn_ipc_set_mock_handler(mock_handler_echo);
    rc = tn_ipc_oneshot(TN_IPC_CMD_GETSTATUS, in_args, 4, &out);

    TN_ASSERT_EQ(rc, 0);
    TN_ASSERT_EQ(out.cmd, TN_IPC_CMD_GETSTATUS);
    TN_ASSERT_EQ(out.args[0], 20);
    TN_ASSERT_EQ(out.args[1], 40);
    TN_ASSERT_EQ(out.args[2], 60);
    TN_ASSERT_EQ(out.args[3], 80);
}

TN_TEST(mock_error_propagation)
{
    TnIpcMsg out;
    int rc;

    tn_ipc_set_mock_handler(mock_handler_error);
    rc = tn_ipc_oneshot(TN_IPC_CMD_RECONFIG, NULL, 0, &out);

    TN_ASSERT_EQ(rc, -1);
    TN_ASSERT_EQ(out.result, -1);
    TN_ASSERT_EQ(out.err_no, 2);
}

int main(void)
{
    TN_TEST_RUN(unhandled_ipc_returns_error);
    TN_TEST_RUN(mock_echo_args_marshaling);
    TN_TEST_RUN(mock_error_propagation);
    TN_TEST_PLAN();
    return tn_test_failures();
}

#else

int main(void) { return 0; }

#endif
