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

#include "../common/log.h"
#include "../common/http_url.h"

struct Library *SocketBase = NULL;

static LONG call_socket(LONG domain, LONG type, LONG protocol)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = domain;
    register LONG d1 __asm__("d1") = type;
    register LONG d2 __asm__("d2") = protocol;

    __asm__ __volatile__ (
        "jsr -30(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2)
        : "d1", "d2", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_connect(LONG sock, struct sockaddr *name, socklen_t namelen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register struct sockaddr *a0 __asm__("a0") = name;
    register LONG d1 __asm__("d1") = (LONG)namelen;

    __asm__ __volatile__ (
        "jsr -54(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1)
        : "d1", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_send(LONG sock, const void *buf, LONG len, LONG flags)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register const void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;

    __asm__ __volatile__ (
        "jsr -66(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2)
        : "d1", "d2", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_recv(LONG sock, void *buf, LONG len, LONG flags)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register void *a0 __asm__("a0") = buf;
    register LONG d1 __asm__("d1") = len;
    register LONG d2 __asm__("d2") = flags;

    __asm__ __volatile__ (
        "jsr -78(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(d1), "r"(d2)
        : "d1", "d2", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_closesocket(LONG sock)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;

    __asm__ __volatile__ (
        "jsr -120(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0)
        : "d1", "a0", "a1", "memory"
    );
    return d0;
}

static STRPTR call_inet_ntoa(in_addr_t ip)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = (LONG)ip;
    register STRPTR a0 __asm__("a0");

    __asm__ __volatile__ (
        "jsr -174(%%a6)"
        : "=r"(a0), "+r"(d0)
        : "r"(a6), "r"(d0)
        : "d1", "a1", "memory"
    );
    return a0;
}

static in_addr_t call_inet_addr(CONST_STRPTR cp)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register CONST_STRPTR a0 __asm__("a0") = cp;
    register LONG d0 __asm__("d0");

    __asm__ __volatile__ (
        "jsr -180(%%a6)"
        : "=r"(d0)
        : "r"(a6), "r"(a0)
        : "d1", "a1", "memory"
    );
    return (in_addr_t)d0;
}

static struct hostent *call_gethostbyname(CONST_STRPTR name)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register CONST_STRPTR a0 __asm__("a0") = name;
    register struct hostent *res __asm__("a0");

    __asm__ __volatile__ (
        "jsr -210(%%a6)"
        : "=r"(res)
        : "r"(a6), "r"(a0)
        : "d0", "d1", "a1", "memory"
    );
    return res;
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
    OPT_NUM_OPTS
};

int main(void)
{
    struct Library *DOSBase;
    struct RDArgs *rdargs;
    LONG opts[OPT_NUM_OPTS];
    struct TnUrl current_url, next_url;
    CONST_STRPTR to_path = NULL;
    BPTR out_fh = 0;
    BOOL to_file = FALSE;
    BOOL quiet = FALSE;
    LONG resume_offset = 0;
    int redirect_count = 0;
    LONG sock = -1;
    LONG total_written = 0;
    LONG last_progress_bytes = 0;
    int exit_code = 0;

    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_BASIC;

    for (int i = 0; i < OPT_NUM_OPTS; i++) opts[i] = 0;

    rdargs = ReadArgs((CONST_STRPTR)"URL/A,PORT/N,PATH,TO/K,QUIET/S", opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"wget");
        CloseLibrary(DOSBase);
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
        CloseLibrary(DOSBase);
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
        CloseLibrary(DOSBase);
        return 20;
    }

    /* 2. Check for resume capability if writing to an existing file */
    if (to_file && to_path != NULL) {
        BPTR lock = Lock(to_path, ACCESS_READ);
        if (lock != 0) {
            struct FileInfoBlock fib;
            if (Examine(lock, &fib) && fib.fib_DirEntryType < 0 && fib.fib_Size > 0) {
                resume_offset = fib.fib_Size;
            }
            UnLock(lock);
        }
    }

    /* 3. Open bsdsocket.library */
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr((CONST_STRPTR)"wget: unable to open bsdsocket.library\n");
        FreeArgs(rdargs);
        CloseLibrary(DOSBase);
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
        target_ip = call_inet_addr((CONST_STRPTR)current_url.host);
        if (target_ip == (in_addr_t)INADDR_NONE) {
            he = call_gethostbyname((CONST_STRPTR)current_url.host);
            if (he != NULL && he->h_addr_list != NULL && he->h_addr_list[0] != NULL) {
                target_ip = *(in_addr_t *)he->h_addr_list[0];
            } else {
                if (!quiet) tn_logf(TN_LOG_BASIC, "wget: unable to resolve host %s\n", current_url.host);
                exit_code = 20;
                break;
            }
        }

        if (!quiet) {
            tn_logf(TN_LOG_BASIC, "wget: connecting to %s (%s) port %lu...\n",
                    current_url.host, call_inet_ntoa(target_ip), (ULONG)current_url.port);
        }

        sock = call_socket(AF_INET, SOCK_STREAM, 0);
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

        rc = call_connect(sock, (struct sockaddr *)&srv_sin, sizeof(srv_sin));
        if (rc != 0) {
            if (!quiet) tn_logf(TN_LOG_BASIC, "wget: connection failed (rc=%ld)\n", rc);
            call_closesocket(sock);
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
                     (APTR)req_args, (VOID (*)())"\x16\xc0\x4e\x75", req_buf);
        } else {
            ULONG req_args[2];
            req_args[0] = (ULONG)current_url.path;
            req_args[1] = (ULONG)current_url.host;
            RawDoFmt((CONST_STRPTR)"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: TolunnetGet/1.1 (AmigaOS)\r\nConnection: close\r\n\r\n",
                     (APTR)req_args, (VOID (*)())"\x16\xc0\x4e\x75", req_buf);
        }

        call_send(sock, req_buf, str_len(req_buf), 0);

        /* Read Response Headers */
        int hdr_len = 0;
        int body_offset = -1;
        struct TnHdrInfo hdr_info;

        while (hdr_len < (int)sizeof(hdr_buf) - 1) {
            LONG n = call_recv(sock, hdr_buf + hdr_len, sizeof(hdr_buf) - 1 - hdr_len, 0);
            if (n <= 0) break;
            hdr_len += n;
            hdr_buf[hdr_len] = '\0';
            body_offset = tn_http_parse_headers(hdr_buf, hdr_len, &hdr_info);
            if (body_offset > 0) break;
        }

        if (body_offset <= 0) {
            if (!quiet) tn_log(TN_LOG_BASIC, "wget: malformed or empty HTTP response\n");
            call_closesocket(sock);
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
                call_closesocket(sock);
                sock = -1;
                exit_code = 20;
                break;
            }

            if (tn_http_resolve_redirect(&current_url, hdr_info.location, &next_url) != 0) {
                if (!quiet) tn_log(TN_LOG_BASIC, "wget: unable to resolve redirect target\n");
                call_closesocket(sock);
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
            call_closesocket(sock);
            sock = -1;
            continue; /* Follow redirect */
        }

        /* Check HTTP Status */
        if (hdr_info.status_code >= 400) {
            if (!quiet) tn_logf(TN_LOG_BASIC, "wget: HTTP error %ld\n", (LONG)hdr_info.status_code);
            call_closesocket(sock);
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
                    if (!quiet) tn_logf(TN_LOG_BASIC, "wget: resuming from byte %ld...\n", resume_offset);
                }
            }
            if (out_fh == 0) {
                out_fh = Open(to_path, MODE_NEWFILE);
                total_written = 0;
            }
            if (out_fh == 0) {
                if (!quiet) tn_logf(TN_LOG_BASIC, "wget: unable to open destination file '%s'\n", to_path);
                call_closesocket(sock);
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

        if (init_body_len > 0) {
            if (hdr_info.is_chunked) {
                int pos = 0;
                while (pos < init_body_len && cst.state != TN_CHUNK_STATE_DONE) {
                    int consumed = 0;
                    const char *chunk_data = NULL;
                    int chunk_len = 0;
                    tn_chunk_feed(&cst, init_body_data + pos, init_body_len - pos,
                                  &consumed, &chunk_data, &chunk_len);
                    pos += consumed;
                    if (chunk_len > 0) {
                        Write(out_fh, (APTR)chunk_data, chunk_len);
                        total_written += chunk_len;
                    }
                }
            } else {
                Write(out_fh, (APTR)init_body_data, init_body_len);
                total_written += init_body_len;
            }
        }

        /* Stream Remainder of Body */
        while (1) {
            if (hdr_info.is_chunked && cst.state == TN_CHUNK_STATE_DONE) break;
            if (!hdr_info.is_chunked && hdr_info.content_length > 0 &&
                total_written >= hdr_info.content_length) break;

            LONG n = call_recv(sock, rx_buf, sizeof(rx_buf), 0);
            if (n > 0) {
                if (hdr_info.is_chunked) {
                    int pos = 0;
                    while (pos < n && cst.state != TN_CHUNK_STATE_DONE) {
                        int consumed = 0;
                        const char *chunk_data = NULL;
                        int chunk_len = 0;
                        tn_chunk_feed(&cst, rx_buf + pos, n - pos,
                                      &consumed, &chunk_data, &chunk_len);
                        pos += consumed;
                        if (chunk_len > 0) {
                            Write(out_fh, (APTR)chunk_data, chunk_len);
                            total_written += chunk_len;
                        }
                    }
                } else {
                    Write(out_fh, (APTR)rx_buf, n);
                    total_written += n;
                }

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
                break; /* EOF */
            } else {
                break; /* Socket error / timeout */
            }
        }

        if (!quiet) {
            if (to_file) {
                tn_logf(TN_LOG_BASIC, "wget: transfer complete (%ld bytes written to '%s').\n",
                        total_written, to_path);
            } else {
                tn_logf(TN_LOG_BASIC, "\nwget: transfer complete (%ld bytes received).\n", total_written);
            }
        }

        call_closesocket(sock);
        sock = -1;
        break; /* Done successfully */
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
        call_closesocket(sock);
        sock = -1;
    }

    CloseLibrary(SocketBase);
    FreeArgs(rdargs);
    CloseLibrary(DOSBase);
    return exit_code;
}
