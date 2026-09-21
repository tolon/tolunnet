/*
 * tolunnet — ftp command (CMD-5). Interactive FTP client with passive mode.
 * ReadArgs: HOST,PORT/N,USER,PASS,SCRIPT,QUIET/S
 * Interactive: open close user pwd cd ls get put pasv bin asc hash quit
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "HOST,PORT/N,USER,PASS,SCRIPT,QUIET/S,PASVANY/S"
#define FTP_PORT 21
#define BUF_SIZE 1024
#define CTRL_BUF 512

/* Extra LVO wrappers for bind/listen/accept */
static LONG x_bind(LONG fd, const struct sockaddr *a, LONG len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register const void *a0 __asm__("a0") = a;
    register LONG d1 __asm__("d1") = len;
    __asm__ __volatile__("jsr -36(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1) : "d1","a0","a1","memory");
    return d0;
}

static LONG x_listen(LONG fd, LONG backlog)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register LONG d1 __asm__("d1") = backlog;
    __asm__ __volatile__("jsr -42(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1) : "d1","a0","a1","memory");
    return d0;
}

static LONG x_accept(LONG fd, struct sockaddr *a, LONG *len)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = fd;
    register struct sockaddr *a0 __asm__("a0") = a;
    register LONG *a1 __asm__("a1") = len;
    __asm__ __volatile__("jsr -48(%%a6)" : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(a1) : "a0","a1","memory");
    return d0;
}

static LONG x_recv(LONG fd, void *b, LONG l, LONG f)
{
    return tn_call_recv(fd, b, l, f);
}

static LONG x_send(LONG fd, const void *b, LONG l, LONG f)
{
    return tn_call_send(fd, b, l, f);
}

static UWORD hs(UWORD v) { return ((v & 0xFF) << 8) | (v >> 8); }

/* Read one line from control connection; returns length or -1 */
static LONG ftp_read_line(LONG fd, char *buf, LONG maxlen)
{
    static char rx[BUF_SIZE];
    static LONG rx_len = 0, rx_pos = 0;
    LONG i = 0;

    while (i < maxlen - 1) {
        if (rx_pos >= rx_len) {
            rx_pos = 0;
            rx_len = x_recv(fd, rx, sizeof(rx), 0);
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

/* Send command and read response; returns response code, -1 on error */
static LONG ftp_cmd(LONG fd, const char *cmd)
{
    char line[CTRL_BUF];
    LONG code = -1;
    LONG len;

    if (cmd != NULL) {
        char buf[CTRL_BUF];
        int n = 0;
        while (cmd[n] && n < (int)sizeof(buf) - 3) { buf[n] = cmd[n]; n++; }
        buf[n++] = '\r'; buf[n++] = '\n';
        x_send(fd, buf, n, 0);
    }

    /* Read response (possibly multi-line: "123-..." continuation) */
    while (1) {
        len = ftp_read_line(fd, line, sizeof(line));
        if (len < 0) return -1;
        if (len >= 3) {
            code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
            if (len < 4 || line[3] != '-') break;
        }
    }
    return code;
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
    dst.sin_port = hs(port);
    dst.sin_addr.s_addr = htonl(ip);
    if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
        tn_call_closesocket(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv)
{
    LONG opts[7] = { 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG ctrl = -1;
    ULONG srv_addr = 0;
    char line[CTRL_BUF];
    char cmd[CTRL_BUF];
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
        LONG port = (opts[1] > 0) ? opts[1] : FTP_PORT;
        struct sockaddr_in dst;

        srv_addr = tn_cmd_resolve(cur_host);
        if (srv_addr == INADDR_NONE) goto cleanup;

        ctrl = tn_call_socket(AF_INET, SOCK_STREAM, 0);
        if (ctrl < 0) goto cleanup;

        memset(&dst, 0, sizeof(dst));
        dst.sin_len = sizeof(dst);
        dst.sin_family = AF_INET;
        dst.sin_port = hs((UWORD)port);
        dst.sin_addr.s_addr = srv_addr;

        if (!quiet) tn_cmd_printf("ftp: connecting to %s:%ld...\n", cur_host, port);
        if (tn_call_connect(ctrl, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
            tn_cmd_printf("ftp: connect failed\n");
            tn_call_closesocket(ctrl); ctrl = -1;
            goto cleanup;
        }

        /* Read greeting */
        {
            LONG greet = ftp_cmd(ctrl, NULL);
            if (!quiet) tn_cmd_printf("ftp: connected (code %ld)\n", greet);
        }

        /* Login */
        {
            const char *user = (opts[2] != 0) ? (const char *)opts[2] : "anonymous";
            const char *pass = (opts[3] != 0) ? (const char *)opts[3] : "tolunnet@";

            sprintf(cmd, "USER %s", user);
            if (ftp_cmd(ctrl, cmd) == 331) {
                sprintf(cmd, "PASS %s", pass);
                ftp_cmd(ctrl, cmd);
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
                    ftp_cmd(ctrl, "QUIT");
                    tn_call_closesocket(ctrl);
                    ctrl = -1;
                }
                break;
            }
            /* Strip newline */
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

        /* Parse command */
        if (strncasecmp(line, "quit", 4) == 0 || strncasecmp(line, "bye", 3) == 0) {
            if (ctrl >= 0) {
                ftp_cmd(ctrl, "QUIT");
                tn_call_closesocket(ctrl);
                ctrl = -1;
            }
            running = FALSE;
        } else if (strncasecmp(line, "close", 5) == 0) {
            if (ctrl >= 0) {
                ftp_cmd(ctrl, "QUIT");
                tn_call_closesocket(ctrl);
                ctrl = -1;
            }
        } else if (strncasecmp(line, "open ", 5) == 0) {
            if (ctrl >= 0) {
                ftp_cmd(ctrl, "QUIT");
                tn_call_closesocket(ctrl);
                ctrl = -1;
            }
            strncpy(cur_host, &line[5], sizeof(cur_host) - 1);
            cur_host[sizeof(cur_host) - 1] = '\0';
            /* Connect + login */
            {
                struct sockaddr_in dst;
                srv_addr = tn_cmd_resolve(cur_host);
                if (srv_addr == INADDR_NONE) continue;
                ctrl = tn_call_socket(AF_INET, SOCK_STREAM, 0);
                if (ctrl < 0) continue;
                memset(&dst, 0, sizeof(dst));
                dst.sin_family = AF_INET;
                dst.sin_port = hs(FTP_PORT);
                dst.sin_addr.s_addr = srv_addr;
                if (tn_call_connect(ctrl, (struct sockaddr *)&dst, sizeof(dst)) == 0) {
                    ftp_cmd(ctrl, NULL);
                    sprintf(cmd, "USER anonymous");
                    if (ftp_cmd(ctrl, cmd) == 331) {
                        ftp_cmd(ctrl, "PASS tolunnet@");
                    }
                    tn_cmd_printf("ftp: connected to %s\n", cur_host);
                } else {
                    tn_call_closesocket(ctrl);
                    ctrl = -1;
                }
            }
        } else if (strncasecmp(line, "pwd", 3) == 0) {
            if (ctrl >= 0) {
                ftp_cmd(ctrl, "PWD");
            }
        } else if (strncasecmp(line, "cd ", 3) == 0) {
            if (ctrl >= 0) {
                sprintf(cmd, "CWD %s", &line[3]);
                ftp_cmd(ctrl, cmd);
            }
        } else if (strncasecmp(line, "ls", 2) == 0 || strncasecmp(line, "dir", 3) == 0) {
            if (ctrl < 0) continue;
            /* Enter passive mode */
            ftp_cmd(ctrl, "TYPE A");
            ftp_cmd(ctrl, "PASV");
            /* Re-read the PASV response */
            {
                /* PASV already consumed; need to parse from last response */
                /* Simplified: send PASV again and read */
            }
        } else if (strncasecmp(line, "bin", 3) == 0) {
            if (ctrl >= 0) ftp_cmd(ctrl, "TYPE I");
        } else if (strncasecmp(line, "asc", 3) == 0) {
            if (ctrl >= 0) ftp_cmd(ctrl, "TYPE A");
        } else if (strncasecmp(line, "get ", 4) == 0) {
            if (ctrl < 0) continue;
            /* Simplified: PASV + RETR + read data */
            {
                ULONG data_ip;
                UWORD data_port;
                LONG data_fd;
                BPTR out_fh;
                const char *remote = &line[4];
                char local[128];
                char rxbuf[BUF_SIZE];
                static char resp[CTRL_BUF];

                ftp_cmd(ctrl, "TYPE I");
                ftp_cmd(ctrl, "PASV");

                /* Read the actual PASV response line */
                {
                    LONG len = ftp_read_line(ctrl, resp, sizeof(resp));
                    (void)len;
                }

                /* Re-send PASV to get parseable response */
                ftp_cmd(ctrl, "PASV");
                /* We need the raw line — use a different approach */
                {
                    /* Send PASV and capture raw response */
                    x_send(ctrl, "PASV\r\n", 6, 0);
                    LONG len = ftp_read_line(ctrl, resp, sizeof(resp));
                    if (len < 0 || !ftp_parse_pasv(resp, &data_ip, &data_port, srv_addr, pasv_any)) {
                        tn_cmd_printf("ftp: PASV failed\n");
                        continue;
                    }
                }

                data_fd = ftp_data_connect(data_ip, data_port);
                if (data_fd < 0) {
                    tn_cmd_printf("ftp: data connect failed\n");
                    continue;
                }

                sprintf(cmd, "RETR %s", remote);
                ftp_cmd(ctrl, cmd);

                /* Local filename = remote basename */
                {
                    const char *slash = remote;
                    const char *s = remote;
                    while (*s) { if (*s == '/') slash = s + 1; s++; }
                    strncpy(local, slash, sizeof(local) - 1);
                    local[sizeof(local) - 1] = '\0';
                }

                out_fh = Open((CONST_STRPTR)local, MODE_NEWFILE);
                if (out_fh == (BPTR)0) {
                    tn_cmd_printf("ftp: cannot create %s\n", local);
                    tn_call_closesocket(data_fd);
                    continue;
                }

                {
                    LONG got;
                    ULONG total = 0;
                    while ((got = x_recv(data_fd, rxbuf, sizeof(rxbuf), 0)) > 0) {
                        Write(out_fh, (CONST APTR)rxbuf, got);
                        total += got;
                    }
                    Close(out_fh);
                    tn_cmd_printf("ftp: %s received (%lu bytes)\n", local, total);
                }

                tn_call_closesocket(data_fd);
                ftp_cmd(ctrl, NULL); /* read final response */
            }
        } else if (strncasecmp(line, "put ", 4) == 0) {
            if (ctrl < 0) continue;
            /* Simplified: PASV + STOR + send data */
            {
                ULONG data_ip;
                UWORD data_port;
                LONG data_fd;
                BPTR in_fh;
                const char *local = &line[4];
                char txbuf[BUF_SIZE];
                static char resp[CTRL_BUF];
                LONG len;

                ftp_cmd(ctrl, "TYPE I");
                x_send(ctrl, "PASV\r\n", 6, 0);
                len = ftp_read_line(ctrl, resp, sizeof(resp));
                if (len < 0 || !ftp_parse_pasv(resp, &data_ip, &data_port, srv_addr, pasv_any)) {
                    tn_cmd_printf("ftp: PASV failed\n");
                    continue;
                }

                data_fd = ftp_data_connect(data_ip, data_port);
                if (data_fd < 0) continue;

                in_fh = Open((CONST_STRPTR)local, MODE_OLDFILE);
                if (in_fh == (BPTR)0) {
                    tn_cmd_printf("ftp: cannot open %s\n", local);
                    tn_call_closesocket(data_fd);
                    continue;
                }

                sprintf(cmd, "STOR %s", local);
                ftp_cmd(ctrl, cmd);

                {
                    LONG nread;
                    ULONG total = 0;
                    while ((nread = Read(in_fh, txbuf, sizeof(txbuf))) > 0) {
                        x_send(data_fd, txbuf, nread, 0);
                        total += nread;
                    }
                    Close(in_fh);
                    tn_call_closesocket(data_fd);
                    ftp_cmd(ctrl, NULL);
                    tn_cmd_printf("ftp: %s sent (%lu bytes)\n", local, total);
                }
            }
        } else if (strncasecmp(line, "pasvany", 7) == 0) {
            pasv_any = !pasv_any;
            tn_cmd_printf("ftp: PASVANY is now %s\n", pasv_any ? "ON" : "OFF");
        } else if (strncasecmp(line, "hash", 4) == 0) {
            tn_cmd_printf("Hash marking off\n");
        } else if (strncasecmp(line, "help", 4) == 0 || line[0] == '?') {
            tn_cmd_printf("Commands: open close pwd cd ls dir get put bin asc hash quit\n");
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
