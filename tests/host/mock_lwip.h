/*
 * mock_lwip.h — Mock lwIP and Exec harness for host-side unit testing.
 *
 * ROUND4b §C (Host-Testable Core).
 * Provides stub structures, mock tracking ring buffer, and Amiga memory stubs
 * for compiling and testing slot_table and ipc_dispatch under ASan/UBSan.
 */
#ifndef TOLUNNET_MOCK_LWIP_H
#define TOLUNNET_MOCK_LWIP_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ipc.h"

/* lwIP basic types */
typedef int8_t   err_t;
typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef int32_t  s32_t;

#define ERR_OK         0
#define ERR_MEM       -1
#define ERR_BUF       -2
#define ERR_TIMEOUT   -3
#define ERR_RTE       -4
#define ERR_INPROGRESS -5
#define ERR_VAL       -6
#define ERR_WOULDBLOCK -7
#define ERR_USE       -8
#define ERR_ALREADY   -9
#define ERR_ISCONN    -10
#define ERR_CONN      -11
#define ERR_IF        -12
#define ERR_ABRT      -13
#define ERR_RST       -14
#define ERR_CLSD      -15
#define ERR_ARG       -16

#ifndef TCP_SND_BUF
#define TCP_SND_BUF 8192
#endif
#ifndef TCP_WND
#define TCP_WND     8192
#endif
#ifndef TCP_MSS
#define TCP_MSS     1460
#endif

/* pbuf layers/flags used by the daemon code under test (TNET-115) */
#define PBUF_TRANSPORT   4
#define PBUF_IP          3
#define PBUF_RAM         1
#define TCP_WRITE_FLAG_COPY 0x01
#ifndef lwip_htons
#define lwip_htons(n) ((((n) & 0xffUL) << 8) | (((n) >> 8) & 0xffUL))
#define lwip_ntohs(n) lwip_htons(n)
#endif

typedef struct ip4_addr {
    uint32_t addr;
} ip4_addr_t;

typedef struct ip_addr {
    uint32_t addr;
} ip_addr_t;

#define ip_2_ip4(ipaddr) ((ip4_addr_t *)(ipaddr))
#define ip_addr_get_ip4_u32(ipaddr) (((const ip_addr_t *)(ipaddr))->addr)
#define ip_addr_set_ip4_u32(ipaddr, val) (((ip_addr_t *)(ipaddr))->addr = (val))

struct pbuf {
    struct pbuf *next;
    void        *payload;
    uint16_t     tot_len;
    uint16_t     len;
    uint8_t      type_internal;
    uint8_t      flags;
    uint16_t     ref;
};

struct tcp_pcb {
    ip_addr_t local_ip;
    ip_addr_t remote_ip;
    uint16_t  local_port;
    uint16_t  remote_port;
    uint8_t   state;
    uint16_t  mss;
    uint32_t  keep_idle;
    uint32_t  keep_intvl;
    uint32_t  keep_cnt;
    uint8_t   tos;
    uint8_t   ttl;
    void     *callback_arg;
};

struct udp_pcb {
    ip_addr_t local_ip;
    ip_addr_t remote_ip;
    uint16_t  local_port;
    uint16_t  remote_port;
    uint8_t   flags;
    uint8_t   tos;
    uint8_t   ttl;
    uint8_t   mcast_ttl;
};

struct raw_pcb {
    ip_addr_t local_ip;
    ip_addr_t remote_ip;
    uint8_t   protocol;
    uint8_t   flags;
    uint8_t   tos;
    uint8_t   ttl;
    uint8_t   mcast_ttl;
};

struct netif {
    ip_addr_t ip_addr;
    ip_addr_t netmask;
    ip_addr_t gw;
    uint16_t  mtu;
    uint8_t   hwaddr[6];
};

/* Dummy Sana2 & Timer for TnDaemon on host */
typedef struct TnSana2If {
    uint8_t  mac[6];
    uint32_t mtu;
} TnSana2If;

typedef struct TnTimer {
    uint32_t sig_mask;
} TnTimer;

/* Mock Call Tracking */
typedef enum MockCallType {
    MOCK_CALL_NONE = 0,
    MOCK_CALL_PBUF_ALLOC,
    MOCK_CALL_PBUF_FREE,
    MOCK_CALL_TCP_ARG,
    MOCK_CALL_TCP_RECV,
    MOCK_CALL_TCP_ERR,
    MOCK_CALL_TCP_ACCEPT,
    MOCK_CALL_TCP_CLOSE,
    MOCK_CALL_TCP_ABORT,
    MOCK_CALL_UDP_REMOVE,
    MOCK_CALL_RAW_REMOVE,
    MOCK_CALL_TCP_WRITE,
    MOCK_CALL_TCP_OUTPUT,
    MOCK_CALL_TCP_RECVED,
    MOCK_CALL_UDP_SENDTO,
    MOCK_CALL_RAW_SENDTO,
    MOCK_CALL_REPLY_MSG,
    MOCK_CALL_SIGNAL
} MockCallType;

typedef struct MockCall {
    MockCallType type;
    void        *ptr1;
    void        *ptr2;
    uint32_t     arg1;
    uint32_t     arg2;
} MockCall;

#define MOCK_MAX_CALLS 128

void mock_lwip_reset(void);
int mock_lwip_call_count(MockCallType type);
const MockCall *mock_lwip_last_call(void);
const MockCall *mock_lwip_call_at(int idx);   /* raw ring index, oldest first */
int mock_lwip_total_calls(void);
void mock_lwip_record(MockCallType type, void *p1, void *p2, uint32_t a1, uint32_t a2);

/* Mock lwIP Functions */
struct pbuf *mock_pbuf_alloc(uint16_t length);
struct pbuf *pbuf_alloc(uint8_t layer, uint16_t length, uint8_t type);
void tcp_recved(struct tcp_pcb *pcb, uint16_t len);
void pbuf_free(struct pbuf *p);
u16_t pbuf_copy_partial(const struct pbuf *buf, void *dataptr, u16_t len, u16_t offset);
err_t pbuf_take_at(struct pbuf *buf, const void *dataptr, u16_t len, u16_t offset);

void udp_remove(struct udp_pcb *pcb);
void raw_remove(struct raw_pcb *pcb);
void tcp_arg(struct tcp_pcb *pcb, void *arg);
void tcp_recv(struct tcp_pcb *pcb, void *func);
void tcp_err(struct tcp_pcb *pcb, void *func);
void tcp_accept(struct tcp_pcb *pcb, void *func);
err_t tcp_close(struct tcp_pcb *pcb);
void tcp_abort(struct tcp_pcb *pcb);
u16_t tcp_sndbuf(struct tcp_pcb *pcb);
err_t tcp_write(struct tcp_pcb *pcb, const void *arg, u16_t len, u8_t apiflags);
err_t tcp_output(struct tcp_pcb *pcb);
err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip, u16_t dst_port);
err_t raw_sendto(struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip);

/* AmigaOS Stubs */
#ifndef MEMF_PUBLIC
#define MEMF_PUBLIC 1
#define MEMF_CLEAR  2
#endif

void *mock_allocvec(uint32_t size, uint32_t flags);
void mock_freevec(void *ptr);
#ifndef AllocVec
#define AllocVec(sz, flags) mock_allocvec((sz), (flags))
#define FreeVec(ptr) mock_freevec(ptr)
#endif

void mock_reply_msg(void *msg);
#ifndef ReplyMsg
#define ReplyMsg(msg) mock_reply_msg(msg)
#endif

void mock_signal(void *task, uint32_t sigs);
#ifndef Signal
#define Signal(task, sigs) mock_signal((task), (sigs))
#endif

#endif /* TOLUNNET_MOCK_LWIP_H */
