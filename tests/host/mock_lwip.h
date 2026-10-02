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
#ifndef TCP_SND_QUEUELEN
#define TCP_SND_QUEUELEN 16
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

/* lwIP enum tcp_state (tcpbase.h) - same names and values (TNET-156) */
enum tcp_state {
    CLOSED = 0, LISTEN = 1, SYN_SENT = 2, SYN_RCVD = 3, ESTABLISHED = 4,
    FIN_WAIT_1 = 5, FIN_WAIT_2 = 6, CLOSE_WAIT = 7, CLOSING = 8,
    LAST_ACK = 9, TIME_WAIT = 10
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
    void     *recv_cb;
    void     *err_cb;
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
    MOCK_CALL_SIGNAL,
    MOCK_CALL_TCP_SHUTDOWN,     /* arg1 = shut_rx, arg2 = shut_tx */
    MOCK_CALL_TCP_SENT,
    MOCK_CALL_TCP_POLL          /* arg1 = interval */
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

/* 9.14: fault injection - reset to the healthy defaults by mock_lwip_reset() */
void mock_set_tcp_sndbuf(u16_t v);          /* default TCP_SND_BUF */
void mock_set_tcp_sndqueuelen(u16_t v);     /* default 0 */
void mock_set_tcp_write_err(err_t e);       /* default ERR_OK */
void mock_set_tcp_close_err(err_t e);       /* default ERR_OK */
void mock_set_tcp_connect_err(err_t e);     /* default ERR_OK */
void mock_set_udp_sendto_err(err_t e);      /* default ERR_OK */
void mock_set_tcp_write_consumes(int on);   /* TNET-159: tcp_write lowers sndbuf; default off */

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
u16_t tcp_sndqueuelen(struct tcp_pcb *pcb);
void tcp_poll(struct tcp_pcb *pcb, void *func, u8_t interval);
err_t tcp_write(struct tcp_pcb *pcb, const void *arg, u16_t len, u8_t apiflags);
err_t tcp_output(struct tcp_pcb *pcb);
err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip, u16_t dst_port);
err_t raw_sendto(struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *dst_ip);
err_t udp_connect(struct udp_pcb *pcb, const ip_addr_t *ip, u16_t port);
err_t raw_connect(struct raw_pcb *pcb, const ip_addr_t *ip);
err_t raw_send(struct raw_pcb *pcb, struct pbuf *p);
err_t tcp_connect(struct tcp_pcb *pcb, const ip_addr_t *ip, u16_t port, void *connected);
void tcp_sent(struct tcp_pcb *pcb, void *func);
err_t tcp_shutdown(struct tcp_pcb *pcb, int shut_rx, int shut_tx);
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *ip, u16_t port);
err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *ip, u16_t port);
err_t raw_bind(struct raw_pcb *pcb, const ip_addr_t *ip);
struct pbuf *pbuf_clone(uint8_t layer, uint8_t type, struct pbuf *p);
struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb, u8_t backlog);
err_t igmp_joingroup(const ip4_addr_t *ifaddr, const ip4_addr_t *groupaddr);
err_t igmp_leavegroup(const ip4_addr_t *ifaddr, const ip4_addr_t *groupaddr);


#ifndef pbuf_take
#define pbuf_take(buf, dataptr, len) pbuf_take_at((buf), (dataptr), (len), 0)
#endif
#ifndef PBUF_RAW
#define PBUF_RAW 2
#endif
#ifndef tcp_nagle_disable
#define tcp_nagle_disable(pcb) ((void)(pcb))
#endif
#ifndef tcp_nagle_enable
#define tcp_nagle_enable(pcb) ((void)(pcb))
#endif
#ifndef SOF_REUSEADDR
#define SOF_REUSEADDR 0x04
#endif
#ifndef SOF_KEEPALIVE
#define SOF_KEEPALIVE 0x08
#endif
#ifndef SOF_BROADCAST
#define SOF_BROADCAST 0x20
#endif
#ifndef ip_set_option
#define ip_set_option(pcb, opt) ((void)0)
#endif
#ifndef ip_reset_option
#define ip_reset_option(pcb, opt) ((void)0)
#endif
#ifndef raw_set_flags
#define raw_set_flags(pcb, flag) ((void)0)
#endif
#ifndef raw_clear_flags
#define raw_clear_flags(pcb, flag) ((void)0)
#endif
#ifndef udp_set_flags
#define udp_set_flags(pcb, flag) ((void)0)
#endif
#ifndef udp_clear_flags
#define udp_clear_flags(pcb, flag) ((void)0)
#endif
#ifndef UDP_FLAGS_MULTICAST_LOOP
#define UDP_FLAGS_MULTICAST_LOOP 0x04
#endif
#ifndef RAW_FLAGS_HDRINCL
#define RAW_FLAGS_HDRINCL 0x01
#endif
#ifndef FD_ACCEPT
#define FD_ACCEPT 0x01
#define FD_CONNECT 0x02
#define FD_OOB 0x04
#define FD_READ 0x08
#define FD_WRITE 0x10
#define FD_ERROR 0x20
#define FD_CLOSE 0x40
#endif
#ifndef TN_LOG_OFF
#define TN_LOG_OFF 0
#define TN_LOG_BASIC 1
#define TN_LOG_VERBOSE 2
#endif

/* AmigaOS Stubs */
#ifndef MEMF_PUBLIC
#define MEMF_PUBLIC 1
#define MEMF_CLEAR  2
#endif

extern struct udp_pcb mock_udp_pcb_unconnected;
extern struct udp_pcb mock_udp_pcb_connected;
extern struct raw_pcb mock_raw_pcb_connected;

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

#ifndef _ECLOCKVAL_DEFINED
#define _ECLOCKVAL_DEFINED
struct EClockVal {
    uint32_t ev_hi;
    uint32_t ev_lo;
};
#endif

uint32_t tn_eclock_to_ms(const struct EClockVal *cur, const struct EClockVal *boot, uint32_t freq);
uint32_t sys_now(void);
void mock_set_sys_now(uint32_t ms);

#endif /* TOLUNNET_MOCK_LWIP_H */
