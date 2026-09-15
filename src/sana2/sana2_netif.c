/*
 * tolunnet — SANA-II netif glue and lwIP adapter implementation.
 */

#include "sana2_netif.h"
#include "../common/log.h"
#include "../common/config_text.h"
#include "../task/task_ctx.h"

#include <stdint.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <exec/memory.h>
#include <exec/io.h>
#include <utility/tagitem.h>
#include <devices/sana2.h>

#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "netif/etharp.h"
#include "lwip/dhcp.h"

/* config_text.h mirrors the S2EVENT_* bits for the host-built parser;
 * keep the two definitions honest against each other (TNET-109). */
_Static_assert(TN_S2EV_ONLINE   == S2EVENT_ONLINE,   "S2EVENT drift");
_Static_assert(TN_S2EV_OFFLINE  == S2EVENT_OFFLINE,  "S2EVENT drift");
_Static_assert(TN_S2EV_ERROR    == S2EVENT_ERROR,    "S2EVENT drift");
_Static_assert(TN_S2EV_TX       == S2EVENT_TX,       "S2EVENT drift");
_Static_assert(TN_S2EV_RX       == S2EVENT_RX,       "S2EVENT drift");
_Static_assert(TN_S2EV_BUFF     == S2EVENT_BUFF,     "S2EVENT drift");
_Static_assert(TN_S2EV_HARDWARE == S2EVENT_HARDWARE, "S2EVENT drift");
_Static_assert(TN_S2EV_SOFTWARE == S2EVENT_SOFTWARE, "S2EVENT drift");

static struct IORequest *tn_create_extio(struct MsgPort *port, ULONG size)
{
    struct IORequest *io = (struct IORequest *)AllocVec(size, MEMF_CLEAR | MEMF_PUBLIC);
    if (io != NULL) {
        io->io_Message.mn_ReplyPort = port;
        io->io_Message.mn_Length    = (UWORD)size;
        io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    }
    return io;
}

static void tn_delete_extio(struct IORequest *io)
{
    if (io != NULL) FreeVec(io);
}

void tn_log_s2err(const char *step, LONG err, LONG wire)
{
    tn_logf(TN_LOG_BASIC, "tolunnet: %s failed err=%ld wire=%ld\n",
            step, err, wire);
}

/* Forward declare assembly trampolines (TNET-006) */
extern void tn_s2_copy_to_buff_asm(void);
extern void tn_s2_copy_from_buff_asm(void);

BOOL tn_copy_to_buff_c(APTR dst, APTR src, ULONG len)
{
    if (dst != NULL && src != NULL && len > 0) {
        CopyMem(src, dst, len);
    }
    return TRUE;
}

BOOL tn_copy_from_buff_c(APTR dst, APTR src, ULONG len)
{
    if (dst != NULL && src != NULL && len > 0) {
        CopyMem(src, dst, len);
    }
    return TRUE;
}

TnS2Result tn_s2_open(TnSana2If *nif, CONST_STRPTR device_name, ULONG unit)
{
    struct MsgPort *tx_port, *rx_port;
    struct IOSana2Req *io;
    BYTE err;
    ULONG i;

    if (nif == NULL || device_name == NULL) return TN_S2_INTERNAL;

    nif->device_name = (STRPTR)device_name;
    nif->unit = unit;
    nif->online = FALSE;
    nif->read_ios = NULL;
    nif->n_read_ios = 0;
    nif->event_port = NULL;
    nif->event_io = NULL;
    nif->event_mask = 0;
    nif->event_armed = FALSE;
    nif->event_supported = FALSE;
    nif->link_down = FALSE;
    nif->mtu = 0;
    nif->addr_bits = 0;
    nif->addr_bytes = 0;

    for (i = 0; i < TN_S2_NREADS; i++) {
        nif->read_bufs[i] = NULL;
        nif->read_armed[i] = FALSE;
    }

    tx_port = CreateMsgPort();
    if (tx_port == NULL) return TN_S2_NO_MEM;
    nif->tx_port = tx_port;

    rx_port = CreateMsgPort();
    if (rx_port == NULL) {
        DeleteMsgPort(tx_port);
        nif->tx_port = NULL;
        return TN_S2_NO_MEM;
    }
    nif->rx_port = rx_port;

    io = (struct IOSana2Req *)tn_create_extio(tx_port, sizeof(struct IOSana2Req));
    if (io == NULL) {
        DeleteMsgPort(rx_port);
        DeleteMsgPort(tx_port);
        nif->rx_port = NULL;
        nif->tx_port = NULL;
        return TN_S2_NO_MEM;
    }
    nif->io = io;

    nif->bm_tags[0].ti_Tag   = S2_CopyToBuff;
    nif->bm_tags[0].ti_Data  = (ULONG)tn_s2_copy_to_buff_asm;
    nif->bm_tags[1].ti_Tag   = S2_CopyFromBuff;
    nif->bm_tags[1].ti_Data  = (ULONG)tn_s2_copy_from_buff_asm;
    nif->bm_tags[2].ti_Tag   = TAG_DONE;

    io->ios2_BufferManagement = nif->bm_tags;

    tn_log(TN_LOG_VERBOSE, "s2: OpenDevice...\n");
    err = OpenDevice((STRPTR)device_name, unit, (struct IORequest *)io, 0UL);
    tn_logf(TN_LOG_VERBOSE, "s2: OpenDevice err=%d\n", (int)err);
    if (err != 0) {
        tn_logf(TN_LOG_BASIC, "tolunnet: OpenDevice(%s, %lu) failed err=%d\n",
                device_name, unit, (int)err);
        tn_delete_extio((struct IORequest *)io);
        nif->io = NULL;
        DeleteMsgPort(rx_port);
        DeleteMsgPort(tx_port);
        nif->rx_port = NULL;
        nif->tx_port = NULL;
        return TN_S2_OPEN_FAIL;
    }

    return TN_S2_OK;
}

static TnS2Result tn_s2_query(TnSana2If *nif)
{
    struct Sana2DeviceQuery q;
    struct IOSana2Req *io = nif->io;

    q.SizeAvailable  = sizeof(q);
    q.SizeSupplied   = 0;
    q.DevQueryFormat = 0;
    q.DeviceLevel    = 0;

    io->ios2_Req.io_Command = S2_DEVICEQUERY;
    io->ios2_StatData       = &q;
    io->ios2_Req.io_Error   = 0;

    DoIO((struct IORequest *)io);
    tn_logf(TN_LOG_VERBOSE, "s2: DEVICEQUERY err=%ld MTU=%lu addr_bits=%lu\n",
           (LONG)io->ios2_Req.io_Error, q.MTU, (ULONG)q.AddrFieldSize);
    if (io->ios2_Req.io_Error != 0) {
        tn_log_s2err("S2_DEVICEQUERY", io->ios2_Req.io_Error, io->ios2_WireError);
        return TN_S2_QUERY_FAIL;
    }

    nif->mtu        = q.MTU;
    nif->addr_bits  = (UWORD)q.AddrFieldSize;
    nif->addr_bytes = (UWORD)((nif->addr_bits + 7) / 8);
    if (nif->addr_bytes > SANA2_MAX_ADDR_BYTES)
        nif->addr_bytes = SANA2_MAX_ADDR_BYTES;

    return TN_S2_OK;
}

TnS2Result tn_s2_online(TnSana2If *nif, const UBYTE *mac)
{
    struct IOSana2Req *io;
    TnS2Result r;
    ULONG i;
    BOOL mac_valid = FALSE;

    if (nif == NULL || nif->io == NULL) return TN_S2_INTERNAL;

    r = tn_s2_query(nif);
    if (r != TN_S2_OK) return r;

    io = nif->io;

    /* S2_GETSTATIONADDRESS */
    io->ios2_Req.io_Command = S2_GETSTATIONADDRESS;
    io->ios2_Req.io_Error   = 0;
    tn_log(TN_LOG_VERBOSE, "s2: GETSTATIONADDRESS...\n");
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log_s2err("S2_GETSTATIONADDRESS", io->ios2_Req.io_Error, io->ios2_WireError);
    } else {
        BOOL src_is_bcast = TRUE;
        for (i = 0; i < nif->addr_bytes; i++) {
            if (io->ios2_SrcAddr[i] != 0xFF) {
                src_is_bcast = FALSE;
                break;
            }
        }
        if (src_is_bcast) {
            for (i = 0; i < nif->addr_bytes; i++) {
                nif->mac[i]   = io->ios2_DstAddr[i];
                nif->bcast[i] = io->ios2_SrcAddr[i];
            }
        } else {
            for (i = 0; i < nif->addr_bytes; i++) {
                nif->mac[i]   = io->ios2_SrcAddr[i];
                nif->bcast[i] = io->ios2_DstAddr[i];
            }
        }
    }

    if (mac != NULL) {
        for (i = 0; i < nif->addr_bytes; i++) {
            nif->mac[i] = mac[i];
        }
        mac_valid = TRUE;
    } else {
        for (i = 0; i < nif->addr_bytes; i++) {
            if (nif->mac[i] != 0x00 && nif->mac[i] != 0xFF) {
                mac_valid = TRUE;
                break;
            }
        }
    }

    if (!mac_valid && nif->addr_bytes == 6) {
        nif->mac[0] = 0x00;
        nif->mac[1] = 0x80;
        nif->mac[2] = 0x10;
        nif->mac[3] = 0x32;
        nif->mac[4] = 0x33;
        nif->mac[5] = 0x34;
    }

    for (i = 0; i < nif->addr_bytes; i++) {
        io->ios2_SrcAddr[i] = nif->mac[i];
    }

    io->ios2_Req.io_Command = S2_CONFIGINTERFACE;
    io->ios2_Req.io_Error   = 0;
    tn_log(TN_LOG_VERBOSE, "s2: CONFIGINTERFACE...\n");
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        if (io->ios2_Req.io_Error == S2ERR_BAD_STATE &&
            io->ios2_WireError == S2WERR_IS_CONFIGURED) {
            /* TNET-060: on a daemon restart (stop/start without reboot) most
             * drivers answer BAD_STATE/IS_CONFIGURED because the unit kept
             * our station address. Treat as success; the station address was
             * already read back above via S2_GETSTATIONADDRESS. */
            tn_log(TN_LOG_BASIC, "tolunnet: S2_CONFIGINTERFACE: already configured (ok)\n");
        } else {
            tn_log_s2err("S2_CONFIGINTERFACE", io->ios2_Req.io_Error, io->ios2_WireError);
            return TN_S2_CONFIG_FAIL;
        }
    }

    /* S2_TRACKTYPE for IPv4 and ARP */
    io->ios2_Req.io_Command = S2_TRACKTYPE;
    io->ios2_PacketType     = TN_ETHERTYPE_IPV4;
    io->ios2_Req.io_Error   = 0;
    DoIO((struct IORequest *)io);

    io->ios2_Req.io_Command = S2_TRACKTYPE;
    io->ios2_PacketType     = TN_ETHERTYPE_ARP;
    io->ios2_Req.io_Error   = 0;
    DoIO((struct IORequest *)io);

    io->ios2_Req.io_Command = S2_ONLINE;
    io->ios2_Req.io_Error   = 0;
    tn_log(TN_LOG_VERBOSE, "s2: ONLINE...\n");
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        if (io->ios2_Req.io_Error == S2ERR_BAD_STATE &&
            io->ios2_WireError == S2WERR_UNIT_ONLINE) {
            tn_log(TN_LOG_BASIC, "tolunnet: S2_ONLINE: already online (ok)\n");
        } else {
            tn_log_s2err("S2_ONLINE", io->ios2_Req.io_Error, io->ios2_WireError);
            return TN_S2_ONLINE_FAIL;
        }
    }

    nif->online = TRUE;
    return TN_S2_OK;
}

LONG tn_s2_send(TnSana2If *nif, const void *buf, LONG len,
                BOOL broadcast, ULONG packet_type, const UBYTE *dst_addr)
{
    struct IOSana2Req *io;
    if (nif == NULL || !nif->online || nif->io == NULL) return -1;

    io = nif->io;
    io->ios2_Req.io_Command = broadcast ? S2_BROADCAST : CMD_WRITE;
    io->ios2_PacketType     = packet_type;
    io->ios2_DataLength     = (ULONG)len;
    io->ios2_Data           = (APTR)buf;
    io->ios2_Req.io_Error   = 0;

    if (!broadcast && dst_addr != NULL) {
        CopyMem((CONST APTR)dst_addr, (APTR)io->ios2_DstAddr, nif->addr_bytes);
    }

    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log_s2err("tn_s2_send", io->ios2_Req.io_Error, io->ios2_WireError);
        return -1;
    }
    return len;
}

LONG tn_s2_recv(TnSana2If *nif, void *buf, ULONG buf_len,
                ULONG idx, UBYTE *src_addr)
{
    struct IOSana2Req *io;
    LONG flen;
    (void)idx;

    if (nif == NULL || !nif->online || nif->io == NULL) return -1;
    io = nif->io;

    io->ios2_Data           = buf;
    io->ios2_DataLength     = buf_len;
    io->ios2_PacketType     = TN_ETHERTYPE_IPV4;
    io->ios2_Req.io_Command = CMD_READ;
    io->ios2_Req.io_Error   = 0;
    DoIO((struct IORequest *)io);

    if (io->ios2_Req.io_Error != 0) {
        return -1;
    }
    flen = (LONG)io->ios2_DataLength;
    if (flen < 0) flen = 0;
    if ((ULONG)flen > buf_len) flen = (LONG)buf_len;
    if (src_addr != NULL) {
        CopyMem((CONST APTR)io->ios2_SrcAddr, (APTR)src_addr, nif->addr_bytes);
    }
    return flen;
}

TnS2Result tn_s2_arm_reads(TnSana2If *nif)
{
    ULONG i, k;
    ULONG buf_size;

    if (nif == NULL || !nif->online) return TN_S2_INTERNAL;
    if (nif->read_ios != NULL) return TN_S2_OK;

    buf_size = nif->mtu + 32;
    if (buf_size < 1600) buf_size = 1600;

    nif->read_ios = (struct IOSana2Req **)AllocVec(
        sizeof(struct IOSana2Req *) * TN_S2_NREADS, MEMF_CLEAR | MEMF_PUBLIC);
    if (nif->read_ios == NULL) return TN_S2_NO_MEM;
    nif->n_read_ios = TN_S2_NREADS;

    for (i = 0; i < TN_S2_NREADS; i++) {
        struct IOSana2Req *rio;
        UBYTE *rbuf;

        rio = (struct IOSana2Req *)tn_create_extio(nif->rx_port,
                                                   sizeof(struct IOSana2Req));
        if (rio == NULL) {
            /* Unwind previous allocations */
            for (k = 0; k < i; k++) {
                if (nif->read_armed[k]) {
                    AbortIO((struct IORequest *)nif->read_ios[k]);
                    WaitIO((struct IORequest *)nif->read_ios[k]);
                    nif->read_armed[k] = FALSE;
                }
                if (nif->read_bufs[k] != NULL) {
                    FreeVec(nif->read_bufs[k]);
                    nif->read_bufs[k] = NULL;
                }
                tn_delete_extio((struct IORequest *)nif->read_ios[k]);
                nif->read_ios[k] = NULL;
            }
            FreeVec(nif->read_ios);
            nif->read_ios = NULL;
            nif->n_read_ios = 0;
            return TN_S2_NO_MEM;
        }

        rbuf = (UBYTE *)AllocVec(buf_size, MEMF_CLEAR | MEMF_PUBLIC);
        if (rbuf == NULL) {
            tn_delete_extio((struct IORequest *)rio);
            for (k = 0; k < i; k++) {
                if (nif->read_armed[k]) {
                    AbortIO((struct IORequest *)nif->read_ios[k]);
                    WaitIO((struct IORequest *)nif->read_ios[k]);
                    nif->read_armed[k] = FALSE;
                }
                if (nif->read_bufs[k] != NULL) {
                    FreeVec(nif->read_bufs[k]);
                    nif->read_bufs[k] = NULL;
                }
                tn_delete_extio((struct IORequest *)nif->read_ios[k]);
                nif->read_ios[k] = NULL;
            }
            FreeVec(nif->read_ios);
            nif->read_ios = NULL;
            nif->n_read_ios = 0;
            return TN_S2_NO_MEM;
        }

        rio->ios2_Req.io_Device    = nif->io->ios2_Req.io_Device;
        rio->ios2_Req.io_Unit      = nif->io->ios2_Req.io_Unit;
        rio->ios2_BufferManagement = nif->io->ios2_BufferManagement;
        rio->ios2_Req.io_Command   = CMD_READ;
        rio->ios2_Data             = rbuf;
        rio->ios2_DataLength       = buf_size;

        if ((i & 1) == 0) {
            rio->ios2_PacketType = TN_ETHERTYPE_IPV4;
        } else {
            rio->ios2_PacketType = TN_ETHERTYPE_ARP;
        }

        nif->read_ios[i]  = rio;
        nif->read_bufs[i] = rbuf;

        SendIO((struct IORequest *)rio);
        nif->read_armed[i] = TRUE;
    }
    return TN_S2_OK;
}

/* ------------------------------------------------- S2_ONEVENT (TNET-109) */
/* Max event completions handled per poll call (storm defense). */
#define TN_S2_EVENT_BATCH 4

TnS2Result tn_s2_arm_events(TnSana2If *nif, ULONG mask)
{
    struct IOSana2Req *eio;

    if (nif == NULL || nif->io == NULL || !nif->online) return TN_S2_INTERNAL;
    if (mask == 0) mask = TN_S2EV_DEFAULT;
    if (nif->event_io != NULL) return TN_S2_OK; /* already armed */

    nif->event_port = CreateMsgPort();
    if (nif->event_port == NULL) return TN_S2_NO_MEM;

    eio = (struct IOSana2Req *)tn_create_extio(nif->event_port,
                                                sizeof(struct IOSana2Req));
    if (eio == NULL) {
        DeleteMsgPort(nif->event_port);
        nif->event_port = NULL;
        return TN_S2_NO_MEM;
    }

    eio->ios2_Req.io_Device    = nif->io->ios2_Req.io_Device;
    eio->ios2_Req.io_Unit      = nif->io->ios2_Req.io_Unit;
    eio->ios2_BufferManagement = nif->io->ios2_BufferManagement;

    nif->event_io = eio;
    nif->event_mask = mask;
    nif->event_supported = TRUE;

    eio->ios2_Req.io_Command = S2_ONEVENT;
    eio->ios2_WireError      = mask;   /* SANA-II: request mask in */
    eio->ios2_Req.io_Error   = 0;
    SendIO((struct IORequest *)eio);
    nif->event_armed = TRUE;
    return TN_S2_OK;
}

ULONG tn_s2_event_sig(const TnSana2If *nif)
{
    if (nif == NULL || nif->event_port == NULL || !nif->event_supported) return 0;
    return (1UL << nif->event_port->mp_SigBit);
}

/* Re-arm unarmed CMD_READ slots. Called on the ONLINE event and once per
 * 100 ms timer tick: error-completed reads are NOT re-armed inline (that is
 * a livelock under a driver that completes them instantly while offline),
 * so this is the single bounded re-arm path. (TNET-109) */
void tn_s2_rearm_reads(TnSana2If *nif)
{
    ULONG i;
    if (nif == NULL || nif->link_down) return;
    for (i = 0; i < nif->n_read_ios; i++) {
        struct IOSana2Req *rio = nif->read_ios[i];
        if (rio != NULL && !nif->read_armed[i]) {
            rio->ios2_Req.io_Command = CMD_READ;
            rio->ios2_DataLength     = nif->mtu + 32;
            if (rio->ios2_DataLength < 1600) rio->ios2_DataLength = 1600;
            rio->ios2_Req.io_Error   = 0;
            SendIO((struct IORequest *)rio);
            nif->read_armed[i] = TRUE;
        }
    }
}

static void tn_s2_handle_event_bits(TnSana2If *nif, struct netif *netif, ULONG bits)
{
    if (bits == 0) return;

    if (bits & S2EVENT_OFFLINE) {
        if (!nif->link_down) {
            nif->link_down = TRUE;
            netif_set_link_down(netif);
            tn_log(TN_LOG_BASIC, "tolunnet: S2 link DOWN (offline event)\n");
        }
    }

    if (bits & S2EVENT_ONLINE) {
        if (nif->link_down) {
            nif->link_down = FALSE;
            netif_set_link_up(netif);
            tn_s2_rearm_reads(nif);
            if (g_daemon.prefs.use_dhcp) {
#if LWIP_DHCP
                if (netif_dhcp_data(netif) != NULL) {
                    dhcp_renew(netif);
                    tn_log(TN_LOG_BASIC, "tolunnet: link up: DHCP renew requested\n");
                } else {
                    dhcp_start(netif);
                    tn_log(TN_LOG_BASIC, "tolunnet: link up: DHCP client started\n");
                }
#endif
            } else {
                netif_set_up(netif);
                tn_log(TN_LOG_BASIC, "tolunnet: S2 link UP (online event)\n");
            }
        }
    }

    if (bits & (S2EVENT_ERROR | S2EVENT_TX | S2EVENT_RX |
                S2EVENT_BUFF | S2EVENT_HARDWARE | S2EVENT_SOFTWARE)) {
        g_daemon.s2_link_errors++;
        tn_logf(TN_LOG_BASIC, "tolunnet: S2 error event bits 0x%lx\n",
                (ULONG)(bits & (S2EVENT_ERROR | S2EVENT_TX | S2EVENT_RX |
                                S2EVENT_BUFF | S2EVENT_HARDWARE |
                                S2EVENT_SOFTWARE)));
    }
}

void tn_s2_poll_events(TnSana2If *nif, struct netif *netif)
{
    struct Message *msg;
    int handled = 0;

    if (nif == NULL || nif->event_port == NULL || netif == NULL) return;

    while ((msg = GetMsg(nif->event_port)) != NULL) {
        struct IOSana2Req *eio = (struct IOSana2Req *)msg;
        nif->event_armed = FALSE;

        if (eio->ios2_Req.io_Error != 0) {
            /* Driver rejected S2_ONEVENT (e.g. IOERR_NOCMD / NOT_SUPPORTED):
             * record once, stop re-arming, keep the stack running. */
            if (nif->event_supported) {
                nif->event_supported = FALSE;
                tn_logf(TN_LOG_BASIC,
                        "tolunnet: S2_ONEVENT not supported by driver "
                        "(err=%ld wire=%ld); link events disabled\n",
                        (LONG)eio->ios2_Req.io_Error, (LONG)eio->ios2_WireError);
            }
            continue;
        }

        tn_s2_handle_event_bits(nif, netif, eio->ios2_WireError);

        /* Storm defense (bench incident 556ea30): uaenet completes S2_ONEVENT
         * instantly, alternating ONLINE/OFFLINE per request, which turns the
         * drain-and-rearm loop into a livelock that starves the whole system.
         * Cap the batch; three capped batches in a row (i.e. a sustained
         * instant-completion rate no physical link can produce) disables
         * event tracking for this driver for good. */
        if (++handled >= TN_S2_EVENT_BATCH) {
            if (++nif->event_strikes >= 3) {
                nif->event_supported = FALSE;
                tn_log(TN_LOG_BASIC,
                       "tolunnet: S2_ONEVENT storm from driver "
                       "(instant completions); link events disabled\n");
                return;
            }
            /* Re-arm once and yield back to the main loop. */
            eio->ios2_Req.io_Command = S2_ONEVENT;
            eio->ios2_WireError      = nif->event_mask;
            eio->ios2_Req.io_Error   = 0;
            SendIO((struct IORequest *)eio);
            nif->event_armed = TRUE;
            return;
        }

        /* Re-arm at once per SANA-II Rev 7 */
        eio->ios2_Req.io_Command = S2_ONEVENT;
        eio->ios2_WireError      = nif->event_mask;
        eio->ios2_Req.io_Error   = 0;
        SendIO((struct IORequest *)eio);
        nif->event_armed = TRUE;
    }

    /* Strikes are monotonic by design: three capped batches at any time is
     * an instant-completion driver; genuine transitions never fill a batch. */
}

void tn_s2_offline_close(TnSana2If *nif)
{
    ULONG i;
    if (nif == NULL) return;

    /* TNET-109: retire the event request BEFORE the CMD_READs and before
     * S2_OFFLINE/CloseDevice, so the driver never sees the unit torn down
     * under an armed S2_ONEVENT. */
    if (nif->event_io != NULL) {
        if (nif->event_armed) {
            if (!CheckIO((struct IORequest *)nif->event_io)) {
                AbortIO((struct IORequest *)nif->event_io);
            }
            WaitIO((struct IORequest *)nif->event_io);
            nif->event_armed = FALSE;
        }
        tn_delete_extio((struct IORequest *)nif->event_io);
        nif->event_io = NULL;
    }
    if (nif->event_port != NULL) {
        struct Message *m;
        while ((m = GetMsg(nif->event_port)) != NULL) {}
        DeleteMsgPort(nif->event_port);
        nif->event_port = NULL;
    }

    if (nif->io != NULL && nif->online) {
        nif->io->ios2_Req.io_Command = S2_UNTRACKTYPE;
        nif->io->ios2_PacketType     = TN_ETHERTYPE_IPV4;
        DoIO((struct IORequest *)nif->io);

        nif->io->ios2_Req.io_Command = S2_UNTRACKTYPE;
        nif->io->ios2_PacketType     = TN_ETHERTYPE_ARP;
        DoIO((struct IORequest *)nif->io);

        nif->io->ios2_Req.io_Command = S2_OFFLINE;
        nif->io->ios2_Req.io_Error   = 0;
        DoIO((struct IORequest *)nif->io);
        nif->online = FALSE;
    }

    if (nif->read_ios != NULL) {
        for (i = 0; i < nif->n_read_ios; i++) {
            struct IOSana2Req *rio = nif->read_ios[i];
            if (rio != NULL) {
                if (nif->read_armed[i]) {
                    if (!CheckIO((struct IORequest *)rio)) {
                        AbortIO((struct IORequest *)rio);
                    }
                    WaitIO((struct IORequest *)rio);
                    nif->read_armed[i] = FALSE;
                }
                if (nif->read_bufs[i] != NULL) {
                    FreeVec(nif->read_bufs[i]);
                    nif->read_bufs[i] = NULL;
                }
                tn_delete_extio((struct IORequest *)rio);
                nif->read_ios[i] = NULL;
            }
        }
        FreeVec(nif->read_ios);
        nif->read_ios = NULL;
        nif->n_read_ios = 0;
    }

    if (nif->io != NULL) {
        CloseDevice((struct IORequest *)nif->io);
        tn_delete_extio((struct IORequest *)nif->io);
        nif->io = NULL;
    }

    if (nif->rx_port != NULL) {
        struct Message *m;
        while ((m = GetMsg(nif->rx_port)) != NULL) {}
        DeleteMsgPort(nif->rx_port);
        nif->rx_port = NULL;
    }
    if (nif->tx_port != NULL) {
        struct Message *m;
        while ((m = GetMsg(nif->tx_port)) != NULL) {}
        DeleteMsgPort(nif->tx_port);
        nif->tx_port = NULL;
    }
}

BOOL tn_s2_add_multicast(TnSana2If *nif, const UBYTE *mac)
{
    BYTE err;
    UWORD i;
    if (nif == NULL || nif->io == NULL || mac == NULL || !nif->online) return FALSE;
    nif->io->ios2_Req.io_Command = S2_ADDMULTICASTADDRESS;
    for (i = 0; i < nif->addr_bytes && i < SANA2_MAX_ADDR_BYTES; i++) {
        nif->io->ios2_SrcAddr[i] = mac[i];
    }
    err = DoIO((struct IORequest *)nif->io);
    return (err == 0);
}

BOOL tn_s2_del_multicast(TnSana2If *nif, const UBYTE *mac)
{
    BYTE err;
    UWORD i;
    if (nif == NULL || nif->io == NULL || mac == NULL || !nif->online) return FALSE;
    nif->io->ios2_Req.io_Command = S2_DELMULTICASTADDRESS;
    for (i = 0; i < nif->addr_bytes && i < SANA2_MAX_ADDR_BYTES; i++) {
        nif->io->ios2_SrcAddr[i] = mac[i];
    }
    err = DoIO((struct IORequest *)nif->io);
    return (err == 0);
}

#if LWIP_IGMP
static err_t tn_sana2_igmp_mac_filter(struct netif *netif, const ip4_addr_t *group, enum netif_mac_filter_action action)
{
    UBYTE mac[6];
    u32_t ip;
    TnSana2If *s2if;
    if (netif == NULL || group == NULL) return ERR_VAL;
    s2if = (TnSana2If *)netif->state;
    if (s2if == NULL) return ERR_VAL;

    ip = lwip_ntohl(group->addr);
    mac[0] = 0x01;
    mac[1] = 0x00;
    mac[2] = 0x5E;
    mac[3] = (UBYTE)((ip >> 16) & 0x7F);
    mac[4] = (UBYTE)((ip >> 8) & 0xFF);
    mac[5] = (UBYTE)(ip & 0xFF);

    if (action == NETIF_ADD_MAC_FILTER) {
        tn_s2_add_multicast(s2if, mac);
    } else if (action == NETIF_DEL_MAC_FILTER) {
        tn_s2_del_multicast(s2if, mac);
    }
    return ERR_OK;
}
#endif

/* --------------------------------------------------------------- lwIP bridge */

/* TNET-109: LWIP_NETIF_LINK_CALLBACK plumbing - mirrors link transitions at
 * VERBOSE so callback registration is observable in the daemon log. */
static void tn_sana2_link_callback(struct netif *netif)
{
    tn_logf(TN_LOG_VERBOSE, "tolunnet: netif link callback: %s\n",
            netif_is_link_up(netif) ? "up" : "down");
}

err_t tn_sana2_netif_init(struct netif *netif)
{
    TnSana2If *nif = (TnSana2If *)netif->state;
    int i;

    if (nif == NULL) return ERR_ARG;

    netif->name[0] = 'e';
    netif->name[1] = 't';
    netif->output = etharp_output;
    netif->linkoutput = tn_sana2_linkoutput;
    netif->mtu = (u16_t)nif->mtu;
    netif->hwaddr_len = 6;

    for (i = 0; i < 6; i++) {
        netif->hwaddr[i] = nif->mac[i];
    }

    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET |
                   NETIF_FLAG_IGMP | NETIF_FLAG_LINK_UP;

#if LWIP_NETIF_LINK_CALLBACK
    netif_set_link_callback(netif, tn_sana2_link_callback);
#endif

#if LWIP_IGMP
    netif_set_igmp_mac_filter(netif, tn_sana2_igmp_mac_filter);
#endif

    return ERR_OK;
}

err_t tn_sana2_linkoutput(struct netif *netif, struct pbuf *p)
{
    TnSana2If *nif = (TnSana2If *)netif->state;
    static UBYTE tx_buf[1600];
    u16_t copied;
    const UBYTE *dst_mac;
    ULONG ethertype;
    BOOL is_bcast;
    LONG sent;

    if (nif == NULL || !nif->online || p == NULL) return ERR_IF;

#if ETH_PAD_SIZE
    pbuf_remove_header(p, ETH_PAD_SIZE);
#endif

    if (p->tot_len < 14 || p->tot_len > sizeof(tx_buf)) {
#if ETH_PAD_SIZE
        pbuf_add_header(p, ETH_PAD_SIZE);
#endif
        g_daemon.s2_tx_drops++;
        return ERR_BUF;
    }

    copied = pbuf_copy_partial(p, tx_buf, p->tot_len, 0);
    if (copied != p->tot_len) {
#if ETH_PAD_SIZE
        pbuf_add_header(p, ETH_PAD_SIZE);
#endif
        g_daemon.s2_tx_drops++;
        return ERR_BUF;
    }

    dst_mac = &tx_buf[0];
    ethertype = ((ULONG)tx_buf[12] << 8) | (ULONG)tx_buf[13];
    is_bcast = (dst_mac[0] == 0xFF && dst_mac[1] == 0xFF &&
                dst_mac[2] == 0xFF && dst_mac[3] == 0xFF &&
                dst_mac[4] == 0xFF && dst_mac[5] == 0xFF) ? TRUE : FALSE;

    sent = tn_s2_send(nif, &tx_buf[14], (LONG)(p->tot_len - 14),
                      is_bcast, ethertype, dst_mac);

#if ETH_PAD_SIZE
    pbuf_add_header(p, ETH_PAD_SIZE);
#endif

    if (sent < 0) {
        g_daemon.s2_tx_drops++;
        return ERR_IF;
    }
    g_daemon.s2_tx_frames++;
    g_daemon.s2_tx_bytes += (p->tot_len - 14);
    return ERR_OK;
}

void tn_sana2_poll_input(TnSana2If *nif, struct netif *netif)
{
    struct Message *msg;
    if (nif == NULL || nif->rx_port == NULL || netif == NULL) return;

    while ((msg = GetMsg(nif->rx_port)) != NULL) {
        struct IOSana2Req *rio = (struct IOSana2Req *)msg;
        ULONG i;
        for (i = 0; i < nif->n_read_ios; i++) {
            if (nif->read_ios[i] == rio) {
                nif->read_armed[i] = FALSE;
                break;
            }
        }

        if (rio->ios2_Req.io_Error != 0) {
            /* Error completion (e.g. unit went offline and the event stream
             * is unavailable): leave the slot unarmed; the timer tick or the
             * ONLINE handler re-arms it. Inline re-arm would spin. */
            continue;
        }

        if (rio->ios2_Req.io_Error == 0 && rio->ios2_DataLength > 0) {
            ULONG flen = rio->ios2_DataLength;
            BOOL valid_packet = TRUE;

            /* 1. Boundary & MTU bounds validation */
            if (flen > nif->mtu || flen > (1600 - 14)) {
                valid_packet = FALSE;
            }

            /* 2. EtherType Whitelist (IPv4 & ARP only) */
            if (rio->ios2_PacketType != TN_ETHERTYPE_IPV4 &&
                rio->ios2_PacketType != TN_ETHERTYPE_ARP) {
                valid_packet = FALSE;
            }

            /* 3. Source MAC validation (RFC: multicast source, all-0, all-FF are invalid) */
            if (valid_packet) {
                const UBYTE *s = rio->ios2_SrcAddr;
                if ((s[0] & 1) != 0 ||
                    (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0 && s[4] == 0 && s[5] == 0) ||
                    (s[0] == 0xFF && s[1] == 0xFF && s[2] == 0xFF && s[3] == 0xFF && s[4] == 0xFF && s[5] == 0xFF)) {
                    valid_packet = FALSE;
                }
            }

            if (valid_packet) {
                ULONG total_len = flen + 14;
                /* TNET-085 / ETH_PAD_SIZE: allocate pad bytes and advance payload so
                 * the ethernet header sits at (payload % 4) == 2 and the IP header
                 * at +14 is 4-aligned. Reclaim padding before passing to netif->input. */
                struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)(total_len + ETH_PAD_SIZE), PBUF_POOL);
                if (p != NULL) {
#if ETH_PAD_SIZE
                    if (pbuf_remove_header(p, ETH_PAD_SIZE) != 0) {
                        pbuf_free(p);
                        g_daemon.s2_rx_drops++;
                        continue;
                    }
#endif
                    UBYTE *dst = (UBYTE *)p->payload;

                    CopyMem((CONST APTR)rio->ios2_DstAddr, (APTR)&dst[0], 6);
                    CopyMem((CONST APTR)rio->ios2_SrcAddr, (APTR)&dst[6], 6);
                    dst[12] = (UBYTE)((rio->ios2_PacketType >> 8) & 0xFF);
                    dst[13] = (UBYTE)(rio->ios2_PacketType & 0xFF);
                    CopyMem((CONST APTR)rio->ios2_Data, (APTR)&dst[14], flen);

#if ETH_PAD_SIZE
                    pbuf_add_header(p, ETH_PAD_SIZE);
#endif

                    g_daemon.s2_rx_frames++;
                    g_daemon.s2_rx_bytes += flen;
                    if (netif->input(p, netif) != ERR_OK) {
                        pbuf_free(p);
                        g_daemon.s2_rx_drops++;
                    }
                } else {
                    g_daemon.s2_rx_drops++;
                }
            } else {
                g_daemon.s2_rx_drops++;
            }
        }

        /* Re-arm the I/O slot (TNET-109: while the link is down the driver
         * would complete reads with errors immediately; the ONLINE event
         * handler re-arms via tn_s2_rearm_reads instead). */
        if (!nif->link_down) {
            rio->ios2_Req.io_Command = CMD_READ;
            rio->ios2_DataLength     = nif->mtu + 32;
            if (rio->ios2_DataLength < 1600) rio->ios2_DataLength = 1600;
            rio->ios2_Req.io_Error   = 0;
            SendIO((struct IORequest *)rio);
            if (i < nif->n_read_ios) {
                nif->read_armed[i] = TRUE;
            }
        }
    }
}
