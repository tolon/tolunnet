/*
 * tolunnet — SANA-II netif glue and lwIP adapter.
 */
#ifndef TOLUNNET_SANA2_NETIF_H
#define TOLUNNET_SANA2_NETIF_H

#include <exec/types.h>
#include <exec/io.h>       /* struct IORequest */
#include <devices/sana2.h> /* IOSana2Req, Sana2DeviceQuery, S2_* */

#include "lwip/err.h"

/* Forward declarations for lwIP structs */
struct netif;
struct pbuf;

/* How many CMD_READ requests we keep outstanding (§M1: ">=4"). */
#define TN_S2_NREADS  4

/* Standard EtherType values. */
#define TN_ETHERTYPE_IPV4 0x0800U
#define TN_ETHERTYPE_ARP  0x0806U

/* Result codes for the SANA-II open/bring-up sequence. */
typedef enum {
    TN_S2_OK            = 0,
    TN_S2_NO_DOS        = 1,   /* dos.library not available            */
    TN_S2_OPEN_FAIL     = 2,   /* OpenDevice returned non-zero         */
    TN_S2_NO_COPYFUNCS  = 3,   /* buffer-management tag list missing   */
    TN_S2_QUERY_FAIL    = 4,   /* S2_DEVICEQUERY returned an error     */
    TN_S2_CONFIG_FAIL   = 5,   /* S2_CONFIGINTERFACE returned an error */
    TN_S2_ONLINE_FAIL   = 6,   /* S2_ONLINE returned an error          */
    TN_S2_NO_MEM        = 7,   /* could not allocate IOSana2Req / bufs */
    TN_S2_INTERNAL      = 99
} TnS2Result;

/*
 * One SANA-II interface with decoupled TX and RX MsgPorts.
 */
typedef struct TnSana2If {
    STRPTR             device_name;     /* e.g. "ethernet.device"         */
    ULONG              unit;            /* e.g. 0                         */
    struct MsgPort    *tx_port;         /* dedicated port for DoIO TX     */
    struct MsgPort    *rx_port;         /* dedicated port for async RX    */
    struct IOSana2Req *io;              /* primary request (query/online) */
    struct IOSana2Req **read_ios;       /* >=4 outstanding CMD_READ reqs  */
    UBYTE             *read_bufs[TN_S2_NREADS]; /* DMA/I/O receive buffers*/
    BOOL               read_armed[TN_S2_NREADS];/* Active in device queue */
    ULONG              n_read_ios;      /* count allocated (>=4)          */
    struct MsgPort    *event_port;      /* dedicated port for S2_ONEVENT (TNET-109) */
    struct IOSana2Req *event_io;        /* outstanding S2_ONEVENT request  */
    ULONG              event_mask;      /* S2EVENT_* bits we asked for     */
    BOOL               event_armed;     /* event_io active in device queue */
    BOOL               event_supported; /* driver answered S2_ONEVENT      */
    BOOL               link_down;       /* last S2EVENT_OFFLINE/ONLINE state */
    ULONG              mtu;             /* from S2_DEVICEQUERY            */
    UWORD              addr_bits;       /* AddrFieldSize from query       */
    UWORD              addr_bytes;      /* (addr_bits + 7) / 8            */
    UBYTE              mac[SANA2_MAX_ADDR_BYTES];   /* Hardware station addr */
    UBYTE              bcast[SANA2_MAX_ADDR_BYTES]; /* Broadcast address     */
    struct TagItem     bm_tags[3];                  /* Persistent BufferManagement tags (TNET-023) */
    BOOL               online;
} TnSana2If;

/* Lifecycle. */
TnS2Result tn_s2_open(TnSana2If *nif, CONST_STRPTR device_name, ULONG unit);
TnS2Result tn_s2_online(TnSana2If *nif, const UBYTE *mac /* nullable */);
void       tn_s2_offline_close(TnSana2If *nif);

/* Arm >=4 outstanding async CMD_READ requests (the receive pump). */
TnS2Result tn_s2_arm_reads(TnSana2If *nif);

/* TNET-109: arm the S2_ONEVENT link-event request (mask = S2EVENT_* bits;
 * 0 = ONLINE|OFFLINE|ERROR). Uses a dedicated MsgPort so event replies can
 * never be mistaken for CMD_READ completions. Returns TN_S2_OK even when the
 * driver later rejects the command — poll_events then records and reports
 * event_supported = FALSE once. */
TnS2Result tn_s2_arm_events(TnSana2If *nif, ULONG mask);

/* TNET-109: signal bit of the event port (0 when unarmed). */
ULONG tn_s2_event_sig(const TnSana2If *nif);

/* TNET-109: drain completed S2_ONEVENT replies, apply link transitions to
 * the lwIP netif, and re-arm. Called from the daemon main loop. */
void tn_s2_poll_events(TnSana2If *nif, struct netif *netif);

/* Frame I/O */
LONG       tn_s2_send(TnSana2If *nif, const void *buf, LONG len,
                      BOOL broadcast, ULONG packet_type, const UBYTE *dst_addr);

/* Blocking receive on slot idx; returns the on-wire frame length or negative. */
LONG       tn_s2_recv(TnSana2If *nif, void *buf, ULONG buf_len,
                      ULONG idx, UBYTE *src_addr /* nullable */);

/* Helper to log SANA-II errors */
void       tn_log_s2err(const char *step, LONG err, LONG wire);

/* Multicast group membership support (S2_ADDMULTICASTADDRESS / S2_DELMULTICASTADDRESS) */
BOOL       tn_s2_add_multicast(TnSana2If *nif, const UBYTE *mac);
BOOL       tn_s2_del_multicast(TnSana2If *nif, const UBYTE *mac);

/* lwIP netif bridge functions */
err_t      tn_sana2_netif_init(struct netif *netif);
err_t      tn_sana2_linkoutput(struct netif *netif, struct pbuf *p);
void       tn_sana2_poll_input(TnSana2If *nif, struct netif *netif);

#endif /* TOLUNNET_SANA2_NETIF_H */
