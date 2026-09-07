/*
 * test_queues.c — Host unit tests for TolunNet queue operations.
 *
 * ROUND4b rev 2 Residual R3.
 * Tests:
 *   1. RX partial reads across chained pbufs (pbuf_copy_partial boundary traversal).
 *   2. RX queue limit (TN_MAX_RX_QUEUE_PER_SOCKET = 32) and FIFO ordering.
 *   3. Accept queue drain aborting unaccepted TCP PCBs.
 *   4. Event queue bitwise coalescing per Roadshow bsdsocket.doc specification.
 */

#include "tn_test.h"
#include "mock_lwip.h"
#include "task/slot_table.h"
#include "sys/socket.h"

TN_TEST(rx_pbuf_chain_partial_reads)
{
    mock_lwip_reset();

    struct pbuf *p1 = mock_pbuf_alloc(16);
    struct pbuf *p2 = mock_pbuf_alloc(24);
    struct pbuf *p3 = mock_pbuf_alloc(10);

    TN_ASSERT_TRUE(p1 != NULL && p2 != NULL && p3 != NULL);

    p1->next = p2;
    p2->next = p3;
    p3->next = NULL;

    p1->tot_len = 16 + 24 + 10; /* 50 */
    p2->tot_len = 24 + 10;      /* 34 */
    p3->tot_len = 10;           /* 10 */

    memcpy(p1->payload, "0123456789ABCDEF", 16);
    memcpy(p2->payload, "ghijklmnopqrstuvwxyz0123", 24);
    memcpy(p3->payload, "!@#$%^&*()", 10);

    char buf[64];
    memset(buf, 0, sizeof(buf));

    /* 1. Read within p1: offset 0, len 10 */
    u16_t copied = pbuf_copy_partial(p1, buf, 10, 0);
    TN_ASSERT_EQ(copied, 10);
    buf[10] = 0;
    TN_ASSERT_STREQ(buf, "0123456789");

    /* 2. Read across boundary of p1 into p2: offset 10, len 12 (6 from p1, 6 from p2) */
    memset(buf, 0, sizeof(buf));
    copied = pbuf_copy_partial(p1, buf, 12, 10);
    TN_ASSERT_EQ(copied, 12);
    buf[12] = 0;
    TN_ASSERT_STREQ(buf, "ABCDEFghijkl");

    /* 3. Read spanning across p2 into p3: offset 35, len 10 (5 from p2, 5 from p3) */
    memset(buf, 0, sizeof(buf));
    copied = pbuf_copy_partial(p1, buf, 10, 35);
    TN_ASSERT_EQ(copied, 10);
    buf[10] = 0;
    TN_ASSERT_STREQ(buf, "z0123!@#$%");

    /* 4. Read entire chain from offset 0, len 50 */
    memset(buf, 0, sizeof(buf));
    copied = pbuf_copy_partial(p1, buf, 50, 0);
    TN_ASSERT_EQ(copied, 50);
    buf[50] = 0;
    TN_ASSERT_STREQ(buf, "0123456789ABCDEFghijklmnopqrstuvwxyz0123!@#$%^&*()");

    /* 5. Read past total length */
    copied = pbuf_copy_partial(p1, buf, 10, 50);
    TN_ASSERT_EQ(copied, 0);

    /* 6. Read with length exceeding remaining bytes: offset 45, len 20 */
    memset(buf, 0, sizeof(buf));
    copied = pbuf_copy_partial(p1, buf, 20, 45);
    TN_ASSERT_EQ(copied, 5);
    buf[5] = 0;
    TN_ASSERT_STREQ(buf, "^&*()");

    /* Test slot queue integration: push chain to a socket slot */
    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);

    ip_addr_t src_ip;
    src_ip.addr = 0x0100007f;
    TN_ASSERT_EQ(tn_rx_queue_push(slot, p1, &src_ip, 8080), 0);
    TN_ASSERT_EQ(slot->rx_count, 1);

    TnRxPacket *pkt = slot->rx_head;
    TN_ASSERT_TRUE(pkt != NULL);
    TN_ASSERT_EQ(pkt->offset, 0);

    /* Read chunk 1: 10 bytes */
    memset(buf, 0, sizeof(buf));
    copied = pbuf_copy_partial(pkt->p, buf, 10, pkt->offset);
    TN_ASSERT_EQ(copied, 10);
    pkt->offset += copied;
    TN_ASSERT_EQ(pkt->offset, 10);
    TN_ASSERT_TRUE(pkt->offset < pkt->p->tot_len);

    /* Read chunk 2: 25 bytes across p1 -> p2 */
    memset(buf, 0, sizeof(buf));
    copied = pbuf_copy_partial(pkt->p, buf, 25, pkt->offset);
    TN_ASSERT_EQ(copied, 25);
    pkt->offset += copied;
    TN_ASSERT_EQ(pkt->offset, 35);
    TN_ASSERT_TRUE(pkt->offset < pkt->p->tot_len);

    /* Read chunk 3: final 15 bytes across p2 -> p3 */
    memset(buf, 0, sizeof(buf));
    copied = pbuf_copy_partial(pkt->p, buf, 15, pkt->offset);
    TN_ASSERT_EQ(copied, 15);
    pkt->offset += copied;
    TN_ASSERT_EQ(pkt->offset, 50);
    TN_ASSERT_EQ(pkt->offset, pkt->p->tot_len);

    /* Once fully consumed, packet is popped and pbuf chain is freed */
    TnRxPacket *consumed = tn_rx_queue_pop(slot);
    TN_ASSERT_EQ(consumed, pkt);
    TN_ASSERT_EQ(slot->rx_count, 0);
    pbuf_free(consumed->p);
    mock_freevec(consumed);

    /* Verify pbuf_free freed all 3 chained buffers */
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_PBUF_FREE), 3);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(rx_queue_limit_and_ordering)
{
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);

    ip_addr_t src_ip;
    src_ip.addr = 0x0100007f;

    /* Push up to limit: TN_MAX_RX_QUEUE_PER_SOCKET (32) */
    for (int i = 0; i < TN_MAX_RX_QUEUE_PER_SOCKET; i++) {
        struct pbuf *p = mock_pbuf_alloc(16);
        *((int *)p->payload) = i;
        int rc = tn_rx_queue_push(slot, p, &src_ip, (u16_t)(1000 + i));
        TN_ASSERT_EQ(rc, 0);
        TN_ASSERT_EQ(slot->rx_count, (ULONG)(i + 1));
    }

    /* 33rd push must be rejected */
    struct pbuf *overflow = mock_pbuf_alloc(16);
    TN_ASSERT_EQ(tn_rx_queue_push(slot, overflow, &src_ip, 9999), -1);
    TN_ASSERT_EQ(slot->rx_count, TN_MAX_RX_QUEUE_PER_SOCKET);
    pbuf_free(overflow);

    /* Verify FIFO order of first 5 packets */
    for (int i = 0; i < 5; i++) {
        TnRxPacket *pkt = tn_rx_queue_pop(slot);
        TN_ASSERT_TRUE(pkt != NULL);
        TN_ASSERT_EQ(*((int *)pkt->p->payload), i);
        TN_ASSERT_EQ(pkt->src_port, (u16_t)(1000 + i));
        pbuf_free(pkt->p);
        mock_freevec(pkt);
    }
    TN_ASSERT_EQ(slot->rx_count, (ULONG)(TN_MAX_RX_QUEUE_PER_SOCKET - 5));

    /* Push a new packet now that there is room */
    struct pbuf *p_new = mock_pbuf_alloc(16);
    *((int *)p_new->payload) = 999;
    TN_ASSERT_EQ(tn_rx_queue_push(slot, p_new, &src_ip, 5000), 0);

    /* Drain all remaining packets cleanly */
    tn_rx_queue_drain(slot);
    TN_ASSERT_EQ(slot->rx_count, 0);
    TN_ASSERT_TRUE(slot->rx_head == NULL);
    TN_ASSERT_TRUE(slot->rx_tail == NULL);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(accept_queue_abort_on_drain)
{
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);

    struct tcp_pcb pcbs[5];
    memset(pcbs, 0, sizeof(pcbs));

    /* Push 5 incoming connections to listening queue */
    for (int i = 0; i < 5; i++) {
        TN_ASSERT_EQ(tn_accept_queue_push(slot, &pcbs[i]), 0);
    }
    TN_ASSERT_EQ(slot->accept_count, 5);

    /* Drain accept queue — per RFC & bsdsocket spec, unaccepted connections must be aborted */
    tn_accept_queue_drain(slot);

    TN_ASSERT_EQ(slot->accept_count, 0);
    TN_ASSERT_TRUE(slot->accept_head == NULL);
    TN_ASSERT_TRUE(slot->accept_tail == NULL);

    /* All 5 must have had tcp_abort called */
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ABORT), 5);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(event_queue_coalescing_per_bsdsocket_doc)
{
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    int slot_idx = -1;
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    base.sig_event = 0x80000000;
    for (int i = 0; i < TN_MAX_FDS_PER_TASK; i++) {
        base.fd_map[i] = -1;
        base.events[i] = 0;
    }

    struct Task *mock_task = (struct Task *)0xDEADC0DE;
    TnSocketSlot *slot = tn_slot_alloc(&d, &base, mock_task, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);

    int fd = 3;
    base.fd_map[fd] = slot_idx;

    /* Initial FD_READ event */
    tn_record_socket_event(&d, slot, 0x01);
    TN_ASSERT_EQ(base.events[fd], 0x01);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 1);

    /* Duplicate FD_READ event -> coalesced into same bit */
    tn_record_socket_event(&d, slot, 0x01);
    TN_ASSERT_EQ(base.events[fd], 0x01);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 2);

    /* FD_WRITE event -> bitwise OR accumulation */
    tn_record_socket_event(&d, slot, 0x02);
    TN_ASSERT_EQ(base.events[fd], (0x01 | 0x02));
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 3);

    /* FD_CLOSE event */
    tn_record_socket_event(&d, slot, 0x20);
    TN_ASSERT_EQ(base.events[fd], (0x01 | 0x02 | 0x20));
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 4);

    /* Simulate GetSocketEvents: consumer reads mask and resets events[fd] to 0 */
    ULONG read_events = base.events[fd];
    base.events[fd] = 0;
    TN_ASSERT_EQ(read_events, (0x01 | 0x02 | 0x20));
    TN_ASSERT_EQ(base.events[fd], 0);

    /* Subsequent event records cleanly to cleared mask */
    tn_record_socket_event(&d, slot, 0x04);
    TN_ASSERT_EQ(base.events[fd], 0x04);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_SIGNAL), 5);

    tn_slot_free(&d, slot_idx);
}

int main(void)
{
    TN_TEST_RUN(rx_pbuf_chain_partial_reads);
    TN_TEST_RUN(rx_queue_limit_and_ordering);
    TN_TEST_RUN(accept_queue_abort_on_drain);
    TN_TEST_RUN(event_queue_coalescing_per_bsdsocket_doc);

    TN_TEST_PLAN();
    return tn_test_failures();
}
