/*
 * mock_lwip.c — Mock lwIP implementation and call recording ring buffer.
 *
 * ROUND4b §C (Host-Testable Core).
 */
#include "mock_lwip.h"
#include "task_ctx.h"

TnDaemon g_daemon;

static MockCall s_calls[MOCK_MAX_CALLS];
static int      s_call_count = 0;

void mock_lwip_reset(void)
{
    memset(s_calls, 0, sizeof(s_calls));
    s_call_count = 0;
}

void mock_lwip_record(MockCallType type, void *p1, void *p2, uint32_t a1, uint32_t a2)
{
    int idx = s_call_count % MOCK_MAX_CALLS;
    s_calls[idx].type = type;
    s_calls[idx].ptr1 = p1;
    s_calls[idx].ptr2 = p2;
    s_calls[idx].arg1 = a1;
    s_calls[idx].arg2 = a2;
    s_call_count++;
}

int mock_lwip_call_count(MockCallType type)
{
    int count = 0;
    int total = (s_call_count > MOCK_MAX_CALLS) ? MOCK_MAX_CALLS : s_call_count;
    for (int i = 0; i < total; i++) {
        if (s_calls[i].type == type) {
            count++;
        }
    }
    return count;
}

const MockCall *mock_lwip_last_call(void)
{
    if (s_call_count == 0) return NULL;
    int idx = (s_call_count - 1) % MOCK_MAX_CALLS;
    return &s_calls[idx];
}

struct pbuf *mock_pbuf_alloc(uint16_t length)
{
    size_t sz = sizeof(struct pbuf) + length;
    struct pbuf *p = (struct pbuf *)malloc(sz);
    if (p == NULL) return NULL;

    p->next          = NULL;
    p->payload       = (void *)((uint8_t *)p + sizeof(struct pbuf));
    p->tot_len       = length;
    p->len           = length;
    p->type_internal = 0;
    p->flags         = 0;
    p->ref           = 1;

    memset(p->payload, 0, length);
    mock_lwip_record(MOCK_CALL_PBUF_ALLOC, p, NULL, length, 0);
    return p;
}

void pbuf_free(struct pbuf *p)
{
    while (p != NULL) {
        struct pbuf *next = p->next;
        mock_lwip_record(MOCK_CALL_PBUF_FREE, p, NULL, p->tot_len, 0);
        free(p);
        p = next;
    }
}

u16_t pbuf_copy_partial(const struct pbuf *buf, void *dataptr, u16_t len, u16_t offset)
{
    const struct pbuf *p;
    u16_t left = len;
    u16_t buf_copy_len;
    u16_t copied_total = 0;

    if (buf == NULL || dataptr == NULL) return 0;

    /* Skip pbufs until offset is within p->len */
    for (p = buf; p != NULL && offset >= p->len; p = p->next) {
        offset -= p->len;
    }

    while (p != NULL && left != 0) {
        buf_copy_len = p->len - offset;
        if (buf_copy_len > left) {
            buf_copy_len = left;
        }
        memcpy((uint8_t *)dataptr + copied_total, (const uint8_t *)p->payload + offset, buf_copy_len);
        copied_total += buf_copy_len;
        left -= buf_copy_len;
        offset = 0;
        p = p->next;
    }

    return copied_total;
}

err_t pbuf_take_at(struct pbuf *buf, const void *dataptr, u16_t len, u16_t offset)
{
    if (buf == NULL || dataptr == NULL || (size_t)offset + len > buf->tot_len) return ERR_VAL;
    memcpy((uint8_t *)buf->payload + offset, dataptr, len);
    return ERR_OK;
}

void udp_remove(struct udp_pcb *pcb)
{
    mock_lwip_record(MOCK_CALL_UDP_REMOVE, pcb, NULL, 0, 0);
}

void raw_remove(struct raw_pcb *pcb)
{
    mock_lwip_record(MOCK_CALL_RAW_REMOVE, pcb, NULL, 0, 0);
}

void tcp_arg(struct tcp_pcb *pcb, void *arg)
{
    mock_lwip_record(MOCK_CALL_TCP_ARG, pcb, arg, 0, 0);
    if (pcb) pcb->callback_arg = arg;
}

void tcp_recv(struct tcp_pcb *pcb, void *func)
{
    mock_lwip_record(MOCK_CALL_TCP_RECV, pcb, func, 0, 0);
}

void tcp_err(struct tcp_pcb *pcb, void *func)
{
    mock_lwip_record(MOCK_CALL_TCP_ERR, pcb, func, 0, 0);
}

void tcp_accept(struct tcp_pcb *pcb, void *func)
{
    mock_lwip_record(MOCK_CALL_TCP_ACCEPT, pcb, func, 0, 0);
}

err_t tcp_close(struct tcp_pcb *pcb)
{
    mock_lwip_record(MOCK_CALL_TCP_CLOSE, pcb, NULL, 0, 0);
    return ERR_OK;
}

void tcp_abort(struct tcp_pcb *pcb)
{
    mock_lwip_record(MOCK_CALL_TCP_ABORT, pcb, NULL, 0, 0);
}

u16_t tcp_sndbuf(struct tcp_pcb *pcb)
{
    (void)pcb;
    return TCP_SND_BUF;
}

err_t tcp_write(struct tcp_pcb *pcb, const void *arg, u16_t len, u8_t apiflags)
{
    mock_lwip_record(MOCK_CALL_TCP_WRITE, pcb, (void *)arg, len, apiflags);
    return ERR_OK;
}

err_t tcp_output(struct tcp_pcb *pcb)
{
    mock_lwip_record(MOCK_CALL_TCP_OUTPUT, pcb, NULL, 0, 0);
    return ERR_OK;
}

err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip, u16_t dst_port)
{
    mock_lwip_record(MOCK_CALL_UDP_SENDTO, pcb, p, dst_port, dst_ip ? dst_ip->addr : 0);
    return ERR_OK;
}

err_t raw_sendto(struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip)
{
    mock_lwip_record(MOCK_CALL_RAW_SENDTO, pcb, p, 0, dst_ip ? dst_ip->addr : 0);
    return ERR_OK;
}

/* AllocVec registry: daemon-side tables (selector table, TNET-108) are
 * process-lifetime allocations in the host harness — the Amiga daemon frees
 * them explicitly at shutdown, but the tests exit without teardown. Holding
 * every allocation in this static array keeps it reachable at exit so LSan
 * does not flag it; entries are dropped when the free goes through
 * mock_freevec (tests that free mock memory directly simply leave a stale
 * root, which is harmless). */
#define MOCK_MAX_ALLOCS 64
static void *s_allocs[MOCK_MAX_ALLOCS];
static int   s_alloc_count;

void *mock_allocvec(uint32_t size, uint32_t flags)
{
    void *p = calloc(1, size);
    (void)flags;
    if (p != NULL && s_alloc_count < MOCK_MAX_ALLOCS) {
        s_allocs[s_alloc_count++] = p;
    }
    return p;
}

void mock_freevec(void *ptr)
{
    int i;
    for (i = 0; i < s_alloc_count; i++) {
        if (s_allocs[i] == ptr) {
            s_allocs[i] = s_allocs[--s_alloc_count];
            break;
        }
    }
    free(ptr);
}

void mock_reply_msg(void *msg)
{
    mock_lwip_record(MOCK_CALL_REPLY_MSG, msg, NULL, 0, 0);
}

void mock_signal(void *task, uint32_t sigs)
{
    mock_lwip_record(MOCK_CALL_SIGNAL, task, NULL, sigs, 0);
}
