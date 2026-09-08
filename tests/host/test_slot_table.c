/*
 * test_slot_table.c — Host unit tests for socket slot table & queues.
 *
 * ROUND4b §C (Host-Testable Core).
 * Tests slot allocation, 64-slot exhaustion, reuse, refcounting, RX/accept queues,
 * and event signaling under ASan / UBSan.
 */
#include "tn_test.h"
#include "mock_lwip.h"
#include "task/slot_table.h"

TN_TEST(slot_table_init_and_empty)
{
    TnDaemon d;
    tn_slot_table_init(&d);

    TN_ASSERT_EQ(tn_slot_live_count(&d), 0);
    TN_ASSERT_EQ(d.next_park_id, 1);
    for (int i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        TN_ASSERT_EQ(d.sockets[i].in_use, 0);
    }
}

TN_TEST(slot_allocation_and_lookup)
{
    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;
    TnSocketSlot *slot;

    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));
    for (int i = 0; i < TN_MAX_FDS_PER_TASK; i++) base.fd_map[i] = -1;

    slot = tn_slot_alloc(&d, &base, NULL, 2 /* AF_INET */, 1 /* SOCK_STREAM */, 6 /* IPPROTO_TCP */, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    TN_ASSERT_EQ(slot_idx, 0);
    TN_ASSERT_EQ(slot->in_use, 1);
    TN_ASSERT_EQ(slot->domain, 2);
    TN_ASSERT_EQ(slot->type, 1);
    TN_ASSERT_EQ(slot->protocol, 6);
    TN_ASSERT_EQ(slot->ref_count, 1);
    TN_ASSERT_EQ(tn_slot_live_count(&d), 1);

    int fd = tn_fd_alloc(&base, slot_idx);
    TN_ASSERT_EQ(fd, 0);
    TN_ASSERT_EQ(base.fd_map[0], slot_idx);

    int out_idx = -1;
    TnSocketSlot *looked_up = tn_slot_lookup(&d, &base, 0, &out_idx);
    TN_ASSERT_EQ(looked_up, slot);
    TN_ASSERT_EQ(out_idx, slot_idx);

    /* Lookup invalid fd */
    TN_ASSERT_TRUE(tn_slot_lookup(&d, &base, -1, NULL) == NULL);
    TN_ASSERT_TRUE(tn_slot_lookup(&d, &base, 1, NULL) == NULL);
    TN_ASSERT_TRUE(tn_slot_lookup(&d, &base, 32, NULL) == NULL);

    tn_slot_free(&d, slot_idx);
    TN_ASSERT_EQ(tn_slot_live_count(&d), 0);
}

TN_TEST(slot_exhaustion_64_limit)
{
    TnDaemon d;
    TnSocketBase base;
    int indices[TN_MAX_GLOBAL_SOCKETS];

    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    /* Allocate all 64 slots */
    for (int i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        int idx = -1;
        TnSocketSlot *s = tn_slot_alloc(&d, &base, NULL, 2, 2, 17, &idx);
        TN_ASSERT_TRUE(s != NULL);
        TN_ASSERT_EQ(idx, i);
        indices[i] = idx;
    }
    TN_ASSERT_EQ(tn_slot_live_count(&d), 64);

    /* 65th allocation must fail */
    int overflow_idx = -1;
    TnSocketSlot *overflow = tn_slot_alloc(&d, &base, NULL, 2, 2, 17, &overflow_idx);
    TN_ASSERT_TRUE(overflow == NULL);
    TN_ASSERT_EQ(overflow_idx, -1);
    TN_ASSERT_EQ(tn_slot_live_count(&d), 64);

    /* Free one slot, verify it can be reused */
    tn_slot_free(&d, indices[10]);
    TN_ASSERT_EQ(tn_slot_live_count(&d), 63);

    int reuse_idx = -1;
    TnSocketSlot *reused = tn_slot_alloc(&d, &base, NULL, 2, 1, 6, &reuse_idx);
    TN_ASSERT_TRUE(reused != NULL);
    TN_ASSERT_EQ(reuse_idx, 10);
    TN_ASSERT_EQ(tn_slot_live_count(&d), 64);

    /* Clean up all */
    for (int i = 0; i < TN_MAX_GLOBAL_SOCKETS; i++) {
        tn_slot_free(&d, i);
    }
    TN_ASSERT_EQ(tn_slot_live_count(&d), 0);
}

TN_TEST(refcounting_semantics)
{
    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;

    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, 2, 1, 6, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    TN_ASSERT_EQ(slot->ref_count, 1);

    /* Simulate dup2 or extra ref */
    tn_slot_ref(slot);
    TN_ASSERT_EQ(slot->ref_count, 2);

    /* First unref decrements ref_count but does not free slot */
    tn_slot_unref(&d, slot_idx);
    TN_ASSERT_EQ(slot->in_use, 1);
    TN_ASSERT_EQ(slot->ref_count, 1);
    TN_ASSERT_EQ(tn_slot_live_count(&d), 1);

    /* Second unref drops ref_count to 0, which frees slot */
    tn_slot_unref(&d, slot_idx);
    TN_ASSERT_EQ(slot->in_use, 0);
    TN_ASSERT_EQ(tn_slot_live_count(&d), 0);
}

TN_TEST(rx_queue_operations)
{
    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;
    ip_addr_t src;
    src.addr = 0x01020304;

    mock_lwip_reset();
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, 2, 2, 17, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);

    /* Push packets */
    struct pbuf *p1 = mock_pbuf_alloc(100);
    struct pbuf *p2 = mock_pbuf_alloc(200);
    TN_ASSERT_EQ(tn_rx_queue_push(slot, p1, &src, 1234), 0);
    TN_ASSERT_EQ(tn_rx_queue_push(slot, p2, &src, 5678), 0);
    TN_ASSERT_EQ(slot->rx_count, 2);

    /* Pop first packet */
    TnRxPacket *pkt1 = tn_rx_queue_pop(slot);
    TN_ASSERT_TRUE(pkt1 != NULL);
    TN_ASSERT_EQ(pkt1->p, p1);
    TN_ASSERT_EQ(pkt1->src_port, 1234);
    TN_ASSERT_EQ(slot->rx_count, 1);
    pbuf_free(pkt1->p);
    free(pkt1);

    /* Drain remaining packet */
    tn_rx_queue_drain(slot);
    TN_ASSERT_EQ(slot->rx_count, 0);
    TN_ASSERT_TRUE(slot->rx_head == NULL);
    TN_ASSERT_TRUE(slot->rx_tail == NULL);

    /* Verify pbuf_free was called for both packets */
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_PBUF_FREE), 2);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(rx_queue_limit_32)
{
    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;
    ip_addr_t src;
    src.addr = 0;

    mock_lwip_reset();
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, 2, 2, 17, &slot_idx);

    /* Fill up to 32 packets */
    for (int i = 0; i < TN_MAX_RX_QUEUE_PER_SOCKET; i++) {
        struct pbuf *p = mock_pbuf_alloc(32);
        TN_ASSERT_EQ(tn_rx_queue_push(slot, p, &src, (u16_t)i), 0);
    }
    TN_ASSERT_EQ(slot->rx_count, 32);

    /* 33rd push must fail */
    struct pbuf *overflow = mock_pbuf_alloc(32);
    TN_ASSERT_EQ(tn_rx_queue_push(slot, overflow, &src, 999), -1);
    pbuf_free(overflow);

    /* Clean up via slot free */
    tn_slot_free(&d, slot_idx);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_PBUF_FREE), 33);
}

TN_TEST(accept_queue_operations)
{
    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;

    mock_lwip_reset();
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, 2, 1, 6, &slot_idx);
    struct tcp_pcb pcb1, pcb2;
    memset(&pcb1, 0, sizeof(pcb1));
    memset(&pcb2, 0, sizeof(pcb2));

    TN_ASSERT_EQ(tn_accept_queue_push(slot, &pcb1), 0);
    TN_ASSERT_EQ(tn_accept_queue_push(slot, &pcb2), 0);
    TN_ASSERT_EQ(slot->accept_count, 2);

    struct tcp_pcb *popped = tn_accept_queue_pop(slot);
    TN_ASSERT_EQ(popped, &pcb1);
    TN_ASSERT_EQ(slot->accept_count, 1);

    /* Drain queue aborts unaccepted pcbs */
    tn_accept_queue_drain(slot);
    TN_ASSERT_EQ(slot->accept_count, 0);
    TN_ASSERT_TRUE(slot->accept_head == NULL);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ABORT), 1);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(event_signaling)
{
    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;

    mock_lwip_reset();
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));
    base.sig_event = 0x80000000;
    for (int i = 0; i < TN_MAX_FDS_PER_TASK; i++) base.fd_map[i] = -1;

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, (struct Task *)0x12345678, 2, 1, 6, &slot_idx);
    base.fd_map[3] = slot_idx;

    /* Signal read event */
    tn_record_socket_event(&d, slot, 0x01 /* FD_READ */);
    TN_ASSERT_EQ(base.events[3] & 0x01, 0x01);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 1);

    const MockCall *c = mock_lwip_last_call();
    TN_ASSERT_TRUE(c != NULL);
    TN_ASSERT_EQ(c->type, MOCK_CALL_SIGNAL);
    TN_ASSERT_EQ(c->ptr1, (void *)0x12345678);
    TN_ASSERT_EQ(c->arg1, 0x80000000);

    tn_slot_free(&d, slot_idx);
}

/* TNET-108: SELECTORS= live grow -- existing entries preserved, new ones
 * zeroed, shrink requests are a no-op success. */
TN_TEST(selector_table_grow)
{
    TnDaemon d;
    TnSelector saved;
    TnSelector *old_ptr;

    tn_slot_table_init(&d);
    TN_ASSERT_TRUE(d.selectors != NULL);
    TN_ASSERT_EQ(d.max_selectors, (uint32_t)TN_MAX_SELECTORS);

    /* Occupy one selector entry */
    d.selectors[1].in_use = TRUE;
    d.selectors[1].sig_select = 0x80000000u;
    d.selectors[1].nfds = 8;
    d.selectors[1].read_mask = 0x000000FFu;
    d.selector_count = 1;
    saved = d.selectors[1];
    old_ptr = d.selectors;

    TN_ASSERT_TRUE(tn_selector_table_grow(&d, 64));
    TN_ASSERT_EQ(d.max_selectors, 64u);
    TN_ASSERT_TRUE(d.selectors != old_ptr);      /* reallocated */
    TN_ASSERT_EQ(d.selectors[1].in_use, saved.in_use);
    TN_ASSERT_EQ(d.selectors[1].sig_select, saved.sig_select);
    TN_ASSERT_EQ(d.selectors[1].nfds, saved.nfds);
    TN_ASSERT_EQ(d.selectors[1].read_mask, saved.read_mask);
    TN_ASSERT_EQ(d.selector_count, 1);
    for (int i = 0; i < 64; i++) {
        if (i == 1) continue;
        TN_ASSERT_EQ(d.selectors[i].in_use, 0);  /* new slots zeroed */
    }

    /* Grow past the cap clamps at 128 */
    TN_ASSERT_TRUE(tn_selector_table_grow(&d, 4096));
    TN_ASSERT_EQ(d.max_selectors, 128u);

    /* Shrink / same-size is a successful no-op */
    TN_ASSERT_TRUE(tn_selector_table_grow(&d, 16));
    TN_ASSERT_EQ(d.max_selectors, 128u);

    /* NULL table -> FALSE */
    {
        TnDaemon e;
        memset(&e, 0, sizeof(e));
        TN_ASSERT_FALSE(tn_selector_table_grow(&e, 32));
    }

    tn_selector_table_free(&d);
    TN_ASSERT_TRUE(d.selectors == NULL);
    TN_ASSERT_EQ(d.max_selectors, 0u);
}

int main(void)
{
    TN_TEST_RUN(selector_table_grow);
    TN_TEST_RUN(slot_table_init_and_empty);
    TN_TEST_RUN(slot_allocation_and_lookup);
    TN_TEST_RUN(slot_exhaustion_64_limit);
    TN_TEST_RUN(refcounting_semantics);
    TN_TEST_RUN(rx_queue_operations);
    TN_TEST_RUN(rx_queue_limit_32);
    TN_TEST_RUN(accept_queue_operations);
    TN_TEST_RUN(event_signaling);

    TN_TEST_PLAN();
    return tn_test_failures();
}
