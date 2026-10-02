/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — telnet command (CMD-4). ReadArgs: HOST/A,PORT/N
 * Minimal NVT: asks the server for ECHO + SGA, refuses every other
 * option, sends CR LF line ends (6.10).
 */
#include "cmdlib.h"
#include <string.h>
TN_VERSTAG_DEF("telnet");

#define TEMPLATE "HOST/A,PORT/N"
#define BUF_SIZE 2048
#define TELNET_PORT 23

/* Minimal NVT: IAC + command bytes */
#define IAC 255
#define DONT 254
#define DO 253
#define WONT 252
#define WILL 251
#define SB 250
#define SE 240
#define OPT_ECHO 1
#define OPT_SGA 3
#define OPT_TTYPE 24
#define OPT_NAWS 31
#define CR 13
#define LF 10

/* 6.10: per-option state of the SERVER side (him), RFC 1143 style
 * without the queue bits. Our own side is always "no": DO is refused
 * with WONT, DONT needs no answer. A reply is only sent when it
 * changes state or refuses a request - never in answer to an answer,
 * so the two sides cannot loop. */
#define OPT_NO      0
#define OPT_YES     1
#define OPT_WANTYES 2
static UBYTE tn_him[256];

static BOOL tn_want_him(UBYTE opt)
{
    return opt == OPT_ECHO || opt == OPT_SGA;
}

static void tn_send_opt(LONG fd, UBYTE cmd, UBYTE opt)
{
    UBYTE r[3];
    r[0] = IAC; r[1] = cmd; r[2] = opt;
    tn_call_send(fd, r, 3, 0);
}

static void tn_handle_opt(LONG fd, UBYTE cmd, UBYTE opt)
{
    switch (cmd) {
    case WILL:
        if (tn_him[opt] == OPT_WANTYES) {
            tn_him[opt] = OPT_YES;           /* answer to our DO */
        } else if (tn_him[opt] == OPT_NO) {
            if (tn_want_him(opt)) {
                tn_him[opt] = OPT_YES;
                tn_send_opt(fd, DO, opt);
            } else {
                tn_send_opt(fd, DONT, opt);  /* refuse */
            }
        }
        break;
    case WONT:
        if (tn_him[opt] == OPT_YES) {
            tn_him[opt] = OPT_NO;
            tn_send_opt(fd, DONT, opt);
        } else if (tn_him[opt] == OPT_WANTYES) {
            tn_him[opt] = OPT_NO;            /* our DO was refused */
        }
        break;
    case DO:
        tn_send_opt(fd, WONT, opt);          /* we enable nothing */
        break;
    default:                                 /* DONT: already off */
        break;
    }
}

/* Console bytes -> NVT: bare LF becomes CR LF, IAC is doubled.
 * out must hold 2 * n bytes. */
static LONG tn_to_nvt(const char *in, LONG n, char *out)
{
    LONG i, o = 0;
    for (i = 0; i < n; i++) {
        UBYTE c = (UBYTE)in[i];
        if (c == LF && (i == 0 || (UBYTE)in[i - 1] != CR)) out[o++] = (char)CR;
        else if (c == IAC) out[o++] = (char)IAC;
        out[o++] = (char)c;
    }
    return o;
}

/* Telnet protocol parser states (SEC item 8) */
enum {
    TN_STATE_DATA = 0,
    TN_STATE_IAC,
    TN_STATE_OPT,
    TN_STATE_SB,
    TN_STATE_SB_IAC
};

int main(int argc, char **argv)
{
    LONG opts[2] = { 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    struct sockaddr_in dst;
    static char rxbuf[BUF_SIZE];
    static char txbuf[512];
    ULONG addr;
    LONG port;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"telnet");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *host = (const char *)opts[0];
    port = TELNET_PORT;
    if (opts[1] != 0) { /* /N is a pointer */
        LONG arg = *(LONG *)opts[1];
        if (arg > 0 && arg < 65536) port = arg;
    }

    addr = tn_cmd_resolve(host);
    if (addr == INADDR_NONE) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    fd = tn_call_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    memset(&dst, 0, sizeof(dst));
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons((UWORD)port);
    dst.sin_addr.s_addr = addr;

    tn_cmd_printf("telnet: connecting to %s:%ld...\n", host, port);
    if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
        tn_cmd_printf("telnet: connect failed (errno=%ld)\n", tn_call_errno());
        tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }
    tn_cmd_printf("telnet: connected. Type Ctrl-C to quit.\n");

    /* Ask the server to echo and suppress go-ahead (6.10: no
     * unsolicited WONT/DONT - those options are already off). */
    tn_him[OPT_ECHO] = OPT_WANTYES;
    tn_send_opt(fd, DO, OPT_ECHO);
    tn_him[OPT_SGA] = OPT_WANTYES;
    tn_send_opt(fd, DO, OPT_SGA);

    /* Main loop: read from socket → write to console; read console → send */
    {
        fd_set rfds;
        struct timeval tv;
        LONG sel;
        LONG running = 1;
        int tn_state = TN_STATE_DATA;
        UBYTE pending_cmd = 0;
        /* z.ai step 8b item 2: file stdin (every script) is pumped with
         * Read(), each LF is sent as CR LF (NVT), and at EOF the socket
         * is half-closed, drained up to 2 s, and telnet exits 0.
         * Interactive stdin keeps the WaitForChar path. */
        BOOL interactive = IsInteractive(Input());
        BOOL stdin_eof = FALSE;
        LONG idle = 0; /* 0.2 s ticks drained after EOF */
        static char crlf_buf[BUF_SIZE];

        while (running && !tn_cmd_check_ctrlc()) {
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
            tv.tv_secs = 0;
            tv.tv_micro = 200000; /* 200ms */
            sel = tn_call_waitselect(fd + 1, &rfds, NULL, NULL, &tv, NULL);

            if (sel > 0 && FD_ISSET(fd, &rfds)) {
                LONG got = tn_call_recv(fd, rxbuf, BUF_SIZE - 1, 0);
                if (got <= 0) {
                    tn_cmd_printf("\ntelnet: connection closed\n");
                    running = 0;
                    break;
                }
                /* Filter out IAC sequences and skip subnegotiation (SEC item 8) */
                {
                    LONG i, w = 0;
                    for (i = 0; i < got; i++) {
                        UBYTE c = (UBYTE)rxbuf[i];
                        switch (tn_state) {
                        case TN_STATE_DATA:
                            if (c == IAC) {
                                tn_state = TN_STATE_IAC;
                            } else {
                                rxbuf[w++] = (char)c;
                            }
                            break;

                        case TN_STATE_IAC:
                            if (c == IAC) {
                                rxbuf[w++] = (char)IAC;
                                tn_state = TN_STATE_DATA;
                            } else if (c == SB) {
                                tn_state = TN_STATE_SB;
                            } else if (c == DO || c == DONT || c == WILL || c == WONT) {
                                pending_cmd = c;
                                tn_state = TN_STATE_OPT;
                            } else {
                                tn_state = TN_STATE_DATA;
                            }
                            break;

                        case TN_STATE_OPT:
                            tn_handle_opt(fd, pending_cmd, c);
                            tn_state = TN_STATE_DATA;
                            break;

                        case TN_STATE_SB:
                            if (c == IAC) {
                                tn_state = TN_STATE_SB_IAC;
                            }
                            break;

                        case TN_STATE_SB_IAC:
                            if (c == SE) {
                                tn_state = TN_STATE_DATA;
                            } else if (c == IAC) {
                                tn_state = TN_STATE_SB;
                            } else {
                                tn_state = TN_STATE_SB;
                            }
                            break;
                        }
                    }
                    got = w;
                }
                if (got > 0) {
                    rxbuf[got] = '\0';
                    Write(Output(), (CONST APTR)rxbuf, got);
                }
            }

            /* Console input, or file stdin with NVT CR LF and EOF */
            if (interactive) {
                if (WaitForChar(Input(), 0)) {
                    if (FGets(Input(), (STRPTR)txbuf, sizeof(txbuf)) != NULL) {
                        /* 6.10: Enter goes out as CR LF (NVT) */
                        LONG len = tn_to_nvt(txbuf, (LONG)strlen(txbuf), crlf_buf);
                        if (len > 0) {
                            tn_call_send(fd, crlf_buf, len, 0);
                        }
                    }
                }
            } else if (!stdin_eof) {
                LONG got = Read(Input(), (APTR)txbuf, sizeof(txbuf));
                if (got > 0) {
                    LONG o = tn_to_nvt(txbuf, got, crlf_buf);
                    if (o > 0) {

                        tn_call_send(fd, crlf_buf, o, 0);
                    }
                    idle = 0;
                } else {
                    stdin_eof = TRUE;
                    tn_call_shutdown(fd, 1); /* half-close, then drain */
                }
            } else {
                idle++;
                if (idle >= 10) running = 0; /* 10 x 0.2 s drained */
            }
        }
    }

    tn_call_closesocket(fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
