/*
 * tolunet — SANA-II netif glue and lwIP adapter implementation.
 */

#include "sana2_netif.h"
#include "../common/log.h"

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
    tn_logf(TN_LOG_BASIC, "tolunet: %s failed err=%ld wire=%ld\n",
            step, err, wire);
}

/* Forward declare assembly trampolines (TNET-006) */
extern void tn_s2_copy_to_buff_asm(void);
extern void tn_s2_copy_from_buff_asm(void);

__saveds BOOL tn_copy_to_buff_c(APTR dst, APTR src, ULONG len)
{
    if (dst != NULL && src != NULL && len > 0) {
        CopyMem(src, dst, len);
    }
    return TRUE;
}

__saveds BOOL tn_copy_from_buff_c(APTR dst, APTR src, ULONG len)
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

    err = OpenDevice((STRPTR)device_name, unit, (struct IORequest *)io, 0UL);
    if (err != 0) {
        tn_logf(TN_LOG_BASIC, "tolunet: OpenDevice(%s, %lu) failed err=%d\n",
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
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log_s2err("S2_CONFIGINTERFACE", io->ios2_Req.io_Error, io->ios2_WireError);
        return TN_S2_CONFIG_FAIL;
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
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        if (io->ios2_Req.io_Error == S2ERR_BAD_STATE &&
            io->ios2_WireError == S2WERR_UNIT_ONLINE) {
            tn_log(TN_LOG_BASIC, "tolunet: S2_ONLINE: already online (ok)\n");
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

void tn_s2_offline_close(TnSana2If *nif)
{
    ULONG i;
    if (nif == NULL) return;

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
        DeleteMsgPort(nif->rx_port);
        nif->rx_port = NULL;
    }
    if (nif->tx_port != NULL) {
        DeleteMsgPort(nif->tx_port);
        nif->tx_port = NULL;
    }
}

/* --------------------------------------------------------------- lwIP bridge */

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
    if (p->tot_len < 14 || p->tot_len > sizeof(tx_buf)) return ERR_BUF;

    copied = pbuf_copy_partial(p, tx_buf, p->tot_len, 0);
    if (copied != p->tot_len) return ERR_BUF;

    dst_mac = &tx_buf[0];
    ethertype = ((ULONG)tx_buf[12] << 8) | (ULONG)tx_buf[13];
    is_bcast = (dst_mac[0] == 0xFF && dst_mac[1] == 0xFF &&
                dst_mac[2] == 0xFF && dst_mac[3] == 0xFF &&
                dst_mac[4] == 0xFF && dst_mac[5] == 0xFF) ? TRUE : FALSE;

    sent = tn_s2_send(nif, &tx_buf[14], (LONG)(p->tot_len - 14),
                      is_bcast, ethertype, dst_mac);
    if (sent < 0) {
        return ERR_IF;
    }
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
                struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)total_len, PBUF_POOL);
                if (p != NULL) {
                    UBYTE *dst = (UBYTE *)p->payload;

                    CopyMem((CONST APTR)rio->ios2_DstAddr, (APTR)&dst[0], 6);
                    CopyMem((CONST APTR)rio->ios2_SrcAddr, (APTR)&dst[6], 6);
                    dst[12] = (UBYTE)((rio->ios2_PacketType >> 8) & 0xFF);
                    dst[13] = (UBYTE)(rio->ios2_PacketType & 0xFF);
                    CopyMem((CONST APTR)rio->ios2_Data, (APTR)&dst[14], flen);

                    if (netif->input(p, netif) != ERR_OK) {
                        pbuf_free(p);
                    }
                }
            }
        }

        /* Re-arm the I/O slot */
        rio->ios2_Req.io_Command = CMD_READ;
        rio->ios2_DataLength     = nif->mtu + 32;
        if (rio->ios2_DataLength < 1600) rio->ios2_DataLength = 1600;
        SendIO((struct IORequest *)rio);
        if (i < nif->n_read_ios) {
            nif->read_armed[i] = TRUE;
        }
    }
}
