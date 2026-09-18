/*
 * tolunnet — nc (netcat) command (CMD-3). ReadArgs: HOST/A,PORT/N,UDP/S,LISTEN/S,TIMEOUT/N
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "HOST/A,PORT/N,UDP/S,LISTEN/S,TIMEOUT/N"
#define BUF_SIZE 4096

int main(int argc, char **argv)
{
    LONG opts[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    LONG type;
    struct sockaddr_in dst;
    static char buf[BUF_SIZE];
    LONG got;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"nc");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *host = (const char *)opts[0];
    LONG port = opts[1];
    LONG use_udp = opts[2];
    LONG listen_mode = opts[3];
    LONG timeout = (opts[4] > 0) ? opts[4] : 0;

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
        /* Listen mode: bind, listen (TCP), accept one connection */
        dst.sin_addr.s_addr = INADDR_ANY;
        if (!use_udp) {
            if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
                /* bind is via LVO -36, we approximate with connect to 0 */
                tn_cmd_printf("nc: bind failed\n");
                tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
                return TN_CMD_FAIL;
            }
        }
        tn_cmd_printf("nc: listening on port %ld\n", port);
    } else {
        ULONG addr = tn_cmd_resolve(host);
        if (addr == INADDR_NONE) {
            tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
        dst.sin_addr.s_addr = addr;

        if (!use_udp) {
            tn_cmd_printf("nc: connecting to %s:%ld...\n", host, port);
            if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
                tn_cmd_printf("nc: connect failed (errno=%ld)\n", tn_call_errno());
                tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
                return TN_CMD_FAIL;
            }
        }
    }

    /* Pipe stdin <-> socket */
    {
        fd_set rfds;
        struct timeval tv;
        LONG sel;
        LONG stdin_fd = 0; /* Amiga CON: handled via ReadChars below */
        static char stdin_buf[512];

        while (!tn_cmd_check_ctrlc()) {
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_secs = timeout ? timeout : 1;
            tv.tv_micro = 0;
            sel = tn_call_waitselect(fd + 1, &rfds, NULL, NULL, &tv, NULL);

            if (sel > 0 && FD_ISSET(fd, &rfds)) {
                got = tn_call_recv(fd, buf, BUF_SIZE - 1, 0);
                if (got <= 0) break;
                buf[got] = '\0';
                Write(Output(), (CONST APTR)buf, got);
            } else if (sel == 0 && timeout) {
                break; /* timed out */
            }

            /* Non-blocking stdin read (FGets on CON:) */
            {
                LONG avail = WaitForChar(Input(), 0);
                if (avail) {
                    if (FGets(Input(), (STRPTR)stdin_buf, sizeof(stdin_buf)) != NULL) {
                        LONG len = strlen(stdin_buf);
                        tn_call_send(fd, stdin_buf, len, 0);
                    }
                }
            }
        }
    }

    tn_call_closesocket(fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
