/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — tftp command (CMD-4). RFC 1350 octet mode.
 * ReadArgs: HOST/A,PORT/K/N,GET/S,PUT/S,FILE/A,LOCAL
 *
 * z.ai step 8c item 2: PORT/K/N (default 69), re-ACK of the last block
 * after a timeout, duplicate-DATA re-ACK, packets accepted only from
 * the transfer ID of the first reply, UWORD block wrap, ERROR text,
 * Ctrl-C -> RC 5, and a trailing empty DATA block when a PUT file is a
 * multiple of 512 bytes.
 */
#include "cmdlib.h"
#include <string.h>
TN_VERSTAG_DEF("tftp");

#define TEMPLATE "HOST/A,PORT/K/N,GET/S,PUT/S,FILE/A,LOCAL"
#define TFTP_PORT 69
#define BLK 512
#define RETRIES 5
#define TIMEOUT 3

#define OP_RRQ 1
#define OP_WRQ 2
#define OP_DATA 3
#define OP_ACK 4
#define OP_ERROR 5

static UWORD pkt_block(const UBYTE *pkt)
{
    return (UWORD)(((UWORD)pkt[2] << 8) | (UWORD)pkt[3]);
}

static const char *tftp_err_text(UWORD code)
{
    switch (code) {
    case 0: return "Not defined";
    case 1: return "File not found";
    case 2: return "Access violation";
    case 3: return "Disk full or allocation exceeded";
    case 4: return "Illegal TFTP operation";
    case 5: return "Unknown transfer ID";
    case 6: return "File already exists";
    case 7: return "No such user";
    default: return "Unknown error";
    }
}

static int tftp_report_error(const UBYTE *pkt, LONG got)
{
    UWORD code = (UWORD)(((UWORD)pkt[2] << 8) | (UWORD)pkt[3]);
    char text[64];
    LONG i = 4, o = 0;
    while (i < got && pkt[i] != 0 && o < (LONG)sizeof(text) - 1) {
        text[o++] = (char)pkt[i++];
    }
    text[o] = '\0';
    if (o == 0) tn_cmd_printf("tftp: server error %ld: %s\n", (LONG)code, tftp_err_text(code));
    else tn_cmd_printf("tftp: server error %ld: %s\n", (LONG)code, text);
    return TN_CMD_FAIL;
}

static int tftp_addr_eq(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
    return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
}

/* Wait up to TIMEOUT for one packet, dropping packets from foreign
 * transfer IDs. Returns the byte count, 0 on timeout, -1 on Ctrl-C. */
static LONG tftp_wait_packet(LONG fd, UBYTE *pkt, LONG maxlen,
                             const struct sockaddr_in *tid,
                             struct sockaddr_in *from)
{
    fd_set r;
    struct timeval tv;
    LONG fromlen, got;

    FD_ZERO(&r);
    FD_SET(fd, &r);
    tv.tv_secs = TIMEOUT;
    tv.tv_micro = 0;
    if (tn_call_waitselect(fd + 1, &r, NULL, NULL, &tv, NULL) <= 0) return 0;
    fromlen = (LONG)sizeof(*from);
    got = tn_call_recvfrom(fd, pkt, maxlen, 0, (struct sockaddr *)from, &fromlen);
    if (got <= 0) return 0;
    if (tid != NULL && !tftp_addr_eq(from, tid)) return 0; /* foreign TID: drop */
    if (tn_cmd_check_ctrlc()) return -1;
    return got;
}

static void tftp_send_ack(LONG fd, UWORD blk, const struct sockaddr_in *to)
{
    UBYTE ack[4];
    ack[0] = 0; ack[1] = OP_ACK;
    ack[2] = (UBYTE)(blk >> 8); ack[3] = (UBYTE)(blk & 0xFF);
    tn_call_sendto(fd, ack, 4, 0, (struct sockaddr *)to, sizeof(*to));
}

int main(int argc, char **argv)
{
    LONG opts[6] = { 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    struct sockaddr_in peer, tid, from;
    static UBYTE pkt[BLK + 16];
    LONG got, pos, tries;
    ULONG addr;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"tftp");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *host = (const char *)opts[0];
    /* z.ai step 8c item 2: PORT/K/N, default 69. /N slots are pointers. */
    LONG port = (opts[1] != 0 && *(LONG *)opts[1] > 0) ? *(LONG *)opts[1] : TFTP_PORT;
    BOOL is_get = (opts[2] != 0);
    BOOL is_put = (opts[3] != 0);
    const char *remote = (const char *)opts[4];
    const char *local = (opts[5] != 0) ? (const char *)opts[5] : remote;

    if (is_get == is_put) {
        tn_cmd_printf("tftp: specify GET or PUT\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    /* SEC item 6: bound remote to BLK - 10 or fail with RC 10 (TN_CMD_FAIL) */
    if (strlen(remote) > (size_t)(BLK - 10)) {
        tn_cmd_printf("tftp: remote filename too long (max %ld)\n", (LONG)(BLK - 10));
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    addr = tn_cmd_resolve(host);
    if (addr == INADDR_NONE) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    fd = tn_call_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    memset(&peer, 0, sizeof(peer));
    peer.sin_len = sizeof(peer);
    peer.sin_family = AF_INET;
    peer.sin_port = htons((UWORD)port);
    peer.sin_addr.s_addr = addr;

    /* Build RRQ or WRQ */
    pos = 0;
    pkt[pos++] = 0;
    pkt[pos++] = is_get ? OP_RRQ : OP_WRQ;
    strcpy((char *)&pkt[pos], remote);
    pos += strlen(remote) + 1;
    strcpy((char *)&pkt[pos], "octet");
    pos += 6;

    if (is_get) {
        BPTR fh;
        UWORD last_acked = 0;

        /* Send the request until the first reply arrives. */
        got = 0;
        for (tries = 0; tries < RETRIES && got <= 0; tries++) {
            if (tn_cmd_check_ctrlc()) { rc = TN_CMD_WARN; goto out; }
            tn_call_sendto(fd, pkt, pos, 0, (struct sockaddr *)&peer, sizeof(peer));
            got = tftp_wait_packet(fd, pkt, sizeof(pkt), NULL, &from);
            if (got < 0) { rc = TN_CMD_WARN; goto out; }
        }
        if (got <= 0) {
            tn_cmd_printf("tftp: no response from server\n");
            rc = TN_CMD_FAIL;
            goto out;
        }
        if (pkt[1] == OP_ERROR) { rc = tftp_report_error(pkt, got); goto out; }
        if (pkt[1] != OP_DATA || got < 4) {
            tn_cmd_printf("tftp: unexpected reply (op %ld)\n", (LONG)pkt[1]);
            rc = TN_CMD_FAIL;
            goto out;
        }
        /* Lock onto the transfer ID of the first reply. */
        tid = from;

        fh = Open((CONST_STRPTR)local, MODE_NEWFILE);
        if (fh == (BPTR)0) {
            tn_cmd_printf("tftp: cannot create %s\n", local);
            rc = TN_CMD_FAIL;
            goto out;
        }

        for (;;) {
            UWORD blk = pkt_block(pkt);
            LONG dlen = got - 4;
            if (tn_cmd_check_ctrlc()) { rc = TN_CMD_WARN; break; }

            if (blk == (UWORD)(last_acked + 1)) {
                if (dlen > 0) Write(fh, (CONST APTR)&pkt[4], dlen);
                last_acked = blk;
                tftp_send_ack(fd, blk, &tid);
                if (dlen < BLK) {
                    tn_cmd_printf("tftp: %s received\n", remote);
                    break;
                }
            } else if (blk == last_acked) {
                /* duplicate DATA: re-ACK and ignore the payload */
                tftp_send_ack(fd, blk, &tid);
            }
            /* any other block: dropped, keep waiting */

            tries = 0;
            for (;;) {
                if (tn_cmd_check_ctrlc()) { rc = TN_CMD_WARN; break; }
                got = tftp_wait_packet(fd, pkt, sizeof(pkt), &tid, &from);
                if (got < 0) { rc = TN_CMD_WARN; break; }
                if (got > 0) {
                    if (pkt[1] == OP_ERROR) { rc = tftp_report_error(pkt, got); break; }
                    if (pkt[1] == OP_DATA) break;
                    continue; /* stray ACK etc. */
                }
                /* timeout: re-ACK the last block so the server
                 * retransmits; fail after RETRIES idle rounds */
                if (++tries > RETRIES) {
                    tn_cmd_printf("tftp: timeout waiting block %ld\n",
                                  (LONG)(UWORD)(last_acked + 1));
                    rc = TN_CMD_FAIL;
                    break;
                }
                tftp_send_ack(fd, last_acked, &tid);
            }
            if (rc != TN_CMD_OK) break;
        }
        Close(fh);
    } else {
        BPTR fh;
        UWORD block = 0;
        LONG dlen;

        fh = Open((CONST_STRPTR)local, MODE_OLDFILE);
        if (fh == (BPTR)0) {
            tn_cmd_printf("tftp: cannot open %s\n", local);
            rc = TN_CMD_FAIL;
            goto out;
        }

        /* Send the request until the first reply (ACK 0) arrives. */
        got = 0;
        for (tries = 0; tries < RETRIES && got <= 0; tries++) {
            if (tn_cmd_check_ctrlc()) { rc = TN_CMD_WARN; break; }
            tn_call_sendto(fd, pkt, pos, 0, (struct sockaddr *)&peer, sizeof(peer));
            got = tftp_wait_packet(fd, pkt, sizeof(pkt), NULL, &from);
            if (got < 0) { rc = TN_CMD_WARN; break; }
        }
        if (rc != TN_CMD_OK) { Close(fh); goto out; }
        if (got <= 0) {
            tn_cmd_printf("tftp: no response from server\n");
            Close(fh);
            rc = TN_CMD_FAIL;
            goto out;
        }
        if (pkt[1] == OP_ERROR) { rc = tftp_report_error(pkt, got); Close(fh); goto out; }
        if (pkt[1] != OP_ACK || got < 4) {
            tn_cmd_printf("tftp: unexpected reply to WRQ\n");
            Close(fh);
            rc = TN_CMD_FAIL;
            goto out;
        }
        tid = from;

        /* do-while: a file whose size is a multiple of BLK ends with an
         * empty final DATA block (RFC 1350); the block counter is a
         * UWORD and wraps. */
        do {
            dlen = Read(fh, (APTR)&pkt[4], BLK);
            if (dlen < 0) dlen = 0;
            block = (UWORD)(block + 1); /* wraps at 65535 */
            pkt[0] = 0; pkt[1] = OP_DATA;
            pkt[2] = (UBYTE)(block >> 8); pkt[3] = (UBYTE)(block & 0xFF);

            tries = 0;
            for (;;) {
                if (tn_cmd_check_ctrlc()) { rc = TN_CMD_WARN; break; }
                tn_call_sendto(fd, pkt, 4 + dlen, 0, (struct sockaddr *)&tid, sizeof(tid));
                got = tftp_wait_packet(fd, pkt, sizeof(pkt), &tid, &from);
                if (got < 0) { rc = TN_CMD_WARN; break; }
                if (got > 0) {
                    if (pkt[1] == OP_ERROR) { rc = tftp_report_error(pkt, got); break; }
                    if (pkt[1] == OP_ACK && pkt_block(pkt) == block) break;
                    continue; /* stale or foreign ACK */
                }
                /* timeout: the DATA block is re-sent by the loop above */
                if (++tries > RETRIES) {
                    tn_cmd_printf("tftp: timeout at block %ld\n", (LONG)block);
                    rc = TN_CMD_FAIL;
                    break;
                }
            }
        } while (rc == TN_CMD_OK && dlen == BLK);

        if (rc == TN_CMD_OK) tn_cmd_printf("tftp: %s sent\n", remote);
        Close(fh);
    }

out:
    tn_call_closesocket(fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
