/*
 * test_stats.c — Host unit tests for TnStats telemetry and versioning (§F).
 *
 * ROUND4b §F (Stats Telemetry).
 * Verifies struct size handshake, version field initialization, and
 * daemon counter serialization under ASan/UBSan.
 */
#include "tn_test.h"
#include "mock_lwip.h"

#define TN_HOST_BUILD 1
#include "task/ipc_status.h"
#include "task/slot_table.h"

#define netif_is_link_up(nif) 1

#define TN_LOG_BASIC 1
int g_log_level = 0;
void tn_log(int tier, const char *msg) { (void)tier; (void)msg; }

/* Stubs for unused reconfig helpers in ipc_status.c */
BOOL tn_prefs_load(TnPrefs *p) { (void)p; return TRUE; }
void tn_apply_live_config(TnDaemon *d) { (void)d; }

/* Include implementation directly for unit test coverage */
#include "../../src/task/ipc_status.c"

TN_TEST(stats_struct_versioning_and_size)
{
    TnDaemon d;
    TnIpcMsg msg;
    TnStats stats;

    memset(&d, 0, sizeof(d));
    memset(&msg, 0, sizeof(msg));
    memset(&stats, 0xFF, sizeof(stats));

    /* Case 1: NULL ptr -> EINVAL */
    msg.cmd = TN_IPC_CMD_GETSTATS;
    msg.args[0] = (LONG)sizeof(TnStats);
    msg.ptrs[0] = NULL;
    TN_ASSERT_EQ(tn_ipc_cmd_getstats(&d, &msg, NULL), 0);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* Case 2: Buffer size too small -> EINVAL */
    msg.ptrs[0] = &stats;
    msg.args[0] = (LONG)sizeof(TnStats) - 1;
    TN_ASSERT_EQ(tn_ipc_cmd_getstats(&d, &msg, NULL), 0);
    TN_ASSERT_EQ(msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EINVAL);

    /* Case 3: Valid buffer -> success, version header check */
    msg.args[0] = (LONG)sizeof(TnStats);
    TN_ASSERT_EQ(tn_ipc_cmd_getstats(&d, &msg, NULL), 0);
    TN_ASSERT_EQ(msg.result, 0);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(stats.struct_size, sizeof(TnStats));
    TN_ASSERT_EQ(stats.version, 1);
}

TN_TEST(stats_daemon_counter_copy)
{
    TnDaemon d;
    TnIpcMsg msg;
    TnStats stats;

    memset(&d, 0, sizeof(d));
    memset(&msg, 0, sizeof(msg));

    /* Populate mock daemon telemetry */
    d.ipc_calls[TN_IPC_CMD_SOCKET] = 42;
    d.ipc_calls[TN_IPC_CMD_CONNECT] = 17;
    d.deferred_replies = 5;
    d.sigio_sent = 123;
    d.selector_wakeups = 88;
    d.mainloop_ticks = 1000;
    d.s2_rx_frames = 200;
    d.s2_rx_bytes = 15000;
    d.s2_rx_drops = 2;
    d.s2_tx_frames = 150;
    d.s2_tx_bytes = 9800;
    d.s2_tx_drops = 1;
    d.rx_high_water = 8;

    msg.cmd = TN_IPC_CMD_GETSTATS;
    msg.args[0] = (LONG)sizeof(TnStats);
    msg.ptrs[0] = &stats;

    TN_ASSERT_EQ(tn_ipc_cmd_getstats(&d, &msg, NULL), 0);
    TN_ASSERT_EQ(msg.result, 0);

    TN_ASSERT_EQ(stats.daemon.ipc_calls[TN_IPC_CMD_SOCKET], 42);
    TN_ASSERT_EQ(stats.daemon.ipc_calls[TN_IPC_CMD_CONNECT], 17);
    TN_ASSERT_EQ(stats.daemon.deferred_replies, 5);
    TN_ASSERT_EQ(stats.daemon.sigio_sent, 123);
    TN_ASSERT_EQ(stats.daemon.selector_wakeups, 88);
    TN_ASSERT_EQ(stats.daemon.mainloop_ticks, 1000);
    TN_ASSERT_EQ(stats.daemon.uptime_secs, 100);
    TN_ASSERT_EQ(stats.daemon.s2_rx_frames, 200);
    TN_ASSERT_EQ(stats.daemon.s2_rx_bytes, 15000);
    TN_ASSERT_EQ(stats.daemon.s2_rx_drops, 2);
    TN_ASSERT_EQ(stats.daemon.s2_tx_frames, 150);
    TN_ASSERT_EQ(stats.daemon.s2_tx_bytes, 9800);
    TN_ASSERT_EQ(stats.daemon.s2_tx_drops, 1);
    TN_ASSERT_EQ(stats.daemon.rx_high_water, 8);
}

int main(void)
{
    TN_TEST_RUN(stats_struct_versioning_and_size);
    TN_TEST_RUN(stats_daemon_counter_copy);
    TN_TEST_PLAN();
    return tn_test_failures();
}
