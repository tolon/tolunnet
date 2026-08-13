/*
 * tolunet — SANA-II netif glue implementation (M1, UNPROVEN).
 *
 * Master prompt §M1. Implements: shared device open (never exclusive), copyfunc
 * tag list, S2_DEVICEQUERY (MTU/address), S2_CONFIGINTERFACE + S2_ONLINE,
 * >=4 outstanding async CMD_READ, S2_BROADCAST send, and clean S2_OFFLINE +
 * AbortIO/WaitIO + CloseDevice shutdown.
 *
 * All SANA-II constants/structs are confirmed against the Roadshow SDK 1.8
 * header include/devices/sana2.h (Rev 7 normative; QUESTIONS.md #15 closed).
 * Field names used: IOSana2Req{ios2_Req,ios2_WireError,ios2_PacketType,
 * ios2_SrcAddr,ios2_DstAddr,ios2_DataLength,ios2_Data,ios2_StatData,
 * ios2_BufferManagement}; Sana2DeviceQuery{SizeAvailable,SizeSupplied,
 * DevQueryFormat,DeviceLevel,AddrFieldSize,MTU,...}; S2_CopyToBuff/
 * S2_CopyFromBuff tags; SANA2_MAX_ADDR_BYTES; S2_DEVICEQUERY/CONFIGINTERFACE/
 * ONLINE/OFFLINE/BROADCAST; S2ERR_*/S2WERR_* error codes.
 *
 * Not yet wired into the M0 Makefile (which builds only the hello-task); M1
 * wiring lands once this compiles under amiga-gcc and the SANA-II device on
 * the bench (a2065/uaenet) is reachable.
 */

#include "sana2_netif.h"
#include "../common/log.h"

#include <proto/exec.h>
#include <proto/dos.h>

#include <exec/memory.h>
#include <exec/io.h>
#include <utility/tagitem.h>   /* struct TagItem, TAG_DONE */
#include <devices/sana2.h>     /* verified vs Roadshow SDK 1.8 */

/* How many CMD_READ requests we keep outstanding (§M1: ">=4"). */
#define TN_S2_NREADS  4

/* EtherType values (big-endian on the wire, bytes 12-13 of an Ethernet frame). */
#define TN_ETHERTYPE_IPV4 0x0800U
#define TN_ETHERTYPE_ARP  0x0806U

/* ------------------------------------------------------------------ copyfuncs
 * The driver calls these to move bytes between its packet and our buffer.
 * S2_CopyToBuff:   driver packet -> our buffer  (receive path).
 * S2_CopyFromBuff: our buffer -> driver packet  (send path).
 * The SANA-II spec does not fix a C typedef for these; the tag carries a raw
 * function pointer. Our signature matches what every known SANA-II driver
 * expects: (APTR dst, APTR src, ULONG len) -> LONG bytes copied.
 */
typedef LONG (*TnS2CopyFunc)(APTR dst, APTR src, ULONG len);

static LONG tn_copy_to_buff(APTR dst, APTR src, ULONG len)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;
    ULONG i;
    for (i = 0; i < len; i++) d[i] = s[i];
    return (LONG)len;
}

static LONG tn_copy_from_buff(APTR dst, APTR src, ULONG len)
{
    return tn_copy_to_buff(dst, src, len);   /* symmetric for plain memcpy */
}

/* --------------------------------------------------------------- s2_open
 * Open the device shared (NEVER exclusive — §M1). Drivers REQUIRE copyfuncs in
 * the buffer-management tag list or OpenDevice fails.
 */
TnS2Result tn_s2_open(TnSana2If *nif, CONST_STRPTR device_name, ULONG unit)
{
    struct TagItem bm_tags[3];
    struct MsgPort *port;
    struct IOSana2Req *io;
    BYTE err;

    if (nif == NULL || device_name == NULL) return TN_S2_INTERNAL;

    nif->device_name = (STRPTR)device_name;   /* caller owns the string */
    nif->unit = unit;
    nif->online = FALSE;
    nif->read_ios = NULL;
    nif->n_read_ios = 0;
    nif->mtu = 0;
    nif->addr_bits = 0;
    nif->addr_bytes = 0;

    /* Reply port for async I/O (CreateMsgPort is exec V36+, same as CreatePort). */
    port = CreateMsgPort();
    if (port == NULL) return TN_S2_NO_MEM;
    nif->reply_port = port;

    /* CreateExtIO fills in mn_ReplyPort, mn_Length and ln_Type=NT_MESSAGE. */
    io = (struct IOSana2Req *)CreateExtIO(port, sizeof(struct IOSana2Req));
    if (io == NULL) {
        DeleteMsgPort(port);
        nif->reply_port = NULL;
        return TN_S2_NO_MEM;
    }
    nif->io = io;

    /* Buffer-management tag list: drivers refuse to open without both funcs. */
    bm_tags[0].ti_Tag   = S2_CopyToBuff;
    bm_tags[0].ti_Data  = (ULONG)(IPTR)tn_copy_to_buff;
    bm_tags[1].ti_Tag   = S2_CopyFromBuff;
    bm_tags[1].ti_Data  = (ULONG)(IPTR)tn_copy_from_buff;
    bm_tags[2].ti_Tag   = TAG_DONE;

    /* OpenDevice reads ios2_BufferManagement as the tag list before opening. */
    io->ios2_BufferManagement = bm_tags;

    /* Shared open: flags = 0 (NOT SANA2OPF_MINE). §M1: never exclusive. */
    err = OpenDevice((STRPTR)device_name, unit, (struct IORequest *)io, 0UL);
    if (err != 0) {
        tn_log(TN_LOG_BASIC, "tolunet: OpenDevice failed\n");
        DeleteExtIO((struct IORequest *)io);
        nif->io = NULL;
        DeleteMsgPort(port);
        nif->reply_port = NULL;
        return TN_S2_OPEN_FAIL;
    }

    /* The driver may overwrite ios2_BufferManagement with its magic cookie;
     * keep whatever it set and use that in all future requests. */
    return TN_S2_OK;
}

/* --------------------------------------------------------------- s2_query
 * Ask the driver for MTU and address size. Fills nif->mtu / addr_bits/bytes.
 */
static TnS2Result tn_s2_query(TnSana2If *nif)
{
    struct Sana2DeviceQuery q;
    struct IOSana2Req *io = nif->io;

    q.SizeAvailable  = sizeof(q);   /* how much room the driver may fill */
    q.SizeSupplied   = 0;
    q.DevQueryFormat = 0;           /* Rev 7: "this is type 0" */
    q.DeviceLevel    = 0;           /* Rev 7: "level 0" */

    io->ios2_Req.io_Command = S2_DEVICEQUERY;
    io->ios2_StatData       = &q;
    io->ios2_Req.io_Error   = 0;

    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log(TN_LOG_BASIC, "tolunet: S2_DEVICEQUERY failed\n");
        return TN_S2_QUERY_FAIL;
    }

    nif->mtu       = q.MTU;                    /* max packet data size (bytes) */
    nif->addr_bits = (UWORD)q.AddrFieldSize;   /* address size in BITS */
    nif->addr_bytes = (UWORD)((nif->addr_bits + 7) / 8);
    if (nif->addr_bytes > SANA2_MAX_ADDR_BYTES)
        nif->addr_bytes = SANA2_MAX_ADDR_BYTES;

    return TN_S2_OK;
}

/* --------------------------------------------------------------- s2_online
 * Configure the interface (set MAC) then bring it online. If mac is NULL we
 * leave the station address at the driver default; §M1 "configure address".
 * Some drivers require S2_CONFIGINTERFACE regardless before S2_ONLINE.
 */
TnS2Result tn_s2_online(TnSana2If *nif, const UBYTE *mac)
{
    struct IOSana2Req *io;
    TnS2Result r;

    if (nif == NULL || nif->io == NULL) return TN_S2_INTERNAL;

    r = tn_s2_query(nif);
    if (r != TN_S2_OK) return r;

    io = nif->io;

    /* S2_CONFIGINTERFACE: set the hardware station address (ios2_SrcAddr). */
    if (mac != NULL) {
        ULONG i;
        for (i = 0; i < nif->addr_bytes; i++)
            io->ios2_SrcAddr[i] = mac[i];
    }
    io->ios2_Req.io_Command = S2_CONFIGINTERFACE;
    io->ios2_Req.io_Error   = 0;
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log(TN_LOG_BASIC, "tolunet: S2_CONFIGINTERFACE failed\n");
        return TN_S2_CONFIG_FAIL;
    }

    /* S2_ONLINE: bring the interface up for active traffic. */
    io->ios2_Req.io_Command = S2_ONLINE;
    io->ios2_Req.io_Error   = 0;
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log(TN_LOG_BASIC, "tolunet: S2_ONLINE failed\n");
        return TN_S2_ONLINE_FAIL;
    }

    /* Capture the negotiated station address back for diagnostics. */
    {
        ULONG i;
        for (i = 0; i < nif->addr_bytes; i++)
            nif->mac[i] = io->ios2_SrcAddr[i];
    }

    nif->online = TRUE;
    return TN_S2_OK;
}

/* --------------------------------------------------------------- s2_send
 * Send one frame. broadcast=TRUE -> S2_BROADCAST; else CMD_WRITE. packet_type
 * is the EtherType (e.g. TN_ETHERTYPE_IPV4). Returns bytes sent or negative.
 */
LONG tn_s2_send(TnSana2If *nif, const void *buf, LONG len,
                BOOL broadcast, ULONG packet_type)
{
    struct IOSana2Req *io;
    if (nif == NULL || !nif->online) return -1;

    /* Reuse the primary io for synchronous send (M1: low throughput ok). */
    io = nif->io;
    io->ios2_Req.io_Command = broadcast ? S2_BROADCAST : CMD_WRITE;
    io->ios2_PacketType     = packet_type;
    io->ios2_DataLength     = (ULONG)len;
    io->ios2_Data           = (APTR)buf;
    io->ios2_Req.io_Error   = 0;

    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        return -1;
    }
    return len;
}

/* --------------------------------------------------------------- s2_recv
 * Blocking receive on one read slot (idx into read_ios). The copyfunc writes
 * the frame into buf (<= buf_len bytes); returns the on-wire length. M2 will
 * switch this to an async pump + completion-signal callback into the task.
 */
LONG tn_s2_recv(TnSana2If *nif, void *buf, ULONG buf_len,
                ULONG idx, UBYTE *src_addr)
{
    struct IOSana2Req *io;
    if (nif == NULL || !nif->online) return -1;
    if (nif->read_ios == NULL || idx >= nif->n_read_ios) return -1;
    io = nif->read_ios[idx];

    io->ios2_Data       = buf;          /* copyfunc writes here */
    io->ios2_DataLength = buf_len;
    io->ios2_Req.io_Error = 0;

    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        return -1;
    }
    if (src_addr != NULL) {
        ULONG i;
        for (i = 0; i < nif->addr_bytes; i++)
            src_addr[i] = io->ios2_SrcAddr[i];
    }
    return (LONG)io->ios2_DataLength;
}

/* --------------------------------------------------------------- arm_reads
 * Allocate and arm >=4 CMD_READ requests (async) so frames keep flowing. Each
 * read io duplicates the device context from the primary io (io_Device and the
 * buffer-management cookie). M1 leaves these armed and polls; M2 wires a
 * completion signal into the task's Wait().
 */
TnS2Result tn_s2_arm_reads(TnSana2If *nif)
{
    ULONG i;
    if (nif == NULL || !nif->online) return TN_S2_INTERNAL;
    if (nif->read_ios != NULL) return TN_S2_OK;  /* already armed */

    nif->read_ios = (struct IOSana2Req **)AllocVec(
        sizeof(struct IOSana2Req *) * TN_S2_NREADS, MEMF_CLEAR | MEMF_ANY);
    if (nif->read_ios == NULL) return TN_S2_NO_MEM;
    nif->n_read_ios = TN_S2_NREADS;

    for (i = 0; i < TN_S2_NREADS; i++) {
        struct IOSana2Req *rio;
        rio = (struct IOSana2Req *)CreateExtIO(nif->reply_port,
                                               sizeof(struct IOSana2Req));
        if (rio == NULL) return TN_S2_NO_MEM;
        /* Bind to the same device the primary io opened, and keep the buffer
         * management cookie the driver returned at open time. */
        rio->ios2_Req.io_Device = nif->io->ios2_Req.io_Device;
        rio->ios2_BufferManagement = nif->io->ios2_BufferManagement;
        nif->read_ios[i] = rio;
    }
    return TN_S2_OK;
}

/* --------------------------------------------------------------- shutdown
 * §M1: "clean S2_OFFLINE+close". Order: OFFLINE, AbortIO+WaitIO every pending
 * read, free read ios, CloseDevice, free primary io + port.
 */
void tn_s2_offline_close(TnSana2If *nif)
{
    ULONG i;
    if (nif == NULL) return;

    if (nif->io != NULL && nif->online) {
        nif->io->ios2_Req.io_Command = S2_OFFLINE;
        nif->io->ios2_Req.io_Error   = 0;
        DoIO((struct IORequest *)nif->io);
        nif->online = FALSE;
    }

    if (nif->read_ios != NULL) {
        for (i = 0; i < nif->n_read_ios; i++) {
            struct IOSana2Req *rio = nif->read_ios[i];
            if (rio != NULL) {
                /* If anything is still pending, abort then wait it out. */
                if (!CheckIO((struct IORequest *)rio)) {
                    AbortIO((struct IORequest *)rio);
                }
                WaitIO((struct IORequest *)rio);
                DeleteExtIO((struct IORequest *)rio);
            }
        }
        FreeVec(nif->read_ios);
        nif->read_ios = NULL;
        nif->n_read_ios = 0;
    }

    if (nif->io != NULL) {
        CloseDevice((struct IORequest *)nif->io);
        DeleteExtIO((struct IORequest *)nif->io);
        nif->io = NULL;
    }
    if (nif->reply_port != NULL) {
        DeleteMsgPort(nif->reply_port);
        nif->reply_port = NULL;
    }
}
