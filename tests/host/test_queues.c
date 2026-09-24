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

/* Host-test TnSocketBase setup: allocates static arrays for the pointer
 * fields (production code allocates these in tn_lib_open on the Amiga). */
#define TN_TEST_BASE_INIT(b) do {     static LONG _fm[TN_DEFAULT_DTABLESIZE];     static ULONG _ev[TN_DEFAULT_DTABLESIZE];     static ULONG _em[TN_DEFAULT_DTABLESIZE];     int _i;     memset(&(b), 0, sizeof(b));     (b).fd_map = _fm; (b).events = _ev; (b).event_masks = _em;     (b).dtablesize = TN_DEFAULT_DTABLESIZE;     for (_i = 0; _i < TN_DEFAULT_DTABLESIZE; _i++) (b).fd_map[_i] = -1; } while(0)
#include "mock_lwip.h"
#include "task/slot_table.h"
#include "sys/socket.h"

/* TNET-115: exercise the REAL scatter-gather message handlers */
/* z.ai step 6 item 1: host mirror of the daemon CANCEL handler
 * (ipc_status.c pulls lwip/stats.h so it cannot be textually included
 * here). Same semantics: unpark the target and reply EINTR — but the
 * host harness has no ReplyMsg, so the reply is recorded via the
 * message fields (as the daemon does before ReplyMsg). */
/* 64-bit host: args[0] (LONG) cannot hold a pointer, so the target rides
 * in this file-scope variable. The m68k daemon passes it in args[0],
 * which is pointer-sized there. */
static TnIpcMsg *host_cancel_target = NULL;
int tn_ipc_cmd_cancel_host(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot_unused)
{
    TnIpcMsg *target = host_cancel_target;
    int s2;
    (void)imsg;
    (void)slot_unused;
    for (s2 = 0; s2 < TN_MAX_GLOBAL_SOCKETS; s2++) {
        TnSocketSlot *sl = &d->sockets[s2];
        if (!sl->in_use) continue;
        if (sl->pending_recv_msg == target) {
            sl->pending_recv_msg = NULL;
            target->result = -1;
            target->err_no = EINTR;
            return 0;
        }
    }
    return 0;
}
#define tn_ipc_cmd_cancel tn_ipc_cmd_cancel_host
#include "../../src/task/ipc_msg.c"

/* the daemon's loopback pump is outside the unit under test */
void tn_drain_loopback(void) { }
void tn_service_pending_recv(TnDaemon *d, TnSocketSlot *slot) { (void)d; (void)slot; }
void tn_signal_socket(TnDaemon *d, TnSocketSlot *slot) { (void)d; (void)slot; }
void tn_logf(int level, const char *fmt, ...) { (void)level; (void)fmt; }

#include "../../src/task/ipc_tcp.c"
#include "../../src/task/ipc_dgram.c"

#ifndef htons
#define htons(n) ((((n) & 0xff) << 8) | (((n) >> 8) & 0xff))
#endif
#ifndef htonl
#define htonl(n) ((((n) & 0xff000000UL) >> 24) | (((n) & 0x00ff0000UL) >> 8) | (((n) & 0x0000ff00UL) << 8) | (((n) & 0x000000ffUL) << 24))
#endif

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
    TN_TEST_BASE_INIT(base);
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
    TN_TEST_BASE_INIT(base);
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
    TN_TEST_BASE_INIT(base);
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
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    base.sig_event = 0x80000000;
    base.event_masks[3] = 0xFFFFFFFF;

    struct Task *mock_task = (struct Task *)0xDEADC0DE;
    TnSocketSlot *slot = tn_slot_alloc(&d, &base, mock_task, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);

    int fd = 3;
    base.fd_map[fd] = slot_idx;
    base.event_masks[fd] = 0xFFFFFFFF;

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

TN_TEST(sendmsg_recvmsg_scatter_tnet115)
{
    /* bsdsocktest #32 replicated: 3-iovec scatter-gather 50+30+20 over a
     * TCP loopback slot — send side chunking and recv side placement,
     * with an ODD base offset so no alignment assumption survives. */
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                       IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    struct tcp_pcb pcb;
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    static unsigned char sbuf[8192], rbuf[8192];
    const int odd = 1;
    for (int i = 0; i < 100; i++) sbuf[i] = (unsigned char)(i * 7 + 3);
    memset(rbuf, 0, sizeof(rbuf));

    /* ---- sendmsg: 3 iovecs 50/30/20 ---- */
    struct iovec iov[3];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = sbuf + odd;       iov[0].iov_len = 50;
    iov[1].iov_base = sbuf + odd + 50;  iov[1].iov_len = 30;
    iov[2].iov_base = sbuf + odd + 80;  iov[2].iov_len = 20;
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    TnIpcMsg imsg;
    memset(&imsg, 0, sizeof(imsg));
    imsg.ptrs[0] = &msg;
    imsg.args[1] = 0;

    TN_ASSERT_EQ(tn_ipc_cmd_sendmsg(&d, &imsg, slot), 0);
    TN_ASSERT_EQ((LONG)imsg.result, 100);
    TN_ASSERT_EQ(imsg.err_no, 0);

    /* three tcp_write calls: exact sizes, exact client pointers, in order —
     * TNET-115 shipped design: batch writes, then up to 4 output+drain
     * flush rounds AFTER the last write (the per-chunk interleaved flush
     * corrupted segmentation and was reverted). */
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_WRITE), 3);
    static const u16_t want_len[3] = { 50, 30, 20 };
    int wi = 0;
    int first_output = -1;
    for (int i = 0; i < mock_lwip_total_calls(); i++) {
        const MockCall *w = mock_lwip_call_at(i);
        TN_ASSERT_TRUE(w != NULL);
        if (w->type == MOCK_CALL_TCP_OUTPUT) {
            if (first_output < 0) first_output = i;
            continue;
        }
        if (w->type != MOCK_CALL_TCP_WRITE || wi >= 3) continue;
        if (first_output >= 0) break; /* batch: no write may follow an output */
        TN_ASSERT_EQ((int)w->arg1, (int)want_len[wi]);
        TN_ASSERT_EQ(w->ptr2, (void *)(sbuf + odd + (wi == 0 ? 0 : (wi == 1 ? 50 : 80))));
        wi++;
    }
    TN_ASSERT_EQ(wi, 3);
    TN_ASSERT_TRUE(first_output >= 0);
    TN_ASSERT_TRUE(mock_lwip_call_count(MOCK_CALL_TCP_OUTPUT) >= 1);

    /* ---- recvmsg: same 3-iovec geometry into rbuf ---- */
    struct pbuf *rp = mock_pbuf_alloc(100);
    TN_ASSERT_TRUE(rp != NULL);
    memcpy(rp->payload, sbuf, 100);
    TnRxPacket *rx = (TnRxPacket *)AllocVec(sizeof(TnRxPacket), 0);
    TN_ASSERT_TRUE(rx != NULL);
    memset(rx, 0, sizeof(TnRxPacket));
    rx->p = rp;
    slot->rx_head = rx;
    slot->rx_tail = rx;
    slot->rx_count = 1;

    struct iovec riov[3];
    struct msghdr rmsg;
    memset(&rmsg, 0, sizeof(rmsg));
    riov[0].iov_base = rbuf + odd;       riov[0].iov_len = 50;
    riov[1].iov_base = rbuf + odd + 50;  riov[1].iov_len = 30;
    riov[2].iov_base = rbuf + odd + 80;  riov[2].iov_len = 20;
    rmsg.msg_iov = riov;
    rmsg.msg_iovlen = 3;

    TnIpcMsg imsg2;
    memset(&imsg2, 0, sizeof(imsg2));
    imsg2.ptrs[0] = &rmsg;
    imsg2.args[1] = 0;

    TN_ASSERT_EQ(tn_ipc_cmd_recvmsg(&d, &imsg2, slot), 0);
    TN_ASSERT_EQ((LONG)imsg2.result, 100);
    TN_ASSERT_EQ(imsg2.err_no, 0);
    TN_ASSERT_EQ(memcmp(rbuf + odd, sbuf, 100), 0);

    /* the consumed packet was popped and the window returned */
    TN_ASSERT_TRUE(slot->rx_head == NULL);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_RECVED), 1);
    TN_ASSERT_EQ((int)mock_lwip_last_call()->arg1, 100);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(sendmsg_recvmsg_edge_cases_tnet115)
{
    /* TN-bugtrack-2 item 2: zero-length iovecs and iovlen>data must
     * terminate with sane results — no spin, no over-copy. */
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);
    memset(&base, 0, sizeof(base));

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                       IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    struct tcp_pcb pcb;
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    static unsigned char sbuf[256], rbuf[256];
    for (int i = 0; i < 40; i++) sbuf[i] = (unsigned char)(i + 1);
    memset(rbuf, 0, sizeof(rbuf));

    /* sendmsg: [0-length, 40 bytes, 0-length] — zero-length iovecs skipped */
    struct iovec iov[3];
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = sbuf;      iov[0].iov_len = 0;
    iov[1].iov_base = sbuf;      iov[1].iov_len = 40;
    iov[2].iov_base = sbuf + 40; iov[2].iov_len = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    TnIpcMsg imsg;
    memset(&imsg, 0, sizeof(imsg));
    imsg.ptrs[0] = &msg;
    imsg.args[1] = 0;

    TN_ASSERT_EQ(tn_ipc_cmd_sendmsg(&d, &imsg, slot), 0);
    TN_ASSERT_EQ((LONG)imsg.result, 40);
    TN_ASSERT_EQ(imsg.err_no, 0);

    /* recvmsg: 3 iovecs (10/20/30 = 60 capacity) against 40 bytes of data:
     * iovlen > available data — must terminate with 40 copied, correct
     * placement, packet popped. */
    struct pbuf *p = mock_pbuf_alloc(40);
    memcpy(p->payload, sbuf, 40);
    TnRxPacket *pkt = (TnRxPacket *)AllocVec(sizeof(TnRxPacket), 0);
    memset(pkt, 0, sizeof(TnRxPacket));
    pkt->p = p;
    pkt->offset = 0;
    slot->rx_head = pkt;
    slot->rx_tail = pkt;
    slot->rx_count = 1;

    struct iovec riov[3];
    struct msghdr rmsg;
    memset(&rmsg, 0, sizeof(rmsg));
    riov[0].iov_base = rbuf;      riov[0].iov_len = 10;
    riov[1].iov_base = rbuf + 10; riov[1].iov_len = 20;
    riov[2].iov_base = rbuf + 30; riov[2].iov_len = 30;
    rmsg.msg_iov = riov;
    rmsg.msg_iovlen = 3;

    TnIpcMsg imsg2;
    memset(&imsg2, 0, sizeof(imsg2));
    imsg2.ptrs[0] = &rmsg;
    imsg2.args[1] = 0;

    TN_ASSERT_EQ(tn_ipc_cmd_recvmsg(&d, &imsg2, slot), 0);
    TN_ASSERT_EQ((LONG)imsg2.result, 40);
    TN_ASSERT_EQ(imsg2.err_no, 0);
    TN_ASSERT_EQ(memcmp(rbuf, sbuf, 40), 0);
    TN_ASSERT_TRUE(slot->rx_head == NULL);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_udp_raw_send_connected)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    /* 1. UDP socket connected */
    TnSocketSlot *slot_udp = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &slot_idx);
    TN_ASSERT_TRUE(slot_udp != NULL);
    struct udp_pcb upcb;
    memset(&upcb, 0, sizeof(upcb));
    upcb.remote_ip.addr = 0x0202000A; /* 10.0.2.2 */
    upcb.remote_port = 8080;
    slot_udp->udp_pcb = &upcb;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    TN_ASSERT_EQ(tn_ipc_cmd_send(&d, &msg, slot_udp), 0);
    TN_ASSERT_EQ((LONG)msg.result, 4);
    TN_ASSERT_EQ(msg.err_no, 0);

    /* 2. RAW socket connected */
    int raw_idx = -1;
    TnSocketSlot *slot_raw = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_RAW, IPPROTO_ICMP, &raw_idx);
    TN_ASSERT_TRUE(slot_raw != NULL);
    struct raw_pcb rpcb;
    memset(&rpcb, 0, sizeof(rpcb));
    rpcb.remote_ip.addr = 0x0202000A;
    slot_raw->raw_pcb = &rpcb;

    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    TN_ASSERT_EQ(tn_ipc_cmd_send(&d, &msg, slot_raw), 0);
    TN_ASSERT_EQ((LONG)msg.result, 4);
    TN_ASSERT_EQ(msg.err_no, 0);

    tn_slot_free(&d, slot_idx);
    tn_slot_free(&d, raw_idx);
}

TN_TEST(net_udp_raw_send_unconnected_edestaddrreq)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    /* 1. UDP socket unconnected: remote_ip:remote_port == 0 */
    TnSocketSlot *slot_udp = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &slot_idx);
    TN_ASSERT_TRUE(slot_udp != NULL);
    struct udp_pcb upcb;
    memset(&upcb, 0, sizeof(upcb));
    slot_udp->udp_pcb = &upcb;

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    TN_ASSERT_EQ(tn_ipc_cmd_send(&d, &msg, slot_udp), 0);
    TN_ASSERT_EQ((LONG)msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EDESTADDRREQ); /* z.ai step 4 item 3 correction */

    /* 2. RAW socket unconnected: remote_ip == 0 */
    int raw_idx = -1;
    TnSocketSlot *slot_raw = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_RAW, IPPROTO_ICMP, &raw_idx);
    TN_ASSERT_TRUE(slot_raw != NULL);
    struct raw_pcb rpcb;
    memset(&rpcb, 0, sizeof(rpcb));
    slot_raw->raw_pcb = &rpcb;

    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    TN_ASSERT_EQ(tn_ipc_cmd_send(&d, &msg, slot_raw), 0);
    TN_ASSERT_EQ((LONG)msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EDESTADDRREQ); /* z.ai step 4 item 3 correction */

    tn_slot_free(&d, slot_idx);
    tn_slot_free(&d, raw_idx);
}

TN_TEST(net_udp_connected_sendto_eisconn)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    struct udp_pcb upcb;
    memset(&upcb, 0, sizeof(upcb));
    upcb.remote_ip.addr = 0x0202000A;
    upcb.remote_port = 8080;
    slot->udp_pcb = &upcb;

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(9090);
    to.sin_addr.s_addr = htonl(0x0A000203);

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    msg.ptrs[1] = &to;
    msg.args[3] = sizeof(to);

    /* 4.4BSD udp_output: sendto with explicit destination on connected UDP returns EISCONN */
    TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EISCONN);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_tcp_recvfrom_unconnected_enotconn)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    struct tcp_pcb pcb;
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;
    slot->tcp_state = TN_TCP_STATE_CLOSED;

    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    char buf[32];

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = buf;
    msg.args[1] = sizeof(buf);
    msg.args[2] = 0;
    msg.ptrs[1] = &from;
    msg.ptrs[2] = &fromlen;

    /* Unconnected TCP socket -> -1, ENOTCONN */
    TN_ASSERT_EQ(tn_ipc_cmd_recvfrom(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, ENOTCONN);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_tcp_recvfrom_connected_fromlen_zero)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    struct tcp_pcb pcb;
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    /* Push 5 bytes into rx_queue */
    struct pbuf *p = mock_pbuf_alloc(5);
    memcpy(p->payload, "HELLO", 5);
    ip_addr_t src_ip;
    src_ip.addr = 0x0100007f;
    TN_ASSERT_EQ(tn_rx_queue_push(slot, p, &src_ip, 8080), 0);

    struct sockaddr_in from;
    memset(&from, 0x55, sizeof(from));
    socklen_t fromlen = sizeof(from);
    char buf[32];
    memset(buf, 0, sizeof(buf));

    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = buf;
    msg.args[1] = sizeof(buf);
    msg.args[2] = 0;
    msg.ptrs[1] = &from;
    msg.ptrs[2] = &fromlen;

    TN_ASSERT_EQ(tn_ipc_cmd_recvfrom(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, 5);
    TN_ASSERT_EQ(msg.err_no, 0);
    TN_ASSERT_EQ(memcmp(buf, "HELLO", 5), 0);
    /* 4.4BSD soreceive: namelen is 0 and from is untouched */
    TN_ASSERT_EQ(fromlen, 0);
    for (size_t i = 0; i < sizeof(from); i++) {
        TN_ASSERT_EQ(((const unsigned char *)&from)[i], 0x55);
    }

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_tcp_sendto_semantics_enotconn_epipe_ignored_to)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    struct tcp_pcb pcb;
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(80);
    to.sin_addr.s_addr = htonl(0x0A000202);

    TnIpcMsg msg;

    /* A. Unconnected -> -1, ENOTCONN */
    slot->tcp_state = TN_TCP_STATE_CLOSED;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    msg.ptrs[1] = &to;
    msg.args[3] = sizeof(to);
    TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, ENOTCONN);

    /* B. Connected with to != NULL -> address ignored, data sent to stream */
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    msg.ptrs[1] = &to;
    msg.args[3] = sizeof(to);
    TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, 4);
    TN_ASSERT_EQ(msg.err_no, 0);

    /* C. Peer closed (CLOSE_WAIT analogue) with a live pcb -> send STILL
     * WORKS (z.ai step 2 / 4.4BSD: EPIPE only after OUR shutdown(SHUT_WR)
     * or a hard error; a server that reads to EOF then replies depends
     * on this). */
    slot->tcp_state = TN_TCP_STATE_PEER_CLOSED;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    msg.ptrs[1] = NULL;
    TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, 4);
    TN_ASSERT_EQ(msg.err_no, 0);

    /* C2. OUR shutdown(SHUT_WR) -> -1, EPIPE */
    slot->shut_wr = TRUE;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    msg.ptrs[1] = NULL;
    TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EPIPE);
    slot->shut_wr = FALSE;

    /* D. Connection broken / error -> -1, EPIPE */
    slot->tcp_state = TN_TCP_STATE_ERROR;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = "TEST";
    msg.args[1] = 4;
    msg.ptrs[1] = &to;
    TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, -1);
    TN_ASSERT_EQ(msg.err_no, EPIPE);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_accept_queue_early_data_transfer)
{
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *listening_slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(listening_slot != NULL);
    listening_slot->tcp_state = TN_TCP_STATE_LISTENING;

    struct tcp_pcb pcb;
    memset(&pcb, 0, sizeof(pcb));
    pcb.remote_port = 12345;
    pcb.remote_ip.addr = 0x0100007f;

    TN_ASSERT_EQ(tn_accept_queue_push(listening_slot, &pcb), 0);
    TN_ASSERT_EQ(listening_slot->accept_count, 1);
    TN_ASSERT_TRUE(pcb.callback_arg != NULL);
    TN_ASSERT_TRUE(pcb.recv_cb != NULL);
    TN_ASSERT_TRUE(pcb.err_cb != NULL);

    /* Simulate peer sending early data before accept() */
    struct pbuf *p = mock_pbuf_alloc(12);
    memcpy(p->payload, "EARLY_DATA\0", 11);
    err_t (*queued_recv)(void *, struct tcp_pcb *, struct pbuf *, err_t) =
        (err_t (*)(void *, struct tcp_pcb *, struct pbuf *, err_t))pcb.recv_cb;
    TN_ASSERT_EQ(queued_recv(pcb.callback_arg, &pcb, p, ERR_OK), ERR_OK);

    /* Now application calls accept() */
    TnIpcMsg accept_msg;
    memset(&accept_msg, 0, sizeof(accept_msg));
    accept_msg.socket_base = &base;
    accept_msg.args[0] = 0; /* listening fd */
    accept_msg.args[3] = -1;
    TN_ASSERT_EQ(tn_ipc_cmd_accept(&d, &accept_msg, listening_slot), 0);
    int accepted_fd = (int)accept_msg.result;
    TN_ASSERT_TRUE(accepted_fd >= 0);

    /* Verify early data was transferred into the accepted socket */
    int accepted_slot_idx = base.fd_map[accepted_fd];
    TnSocketSlot *accepted_slot = &d.sockets[accepted_slot_idx];
    TN_ASSERT_TRUE(accepted_slot->in_use);
    TN_ASSERT_EQ(accepted_slot->rx_count, 1);

    /* Call recv() to read the buffered early data */
    char buf[32];
    memset(buf, 0, sizeof(buf));
    TnIpcMsg recv_msg;
    memset(&recv_msg, 0, sizeof(recv_msg));
    recv_msg.socket_base = &base;
    recv_msg.ptrs[0] = buf;
    recv_msg.args[1] = sizeof(buf);
    recv_msg.args[2] = 0;
    TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &recv_msg, accepted_slot), 0);
    TN_ASSERT_EQ((LONG)recv_msg.result, 12);
    TN_ASSERT_STREQ(buf, "EARLY_DATA");

    tn_slot_free(&d, accepted_slot_idx);
    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_accept_queue_rst_removal_no_double_abort)
{
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    slot->tcp_state = TN_TCP_STATE_LISTENING;

    struct tcp_pcb pcb1, pcb2;
    memset(&pcb1, 0, sizeof(pcb1));
    memset(&pcb2, 0, sizeof(pcb2));

    TN_ASSERT_EQ(tn_accept_queue_push(slot, &pcb1), 0);
    TN_ASSERT_EQ(tn_accept_queue_push(slot, &pcb2), 0);
    TN_ASSERT_EQ(slot->accept_count, 2);

    /* Peer 1 sends RST -> lwIP invokes err_cb and frees pcb1 */
    void (*queued_err)(void *, err_t) = (void (*)(void *, err_t))pcb1.err_cb;
    TN_ASSERT_TRUE(queued_err != NULL);
    queued_err(pcb1.callback_arg, ERR_RST);

    /* pcb1 must be automatically unlinked from accept queue */
    TN_ASSERT_EQ(slot->accept_count, 1);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ABORT), 0);

    /* Draining accept queue must only abort pcb2, NOT pcb1 */
    tn_accept_queue_drain(slot);
    TN_ASSERT_EQ(slot->accept_count, 0);
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_TCP_ABORT), 1);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_tcp_shutdown_semantics)
{
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    struct tcp_pcb pcb;
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    /* A. SHUT_WR (how = 1): sets shut_wr = TRUE, but NOT PEER_CLOSED */
    TnIpcMsg shut_msg;
    memset(&shut_msg, 0, sizeof(shut_msg));
    shut_msg.socket_base = &base;
    shut_msg.args[0] = 0;
    shut_msg.args[1] = 1; /* SHUT_WR */
    TN_ASSERT_EQ(tn_ipc_cmd_shutdown(&d, &shut_msg, slot), 0);
    TN_ASSERT_EQ(shut_msg.result, 0);
    TN_ASSERT_TRUE(slot->shut_wr);
    TN_ASSERT_FALSE(slot->shut_rd);
    TN_ASSERT_EQ(slot->tcp_state, TN_TCP_STATE_ESTABLISHED);

    /* send() after SHUT_WR must return -1, EPIPE */
    TnIpcMsg send_msg;
    memset(&send_msg, 0, sizeof(send_msg));
    send_msg.socket_base = &base;
    send_msg.ptrs[0] = "HELLO";
    send_msg.args[1] = 5;
    TN_ASSERT_EQ(tn_ipc_cmd_send(&d, &send_msg, slot), 0);
    TN_ASSERT_EQ((LONG)send_msg.result, -1);
    TN_ASSERT_EQ(send_msg.err_no, EPIPE);

    /* recv() after SHUT_WR must still read queued data from peer */
    struct pbuf *p = mock_pbuf_alloc(4);
    memcpy(p->payload, "PEER", 4);
    TN_ASSERT_EQ(tn_rx_queue_push(slot, p, NULL, 0), 0);
    char buf[16];
    memset(buf, 0, sizeof(buf));
    TnIpcMsg recv_msg;
    memset(&recv_msg, 0, sizeof(recv_msg));
    recv_msg.socket_base = &base;
    recv_msg.ptrs[0] = buf;
    recv_msg.args[1] = sizeof(buf);
    TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &recv_msg, slot), 0);
    TN_ASSERT_EQ((LONG)recv_msg.result, 4);
    TN_ASSERT_STREQ(buf, "PEER");

    /* B. SHUT_RDWR (how = 2): full shutdown frees PCB and clears callbacks */
    memset(&shut_msg, 0, sizeof(shut_msg));
    shut_msg.socket_base = &base;
    shut_msg.args[0] = 0;
    shut_msg.args[1] = 2; /* SHUT_RDWR */
    TN_ASSERT_EQ(tn_ipc_cmd_shutdown(&d, &shut_msg, slot), 0);
    TN_ASSERT_EQ(shut_msg.result, 0);
    TN_ASSERT_TRUE(slot->tcp_pcb == NULL);
    TN_ASSERT_EQ(slot->tcp_state, TN_TCP_STATE_CLOSED);

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_recv_65536_clamped_not_truncated_to_zero)
{
    mock_lwip_reset();

    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    /* Push 100 bytes into rx_head */
    struct pbuf *p = mock_pbuf_alloc(100);
    memset(p->payload, 'A', 100);
    TN_ASSERT_EQ(tn_rx_queue_push(slot, p, NULL, 0), 0);

    /* recv with len = 65536 (0x10000): must NOT cast to 0 (false EOF), must receive 100 bytes */
    char buf[128];
    memset(buf, 0, sizeof(buf));
    TnIpcMsg recv_msg;
    memset(&recv_msg, 0, sizeof(recv_msg));
    recv_msg.socket_base = &base;
    recv_msg.ptrs[0] = buf;
    recv_msg.args[1] = 65536; /* 0x10000 */
    recv_msg.args[2] = 0;
    TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &recv_msg, slot), 0);
    TN_ASSERT_EQ((LONG)recv_msg.result, 100);
    TN_ASSERT_EQ(buf[0], 'A');
    TN_ASSERT_EQ(buf[99], 'A');

    tn_slot_free(&d, slot_idx);
}

TN_TEST(net_udp_recv_clones_to_pbuf_ram_frees_incoming_pool_pbuf)
{
    /* Item 9: UDP RX callback must clone incoming pool pbuf into PBUF_RAM
     * and free the incoming pbuf immediately, preventing PBUF_POOL starvation. */
    mock_lwip_reset();
    tn_slot_table_init(&g_daemon);

    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;

    TnSocketSlot *slot = tn_slot_alloc(&g_daemon, &base, NULL, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    TN_ASSERT_EQ(slot_idx, 0);

    /* Allocate mock incoming packet (simulating SANA2/lwIP PBUF_POOL) */
    struct pbuf *incoming = mock_pbuf_alloc(32);
    TN_ASSERT_TRUE(incoming != NULL);
    memcpy(incoming->payload, "UDP_TEST_PAYLOAD", 16);

    ip_addr_t src_ip;
    src_ip.addr = 0x0202000A; /* 10.0.2.2 */
    u16_t src_port = 5353;

    int frees_before = mock_lwip_call_count(MOCK_CALL_PBUF_FREE);

    /* Deliver packet through UDP recv callback */
    tn_udp_recv_cb((void *)(intptr_t)slot_idx, NULL, incoming, &src_ip, src_port);

    /* 1. The original incoming pbuf must be freed */
    TN_ASSERT_EQ(mock_lwip_call_count(MOCK_CALL_PBUF_FREE), frees_before + 1);

    /* 2. Slot must have 1 packet queued */
    TN_ASSERT_EQ(slot->rx_count, 1u);
    TN_ASSERT_TRUE(slot->rx_head != NULL);

    /* 3. The queued pbuf must NOT be the original pointer (it's a clone) */
    TN_ASSERT_TRUE(slot->rx_head->p != incoming);
    TN_ASSERT_TRUE(slot->rx_head->p != NULL);
    TN_ASSERT_EQ(slot->rx_head->p->tot_len, 32);
    TN_ASSERT_EQ(memcmp(slot->rx_head->p->payload, "UDP_TEST_PAYLOAD", 16), 0);

    /* 4. Reading through recvfrom yields the cloned data */
    char buf[64];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    TnIpcMsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.ptrs[0] = buf;
    msg.args[1] = sizeof(buf);
    msg.ptrs[1] = &from;
    msg.ptrs[2] = &fromlen;

    TN_ASSERT_EQ(tn_ipc_cmd_recvfrom(&g_daemon, &msg, slot), 0);
    TN_ASSERT_EQ((LONG)msg.result, 32);
    TN_ASSERT_EQ(memcmp(buf, "UDP_TEST_PAYLOAD", 16), 0);
    TN_ASSERT_EQ(slot->rx_count, 0u);

    tn_slot_free(&g_daemon, slot_idx);
}

TN_TEST(net_sys_now_eclock_monotonic_conversion)
{
    struct EClockVal boot = {0, 0}, cur = {0, 0};
    uint32_t ms;

    /* 1. NULL safety */
    TN_ASSERT_EQ(tn_eclock_to_ms(NULL, &boot, 709379), 0);
    TN_ASSERT_EQ(tn_eclock_to_ms(&cur, NULL, 709379), 0);

    /* 2. Zero diff */
    boot.ev_hi = 10;
    boot.ev_lo = 50000;
    cur.ev_hi = 10;
    cur.ev_lo = 50000;
    TN_ASSERT_EQ(tn_eclock_to_ms(&cur, &boot, 709379), 0);

    /* 3. Negative diff (backwards step guard) */
    cur.ev_lo = 40000;
    TN_ASSERT_EQ(tn_eclock_to_ms(&cur, &boot, 709379), 0);

    /* 4. PAL E-Clock (709,379 ticks/s): 1 second */
    boot.ev_hi = 0;
    boot.ev_lo = 0;
    cur.ev_hi = 0;
    cur.ev_lo = 709379;
    ms = tn_eclock_to_ms(&cur, &boot, 709379);
    TN_ASSERT_EQ(ms, 1000);

    /* 5. PAL E-Clock: 100 ms (approx 70,938 ticks) */
    cur.ev_lo = 70938;
    ms = tn_eclock_to_ms(&cur, &boot, 709379);
    TN_ASSERT_EQ(ms, 100);

    /* 6. NTSC E-Clock (715,909 ticks/s): 1 second */
    cur.ev_lo = 715909;
    ms = tn_eclock_to_ms(&cur, &boot, 715909);
    TN_ASSERT_EQ(ms, 1000);

    /* 7. Zero frequency fallback uses 709379 */
    cur.ev_lo = 709379;
    ms = tn_eclock_to_ms(&cur, &boot, 0);
    TN_ASSERT_EQ(ms, 1000);

    /* 8. 32-bit ev_lo wrap across 100-minute mark */
    boot.ev_hi = 0;
    boot.ev_lo = 0xFFFFFFFFUL - 709378UL;
    cur.ev_hi = 1;
    cur.ev_lo = 1;
    ms = tn_eclock_to_ms(&cur, &boot, 709379);
    TN_ASSERT_EQ(ms, 1000);

    /* 9. Long uptime: 10 days (864,000 seconds) without 64-bit overflow */
    uint64_t ticks_10d = (uint64_t)864000UL * 709379ULL;
    boot.ev_hi = 0;
    boot.ev_lo = 0;
    cur.ev_hi = (uint32_t)(ticks_10d >> 32);
    cur.ev_lo = (uint32_t)(ticks_10d & 0xFFFFFFFFUL);
    ms = tn_eclock_to_ms(&cur, &boot, 709379);
    TN_ASSERT_EQ(ms, 864000000UL);
}

/* z.ai step 2: 4.4BSD CLOSE_WAIT semantics - send() with a live pcb after
 * the peer's FIN must deliver data, not EPIPE. */
TN_TEST(send_succeeds_in_peer_closed)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    TnIpcMsg imsg;
    struct tcp_pcb pcb;
    struct iovec iov[1];
    struct msghdr msg;
    static unsigned char sbuf[64], rbuf[64];

    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                       IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;
    slot->tcp_state = TN_TCP_STATE_PEER_CLOSED;   /* CLOSE_WAIT analogue */

    iov[0].iov_base = sbuf;
    iov[0].iov_len = 10;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    memset(&imsg, 0, sizeof(imsg));
    imsg.ptrs[0] = &msg;

    TN_ASSERT_EQ(tn_ipc_cmd_sendmsg(&d, &imsg, slot), 0);
    TN_ASSERT_EQ((LONG)imsg.result, 10);
    TN_ASSERT_EQ(imsg.err_no, 0);

    /* but shut_wr still earns EPIPE */
    slot->shut_wr = TRUE;
    memset(&imsg, 0, sizeof(imsg));
    imsg.ptrs[0] = &msg;
    TN_ASSERT_EQ(tn_ipc_cmd_sendmsg(&d, &imsg, slot), 0);
    TN_ASSERT_EQ(imsg.result, -1);
    TN_ASSERT_EQ(imsg.err_no, EPIPE);
    (void)rbuf;
}

/* z.ai step 4 item 1: recv on CLOSED/LISTENING returns ENOTCONN instead
 * of parking forever; shut_rd gives EOF; recvfrom on TCP delegates. */
TN_TEST(net_recv_unconnected_tcp_enotconn_not_parked)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    TnIpcMsg imsg;
    static char buf[16];

    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);

    /* A. CLOSED */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                           IPPROTO_TCP, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        slot->tcp_state = TN_TCP_STATE_CLOSED;
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = buf;
        imsg.args[1] = sizeof(buf);
        TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, ENOTCONN);
        tn_slot_free(&d, slot_idx);
    }

    /* B. LISTENING */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                           IPPROTO_TCP, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        slot->tcp_state = TN_TCP_STATE_LISTENING;
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = buf;
        imsg.args[1] = sizeof(buf);
        TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, ENOTCONN);
        tn_slot_free(&d, slot_idx);
    }

    /* C. shut_rd without data -> EOF 0 (recvmsg path used to ignore it) */
    {
        struct iovec iov[1];
        struct msghdr msg;
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                           IPPROTO_TCP, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        slot->tcp_state = TN_TCP_STATE_ESTABLISHED;
        slot->shut_rd = TRUE;
        iov[0].iov_base = buf;
        iov[0].iov_len = sizeof(buf);
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = &msg;
        TN_ASSERT_EQ(tn_ipc_cmd_recvmsg(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, 0);
        TN_ASSERT_EQ(imsg.err_no, 0);
        tn_slot_free(&d, slot_idx);
    }

    /* D. recvfrom on a CLOSED TCP socket delegates to recv -> ENOTCONN */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                           IPPROTO_TCP, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        slot->tcp_state = TN_TCP_STATE_CLOSED;
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = buf;
        imsg.args[1] = sizeof(buf);
        TN_ASSERT_EQ(tn_ipc_cmd_recvfrom(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, ENOTCONN);
        tn_slot_free(&d, slot_idx);
    }
}

/* z.ai step 4 item 2: SHUT_RDWR detaches before tcp_close (no
 * use-after-free on the mock), returns FIN not RST (rx drained + recved),
 * second shutdown is quiet, invalid how -> EINVAL; ERROR send reports
 * last_error once then EPIPE. */
TN_TEST(net_shutdown_shut_rdwr_and_error_order)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    TnIpcMsg imsg;
    struct tcp_pcb pcb;
    struct iovec iov[1];
    struct msghdr msg;
    static unsigned char sbuf[16];

    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                       IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    memset(&pcb, 0, sizeof(pcb));
    slot->tcp_pcb = &pcb;
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    /* A. invalid how -> EINVAL */
    memset(&imsg, 0, sizeof(imsg));
    imsg.args[1] = 3;
    TN_ASSERT_EQ(tn_ipc_cmd_shutdown(&d, &imsg, slot), 0);
    TN_ASSERT_EQ(imsg.result, -1);
    TN_ASSERT_EQ(imsg.err_no, EINVAL);

    /* B. SHUT_RDWR -> ok; slot detached (pcb NULL, state CLOSED) */
    memset(&imsg, 0, sizeof(imsg));
    imsg.args[1] = 2;
    TN_ASSERT_EQ(tn_ipc_cmd_shutdown(&d, &imsg, slot), 0);
    TN_ASSERT_EQ(imsg.result, 0);
    TN_ASSERT_TRUE(slot->tcp_pcb == NULL);
    TN_ASSERT_EQ(slot->tcp_state, TN_TCP_STATE_CLOSED);
    TN_ASSERT_TRUE(slot->shut_wr);
    TN_ASSERT_TRUE(slot->shut_rd);

    /* C. second shutdown -> quiet 0 */
    memset(&imsg, 0, sizeof(imsg));
    imsg.args[1] = 2;
    TN_ASSERT_EQ(tn_ipc_cmd_shutdown(&d, &imsg, slot), 0);
    TN_ASSERT_EQ(imsg.result, 0);

    /* D. send after SHUT_RDWR -> EPIPE */
    iov[0].iov_base = sbuf;
    iov[0].iov_len = 4;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    memset(&imsg, 0, sizeof(imsg));
    imsg.ptrs[0] = &msg;
    TN_ASSERT_EQ(tn_ipc_cmd_sendmsg(&d, &imsg, slot), 0);
    TN_ASSERT_EQ(imsg.result, -1);
    TN_ASSERT_EQ(imsg.err_no, EPIPE);

    /* E. ERROR state: last_error reported once, then EPIPE */
    slot->tcp_state = TN_TCP_STATE_ERROR;
    slot->tcp_pcb = &pcb;      /* pretend a pcb still exists for the gate */
    slot->shut_wr = FALSE;
    slot->last_error = ECONNRESET;
    memset(&imsg, 0, sizeof(imsg));
    imsg.ptrs[0] = &msg;
    TN_ASSERT_EQ(tn_ipc_cmd_sendmsg(&d, &imsg, slot), 0);
    TN_ASSERT_EQ(imsg.err_no, ECONNRESET);
    memset(&imsg, 0, sizeof(imsg));
    imsg.ptrs[0] = &msg;
    TN_ASSERT_EQ(tn_ipc_cmd_sendmsg(&d, &imsg, slot), 0);
    TN_ASSERT_EQ(imsg.err_no, EPIPE);

    slot->tcp_pcb = NULL;
    tn_slot_free(&d, slot_idx);
}

/* z.ai step 4 item 3: EDESTADDRREQ (correction of the earlier ENOTCONN
 * decision) on unconnected UDP/RAW send/sendto(to=NULL); EISCONN on a
 * connected RAW with a destination; len>65507 -> EMSGSIZE. */
TN_TEST(net_send_edestaddrreq_correction)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    TnIpcMsg imsg;
    struct sockaddr_in to;
    static unsigned char sbuf[70000];

    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);

    /* A. send() on unconnected UDP -> EDESTADDRREQ */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM,
                                           0, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        slot->udp_pcb = &mock_udp_pcb_unconnected;
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = sbuf;
        imsg.args[1] = 4;
        TN_ASSERT_EQ(tn_ipc_cmd_send(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, EDESTADDRREQ);
        slot->udp_pcb = NULL;
        tn_slot_free(&d, slot_idx);
    }

    /* B. sendto(to=NULL) on unconnected UDP -> EDESTADDRREQ */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM,
                                           0, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        slot->udp_pcb = &mock_udp_pcb_unconnected;
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = sbuf;
        imsg.args[1] = 4;
        imsg.ptrs[1] = NULL;
        TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, EDESTADDRREQ);
        slot->udp_pcb = NULL;
        tn_slot_free(&d, slot_idx);
    }

    /* C. sendto(to=NULL) on unconnected RAW -> EDESTADDRREQ */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_RAW,
                                           1, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        slot->raw_pcb = &mock_raw_pcb_connected;
        mock_raw_pcb_connected.remote_ip.addr = 0; /* unconnected */
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = sbuf;
        imsg.args[1] = 4;
        TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, EDESTADDRREQ);
        slot->raw_pcb = NULL;
        tn_slot_free(&d, slot_idx);
    }

    /* D. connected RAW + to != NULL -> EISCONN */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_RAW,
                                           1, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        ip_addr_set_ip4_u32(&mock_raw_pcb_connected.remote_ip, 0x0A000202UL);
        slot->raw_pcb = &mock_raw_pcb_connected;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(7);
        to.sin_addr.s_addr = htonl(0x0A000202UL);
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = sbuf;
        imsg.args[1] = 4;
        imsg.ptrs[1] = &to;
        TN_ASSERT_EQ(tn_ipc_cmd_sendto(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, EISCONN);
        slot->raw_pcb = NULL;
        tn_slot_free(&d, slot_idx);
    }

    /* E. len > 65507 on UDP -> EMSGSIZE */
    {
        TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_DGRAM,
                                           0, &slot_idx);
        TN_ASSERT_TRUE(slot != NULL);
        ip_addr_set_ip4_u32(&mock_udp_pcb_connected.remote_ip, 0x0A000202UL);
        mock_udp_pcb_connected.remote_port = htons(7);
        slot->udp_pcb = &mock_udp_pcb_connected;
        memset(&imsg, 0, sizeof(imsg));
        imsg.ptrs[0] = sbuf;
        imsg.args[1] = 65508;
        TN_ASSERT_EQ(tn_ipc_cmd_send(&d, &imsg, slot), 0);
        TN_ASSERT_EQ(imsg.result, -1);
        TN_ASSERT_EQ(imsg.err_no, EMSGSIZE);
        slot->udp_pcb = NULL;
        tn_slot_free(&d, slot_idx);
    }
}

/* z.ai step 6 item 1: watchdog expiry must CANCEL the parked recv
 * (unparking it), not orphan it — the next recv on the same socket must
 * work instead of failing EALREADY forever. Exercises tn_ipc_cmd_cancel
 * through the mock: park -> cancel -> park again. */
TN_TEST(net_watchdog_cancel_unparks_recv)
{
    TnDaemon d;
    TnSocketBase base;
    TN_TEST_BASE_INIT(base);
    int slot_idx = -1;
    TnIpcMsg imsg1, imsg2, cancel;
    static char buf[16];

    memset(&d, 0, sizeof(d));
    tn_slot_table_init(&d);

    TnSocketSlot *slot = tn_slot_alloc(&d, &base, NULL, AF_INET, SOCK_STREAM,
                                       IPPROTO_TCP, &slot_idx);
    TN_ASSERT_TRUE(slot != NULL);
    slot->tcp_state = TN_TCP_STATE_ESTABLISHED;

    /* first recv parks (simulating the watchdog-expired call) */
    memset(&imsg1, 0, sizeof(imsg1));
    imsg1.ptrs[0] = buf;
    imsg1.args[1] = sizeof(buf);
    TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &imsg1, slot), 1); /* TN_IPC_DEFER */
    TN_ASSERT_TRUE(slot->pending_recv_msg == &imsg1);

    /* the client-side watchdog sends CANCEL(args[0]=imsg1) */
    memset(&cancel, 0, sizeof(cancel));
    cancel.cmd = TN_IPC_CMD_CANCEL;
    host_cancel_target = &imsg1;
    cancel.args[0] = 0; /* unused on the host shim */
    TN_ASSERT_EQ(tn_ipc_cmd_cancel(&d, &cancel, slot), 0);
    TN_ASSERT_EQ(cancel.result, 0);
    TN_ASSERT_TRUE(slot->pending_recv_msg == NULL);
    TN_ASSERT_EQ(imsg1.result, -1);
    TN_ASSERT_EQ(imsg1.err_no, EINTR);

    /* the NEXT recv on the same socket must park again cleanly
     * (pre-fix it got EALREADY forever) */
    memset(&imsg2, 0, sizeof(imsg2));
    imsg2.ptrs[0] = buf;
    imsg2.args[1] = sizeof(buf);
    TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &imsg2, slot), 1); /* parks again */
    TN_ASSERT_TRUE(slot->pending_recv_msg == &imsg2);

    /* and data arrival completes it */
    {
        struct pbuf *p = mock_pbuf_alloc(5);
        TN_ASSERT_TRUE(p != NULL);
        memcpy(p->payload, "hello", 5);
        tn_rx_queue_push(slot, p, NULL, 0);
    }
    TN_ASSERT_EQ(tn_ipc_cmd_recv(&d, &imsg2, slot), 0);
    TN_ASSERT_EQ((LONG)imsg2.result, 5);
    tn_slot_free(&d, slot_idx);
}

int main(void)
{
    TN_TEST_RUN(rx_pbuf_chain_partial_reads);
    TN_TEST_RUN(rx_queue_limit_and_ordering);
    TN_TEST_RUN(accept_queue_abort_on_drain);
    TN_TEST_RUN(event_queue_coalescing_per_bsdsocket_doc);
    TN_TEST_RUN(sendmsg_recvmsg_scatter_tnet115);
    TN_TEST_RUN(sendmsg_recvmsg_edge_cases_tnet115);
    TN_TEST_RUN(send_succeeds_in_peer_closed);
    TN_TEST_RUN(net_recv_unconnected_tcp_enotconn_not_parked);
    TN_TEST_RUN(net_shutdown_shut_rdwr_and_error_order);
    TN_TEST_RUN(net_send_edestaddrreq_correction);
    TN_TEST_RUN(net_watchdog_cancel_unparks_recv);
    TN_TEST_RUN(net_udp_raw_send_connected);
    TN_TEST_RUN(net_udp_raw_send_unconnected_edestaddrreq);
    TN_TEST_RUN(net_udp_connected_sendto_eisconn);
    TN_TEST_RUN(net_tcp_recvfrom_unconnected_enotconn);
    TN_TEST_RUN(net_tcp_recvfrom_connected_fromlen_zero);
    TN_TEST_RUN(net_tcp_sendto_semantics_enotconn_epipe_ignored_to);
    TN_TEST_RUN(net_accept_queue_early_data_transfer);
    TN_TEST_RUN(net_accept_queue_rst_removal_no_double_abort);
    TN_TEST_RUN(net_tcp_shutdown_semantics);
    TN_TEST_RUN(net_recv_65536_clamped_not_truncated_to_zero);
    TN_TEST_RUN(net_udp_recv_clones_to_pbuf_ram_frees_incoming_pool_pbuf);
    TN_TEST_RUN(net_sys_now_eclock_monotonic_conversion);

    TN_TEST_PLAN();
    return tn_test_failures();
}
