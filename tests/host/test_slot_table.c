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
    static LONG test_fd_map[TN_DEFAULT_DTABLESIZE];
    static ULONG test_events[TN_DEFAULT_DTABLESIZE];
    static ULONG test_event_masks[TN_DEFAULT_DTABLESIZE];
    int _i;
    memset(&base, 0, sizeof(base));
    base.fd_map = test_fd_map;
    base.events = test_events;
    base.event_masks = test_event_masks;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) base.fd_map[_i] = -1;
    int slot_idx = -1;
    TnSocketSlot *slot;

    tn_slot_table_init(&d);

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
    static LONG test_fd_map[TN_DEFAULT_DTABLESIZE];
    static ULONG test_events[TN_DEFAULT_DTABLESIZE];
    static ULONG test_event_masks[TN_DEFAULT_DTABLESIZE];
    int _i;
    memset(&base, 0, sizeof(base));
    base.fd_map = test_fd_map;
    base.events = test_events;
    base.event_masks = test_event_masks;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) base.fd_map[_i] = -1;
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
    static LONG test_fd_map[TN_DEFAULT_DTABLESIZE];
    static ULONG test_events[TN_DEFAULT_DTABLESIZE];
    static ULONG test_event_masks[TN_DEFAULT_DTABLESIZE];
    int _i;
    memset(&base, 0, sizeof(base));
    base.fd_map = test_fd_map;
    base.events = test_events;
    base.event_masks = test_event_masks;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) base.fd_map[_i] = -1;
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
    static LONG test_fd_map[TN_DEFAULT_DTABLESIZE];
    static ULONG test_events[TN_DEFAULT_DTABLESIZE];
    static ULONG test_event_masks[TN_DEFAULT_DTABLESIZE];
    int _i;
    memset(&base, 0, sizeof(base));
    base.fd_map = test_fd_map;
    base.events = test_events;
    base.event_masks = test_event_masks;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) base.fd_map[_i] = -1;
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
    static LONG test_fd_map[TN_DEFAULT_DTABLESIZE];
    static ULONG test_events[TN_DEFAULT_DTABLESIZE];
    static ULONG test_event_masks[TN_DEFAULT_DTABLESIZE];
    int _i;
    memset(&base, 0, sizeof(base));
    base.fd_map = test_fd_map;
    base.events = test_events;
    base.event_masks = test_event_masks;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) base.fd_map[_i] = -1;
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
    static LONG test_fd_map[TN_DEFAULT_DTABLESIZE];
    static ULONG test_events[TN_DEFAULT_DTABLESIZE];
    static ULONG test_event_masks[TN_DEFAULT_DTABLESIZE];
    int _i;
    memset(&base, 0, sizeof(base));
    base.fd_map = test_fd_map;
    base.events = test_events;
    base.event_masks = test_event_masks;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) base.fd_map[_i] = -1;
    int slot_idx = -1;

    mock_lwip_reset();
    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);
    d.open_bases[0] = &base; /* 3.7: events only reach registered bases */
    { static LONG _fm[TN_DEFAULT_DTABLESIZE]; static ULONG _ev[TN_DEFAULT_DTABLESIZE]; static ULONG _em[TN_DEFAULT_DTABLESIZE];
      base.fd_map = _fm; base.events = _ev; base.event_masks = _em; base.dtablesize = TN_DEFAULT_DTABLESIZE;
      { int i; for (i = 0; i < TN_DEFAULT_DTABLESIZE; i++) base.fd_map[i] = -1; } }
    base.sig_event = 0x80000000;

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, (struct Task *)0x12345678, 2, 1, 6, &slot_idx);
    base.fd_map[3] = slot_idx;
    base.event_masks[3] = 0xFFFFFFFF;

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

TN_TEST(recv_parking_and_rcvtimeo_lifecycle)
{
    TnDaemon d;
    TnSocketBase base;
    static LONG test_fd_map[TN_DEFAULT_DTABLESIZE];
    int _i;
    int slot_idx = -1;
    TnSocketSlot *slot;
    TnIpcMsg msg1, msg2;

    memset(&base, 0, sizeof(base));
    base.fd_map = test_fd_map;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) base.fd_map[_i] = -1;

    memset(&msg1, 0, sizeof(msg1));
    msg1.cmd = TN_IPC_CMD_RECV;
    msg1.socket_base = (struct Library *)&base;

    memset(&msg2, 0, sizeof(msg2));
    msg2.cmd = TN_IPC_CMD_RECV;
    msg2.socket_base = (struct Library *)&base;

    tn_slot_table_init(&d);
    d.mainloop_ticks = 100;

    slot = tn_slot_alloc(&d, &base, NULL, 2, 1, 6, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    TN_ASSERT_TRUE(slot->pending_recv_msg == NULL);
    TN_ASSERT_EQ(slot->recv_deadline_tick, 0u);

    /* 1. Park without timeout */
    int act = tn_slot_park_recv(&d, slot, &msg1);
    TN_ASSERT_EQ(act, 1);
    TN_ASSERT_EQ(slot->pending_recv_msg, &msg1);
    TN_ASSERT_EQ(slot->recv_deadline_tick, 0u);

    /* 2. Re-parking the same message returns 1 (deferred) */
    TN_ASSERT_EQ(tn_slot_park_recv(&d, slot, &msg1), 1);

    /* 3. Parking another message while busy returns 0 with EALREADY */
    act = tn_slot_park_recv(&d, slot, &msg2);
    TN_ASSERT_EQ(act, 0);
    TN_ASSERT_EQ(msg2.result, -1);
    TN_ASSERT_EQ(msg2.err_no, EALREADY);

    /* 4. Reset and configure SO_RCVTIMEO (500 ms = 5 ticks) */
    slot->pending_recv_msg = NULL;
    slot->opt_rcvtimeo.tv_secs = 0;
    slot->opt_rcvtimeo.tv_micro = 500000;

    act = tn_slot_park_recv(&d, slot, &msg1);
    TN_ASSERT_EQ(act, 1);
    TN_ASSERT_EQ(slot->recv_deadline_tick, 105u); /* 100 + 5 ticks */

    /* Advance ticks but not past deadline */
    d.mainloop_ticks = 104;
    tn_slot_check_recv_timeouts(&d);
    TN_ASSERT_EQ(slot->pending_recv_msg, &msg1);

    /* Advance ticks to deadline -> expires with EWOULDBLOCK */
    d.mainloop_ticks = 105;
    tn_slot_check_recv_timeouts(&d);
    TN_ASSERT_TRUE(slot->pending_recv_msg == NULL);
    TN_ASSERT_EQ(slot->recv_deadline_tick, 0u);
    TN_ASSERT_EQ(msg1.result, -1);
    TN_ASSERT_EQ(msg1.err_no, EWOULDBLOCK);

    /* 5. Cancel on base close */
    msg1.result = 0;
    msg1.err_no = 0;
    slot->opt_rcvtimeo.tv_micro = 0;
    tn_slot_park_recv(&d, slot, &msg1);
    TN_ASSERT_EQ(slot->pending_recv_msg, &msg1);

    tn_recv_cancel_for_base(&d, &base);
    TN_ASSERT_TRUE(slot->pending_recv_msg == NULL);
    TN_ASSERT_EQ(msg1.result, -1);
    TN_ASSERT_EQ(msg1.err_no, ECONNABORTED);

    /* 6. Cancel on slot_free */
    msg1.result = 0;
    msg1.err_no = 0;
    tn_slot_park_recv(&d, slot, &msg1);
    TN_ASSERT_EQ(slot->pending_recv_msg, &msg1);

    tn_slot_free(&d, slot_idx);
    TN_ASSERT_TRUE(slot->pending_recv_msg == NULL);
    TN_ASSERT_EQ(msg1.result, -1);
    TN_ASSERT_EQ(msg1.err_no, EBADF);
}

TN_TEST(dead_base_clear_on_close)
{
    TnDaemon d;
    TnSocketBase base1, base2;
    int idx0 = -1, idx1 = -1, idx2 = -1;

    tn_slot_table_init(&d);
    memset(&base1, 0, sizeof(base1));
    memset(&base2, 0, sizeof(base2));

    TnSocketSlot *s0 = tn_slot_alloc(&d, &base1, (struct Task *)0x1111, AF_INET, SOCK_STREAM, IPPROTO_TCP, &idx0);
    TnSocketSlot *s1 = tn_slot_alloc(&d, &base1, (struct Task *)0x1111, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &idx1);
    TnSocketSlot *s2 = tn_slot_alloc(&d, &base2, (struct Task *)0x2222, AF_INET, SOCK_STREAM, IPPROTO_TCP, &idx2);

    TN_ASSERT_TRUE(s0 != NULL && s1 != NULL && s2 != NULL);
    TN_ASSERT_EQ(s0->owner_base, &base1);
    TN_ASSERT_EQ(s0->owner_task, (struct Task *)0x1111);
    TN_ASSERT_EQ(s1->owner_base, &base1);
    TN_ASSERT_EQ(s1->owner_task, (struct Task *)0x1111);
    TN_ASSERT_EQ(s2->owner_base, &base2);
    TN_ASSERT_EQ(s2->owner_task, (struct Task *)0x2222);

    /* Item 6: closing base1 must clear owner pointers across all its slots */
    tn_slot_clear_owner_base(&d, &base1);

    TN_ASSERT_TRUE(s0->owner_base == NULL);
    TN_ASSERT_TRUE(s0->owner_task == NULL);
    TN_ASSERT_TRUE(s1->owner_base == NULL);
    TN_ASSERT_TRUE(s1->owner_task == NULL);
    TN_ASSERT_EQ(s2->owner_base, &base2);
    TN_ASSERT_EQ(s2->owner_task, (struct Task *)0x2222);

    /* Subsequent event recording must be completely safe with NULL base */
    int sig_count_before = mock_lwip_call_count(MOCK_CALL_SIGNAL);
    tn_record_socket_event(&d, s0, 0x01);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), sig_count_before);

    tn_slot_free(&d, idx0);
    tn_slot_free(&d, idx1);
    tn_slot_free(&d, idx2);
}

/* z.ai step 2: 17e0e53 regression - slot reuse must reset shut_wr/shut_rd
 * (bsdsocktest #36: #35's shutdown() leaked into #36's slot -> EPIPE). */
TN_TEST(slot_reuse_resets_shutdown_flags)
{
    TnDaemon d;
    TnSocketBase base;
    int idx = -1, idx2 = -1;
    TnSocketSlot *s;

    memset(&base, 0, sizeof(base));
    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);

    s = tn_slot_alloc(&d, &base, (struct Task *)0x1111, AF_INET, SOCK_STREAM, 0, &idx);
    TN_ASSERT_TRUE(s != NULL);
    s->shut_wr = TRUE;
    s->shut_rd = TRUE;
    tn_slot_free(&d, idx);

    s = tn_slot_alloc(&d, &base, (struct Task *)0x1111, AF_INET, SOCK_STREAM, 0, &idx2);
    TN_ASSERT_TRUE(s != NULL);
    TN_ASSERT_TRUE(s->shut_wr == FALSE);
    TN_ASSERT_TRUE(s->shut_rd == FALSE);
    tn_slot_free(&d, idx2);
}

/* 4.1: a LISTEN pcb is a tcp_pcb_listen - only arg/accept may be reset
 * before tcp_close (tcp_recv/sent/err/poll write past its end). */
TN_TEST(slot_free_listen_pcb_touches_only_arg_accept)
{
    TnDaemon d;
    TnSocketBase base;
    struct tcp_pcb lpcb;
    int idx = -1;
    TnSocketSlot *s;

    memset(&base, 0, sizeof(base));
    memset(&d, 0, sizeof(d));
    memset(&lpcb, 0, sizeof(lpcb));
    tn_slot_table_init(&d);
    s = tn_slot_alloc(&d, &base, (struct Task *)0x1111, AF_INET, SOCK_STREAM, 0, &idx);
    TN_ASSERT_TRUE(s != NULL);
    s->tcp_pcb = &lpcb;
    s->tcp_state = TN_TCP_STATE_LISTENING;

    mock_lwip_reset();
    tn_slot_free(&d, idx);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_RECV), 0);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ERR), 0);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_SENT), 0);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_POLL), 0);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ARG), 1);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ACCEPT), 1);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_CLOSE), 1);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ABORT), 0);
}

/* 3.9: tcp_close ERR_MEM leaves the pcb alive and unowned - abort it. */
TN_TEST(slot_free_close_failure_aborts_pcb)
{
    TnDaemon d;
    TnSocketBase base;
    struct tcp_pcb pcb;
    int idx = -1;
    TnSocketSlot *s;

    memset(&base, 0, sizeof(base));
    memset(&d, 0, sizeof(d));
    memset(&pcb, 0, sizeof(pcb));
    tn_slot_table_init(&d);
    s = tn_slot_alloc(&d, &base, (struct Task *)0x1111, AF_INET, SOCK_STREAM, 0, &idx);
    TN_ASSERT_TRUE(s != NULL);
    s->tcp_pcb = &pcb;
    s->tcp_state = TN_TCP_STATE_ESTABLISHED;

    mock_lwip_reset();
    mock_set_tcp_close_err(ERR_MEM);
    tn_slot_free(&d, idx);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_CLOSE), 1);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ABORT), 1);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_SENT), 1); /* detached */
    TN_ASSERT_EQ(pcb.callback_arg, NULL);
    TN_ASSERT_EQ(pcb.err_cb, NULL);
    mock_lwip_reset();
}

/* 3.7: an owner base that left the open-bases registry (client died
 * without CloseLibrary) must not be dereferenced or signalled. */
TN_TEST(record_event_skips_unregistered_base)
{
    TnDaemon d;
    TnSocketBase base;
    static LONG fm[TN_DEFAULT_DTABLESIZE];
    static ULONG ev[TN_DEFAULT_DTABLESIZE], em[TN_DEFAULT_DTABLESIZE];
    int idx = -1, i;
    TnSocketSlot *s;

    memset(&base, 0, sizeof(base));
    base.fd_map = fm; base.events = ev; base.event_masks = em;
    base.dtablesize = TN_DEFAULT_DTABLESIZE;
    for (i = 0; i < TN_DEFAULT_DTABLESIZE; i++) { fm[i] = -1; ev[i] = 0; em[i] = 0; }
    base.sig_event = 0x100;
    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);   /* open_bases left empty */
    s = tn_slot_alloc(&d, &base, (struct Task *)0x4444, AF_INET, SOCK_STREAM, 0, &idx);
    TN_ASSERT_TRUE(s != NULL);
    fm[5] = idx;
    em[5] = 0xFFFFFFFF;

    mock_lwip_reset();
    tn_record_socket_event(&d, s, 0x08);
    TN_ASSERT_EQ(ev[5], 0);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 0);
    TN_ASSERT_TRUE(s->owner_base == NULL);
    TN_ASSERT_TRUE(s->owner_task == NULL);

    /* registered: delivered */
    s->owner_base = &base;
    s->owner_task = (struct Task *)0x4444;
    d.open_bases[3] = &base;
    tn_record_socket_event(&d, s, 0x08);
    TN_ASSERT_EQ(ev[5], 0x08);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 1);
    tn_slot_free(&d, idx);
}

/* 4.2: a parked blocking send is answered on every exit path - SO_SNDTIMEO
 * expiry, CANCEL, base close/reap and slot free - with the partial count
 * once bytes were queued (BSD sosend). */
TN_TEST(parked_send_exit_paths)
{
    TnDaemon d;
    TnSocketBase base;
    TnIpcMsg m;
    int idx = -1;
    TnSocketSlot *s;

    memset(&base, 0, sizeof(base));
    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);
    s = tn_slot_alloc(&d, &base, (struct Task *)0x1111, AF_INET, SOCK_STREAM, 0, &idx);
    TN_ASSERT_TRUE(s != NULL);
    TN_ASSERT_TRUE(s->pending_send_msg == NULL);
    TN_ASSERT_EQ(s->listen_backlog, TN_ACCEPT_QUEUE_MAX);

    /* SO_SNDTIMEO deadline, nothing sent yet -> EWOULDBLOCK */
    memset(&m, 0, sizeof(m));
    m.socket_base = (APTR)&base;
    s->pending_send_msg = &m;
    d.mainloop_ticks = 100;
    s->send_deadline_tick = 105;
    mock_lwip_reset();
    tn_slot_check_recv_timeouts(&d);
    TN_ASSERT_TRUE(s->pending_send_msg == &m);   /* not yet */
    d.mainloop_ticks = 105;
    tn_slot_check_recv_timeouts(&d);
    TN_ASSERT_TRUE(s->pending_send_msg == NULL);
    TN_ASSERT_EQ(m.result, -1);
    TN_ASSERT_EQ(m.err_no, EWOULDBLOCK);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_REPLY_MSG), 1);

    /* timeout after partial progress -> byte count */
    memset(&m, 0, sizeof(m));
    m.socket_base = (APTR)&base;
    s->pending_send_msg = &m;
    s->send_done = 1234;
    s->send_deadline_tick = 106;
    d.mainloop_ticks = 200;
    tn_slot_check_recv_timeouts(&d);
    TN_ASSERT_EQ(m.result, 1234);
    TN_ASSERT_EQ(m.err_no, 0);
    TN_ASSERT_EQ(s->send_done, 0);

    /* CANCEL (client break) -> EINTR */
    memset(&m, 0, sizeof(m));
    m.socket_base = (APTR)&base;
    s->pending_send_msg = &m;
    s->send_deadline_tick = 0;
    TN_ASSERT_TRUE(tn_slot_cancel_parked_send(&d, &m));
    TN_ASSERT_EQ(m.err_no, EINTR);
    TN_ASSERT_FALSE(tn_slot_cancel_parked_send(&d, &m));

    /* CLOSE of the owning base -> ECONNABORTED + reply; reap -> silent */
    memset(&m, 0, sizeof(m));
    m.socket_base = (APTR)&base;
    s->pending_send_msg = &m;
    mock_lwip_reset();
    tn_recv_cancel_for_base(&d, &base);
    TN_ASSERT_TRUE(s->pending_send_msg == NULL);
    TN_ASSERT_EQ(m.err_no, ECONNABORTED);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_REPLY_MSG), 1);
    s->pending_send_msg = &m;
    mock_lwip_reset();
    tn_recv_cancel_for_base2(&d, &base, 0);
    TN_ASSERT_TRUE(s->pending_send_msg == NULL);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_REPLY_MSG), 0);

    /* slot free (CloseSocket / daemon shutdown) -> EBADF */
    memset(&m, 0, sizeof(m));
    s->pending_send_msg = &m;
    mock_lwip_reset();
    tn_slot_free(&d, idx);
    TN_ASSERT_EQ(m.result, -1);
    TN_ASSERT_EQ(m.err_no, EBADF);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_REPLY_MSG), 1);
}

/* bugtrack 4.10: tn_slot_free runs the daemon's slot-free hook (IGMP
 * leave) exactly once with the freed index; init clears the hook. */
static int s_hook_calls, s_hook_idx;
static void test_slot_free_hook(TnDaemon *d, int slot_idx)
{
    (void)d;
    s_hook_calls++;
    s_hook_idx = slot_idx;
}

TN_TEST(slot_free_runs_hook)
{
    TnDaemon d;
    TnSocketBase base;
    int idx = -1;

    memset(&base, 0, sizeof(base));
    memset(&d, 0xA5, sizeof(d));          /* garbage, as a stack local */
    d.selectors = NULL;
    tn_slot_table_init(&d);
    TN_ASSERT_TRUE(d.slot_free_hook == NULL);
    d.slot_free_hook = test_slot_free_hook;
    TN_ASSERT_TRUE(tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM, 0, &idx) != NULL);
    s_hook_calls = 0;
    tn_slot_free(&d, idx);
    TN_ASSERT_EQ(s_hook_calls, 1);
    TN_ASSERT_EQ(s_hook_idx, idx);
    tn_slot_free(&d, idx);                /* already free: no second call */
    TN_ASSERT_EQ(s_hook_calls, 1);
}

int main(void)
{
    TN_TEST_RUN(selector_table_grow);
    TN_TEST_RUN(slot_table_init_and_empty);
    TN_TEST_RUN(slot_allocation_and_lookup);
    TN_TEST_RUN(slot_reuse_resets_shutdown_flags);
    TN_TEST_RUN(slot_exhaustion_64_limit);
    TN_TEST_RUN(refcounting_semantics);
    TN_TEST_RUN(rx_queue_operations);
    TN_TEST_RUN(rx_queue_limit_32);
    TN_TEST_RUN(accept_queue_operations);
    TN_TEST_RUN(event_signaling);
    TN_TEST_RUN(recv_parking_and_rcvtimeo_lifecycle);
    TN_TEST_RUN(dead_base_clear_on_close);
    TN_TEST_RUN(slot_free_listen_pcb_touches_only_arg_accept);
    TN_TEST_RUN(slot_free_close_failure_aborts_pcb);
    TN_TEST_RUN(record_event_skips_unregistered_base);
    TN_TEST_RUN(parked_send_exit_paths);
    TN_TEST_RUN(slot_free_runs_hook);

    TN_TEST_PLAN();
    return tn_test_failures();
}

