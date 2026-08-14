/*
 * TolunetGet — Standalone HTTP/TCP Client Command for AmigaOS.
 *
 * Performs an HTTP GET request over bsdsocket.library TCP stream sockets.
 * Usage: TolunetGet <HOST> [PORT] [PATH]
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <devices/timer.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "../common/log.h"

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

int main(int argc, char *argv[])
{
    struct Library *DOSBase;
    struct sockaddr_in srv_sin;
    struct hostent *he;
    CONST_STRPTR host_str = (CONST_STRPTR)"10.0.2.2";
    CONST_STRPTR path_str = (CONST_STRPTR)"/";
    ULONG port = 80;
    in_addr_t target_ip;
    LONG sock, rc, total_received = 0;
    char req_buf[1024];
    char rx_buf[1024];
    static char host_buf[256];
    static char path_buf[512];
    int req_len;

    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    if (argc >= 2) {
        const char *arg = argv[1];
        if (arg[0] == '?' || (arg[0] == '-' && (arg[1] == '?' || arg[1] == 'h'))) {
            PutStr((CONST_STRPTR)"Usage: curl/wget <url|host> [port] [path]\nExample: curl http://aminet.net/recent.txt\nExample: wget aminet.net 80 /\n");
            CloseLibrary(DOSBase);
            return 0;
        }

        /* Check for http:// prefix */
        if (arg[0] == 'h' && arg[1] == 't' && arg[2] == 't' && arg[3] == 'p' &&
            arg[4] == ':' && arg[5] == '/' && arg[6] == '/') {
            const char *h = arg + 7;
            const char *slash = h;
            int hlen = 0;

            while (*slash && *slash != '/' && *slash != ':') {
                if (hlen < 255) host_buf[hlen++] = *slash;
                slash++;
            }
            host_buf[hlen] = '\0';
            host_str = (CONST_STRPTR)host_buf;

            if (*slash == ':') {
                LONG p = 0;
                if (StrToLong((CONST_STRPTR)(slash + 1), &p) > 0 && p > 0) port = (ULONG)p;
                while (*slash && *slash != '/') slash++;
            }

            if (*slash == '/') {
                int plen = 0;
                while (*slash && plen < 511) {
                    path_buf[plen++] = *slash++;
                }
                path_buf[plen] = '\0';
                path_str = (CONST_STRPTR)path_buf;
            }
        } else {
            int hlen = 0;
            while (arg[hlen] && hlen < 255) {
                host_buf[hlen] = arg[hlen];
                hlen++;
            }
            host_buf[hlen] = '\0';
            host_str = (CONST_STRPTR)host_buf;
        }
    }
    if (argc >= 3) {
        LONG p = 0;
        if (StrToLong((CONST_STRPTR)argv[2], &p) > 0 && p > 0) port = (ULONG)p;
    }
    if (argc >= 4) {
        const char *p_arg = argv[3];
        int plen = 0;
        while (p_arg[plen] && plen < 511) {
            path_buf[plen] = p_arg[plen];
            plen++;
        }
        path_buf[plen] = '\0';
        path_str = (CONST_STRPTR)path_buf;
    }

    /* 1. Open bsdsocket.library */
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        PutStr((CONST_STRPTR)"wget: unable to open bsdsocket.library\n");
        CloseLibrary(DOSBase);
        return 20;
    }

    /* 2. Resolve host address */
    target_ip = call_inet_addr(host_str);
    if (target_ip == (in_addr_t)INADDR_NONE) {
        he = call_gethostbyname(host_str);
        if (he != NULL && he->h_addr_list != NULL && he->h_addr_list[0] != NULL) {
            target_ip = *(in_addr_t *)he->h_addr_list[0];
        } else {
            tn_logf(TN_LOG_BASIC, "TolunetGet: unable to resolve host %s\n", host_str);
            CloseLibrary(SocketBase);
            CloseLibrary(DOSBase);
            return 20;
        }
    }

    tn_logf(TN_LOG_BASIC, "TolunetGet: connecting to %s (%s) port %lu...\n",
            host_str, call_inet_ntoa(target_ip), port);

    /* 3. Open TCP stream socket */
    sock = call_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        tn_logf(TN_LOG_BASIC, "TolunetGet: socket creation failed (rc=%ld)\n", sock);
        CloseLibrary(SocketBase);
        CloseLibrary(DOSBase);
        return 20;
    }

    /* Initialize BSD44 sockaddr_in (TNET-028 & TNET-029) */
    for (int i = 0; i < (int)sizeof(srv_sin); i++) ((char *)&srv_sin)[i] = 0;
    srv_sin.sin_len         = sizeof(struct sockaddr_in);
    srv_sin.sin_family      = AF_INET;
    srv_sin.sin_port        = htons((UWORD)port);
    srv_sin.sin_addr.s_addr = target_ip;

    /* 4. Connect to HTTP server */
    rc = call_connect(sock, (struct sockaddr *)&srv_sin, sizeof(srv_sin));
    if (rc != 0) {
        tn_logf(TN_LOG_BASIC, "wget: connection failed (rc=%ld)\n", rc);
        call_closesocket(sock);
        CloseLibrary(SocketBase);
        CloseLibrary(DOSBase);
        return 20;
    }

    tn_log(TN_LOG_BASIC, "wget: connected! Sending HTTP GET request...\n");

    /* 5. Format and send HTTP GET request with contiguous arg array (TNET-031) */
    ULONG req_args[2];
    req_args[0] = (ULONG)path_str;
    req_args[1] = (ULONG)host_str;

    req_buf[0] = '\0';
    RawDoFmt((CONST_STRPTR)"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: tolunnet-wget/1.1 (AmigaOS)\r\nConnection: close\r\n\r\n",
             (APTR)req_args,
             (VOID (*)())"\x16\xc0\x4e\x75",
             req_buf);
    req_len = str_len(req_buf);

    call_send(sock, req_buf, req_len, 0);

    /* 6. Receive response stream */
    tn_log(TN_LOG_BASIC, "---------------- HTTP RESPONSE ----------------\n");
    int error_retries = 0;
    while (1) {
        LONG n = call_recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
        if (n > 0) {
            rx_buf[n] = '\0';
            PutStr((CONST_STRPTR)rx_buf);
            total_received += n;
            error_retries = 0;
        } else if (n == 0) {
            /* EOF received from server */
            break;
        } else {
            error_retries++;
            if (error_retries > 20) break; /* 1 sec timeout */
            Delay(3); /* 60 ms wait */
        }
    }
    tn_log(TN_LOG_BASIC, "\n-----------------------------------------------\n");
    tn_logf(TN_LOG_BASIC, "wget: transfer complete (%ld bytes received).\n", total_received);

    /* 7. Clean up */
    call_closesocket(sock);
    CloseLibrary(SocketBase);
    CloseLibrary(DOSBase);
    return 0;
}
