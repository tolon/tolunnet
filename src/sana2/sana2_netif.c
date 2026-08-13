/*
 * tolunet — SANA-II netif glue implementation (M1, UNPROVEN).
 *
 * Master prompt §M1. Implements: shared device open (never exclusive), copyfunc
 * tag list, S2_DEVICEQUERY (MTU/address), S2_CONFIGINTERFACE + S2_ONLINE,
 * >=4 outstanding async CMD_READ, S2_BROADCAST send, and clean S2_OFFLINE +
 * AbortIO/WaitIO + CloseDevice shutdown.
 *
 * All SANA-II constants/structs come from the Rev 7 spec (normative per §3.1,
 * resolved conflict #13) via <devices/sana2.h>. Anything not confirmed against
 * the Roadshow SDK local header is tagged
 *   /* VERIFY: sana2.h (Rev 7) */
 * and tracked in QUESTIONS.md. This file is NOT yet wired into the M0 build
 * (the Makefile builds only the hello-task in M0); it compiles as part of M1
 * once the bench confirms the SANA-II header paths and field names.
 */

#include "sana2_netif.h"
#include "../common/log.h"

#include <proto/exec.h>
#include <proto/dos.h>

#include <exec/memory.h>
#include <exec/io.h>
#include <utility/tagitem.h>   /* struct TagItem, TAG_DONE — VERIFY */
#include <devices/sana2.h>     /* VERIFY: path, field names vs Rev 7 */

/* How many CMD_READ requests we keep outstanding (§M1: ">=4"). */
#define TN_S2_NREADS  4

/* Packet types (EtherType values, big-endian on the wire). */
#define TN_ETHERTYPE_IPV4 0x0800
#define TN_ETHERTYPE_ARP  0x0806

/* ------------------------------------------------------------------ copyfuncs
 * The driver calls these to move bytes between its packet and our buffer.
 * S2_CopyToBuff: driver -> our buffer (receive path); returns bytes copied.
 * S2_CopyFromBuff: our buffer -> driver (send path); returns bytes copied.
 * VERIFY: exact function-pointer typedef/signature in sana2.h.
 */
typedef LONG (*TnS2CopyToFunc)(APTR dst, APTR src, ULONG len);   /* VERIFY */
typedef LONG (*TnS2CopyFromFunc)(APTR dst, APTR src, ULONG len); /* VERIFY */

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
    struct TagItem bm_tags[3];   /* VERIFY: S2_CopyToBuff/S2_CopyFromBuff tags */
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

    /* Reply port for async I/O. CreatePort = exec CreateMsgPort (V36+). */
    port = CreateMsgPort();              /* VERIFY: proto/exec.h */
    if (port == NULL) return TN_S2_NO_MEM;
    nif->reply_port = port;

    io = (struct IOSana2Req *)CreateExtIO(port, sizeof(struct IOSana2Req));
    /* VERIFY: CreateExtIO availability/prototype in amiga-gcc; alternative is
     * AllocMem + mn_ReplyPort = port + mn_Length. */
    if (io == NULL) {
        DeleteMsgPort(port);
        nif->reply_port = NULL;
        return TN_S2_NO_MEM;
    }
    nif->io = io;

    /* Buffer-management tag list: drivers refuse to open without both funcs. */
    bm_tags[0].ti_Tag   = S2_CopyToBuff;     /* VERIFY: tag name vs sana2.h */
    bm_tags[0].ti_Data  = (ULONG)tn_copy_to_buff;
    bm_tags[1].ti_Tag   = S2_CopyFromBuff;   /* VERIFY: tag name vs sana2.h */
    bm_tags[1].ti_Data  = (ULONG)tn_copy_from_buff;
    bm_tags[2].ti_Tag   = TAG_DONE;          /* VERIFY: from utility/tagitem.h */

    /* OpenDevice wants ios2_BufferManagement preset to the tag list. */
    io->ios2_Req.io_Message.mn_Node.ln_Type = NT_MESSAGE; /* VERIFY */
    io->ios2_BufferManagement = bm_tags;     /* VERIFY: field name vs Rev 7 */

    /* Shared open: flags = 0 (NOT SANA2OPB_MINE). §M1. */
    err = OpenDevice((STRPTR)device_name, unit, (struct IORequest *)io, 0);
    if (err != 0) {
        /* io_Error may also carry detail; log it in M1 tool. */
        tn_log(TN_LOG_BASIC, "tolunet: OpenDevice failed\n");
        DeleteExtIO((struct IORequest *)io);  /* VERIFY */
        nif->io = NULL;
        DeleteMsgPort(port);
        nif->reply_port = NULL;
        return TN_S2_OPEN_FAIL;
    }

    /* The driver may overwrite ios2_BufferManagement with its magic cookie;
     * we must keep using whatever it set. Leave it as-is. */

    return TN_S2_OK;
}

/* --------------------------------------------------------------- s2_query
 * Ask the driver for MTU and address size. Fills nif->mtu / addr_bits/bytes.
 */
static TnS2Result tn_s2_query(TnSana2If *nif)
{
    struct Sana2DeviceQuery q;   /* VERIFY: struct fields vs Rev 7 */
    struct IOSana2Req *io = nif->io;

    /* SizeAvailable tells the driver how much room it may fill. */
    q.SizeAvailable = sizeof(q);   /* VERIFY: field name */
    q.SizeSupplied  = 0;
    q.DevQueryFormat = 0;          /* Rev 7: "this is type 0" */
    q.DeviceLevel   = 0;           /* Rev 7: "level 0" */

    io->ios2_Req.io_Command = S2_DEVICEQUERY;   /* VERIFY: cmd const */
    io->ios2_StatData       = &q;               /* VERIFY: field name */
    io->ios2_Req.io_Error   = 0;

    DoIO((struct IORequest *)io);   /* VERIFY: DoIO proto */
    if (io->ios2_Req.io_Error != 0) {
        tn_log(TN_LOG_BASIC, "tolunet: S2_DEVICEQUERY failed\n");
        return TN_S2_QUERY_FAIL;
    }

    nif->mtu       = q.MTU;            /* VERIFY: field name */
    nif->addr_bits = (UWORD)q.AddrFieldSize;  /* VERIFY: field name, in BITS */
    nif->addr_bytes = (UWORD)((nif->addr_bits + 7) / 8);
    if (nif->addr_bytes > SANA2_MAX_ADDR_BYTES)   /* VERIFY: const */
        nif->addr_bytes = SANA2_MAX_ADDR_BYTES;

    return TN_S2_OK;
}

/* --------------------------------------------------------------- s2_online
 * Configure the interface (set MAC) then bring it online. If mac is NULL we
 * leave the station address at whatever the driver default is and still send
 * S2_CONFIGINTERFACE with the current SrcAddr (some drivers require the call
 * regardless). §M1: "configure address; S2_ONLINE".
 */
TnS2Result tn_s2_online(TnSana2If *nif, const UBYTE *mac)
{
    struct IOSana2Req *io;
    TnS2Result r;

    if (nif == NULL || nif->io == NULL) return TN_S2_INTERNAL;

    r = tn_s2_query(nif);
    if (r != TN_S2_OK) return r;

    io = nif->io;

    /* S2_CONFIGINTERFACE: set the hardware station address. §Rev7. */
    if (mac != NULL) {
        ULONG i;
        for (i = 0; i < nif->addr_bytes && i < SANA2_MAX_ADDR_BYTES; i++)
            io->ios2_SrcAddr[i] = mac[i];   /* VERIFY: field name, array size */
    }
    io->ios2_Req.io_Command = S2_CONFIGINTERFACE;  /* VERIFY: cmd const */
    io->ios2_Req.io_Error   = 0;
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log(TN_LOG_BASIC, "tolunet: S2_CONFIGINTERFACE failed\n");
        return TN_S2_CONFIG_FAIL;
    }

    /* S2_ONLINE: bring it up. */
    io->ios2_Req.io_Command = S2_ONLINE;   /* VERIFY: cmd const */
    io->ios2_Req.io_Error   = 0;
    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        tn_log(TN_LOG_BASIC, "tolunet: S2_ONLINE failed\n");
        return TN_S2_ONLINE_FAIL;
    }

    /* Capture the negotiated station address back for diagnostics. */
    {
        ULONG i;
        for (i = 0; i < nif->addr_bytes && i < SANA2_MAX_ADDR_BYTES; i++)
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
    io->ios2_Req.io_Command = broadcast ? S2_BROADCAST : CMD_WRITE; /* VERIFY */
    io->ios2_PacketType     = packet_type;            /* VERIFY: field */
    io->ios2_DataLength     = (ULONG)len;             /* VERIFY: field */
    io->ios2_Data           = (APTR)buf;              /* VERIFY: field */
    io->ios2_Req.io_Error   = 0;

    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        return -1;
    }
    return len;
}

/* --------------------------------------------------------------- s2_recv
 * Blocking receive on one read slot (idx into read_ios). Copies the arrived
 * frame into buf (<= buf_len bytes) and returns the on-wire length. The caller
 * (M1 logger) re-arms via tn_s2_arm_read. src_addr (optional) gets SrcAddr.
 * M2 will switch this to async pump + callback.
 */
LONG tn_s2_recv(TnSana2If *nif, void *buf, ULONG buf_len,
                ULONG idx, UBYTE *src_addr)
{
    struct IOSana2Req *io;
    if (nif == NULL || !nif->online) return -1;
    if (nif->read_ios == NULL || idx >= nif->n_read_ios) return -1;
    io = nif->read_ios[idx];

    io->ios2_Data       = buf;          /* copyfunc writes here */
    io->ios2_DataLength = buf_len;      /* VERIFY: direction of this field */
    /* VERIFY: SANA2IOB_RAW handling — for M1 raw logging we may want the full
     * frame. Setting ios2_PacketType to 0 or a specific type is driver-
     * dependent; left to the arming function. */

    DoIO((struct IORequest *)io);
    if (io->ios2_Req.io_Error != 0) {
        return -1;
    }
    if (src_addr != NULL) {
        ULONG i;
        for (i = 0; i < nif->addr_bytes && i < SANA2_MAX_ADDR_BYTES; i++)
            src_addr[i] = io->ios2_SrcAddr[i];
    }
    return (LONG)io->ios2_DataLength;   /* VERIFY: field name */
}

/* --------------------------------------------------------------- arm/cancel
 * Allocate and arm >=4 CMD_READ requests (async) so frames keep flowing. M1
 * leaves these armed and polls; M2 wires a completion signal into the task.
 */
TnS2Result tn_s2_arm_reads(TnSana2If *nif)
{
    ULONG i;
    if (nif == NULL || !nif->online) return TN_S2_INTERNAL;
    if (nif->read_ios != NULL) return TN_S2_OK;  /* already armed */

    nif->read_ios = AllocVec(sizeof(struct IOSana2Req *) * TN_S2_NREADS,
                             MEMF_CLEAR | MEMF_ANY);   /* VERIFY: AllocVec */
    if (nif->read_ios == NULL) return TN_S2_NO_MEM;
    nif->n_read_ios = TN_S2_NREADS;

    for (i = 0; i < TN_S2_NREADS; i++) {
        struct IOSana2Req *rio;
        rio = (struct IOSana2Req *)CreateExtIO(nif->reply_port,
                                               sizeof(struct IOSana2Req));
        if (rio == NULL) return TN_S2_NO_MEM;
        /* Duplicate the device context from the primary io. OpenDevice binds
         * the device into the IORequest; we must copy io_Device and the buffer
         * management cookie. */
        rio->ios2_Req.io_Device = nif->io->ios2_Req.io_Device; /* VERIFY */
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
        nif->io->ios2_Req.io_Command = S2_OFFLINE;  /* VERIFY: cmd const */
        nif->io->ios2_Req.io_Error   = 0;
        DoIO((struct IORequest *)nif->io);
        nif->online = FALSE;
    }

    if (nif->read_ios != NULL) {
        for (i = 0; i < nif->n_read_ios; i++) {
            struct IOSana2Req *rio = nif->read_ios[i];
            if (rio != NULL) {
                /* If anything is still pending, abort then wait it out. */
                if (!CheckIO((struct IORequest *)rio)) {  /* VERIFY */
                    AbortIO((struct IORequest *)rio);     /* VERIFY */
                }
                WaitIO((struct IORequest *)rio);          /* VERIFY */
                DeleteExtIO((struct IORequest *)rio);     /* VERIFY */
            }
        }
        FreeVec(nif->read_ios);                          /* VERIFY */
        nif->read_ios = NULL;
        nif->n_read_ios = 0;
    }

    if (nif->io != NULL) {
        CloseDevice((struct IORequest *)nif->io);        /* VERIFY */
        DeleteExtIO((struct IORequest *)nif->io);
        nif->io = NULL;
    }
    if (nif->reply_port != NULL) {
        DeleteMsgPort(nif->reply_port);
        nif->reply_port = NULL;
    }
}
