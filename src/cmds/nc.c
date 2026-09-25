/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — nc (netcat) command (CMD-3). ReadArgs: HOST/A,PORT/N,UDP/S,LISTEN/S,TIMEOUT/N
 *
 * z.ai step 8b item 1: non-interactive stdin (redirected file, every
 * script) is pumped with Read(); at EOF the socket is half-closed once
 * and the peer's tail is drained until it closes or TIMEOUT idle seconds
 * pass. Interactive stdin keeps the WaitForChar path. LISTEN is a real
 * bind+listen+accept. UDP connects its socket so send/recv work.
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "HOST/A,PORT/N,UDP/S,LISTEN/S,TIMEOUT/N"
#define BUF_SIZE 4096
#define IN_BUF 1024
#define IDLE_TICK 1   /* seconds per waitselect idle step */

static void pipe_stream(LONG fd, LONG timeout, BOOL interactive)
{
    fd_set rfds;
    struct timeval tv;
    LONG sel, got;
    LONG idle = 0;
    BOOL stdin_eof = FALSE;
    static char buf[BUF_SIZE];
    static char inbuf[IN_BUF];

    while (!tn_cmd_check_ctrlc()) {
        if (interactive) {
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_secs = timeout ? timeout : IDLE_TICK;
            tv.tv_micro = 0;
            sel = tn_call_waitselect(fd + 1, &rfds, NULL, NULL, &tv, NULL);

            if (sel > 0 && FD_ISSET(fd, &rfds)) {
                got = tn_call_recv(fd, buf, BUF_SIZE - 1, 0);
                if (got <= 0) break;
                Write(Output(), (CONST APTR)buf, got);
            } else if (sel == 0 && timeout) {
                break; /* timed out */
            }

            /* Non-blocking stdin read (FGets on CON:) */
            {
                LONG avail = WaitForChar(Input(), 0);
                if (avail) {
                    if (FGets(Input(), (STRPTR)inbuf, sizeof(inbuf)) != NULL) {
                        LONG len = strlen(inbuf);
                        tn_call_send(fd, inbuf, len, 0);
                    }
                }
            }
        } else {
            /* File stdin: send everything first, then drain the peer. */
            if (!stdin_eof) {
                got = Read(Input(), (APTR)inbuf, sizeof(inbuf));
                if (got > 0) {
                    idle = 0;
                    if (tn_call_send(fd, inbuf, got, 0) != got) {
                        /* send failed: stop sending, still drain */
                        stdin_eof = TRUE;
                        tn_call_shutdown(fd, 1);
                    }
                } else {
                    /* EOF (or read error): half-close once */
                    stdin_eof = TRUE;
                    tn_call_shutdown(fd, 1);
                }
            }

            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_secs = IDLE_TICK;
            tv.tv_micro = 0;
            sel = tn_call_waitselect(fd + 1, &rfds, NULL, NULL, &tv, NULL);
            if (sel > 0 && FD_ISSET(fd, &rfds)) {
                got = tn_call_recv(fd, buf, BUF_SIZE - 1, 0);
                if (got <= 0) break; /* peer closed */
                idle = 0;
                Write(Output(), (CONST APTR)buf, got);
            } else {
                idle++;
            }
            if (stdin_eof && timeout && idle >= timeout) break;
        }
    }
}

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    LONG type;
    struct sockaddr_in dst;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"nc");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *host = (const char *)opts[0];
    LONG port = (opts[1] != 0) ? *(LONG *)opts[1] : 0; /* /N is a pointer */
    LONG use_udp = opts[2];
    LONG listen_mode = opts[3];
    LONG timeout = (opts[4] != 0 && *(LONG *)opts[4] > 0) ? *(LONG *)opts[4] : 0;

    if (port <= 0 || port > 65535) {
        tn_cmd_printf("nc: invalid port\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    type = use_udp ? SOCK_DGRAM : SOCK_STREAM;
    fd = tn_call_socket(AF_INET, type, 0);
    if (fd < 0) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    memset(&dst, 0, sizeof(dst));
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons((UWORD)port);

    if (listen_mode) {
        /* z.ai step 8b item 1: real bind + listen + accept (TCP). */
        if (use_udp) {
            tn_cmd_printf("nc: UDP LISTEN not supported\n");
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        dst.sin_addr.s_addr = INADDR_ANY;
        if (tn_call_bind(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
            tn_cmd_printf("nc: bind failed (errno=%ld)\n", tn_call_errno());
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        if (tn_call_listen(fd, 1) != 0) {
            tn_cmd_printf("nc: listen failed (errno=%ld)\n", tn_call_errno());
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        tn_cmd_printf("nc: listening on port %ld\n", port);
        {
            struct sockaddr_in acc_addr;
            LONG acc_len = (LONG)sizeof(acc_addr);
            LONG afd = tn_call_accept(fd, (struct sockaddr *)&acc_addr, &acc_len);
            if (afd < 0) {
                tn_cmd_printf("nc: accept failed (errno=%ld)\n", tn_call_errno());
                tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
                return TN_CMD_FAIL;
            }
            pipe_stream(afd, timeout, IsInteractive(Input()));
            tn_call_closesocket(afd);
        }
    } else {
        ULONG addr = tn_cmd_resolve(host);
        if (addr == INADDR_NONE) {
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        dst.sin_addr.s_addr = addr;

        if (!use_udp) {
            tn_cmd_printf("nc: connecting to %s:%ld...\n", host, port);
        }
        /* z.ai step 8b item 1: UDP connects its socket too, so plain
         * send/recv work (unconnected send is EDESTADDRREQ). */
        if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
            tn_cmd_printf("nc: connect failed (errno=%ld)\n", tn_call_errno());
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }

        pipe_stream(fd, timeout, IsInteractive(Input()));
    }

    tn_call_closesocket(fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
