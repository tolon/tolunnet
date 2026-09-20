/*
 * test_ipc_dispatch.c — Host unit tests for table-driven IPC dispatch.
 *
 * ROUND4b §C (Host-Testable Core).
 * Tests command validation, table lookup, fd checking, permission/ownership,
 * and command name string resolution under ASan / UBSan.
 */
#include "tn_test.h"
#include "mock_lwip.h"
#include "task/ipc_dispatch.h"
#include "task/slot_table.h"

/* Mock handler call tracker */
static TnIpcCmd s_last_handled_cmd = (TnIpcCmd)-1;
static TnSocketSlot *s_last_handled_slot = NULL;

#define DEFINE_MOCK_HANDLER(name, cmd_val) \
int name(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot) { \
    (void)d; (void)imsg; \
    s_last_handled_cmd = cmd_val; \
    s_last_handled_slot = slot; \
    return TN_IPC_REPLY_NOW; \
}

DEFINE_MOCK_HANDLER(tn_ipc_cmd_open, TN_IPC_CMD_OPEN)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_close, TN_IPC_CMD_CLOSE)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_socket, TN_IPC_CMD_SOCKET)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_bind, TN_IPC_CMD_BIND)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_listen, TN_IPC_CMD_LISTEN)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_accept, TN_IPC_CMD_ACCEPT)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_connect, TN_IPC_CMD_CONNECT)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_sendto, TN_IPC_CMD_SENDTO)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_send, TN_IPC_CMD_SEND)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_recvfrom, TN_IPC_CMD_RECVFROM)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_recv, TN_IPC_CMD_RECV)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_shutdown, TN_IPC_CMD_SHUTDOWN)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_setsockopt, TN_IPC_CMD_SETSOCKOPT)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_getsockopt, TN_IPC_CMD_GETSOCKOPT)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_getsockname, TN_IPC_CMD_GETSOCKNAME)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_getpeername, TN_IPC_CMD_GETPEERNAME)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_ioctl, TN_IPC_CMD_IOCTL)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_closesocket, TN_IPC_CMD_CLOSESOCKET)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_gethostbyname, TN_IPC_CMD_GETHOSTBYNAME)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_waitselect, TN_IPC_CMD_WAITSELECT)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_dup2, TN_IPC_CMD_DUP2)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_getstatus, TN_IPC_CMD_GETSTATUS)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_reconfig, TN_IPC_CMD_RECONFIG)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_enumsockets, TN_IPC_CMD_ENUMSOCKETS)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_sendmsg, TN_IPC_CMD_SENDMSG)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_recvmsg, TN_IPC_CMD_RECVMSG)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_releasesocket, TN_IPC_CMD_RELEASESOCKET)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_obtainsocket, TN_IPC_CMD_OBTAINSOCKET)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_select_arm, TN_IPC_CMD_SELECT_ARM)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_select_disarm, TN_IPC_CMD_SELECT_DISARM)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_getstats, TN_IPC_CMD_GETSTATS)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_routectl, TN_IPC_CMD_ROUTECTL)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_ifctl, TN_IPC_CMD_IFCTL)
DEFINE_MOCK_HANDLER(tn_ipc_cmd_stop, TN_IPC_CMD_STOP)

#include "../../src/task/ipc_dispatch.c"

TN_TEST(cmd_name_resolution)
{
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_OPEN), "OPEN");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_SOCKET), "SOCKET");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_BIND), "BIND");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_GETSTATUS), "GETSTATUS");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_OBTAINSOCKET), "OBTAINSOCKET");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_SELECT_ARM), "SELECT_ARM");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_SELECT_DISARM), "SELECT_DISARM");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_GETSTATS), "GETSTATS");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_ROUTECTL), "ROUTECTL");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_IFCTL), "IFCTL");
    TN_ASSERT_STREQ(tn_ipc_cmd_name(TN_IPC_CMD_STOP), "STOP");

    /* Out of bounds */
    TN_ASSERT_STREQ(tn_ipc_cmd_name((TnIpcCmd)-1), "UNKNOWN");
    TN_ASSERT_STREQ(tn_ipc_cmd_name((TnIpcCmd)99), "UNKNOWN");
}

TN_TEST(null_msg_handling)
{
    TnDaemon d;
    tn_slot_table_init(&d);
    TN_ASSERT_EQ(tn_handle_ipc(&d, NULL), FALSE);
}

TN_TEST(invalid_cmd_returns_enosys)
{
    TnDaemon d;
    TnIpcMsg msg;
    tn_slot_table_init(&d);

    memset(&msg, 0, sizeof(msg));
    msg.cmd = (TnIpcCmd)-1;
    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, ENOSYS);

    msg.cmd = (TnIpcCmd)100;
    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, ENOSYS);

    /* Unimplemented command (GETHOSTBYADDR handler is NULL) */
    msg.cmd = TN_IPC_CMD_GETHOSTBYADDR;
    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, ENOSYS);
}

TN_TEST(needs_base_validation)
{
    TnDaemon d;
    TnIpcMsg msg;
    tn_slot_table_init(&d);

    memset(&msg, 0, sizeof(msg));
    msg.cmd = TN_IPC_CMD_SOCKET;
    msg.socket_base = NULL;

    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);
}

TN_TEST(needs_fd_validation)
{
    TnDaemon d;
    TnSocketBase base;
    TnIpcMsg msg;

    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));
    { static LONG _fm[TN_DEFAULT_DTABLESIZE]; static ULONG _ev[TN_DEFAULT_DTABLESIZE]; static ULONG _em[TN_DEFAULT_DTABLESIZE];
      base.fd_map = _fm; base.events = _ev; base.event_masks = _em; base.dtablesize = TN_DEFAULT_DTABLESIZE;
      for (int i = 0; i < TN_DEFAULT_DTABLESIZE; i++) base.fd_map[i] = -1; }

    memset(&msg, 0, sizeof(msg));
    msg.cmd = TN_IPC_CMD_SEND;
    msg.socket_base = &base;
    msg.args[0] = 5; /* unmapped fd */

    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EBADF);

    /* Negative fd */
    msg.args[0] = -1;
    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EBADF);

    /* Fd out of range */
    msg.args[0] = 32;
    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EBADF);
}

TN_TEST(dispatch_routing_success)
{
    TnDaemon d;
    TnSocketBase base;
    TnIpcMsg msg;
    int slot_idx = -1;

    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));
    { static LONG _fm[TN_DEFAULT_DTABLESIZE]; static ULONG _ev[TN_DEFAULT_DTABLESIZE]; static ULONG _em[TN_DEFAULT_DTABLESIZE];
      base.fd_map = _fm; base.events = _ev; base.event_masks = _em; base.dtablesize = TN_DEFAULT_DTABLESIZE;
      for (int i = 0; i < TN_DEFAULT_DTABLESIZE; i++) base.fd_map[i] = -1; }

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, 2, 1, 6, &slot_idx);
    base.fd_map[2] = slot_idx;

    s_last_handled_cmd = (TnIpcCmd)-1;
    s_last_handled_slot = NULL;

    memset(&msg, 0, sizeof(msg));
    msg.cmd = TN_IPC_CMD_SEND;
    msg.socket_base = &base;
    msg.args[0] = 2;

    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(s_last_handled_cmd, TN_IPC_CMD_SEND);
    TN_ASSERT_EQ(s_last_handled_slot, slot);

    /* Test command that does not require fd (e.g. GETSTATUS) */
    s_last_handled_cmd = (TnIpcCmd)-1;
    msg.cmd = TN_IPC_CMD_GETSTATUS;
    msg.socket_base = NULL;
    TN_ASSERT_EQ(tn_handle_ipc(&d, &msg), TRUE);
    TN_ASSERT_EQ(s_last_handled_cmd, TN_IPC_CMD_GETSTATUS);

    tn_slot_free(&d, slot_idx);
}

int main(void)
{
    TN_TEST_RUN(cmd_name_resolution);
    TN_TEST_RUN(null_msg_handling);
    TN_TEST_RUN(invalid_cmd_returns_enosys);
    TN_TEST_RUN(needs_base_validation);
    TN_TEST_RUN(needs_fd_validation);
    TN_TEST_RUN(dispatch_routing_success);

    TN_TEST_PLAN();
    return tn_test_failures();
}
