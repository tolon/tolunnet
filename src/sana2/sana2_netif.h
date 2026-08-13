/*
 * tolunet — SANA-II netif glue (M1, UNPROVEN).
 *
 * Master prompt §M1: open a SANA-II device/unit from config, query it, configure
 * the address, bring it online, keep >=4 CMD_READ outstanding, map S2ERR/S2WERR
 * to logged reasons, open shared (never exclusive), and shut down cleanly.
 *
 * This is the lwIP-netif ↔ SANA-II boundary. In M1 it is exercised by a raw
 * frame logger (src/cmds/TolunetStatus.c) — there is no IP yet (that is M2).
 *
 * UNPROVEN: written against the SANA-II Rev 7 spec (wiki) plus the web-fetched
 * field list. Every struct/constant below that could not be verified against
 * the Roadshow SDK's local include/devices/sana2.h is tagged
 *   /* VERIFY: sana2.h (Rev 7) */
 * and listed in QUESTIONS.md. Do not assume this compiles or runs until the
 * M1 exit test is pasted in STATUS.md.
 */
#ifndef TOLUNET_SANA2_NETIF_H
#define TOLUNET_SANA2_NETIF_H

#include <exec/types.h>
#include <exec/io.h>     /* struct IORequest — VERIFY: actual header */
#include <devices/sana2.h> /* SANA-II Rev 7 types — VERIFY: path/fields */

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
 * One SANA-II interface. In v1 we ship exactly one (master prompt §1); the
 * struct is designed so multiple could coexist later (design allows, ship one).
 */
typedef struct TnSana2If {
    STRPTR           device_name;     /* e.g. "wifipi.device"           */
    ULONG            unit;            /* e.g. 0                         */
    struct MsgPort  *reply_port;      /* for async CMD_READ completion  */
    struct IOSana2Req *io;            /* primary request (query/online) */
    struct IOSana2Req **read_ios;     /* >=4 outstanding CMD_READ reqs  */
    ULONG            n_read_ios;      /* count allocated (>=4)          */
    ULONG            mtu;             /* from S2_DEVICEQUERY            */
    UWORD            addr_bits;       /* AddrFieldSize from query       */
    UWORD            addr_bytes;      /* (addr_bits + 7) / 8            */
    UBYTE            mac[SANA2_MAX_ADDR_BYTES]; /* VERIFY: const in sana2.h */
    BOOL             online;
} TnSana2If;

/* Lifecycle. config-driven: reads DEVICE/UNIT from the parsed config (§5.2). */
TnS2Result tn_s2_open(TnSana2If *nif, CONST_STRPTR device_name, ULONG unit);
TnS2Result tn_s2_online(TnSana2If *nif, const UBYTE *mac /* nullable */);
void       tn_s2_offline_close(TnSana2If *nif);

/* Frame I/O (M1: logger only; M2: feed lwIP). */
LONG       tn_s2_send(TnSana2If *nif, const void *buf, LONG len,
                      BOOL broadcast, ULONG packet_type);
/* Re-arm one completed CMD_READ; returns the frame length or a negative err. */
LONG       tn_s2_recv(TnSana2If *nif, void *buf, ULONG buf_len,
                      ULONG idx, UBYTE *src_addr /* nullable */);

#endif /* TOLUNET_SANA2_NETIF_H */
