/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — ftp command (CMD-5). Interactive FTP client with passive mode.
 * ReadArgs: HOST,PORT/N,USER,PASS,SCRIPT,QUIET/S,PASVANY/S
 * Interactive: open close pwd cd ls dir get put bin asc pasvany hash quit
 *
 * z.ai step 8c item 1: RFC 959 replies (read until <same code> + space),
 * one PASV per transfer, working ls/get/put, real exit code (10 on any
 * 4xx/5xx or failed transfer).
 */
#include "cmdlib.h"
#include <string.h>
TN_VERSTAG_DEF("ftp");

#define TEMPLATE "HOST,PORT/N,USER,PASS,SCRIPT,QUIET/S,PASVANY/S"
#define FTP_PORT 21
#define BUF_SIZE 1024
#define CTRL_BUF 512

/* Read one CRLF line from the control connection; returns length or -1 */
static LONG ftp_read_line(LONG fd, char *buf, LONG maxlen)
{
    static char rx[BUF_SIZE];
    static LONG rx_len = 0, rx_pos = 0;
    LONG i = 0;

    while (i < maxlen - 1) {
        if (rx_pos >= rx_len) {
            rx_pos = 0;
            rx_len = tn_call_recv(fd, rx, sizeof(rx), 0);
            if (rx_len <= 0) return -1;
        }
        buf[i] = rx[rx_pos];
        rx_pos++;
        if (buf[i] == '\n') {
            buf[i] = '\0';
            if (i > 0 && buf[i-1] == '\r') buf[i-1] = '\0';
            return i;
        }
        i++;
    }
    buf[maxlen - 1] = '\0';
    return i;
}

/* Send VERB [ARG], read the reply per RFC 959: the reply ends at the
 * first line that starts with the same 3-digit code as the first line
 * followed by a space (or a bare code). Every reply line is printed
 * unless quiet. Returns the final code, -1 on connection error.
 * The final line is copied to last_line when requested. */
static LONG ftp_cmd_resp(LONG fd, const char *verb, const char *arg,
                         char *last_line, LONG last_max, BOOL quiet)
{
    char line[CTRL_BUF];
    LONG code = -1, first = -1, len;

    if (verb != NULL) {
        char buf[CTRL_BUF];
        int n = 0;
        /* SEC item 6: bounded copy prevents overflow from long argv */
        while (verb[n] && n < (int)sizeof(buf) - 3) { buf[n] = verb[n]; n++; }
        if (arg != NULL && n < (int)sizeof(buf) - 4) {
            buf[n++] = ' ';
            while (*arg && n < (int)sizeof(buf) - 3) { buf[n++] = *arg++; }
        }
        buf[n++] = '\r'; buf[n++] = '\n';
        if (tn_call_send(fd, buf, n, 0) != n) return -1;
    }

    for (;;) {
        len = ftp_read_line(fd, line, sizeof(line));
        if (len < 0) return -1;
        if (!quiet) tn_cmd_printf("%s\n", line);
        if (len >= 3 &&
            line[0] >= '0' && line[0] <= '9' &&
            line[1] >= '0' && line[1] <= '9' &&
            line[2] >= '0' && line[2] <= '9') {
            code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
            if (first < 0) first = code;
            if (code == first && (len == 3 || line[3] == ' ')) {
                if (last_line != NULL && last_max > 0) {
                    LONG k = 0;
                    while (line[k] && k < last_max - 1) { last_line[k] = line[k]; k++; }
                    last_line[k] = '\0';
                }
                return code;
            }
        }
        /* interim continuation: keep reading */
    }
}

/* Parse PASV response: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)"
 * SEC item 5: reject if ip != peer unless pasv_any is given; clamp octets/port; log refusal */
static int ftp_parse_pasv(const char *resp, ULONG *ip, UWORD *port, ULONG peer_ip, BOOL pasv_any)
{
    const char *p = resp;
    int comma = 0;
    ULONG parts[6];
    while (*p && *p != '(') p++;
    if (*p != '(') return 0;
    p++;
    while (*p && comma < 6) {
        ULONG v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        if (v > 255) v = 255;
        parts[comma++] = v;
        if (*p == ',') p++;
    }
    if (comma != 6) return 0;
    {
        ULONG parsed_ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        UWORD parsed_port = (UWORD)((parts[4] << 8) | parts[5]);

        if (!pasv_any && peer_ip != 0 && parsed_ip != peer_ip) {
            tn_cmd_printf("ftp: security: PASV IP %lu.%lu.%lu.%lu != peer %lu.%lu.%lu.%lu (refused; use PASVANY to allow)\n",
                          parts[0], parts[1], parts[2], parts[3],
                          (peer_ip >> 24) & 0xFF, (peer_ip >> 16) & 0xFF,
                          (peer_ip >> 8) & 0xFF, peer_ip & 0xFF);
            return 0;
        }

        *ip = parsed_ip;
        *port = parsed_port;
        return 1;
    }
}

/* z.ai step 8c item 1: ONE PASV per transfer - send PASV and parse the
 * 227 line that answers it. */
static int ftp_pasv(LONG ctrl, ULONG *ip, UWORD *port, ULONG peer_ip,
                    BOOL pasv_any, BOOL quiet)
{
    char resp[CTRL_BUF];
    if (ftp_cmd_resp(ctrl, "PASV", NULL, resp, sizeof(resp), quiet) != 227) return 0;
    return ftp_parse_pasv(resp, ip, port, peer_ip, pasv_any);
}

/* Open passive data connection */
static LONG ftp_data_connect(ULONG ip, UWORD port)
{
    LONG fd;
    struct sockaddr_in dst;
    fd = tn_call_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&dst, 0, sizeof(dst));
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = htonl(ip);
    if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
        tn_call_closesocket(fd);
        return -1;
    }
    return fd;
}

static char *ftp_skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int main(int argc, char **argv)
{
    LONG opts[7] = { 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG ctrl = -1;
    ULONG srv_addr = 0;
    char line[CTRL_BUF];
    BPTR script_fh = (BPTR)0;
    BOOL quiet;
    BOOL pasv_any;
    BOOL running = TRUE;
    char cur_host[64];

    cur_host[0] = '\0';

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"ftp");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    quiet = (opts[5] != 0);
    pasv_any = (opts[6] != 0);

    if (opts[4] != 0) {
        script_fh = Open((CONST_STRPTR)opts[4], MODE_OLDFILE);
        if (script_fh == (BPTR)0) {
            tn_cmd_printf("ftp: cannot open script %s\n", (const char *)opts[4]);
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }
    }

    /* Auto-connect if HOST given */
    if (opts[0] != 0) {
        strncpy(cur_host, (const char *)opts[0], sizeof(cur_host) - 1);
        cur_host[sizeof(cur_host) - 1] = '\0';
    }

    if (cur_host[0] != '\0') {
        LONG port = (opts[1] != 0 && *(LONG *)opts[1] > 0)
                        ? *(LONG *)opts[1] : FTP_PORT; /* /N is a pointer */
        struct sockaddr_in dst;

        srv_addr = tn_cmd_resolve(cur_host);
        if (srv_addr == INADDR_NONE) { rc = TN_CMD_FAIL; goto cleanup; }

        ctrl = tn_call_socket(AF_INET, SOCK_STREAM, 0);
        if (ctrl < 0) { rc = TN_CMD_FAIL; goto cleanup; }

        memset(&dst, 0, sizeof(dst));
        dst.sin_len = sizeof(dst);
        dst.sin_family = AF_INET;
        dst.sin_port = htons((UWORD)port);
        dst.sin_addr.s_addr = srv_addr;

        if (!quiet) tn_cmd_printf("ftp: connecting to %s:%ld...\n", cur_host, port);
        if (tn_call_connect(ctrl, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
            tn_cmd_printf("ftp: connect failed\n");
            tn_call_closesocket(ctrl); ctrl = -1;
            rc = TN_CMD_FAIL;
            goto cleanup;
        }

        {
            LONG greet = ftp_cmd_resp(ctrl, NULL, NULL, NULL, 0, quiet);
            if (greet < 200 || greet >= 300) {
                tn_call_closesocket(ctrl); ctrl = -1;
                rc = TN_CMD_FAIL;
                goto cleanup;
            }
        }

        {
            const char *user = (opts[2] != 0) ? (const char *)opts[2] : "anonymous";
            const char *pass = (opts[3] != 0) ? (const char *)opts[3] : "tolunnet@";
            LONG ucode = ftp_cmd_resp(ctrl, "USER", user, NULL, 0, quiet);
            if (ucode == 331) {
                if (ftp_cmd_resp(ctrl, "PASS", pass, NULL, 0, quiet) >= 400) rc = TN_CMD_FAIL;
            } else if (ucode < 200 || ucode >= 400) {
                rc = TN_CMD_FAIL;
            }
        }
    }

    /* Main loop: read commands from console or script */
    while (running) {
        if (script_fh != (BPTR)0) {
            if (FGets(script_fh, (STRPTR)line, sizeof(line)) == NULL) {
                Close(script_fh);
                script_fh = (BPTR)0;
                if (ctrl >= 0) {
                    ftp_cmd_resp(ctrl, "QUIT", NULL, NULL, 0, quiet);
                    tn_call_closesocket(ctrl);
                    ctrl = -1;
                }
                break;
            }
            {
                int n = strlen(line);
                while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
            }
            if (!quiet) tn_cmd_printf("ftp> %s\n", line);
        } else {
            if (ctrl >= 0) {
                tn_cmd_printf("ftp> ");
                Flush(Output());
            }
            if (FGets(Input(), (STRPTR)line, sizeof(line)) == NULL) break;
            {
                int n = strlen(line);
                while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
            }
        }

        if (line[0] == '\0') continue;

        if (strncasecmp(line, "quit", 4) == 0 || strncasecmp(line, "bye", 3) == 0) {
            if (ctrl >= 0) {
                ftp_cmd_resp(ctrl, "QUIT", NULL, NULL, 0, quiet);
                tn_call_closesocket(ctrl);
                ctrl = -1;
            }
            running = FALSE;
        } else if (strncasecmp(line, "close", 5) == 0) {
            if (ctrl >= 0) {
                ftp_cmd_resp(ctrl, "QUIT", NULL, NULL, 0, quiet);
                tn_call_closesocket(ctrl);
                ctrl = -1;
            }
        } else if (strncasecmp(line, "open ", 5) == 0) {
            char *p, *host, *pport = NULL;
            if (ctrl >= 0) {
                ftp_cmd_resp(ctrl, "QUIT", NULL, NULL, 0, quiet);
                tn_call_closesocket(ctrl);
                ctrl = -1;
            }
            p = ftp_skip_ws(&line[5]);
            host = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = '\0'; pport = ftp_skip_ws(p + 1); }
            strncpy(cur_host, host, sizeof(cur_host) - 1);
            cur_host[sizeof(cur_host) - 1] = '\0';
            {
                LONG port = FTP_PORT;
                struct sockaddr_in dst;
                if (pport != NULL && *pport) {
                    LONG v = 0;
                    while (*pport >= '0' && *pport <= '9') { v = v * 10 + (*pport - '0'); pport++; }
                    if (v > 0 && v < 65536) port = v;
                }
                srv_addr = tn_cmd_resolve(cur_host);
                if (srv_addr == INADDR_NONE) { rc = TN_CMD_FAIL; continue; }
                ctrl = tn_call_socket(AF_INET, SOCK_STREAM, 0);
                if (ctrl < 0) { rc = TN_CMD_FAIL; continue; }
                /* z.ai step 8c item 1: sin_len is set here */
                memset(&dst, 0, sizeof(dst));
                dst.sin_len = sizeof(dst);
                dst.sin_family = AF_INET;
                dst.sin_port = htons((UWORD)port);
                dst.sin_addr.s_addr = srv_addr;
                if (tn_call_connect(ctrl, (struct sockaddr *)&dst, sizeof(dst)) == 0) {
                    if (ftp_cmd_resp(ctrl, NULL, NULL, NULL, 0, quiet) < 200 ||
                        ftp_cmd_resp(ctrl, "USER", "anonymous", NULL, 0, quiet) != 331) {
                        tn_call_closesocket(ctrl); ctrl = -1; rc = TN_CMD_FAIL;
                        continue;
                    }
                    if (ftp_cmd_resp(ctrl, "PASS", "tolunnet@", NULL, 0, quiet) >= 400) {
                        rc = TN_CMD_FAIL;
                        continue;
                    }
                    tn_cmd_printf("ftp: connected to %s\n", cur_host);
                } else {
                    tn_call_closesocket(ctrl);
                    ctrl = -1;
                    rc = TN_CMD_FAIL;
                }
            }
        } else if (strncasecmp(line, "pwd", 3) == 0) {
            if (ctrl < 0) continue;
            if (ftp_cmd_resp(ctrl, "PWD", NULL, NULL, 0, quiet) >= 400) rc = TN_CMD_FAIL;
        } else if (strncasecmp(line, "cd ", 3) == 0) {
            if (ctrl < 0) continue;
            if (ftp_cmd_resp(ctrl, "CWD", ftp_skip_ws(&line[3]), NULL, 0, quiet) >= 400) rc = TN_CMD_FAIL;
        } else if (strncasecmp(line, "ls", 2) == 0 || strncasecmp(line, "dir", 3) == 0) {
            /* z.ai step 8c item 1: PASV -> data connect -> LIST/NLST,
             * copy data to stdout, require 226/250. */
            ULONG data_ip;
            UWORD data_port;
            LONG data_fd, code;
            BOOL is_list = (strncasecmp(line, "ls", 2) == 0);
            char *path = NULL;

            if (ctrl < 0) continue;
            if (line[2] == ' ' || (line[3] == ' ')) {
                path = ftp_skip_ws(&line[is_list ? 2 : 3]);
                if (*path == '\0') path = NULL;
            }
            if (ftp_cmd_resp(ctrl, "TYPE", "A", NULL, 0, quiet) >= 400) { rc = TN_CMD_FAIL; continue; }
            if (!ftp_pasv(ctrl, &data_ip, &data_port, srv_addr, pasv_any, quiet)) {
                tn_cmd_printf("ftp: PASV failed\n");
                rc = TN_CMD_FAIL;
                continue;
            }
            data_fd = ftp_data_connect(data_ip, data_port);
            if (data_fd < 0) {
                tn_cmd_printf("ftp: data connect failed\n");
                rc = TN_CMD_FAIL;
                continue;
            }
            code = ftp_cmd_resp(ctrl, is_list ? "LIST" : "NLST", path, NULL, 0, quiet);
            if (code < 100 || code >= 200) {
                tn_call_closesocket(data_fd);
                rc = TN_CMD_FAIL;
                continue;
            }
            {
                char rxbuf[BUF_SIZE];
                LONG got;
                while ((got = tn_call_recv(data_fd, rxbuf, sizeof(rxbuf), 0)) > 0) {
                    Write(Output(), (CONST APTR)rxbuf, got);
                }
            }
            tn_call_closesocket(data_fd);
            code = ftp_cmd_resp(ctrl, NULL, NULL, NULL, 0, quiet);
            if (code != 226 && code != 250) rc = TN_CMD_FAIL;
        } else if (strncasecmp(line, "bin", 3) == 0) {
            if (ctrl >= 0 && ftp_cmd_resp(ctrl, "TYPE", "I", NULL, 0, quiet) >= 400) rc = TN_CMD_FAIL;
        } else if (strncasecmp(line, "asc", 3) == 0) {
            if (ctrl >= 0 && ftp_cmd_resp(ctrl, "TYPE", "A", NULL, 0, quiet) >= 400) rc = TN_CMD_FAIL;
        } else if (strncasecmp(line, "get ", 4) == 0) {
            /* z.ai step 8c item 1: TYPE I -> one PASV -> connect -> RETR.
             * A non-1xx RETR reply is printed, the data socket closed and
             * rc set. Otherwise write the file and require 2xx. */
            ULONG data_ip;
            UWORD data_port;
            LONG data_fd, code;
            BPTR out_fh;
            char *remote, *local, *p;
            char rxbuf[BUF_SIZE];

            if (ctrl < 0) continue;
            remote = ftp_skip_ws(&line[4]);
            p = remote;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = '\0'; local = ftp_skip_ws(p + 1); } else { local = NULL; }
            if (*remote == '\0') { tn_cmd_printf("?Invalid get\n"); continue; }

            if (ftp_cmd_resp(ctrl, "TYPE", "I", NULL, 0, quiet) >= 400) { rc = TN_CMD_FAIL; continue; }
            if (!ftp_pasv(ctrl, &data_ip, &data_port, srv_addr, pasv_any, quiet)) {
                tn_cmd_printf("ftp: PASV failed\n");
                rc = TN_CMD_FAIL;
                continue;
            }
            data_fd = ftp_data_connect(data_ip, data_port);
            if (data_fd < 0) {
                tn_cmd_printf("ftp: data connect failed\n");
                rc = TN_CMD_FAIL;
                continue;
            }
            code = ftp_cmd_resp(ctrl, "RETR", remote, NULL, 0, quiet);
            if (code < 100 || code >= 200) {
                /* reply line (e.g. 550) already printed */
                tn_call_closesocket(data_fd);
                rc = TN_CMD_FAIL;
                continue;
            }
            if (local == NULL) local = remote;
            out_fh = Open((CONST_STRPTR)local, MODE_NEWFILE);
            if (out_fh == (BPTR)0) {
                tn_cmd_printf("ftp: cannot create %s\n", local);
                tn_call_closesocket(data_fd);
                rc = TN_CMD_FAIL;
                continue;
            }
            {
                LONG got;
                ULONG total = 0;
                while ((got = tn_call_recv(data_fd, rxbuf, sizeof(rxbuf), 0)) > 0) {
                    Write(out_fh, (CONST APTR)rxbuf, got);
                    total += got;
                }
                Close(out_fh);
                tn_call_closesocket(data_fd);
                code = ftp_cmd_resp(ctrl, NULL, NULL, NULL, 0, quiet);
                if (code < 200 || code >= 300) {
                    rc = TN_CMD_FAIL;
                } else {
                    tn_cmd_printf("ftp: %s received (%lu bytes)\n", local, total);
                }
            }
        } else if (strncasecmp(line, "put ", 4) == 0) {
            /* z.ai step 8c item 1: same shape with STOR. */
            ULONG data_ip;
            UWORD data_port;
            LONG data_fd, code;
            BPTR in_fh;
            char *local, *remote, *p;
            char txbuf[BUF_SIZE];

            if (ctrl < 0) continue;
            local = ftp_skip_ws(&line[4]);
            p = local;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = '\0'; remote = ftp_skip_ws(p + 1); } else { remote = NULL; }
            if (*local == '\0') { tn_cmd_printf("?Invalid put\n"); continue; }

            if (ftp_cmd_resp(ctrl, "TYPE", "I", NULL, 0, quiet) >= 400) { rc = TN_CMD_FAIL; continue; }
            if (!ftp_pasv(ctrl, &data_ip, &data_port, srv_addr, pasv_any, quiet)) {
                tn_cmd_printf("ftp: PASV failed\n");
                rc = TN_CMD_FAIL;
                continue;
            }
            data_fd = ftp_data_connect(data_ip, data_port);
            if (data_fd < 0) {
                tn_cmd_printf("ftp: data connect failed\n");
                rc = TN_CMD_FAIL;
                continue;
            }
            in_fh = Open((CONST_STRPTR)local, MODE_OLDFILE);
            if (in_fh == (BPTR)0) {
                tn_cmd_printf("ftp: cannot open %s\n", local);
                tn_call_closesocket(data_fd);
                rc = TN_CMD_FAIL;
                continue;
            }
            if (remote == NULL) {
                const char *slash = local;
                const char *s = local;
                while (*s) { if (*s == '/' || *s == ':') slash = s + 1; s++; }
                remote = (char *)slash;
            }
            code = ftp_cmd_resp(ctrl, "STOR", remote, NULL, 0, quiet);
            if (code < 100 || code >= 200) {
                Close(in_fh);
                tn_call_closesocket(data_fd);
                rc = TN_CMD_FAIL;
                continue;
            }
            {
                LONG nread;
                ULONG total = 0;
                while ((nread = Read(in_fh, txbuf, sizeof(txbuf))) > 0) {
                    if (tn_call_send(data_fd, txbuf, nread, 0) != nread) {
                        rc = TN_CMD_FAIL;
                        break;
                    }
                    total += nread;
                }
                Close(in_fh);
                tn_call_closesocket(data_fd);
                code = ftp_cmd_resp(ctrl, NULL, NULL, NULL, 0, quiet);
                if (code < 200 || code >= 300) {
                    rc = TN_CMD_FAIL;
                } else {
                    tn_cmd_printf("ftp: %s sent (%lu bytes)\n", local, total);
                }
            }
        } else if (strncasecmp(line, "pasvany", 7) == 0) {
            pasv_any = !pasv_any;
            tn_cmd_printf("ftp: PASVANY is now %s\n", pasv_any ? "ON" : "OFF");
        } else if (strncasecmp(line, "hash", 4) == 0) {
            tn_cmd_printf("Hash marking off\n");
        } else if (strncasecmp(line, "help", 4) == 0 || line[0] == '?') {
            tn_cmd_printf("Commands: open host [port] close pwd cd ls [path] dir get remote [local] put local [remote] bin asc pasvany hash quit\n");
        } else {
            tn_cmd_printf("?Unknown command '%s'\n", line);
        }
    }

cleanup:
    if (script_fh != (BPTR)0) Close(script_fh);
    if (ctrl >= 0) tn_call_closesocket(ctrl);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
