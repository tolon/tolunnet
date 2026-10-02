/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * TolunnetGet — Standalone HTTP/TCP Client Command for AmigaOS (curl/wget).
 *
 * Implements HTTP/1.1 with Host header, redirect following (<=5 hops),
 * Content-Length progress display, chunked transfer decoding, Range resume,
 * and standard AmigaOS ReadArgs template: "URL/A,PORT/N,PATH,TO/K,QUIET/S".
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/memory.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "cmdlib.h"

#include "../common/log.h"
#include "../common/http_url.h"
#include "../common/rawfmt.h"
#include <string.h>
TN_VERSTAG_DEF("TolunnetGet");

/* 6.10: the private LVO wrappers that read results from A0 are gone -
 * the bsdsocket ABI returns in D0; cmdlib's tn_call_* wrappers do. */

/* 6.5: body bytes to the output; 0 ok, -1 short Write, -2 bad chunking */
static int get_feed_body(BPTR fh, struct TnChunkState *cst, int chunked,
                         const char *buf, LONG n, LONG *total)
{
    if (chunked) {
        LONG pos = 0;
        while (pos < n && cst->state != TN_CHUNK_STATE_DONE) {
            int consumed = 0;
            const char *chunk_data = NULL;
            int chunk_len = 0;
            tn_chunk_feed(cst, buf + pos, (int)(n - pos),
                          &consumed, &chunk_data, &chunk_len);
            if (cst->state == TN_CHUNK_STATE_ERROR) return -2;
            pos += consumed;
            if (chunk_len > 0) {
                if (Write(fh, (APTR)chunk_data, chunk_len) != chunk_len) return -1;
                *total += chunk_len;
            }
            if (consumed <= 0 && chunk_len <= 0) return -2; /* no progress */
        }
    } else if (n > 0) {
        if (Write(fh, (APTR)buf, n) != n) return -1;
        *total += n;
    }
    return 0;
}

static int str_len(const char *s)
{
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

enum {
    OPT_URL,
    OPT_PORT,
    OPT_PATH,
    OPT_TO,
    OPT_QUIET,
    OPT_CONTINUE,
    OPT_NUM_OPTS
};

int main(void)
{
    struct RDArgs *rdargs;
    LONG opts[OPT_NUM_OPTS];
    struct TnUrl current_url, next_url;
    CONST_STRPTR to_path = NULL;
    BPTR out_fh = 0;
    BOOL to_file = FALSE;
    BOOL quiet = FALSE;
    BOOL do_continue = FALSE;
    LONG resume_offset = 0;
    LONG resp_start = 0;
    int redirect_count = 0;
    LONG sock = -1;
    LONG total_written = 0;
    LONG last_progress_bytes = 0;
    int exit_code = 0;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_BASIC;

    for (int i = 0; i < OPT_NUM_OPTS; i++) opts[i] = 0;

    rdargs = ReadArgs((CONST_STRPTR)"URL/A,PORT/N,PATH,TO/K,QUIET/S,CONTINUE/S", opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"wget");
        return 20;
    }

    if (opts[OPT_QUIET]) {
        quiet = TRUE;
        g_log_level = TN_LOG_OFF;
    }

    if (opts[OPT_TO]) {
        to_path = (CONST_STRPTR)opts[OPT_TO];
        to_file = TRUE;
    }

    /* 1. Initial URL Parse */
    if (tn_http_parse_url((const char *)opts[OPT_URL], &current_url) != 0) {
        PutStr((CONST_STRPTR)"wget: invalid URL or hostname\n");
        FreeArgs(rdargs);
        return 20;
    }

    if (opts[OPT_PORT] && current_url.port == 80) {
        LONG p = *(LONG *)opts[OPT_PORT];
        if (p > 0 && p <= 65535) current_url.port = (uint32_t)p;
    }

    if (opts[OPT_PATH] && (current_url.path[0] == '\0' || (current_url.path[0] == '/' && current_url.path[1] == '\0'))) {
        const char *p = (const char *)opts[OPT_PATH];
        size_t idx = 0;
        if (p[0] != '/') current_url.path[idx++] = '/';
        while (*p && idx < sizeof(current_url.path) - 1) current_url.path[idx++] = *p++;
        current_url.path[idx] = '\0';
    }

    if (current_url.scheme == TN_SCHEME_HTTPS) {
        PutStr((CONST_STRPTR)"https:// not supported; URL requires TLS/SSL\n");
        FreeArgs(rdargs);
        return 20;
    }

    /* 2. Resume an existing file only with CONTINUE (z.ai step 8b item 4):
     * without it an existing target is overwritten. The FileInfoBlock
     * must be longword-aligned, so it comes from AllocDosObject. */
    if (opts[OPT_CONTINUE]) do_continue = TRUE;
    if (do_continue && to_file && to_path != NULL) {
        BPTR lock = Lock(to_path, ACCESS_READ);
        if (lock != 0) {
            struct FileInfoBlock *fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
            if (fib != NULL) {
                if (Examine(lock, fib) && fib->fib_DirEntryType < 0 && fib->fib_Size > 0) {
                    resume_offset = (LONG)fib->fib_Size;
                }
                FreeDosObject(DOS_FIB, fib);
            }
            UnLock(lock);
        }
    }

    /* 3. Open bsdsocket.library */
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr((CONST_STRPTR)"wget: unable to open bsdsocket.library\n");
        FreeArgs(rdargs);
        return 20;
    }

    /* 4. Redirect & Request Loop */
    static char req_buf[1024];
    static char rx_buf[1024];
    static char hdr_buf[2048];

    while (redirect_count <= 5) {
        struct sockaddr_in srv_sin;
        struct hostent *he;
        in_addr_t target_ip;
        LONG rc;

        if (current_url.scheme == TN_SCHEME_HTTPS) {
            PutStr((CONST_STRPTR)"https:// not supported; URL requires TLS/SSL\n");
            exit_code = 20;
            break;
        }

        /* Resolve Host */
        target_ip = (in_addr_t)tn_call_inet_addr(current_url.host);
        if (target_ip == (in_addr_t)INADDR_NONE) {
            he = tn_call_gethostbyname(current_url.host);
            if (he != NULL && he->h_addr_list != NULL && he->h_addr_list[0] != NULL) {
                memcpy(&target_ip, he->h_addr_list[0], sizeof(target_ip)); /* TNET-139 */
            } else {
                if (!quiet) tn_logf(TN_LOG_BASIC, "wget: unable to resolve host %s\n", current_url.host);
                exit_code = 20;
                break;
            }
        }

        if (!quiet) {
            struct in_addr ia;
            ia.s_addr = target_ip;
            tn_logf(TN_LOG_BASIC, "wget: connecting to %s (%s) port %lu...\n",
                    current_url.host, tn_call_inet_ntoa(ia), (ULONG)current_url.port);
        }

        sock = tn_call_socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            if (!quiet) tn_logf(TN_LOG_BASIC, "wget: socket creation failed (rc=%ld)\n", sock);
            exit_code = 20;
            break;
        }

        for (size_t i = 0; i < sizeof(srv_sin); i++) ((char *)&srv_sin)[i] = 0;
        srv_sin.sin_len         = sizeof(struct sockaddr_in);
        srv_sin.sin_family      = AF_INET;
        srv_sin.sin_port        = htons((UWORD)current_url.port);
        srv_sin.sin_addr.s_addr = target_ip;

        rc = tn_call_connect(sock, (struct sockaddr *)&srv_sin, sizeof(srv_sin));
        if (rc != 0) {
            if (!quiet) tn_logf(TN_LOG_BASIC, "wget: connection failed (rc=%ld)\n", rc);
            tn_call_closesocket(sock);
            sock = -1;
            exit_code = 20;
            break;
        }

        /* Format HTTP/1.1 Request with Host and Connection: close */
        req_buf[0] = '\0';
        if (resume_offset > 0) {
            ULONG req_args[3];
            req_args[0] = (ULONG)current_url.path;
            req_args[1] = (ULONG)current_url.host;
            req_args[2] = (ULONG)resume_offset;
            RawDoFmt((CONST_STRPTR)"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: TolunnetGet/1.1 (AmigaOS)\r\nRange: bytes=%ld-\r\nConnection: close\r\n\r\n",
                     (APTR)req_args, TN_RAWFMT_PUTCH, req_buf);
        } else {
            ULONG req_args[2];
            req_args[0] = (ULONG)current_url.path;
            req_args[1] = (ULONG)current_url.host;
            RawDoFmt((CONST_STRPTR)"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: TolunnetGet/1.1 (AmigaOS)\r\nConnection: close\r\n\r\n",
                     (APTR)req_args, TN_RAWFMT_PUTCH, req_buf);
        }

        tn_call_send(sock, req_buf, str_len(req_buf), 0);

        /* Read Response Headers */
        int hdr_len = 0;
        int body_offset = -1;
        struct TnHdrInfo hdr_info;

        while (hdr_len < (int)sizeof(hdr_buf) - 1) {
            LONG n = tn_call_recv(sock, hdr_buf + hdr_len, sizeof(hdr_buf) - 1 - hdr_len, 0);
            if (n <= 0) break;
            hdr_len += n;
            hdr_buf[hdr_len] = '\0';
            body_offset = tn_http_parse_headers(hdr_buf, hdr_len, &hdr_info);
            if (body_offset > 0) break;
        }

        if (body_offset <= 0) {
            if (!quiet) tn_log(TN_LOG_BASIC, "wget: malformed or empty HTTP response\n");
            tn_call_closesocket(sock);
            sock = -1;
            exit_code = 20;
            break;
        }

        /* Check for Redirection (301, 302, 303, 307, 308) */
        if (hdr_info.status_code == 301 || hdr_info.status_code == 302 ||
            hdr_info.status_code == 303 || hdr_info.status_code == 307 ||
            hdr_info.status_code == 308) {
            if (hdr_info.location[0] == '\0') {
                if (!quiet) tn_log(TN_LOG_BASIC, "wget: redirect without Location header\n");
                tn_call_closesocket(sock);
                sock = -1;
                exit_code = 20;
                break;
            }

            if (tn_http_resolve_redirect(&current_url, hdr_info.location, &next_url) != 0) {
                if (!quiet) tn_log(TN_LOG_BASIC, "wget: unable to resolve redirect target\n");
                tn_call_closesocket(sock);
                sock = -1;
                exit_code = 20;
                break;
            }

            current_url = next_url;
            redirect_count++;
            if (!quiet) {
                tn_logf(TN_LOG_BASIC, "wget: redirecting (%d) to %s:%lu%s...\n",
                        redirect_count, current_url.host, (ULONG)current_url.port, current_url.path);
            }
            tn_call_closesocket(sock);
            sock = -1;
            continue; /* Follow redirect */
        }

        /* Check HTTP Status */
        if (hdr_info.status_code >= 400) {
            if (!quiet) tn_logf(TN_LOG_BASIC, "wget: HTTP error %ld\n", (LONG)hdr_info.status_code);
            tn_call_closesocket(sock);
            sock = -1;
            exit_code = 20;
            break;
        }

        /* Setup Output File or Stream */
        if (to_file && to_path != NULL) {
            if (hdr_info.status_code == 206 && resume_offset > 0) {
                out_fh = Open(to_path, MODE_READWRITE);
                if (out_fh != 0) {
                    Seek(out_fh, 0, OFFSET_END);
                    total_written = resume_offset;
                    resp_start = resume_offset;
                    if (!quiet) tn_logf(TN_LOG_BASIC, "wget: resuming from byte %ld...\n", resume_offset);
                }
            }
            if (out_fh == 0) {
                out_fh = Open(to_path, MODE_NEWFILE);
                total_written = 0;
            }
            if (out_fh == 0) {
                if (!quiet) tn_logf(TN_LOG_BASIC, "wget: unable to open destination file '%s'\n", to_path);
                tn_call_closesocket(sock);
                sock = -1;
                exit_code = 20;
                break;
            }
        } else {
            out_fh = Output();
            total_written = 0;
        }

        /* Process Initial Body Data in hdr_buf */
        struct TnChunkState cst;
        tn_chunk_init(&cst);
        int init_body_len = hdr_len - body_offset;
        const char *init_body_data = hdr_buf + body_offset;
        /* 6.5: why the body ended early; NULL = complete */
        const char *fail = NULL;
        int feed_rc = 0;

        if (init_body_len > 0) {
            feed_rc = get_feed_body(out_fh, &cst, hdr_info.is_chunked,
                                    init_body_data, init_body_len, &total_written);
        }

        /* Stream Remainder of Body */
        while (feed_rc == 0) {
            if (hdr_info.is_chunked && cst.state == TN_CHUNK_STATE_DONE) break;
            /* z.ai step 8b item 4: stop when the bytes of THIS response
             * reach its Content-Length (total_written includes any
             * resume offset, the 206 length is the remainder only). */
            if (!hdr_info.is_chunked && hdr_info.content_length > 0 &&
                total_written - resp_start >= hdr_info.content_length) break;

            if (tn_cmd_check_ctrlc()) {
                fail = "interrupted (Ctrl-C)";
                exit_code = 10;
                break;
            }

            LONG n = tn_call_recv(sock, rx_buf, sizeof(rx_buf), 0);
            if (n > 0) {
                feed_rc = get_feed_body(out_fh, &cst, hdr_info.is_chunked,
                                        rx_buf, n, &total_written);
                if (feed_rc != 0) break;

                /* Display Progress if downloading to file */
                if (!quiet && to_file && hdr_info.content_length > 0) {
                    if (total_written - last_progress_bytes >= 16384 ||
                        total_written >= hdr_info.content_length) {
                        ULONG pct = 0;
                        if (hdr_info.content_length <= 40000000L) {
                            pct = (ULONG)(((ULONG)total_written * 100) / (ULONG)hdr_info.content_length);
                        } else {
                            pct = (ULONG)((ULONG)total_written / ((ULONG)hdr_info.content_length / 100));
                        }
                        if (pct > 100) pct = 100;
                        tn_logf(TN_LOG_BASIC, "wget: %ld / %ld bytes (%lu%%)\n",
                                total_written, (LONG)hdr_info.content_length, pct);
                        last_progress_bytes = total_written;
                    }
                }
            } else if (n == 0) {
                /* EOF: complete only when nothing else marks the end */
                if (hdr_info.is_chunked) fail = "connection closed before the final chunk";
                else if (hdr_info.content_length > 0) fail = "connection closed before Content-Length";
                break;
            } else {
                /* Socket error / timeout / Ctrl-C (EINTR) */
                if (tn_cmd_check_ctrlc()) {
                    fail = "interrupted (Ctrl-C)";
                    exit_code = 10;
                } else {
                    fail = "receive error";
                }
                break;
            }
        }
        if (feed_rc == -1) fail = "write error on output";
        else if (feed_rc == -2) fail = "malformed chunked encoding";

        if (fail != NULL) {
            /* 6.5: partial body - never report success (RC 0) */
            if (exit_code == 0) exit_code = 20;
            if (!quiet) {
                tn_logf(TN_LOG_BASIC, "\nwget: transfer incomplete: %s (%ld bytes kept).\n",
                        fail, total_written);
            }
        } else if (!quiet) {
            if (to_file) {
                tn_logf(TN_LOG_BASIC, "wget: transfer complete (%ld bytes written to '%s').\n",
                        total_written, to_path);
            } else {
                tn_logf(TN_LOG_BASIC, "\nwget: transfer complete (%ld bytes received).\n", total_written);
            }
        }

        tn_call_closesocket(sock);
        sock = -1;
        break; /* Done (exit_code says whether it was complete) */
    }

    if (redirect_count > 5) {
        PutStr((CONST_STRPTR)"wget: maximum redirect limit (5) exceeded\n");
        exit_code = 20;
    }

    if (to_file && out_fh != 0) {
        Close(out_fh);
        out_fh = 0;
    }

    if (sock >= 0) {
        tn_call_closesocket(sock);
        sock = -1;
    }

    CloseLibrary(SocketBase);
    FreeArgs(rdargs);
    /* 6.10: DOSBase belongs to the libnix startup, which closes it */
    return exit_code;

}
