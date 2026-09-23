/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — tftp command (CMD-4). RFC 1350 octet mode.
 * ReadArgs: HOST/A,GET/S,PUT/S,FILE/A,LOCAL
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "HOST/A,GET/S,PUT/S,FILE/A,LOCAL"
#define TFTP_PORT 69
#define BLK 512
#define RETRIES 5
#define TIMEOUT 5

#define OP_RRQ 1
#define OP_WRQ 2
#define OP_DATA 3
#define OP_ACK 4
#define OP_ERROR 5

static LONG x_sendto(LONG fd, const void *b, LONG l, const struct sockaddr *to)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register const void *a0 __asm__("a0") = b;
    register LONG d1 __asm__("d1") = l;
    register const void *a1 __asm__("a1") = to;
    __asm__ __volatile__("jsr -60(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(a1)
        : "d1", "a0", "a1", "memory");
    return d0;
}

static LONG x_recvfrom(LONG fd, void *b, LONG l, struct sockaddr *from)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register void *a0 __asm__("a0") = b;
    register LONG d1 __asm__("d1") = l;
    register void *a1 __asm__("a1") = from;
    __asm__ __volatile__("jsr -72(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(a1)
        : "d1", "a0", "a1", "memory");
    return d0;
}

static UWORD hs(UWORD v) { return ((v & 0xFF) << 8) | (v >> 8); }

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    struct sockaddr_in peer;
    static UBYTE pkt[BLK + 16];
    LONG got, pos, block;
    ULONG addr;
    BPTR fh = (BPTR)0;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"tftp");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *host = (const char *)opts[0];
    BOOL is_get = (opts[1] != 0);
    BOOL is_put = (opts[2] != 0);
    const char *remote = (const char *)opts[3];
    const char *local = (opts[4] != 0) ? (const char *)opts[4] : remote;

    if (is_get == is_put) {
        tn_cmd_printf("tftp: specify GET or PUT\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    /* SEC item 6: bound remote to BLK - 10 or fail with RC 10 (TN_CMD_FAIL) */
    if (strlen(remote) > (size_t)(BLK - 10)) {
        tn_cmd_printf("tftp: remote filename too long (max %d)\n", BLK - 10);
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
    peer.sin_port = hs(TFTP_PORT);
    peer.sin_addr.s_addr = addr;

    /* Build RRQ or WRQ */
    pos = 0;
    pkt[pos++] = 0;
    pkt[pos++] = is_get ? OP_RRQ : OP_WRQ;
    strcpy((char *)&pkt[pos], remote);
    pos += strlen(remote) + 1;
    strcpy((char *)&pkt[pos], "octet");
    pos += 6;

    /* Send request and get first reply */
    {
        LONG i;
        got = -1;
        for (i = 0; i < RETRIES && got < 0; i++) {
            fd_set r;
            struct timeval tv;
            x_sendto(fd, pkt, pos, (struct sockaddr *)&peer);
            FD_ZERO(&r);
            FD_SET(fd, &r);
            tv.tv_secs = TIMEOUT;
            tv.tv_micro = 0;
            if (tn_call_waitselect(fd + 1, &r, NULL, NULL, &tv, NULL) > 0) {
                got = x_recvfrom(fd, pkt, sizeof(pkt), (struct sockaddr *)&peer);
            }
        }
    }
    if (got < 4) {
        tn_cmd_printf("tftp: no response from server\n");
        tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    /* Check for error */
    if (pkt[1] == OP_ERROR) {
        tn_cmd_printf("tftp: server error %ld\n", (LONG)((pkt[2] << 8) | pkt[3]));
        tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    if (is_get) {
        fh = Open((CONST_STRPTR)local, MODE_NEWFILE);
        if (fh == (BPTR)0) {
            tn_cmd_printf("tftp: cannot create %s\n", local);
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }

        block = 1;
        for (;;) {
            if (pkt[1] == OP_DATA) {
                LONG dlen = got - 4;
                if (((pkt[2] << 8) | pkt[3]) == block) {
                    if (dlen > 0) Write(fh, (CONST APTR)&pkt[4], dlen);
                    /* ACK */
                    UBYTE ack[4] = { 0, OP_ACK, pkt[2], pkt[3] };
                    x_sendto(fd, ack, 4, (struct sockaddr *)&peer);
                    if (dlen < BLK) {
                        tn_cmd_printf("tftp: %s received\n", remote);
                        break;
                    }
                    block++;
                }
            }

            {
                fd_set r;
                struct timeval tv;
                FD_ZERO(&r);
                FD_SET(fd, &r);
                tv.tv_secs = TIMEOUT;
                tv.tv_micro = 0;
                if (tn_call_waitselect(fd + 1, &r, NULL, NULL, &tv, NULL) <= 0) {
                    tn_cmd_printf("tftp: timeout\n");
                    rc = TN_CMD_FAIL;
                    break;
                }
                got = x_recvfrom(fd, pkt, sizeof(pkt), (struct sockaddr *)&peer);
                if (got <= 0) { rc = TN_CMD_FAIL; break; }
            }
        }
        Close(fh);
    } else {
        /* PUT: first reply was ACK 0, now send DATA blocks */
        fh = Open((CONST_STRPTR)local, MODE_OLDFILE);
        if (fh == (BPTR)0) {
            tn_cmd_printf("tftp: cannot open %s\n", local);
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }

        block = 1;
        for (;;) {
            LONG dlen = Read(fh, &pkt[4], BLK);
            if (dlen < 0) dlen = 0;
            pkt[0] = 0; pkt[1] = OP_DATA;
            pkt[2] = (UBYTE)(block >> 8); pkt[3] = (UBYTE)(block & 0xFF);

            {
                LONG i;
                BOOL acked = FALSE;
                for (i = 0; i < RETRIES && !acked; i++) {
                    fd_set r;
                    struct timeval tv;
                    x_sendto(fd, pkt, 4 + dlen, (struct sockaddr *)&peer);
                    FD_ZERO(&r);
                    FD_SET(fd, &r);
                    tv.tv_secs = TIMEOUT;
                    tv.tv_micro = 0;
                    if (tn_call_waitselect(fd + 1, &r, NULL, NULL, &tv, NULL) > 0) {
                        LONG g = x_recvfrom(fd, pkt, sizeof(pkt), (struct sockaddr *)&peer);
                        if (g >= 4 && pkt[1] == OP_ACK &&
                            ((pkt[2] << 8) | pkt[3]) == block) {
                            acked = TRUE;
                        }
                    }
                }
                if (!acked) {
                    tn_cmd_printf("tftp: timeout at block %ld\n", block);
                    rc = TN_CMD_FAIL;
                    break;
                }
            }

            if (dlen < BLK) {
                tn_cmd_printf("tftp: %s sent\n", remote);
                break;
            }
            block++;
        }
        Close(fh);
    }

    tn_call_closesocket(fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
