/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Milestone M6 & Event Engine Test Suite.
 *
 * Verifies:
 * - OpenLibrary / CloseLibrary
 * - socket(AF_INET, SOCK_STREAM, 0)
 * - Inet_NtoA / inet_addr bounds
 * - IoctlSocket(FIONBIO) non-blocking mode
 * - IoctlSocket(FIONREAD) buffer query
 * - setsockopt(SO_REUSEADDR) / getsockopt(SO_REUSEADDR)
 * - setsockopt(TCP_NODELAY) / getsockopt(TCP_NODELAY)
 * - WaitSelect() timeout and signal integration
 * - CloseSocket()
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <exec/libraries.h>

#include <sys/socket.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/errno.h>

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

static LONG call_ioctlsocket(LONG sock, ULONG req, APTR argp)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register ULONG d1 __asm__("d1") = req;
    register APTR a0 __asm__("a0") = argp;

    __asm__ __volatile__ (
        "jsr -114(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(a0)
        : "d1", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_setsockopt(LONG sock, LONG level, LONG optname, const void *optval, socklen_t optlen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register LONG d1 __asm__("d1") = level;
    register LONG d2 __asm__("d2") = optname;
    register const void *a0 __asm__("a0") = optval;
    register LONG d3 __asm__("d3") = (LONG)optlen;

    __asm__ __volatile__ (
        "jsr -90(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2), "r"(a0), "r"(d3)
        : "d1", "d2", "d3", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_getsockopt(LONG sock, LONG level, LONG optname, void *optval, socklen_t *optlen)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = sock;
    register LONG d1 __asm__("d1") = level;
    register LONG d2 __asm__("d2") = optname;
    register void *a0 __asm__("a0") = optval;
    register socklen_t *a1 __asm__("a1") = optlen;

    __asm__ __volatile__ (
        "jsr -96(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(d1), "r"(d2), "r"(a0), "r"(a1)
        : "d1", "d2", "a0", "a1", "memory"
    );
    return d0;
}

static LONG call_waitselect(LONG nfds, APTR read_fds, APTR write_fds, APTR except_fds,
                            struct timeval *timeout, ULONG *signals)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0") = nfds;
    register APTR a0 __asm__("a0") = read_fds;
    register APTR a1 __asm__("a1") = write_fds;
    register APTR a2 __asm__("a2") = except_fds;
    register struct timeval *a3 __asm__("a3") = timeout;
    register ULONG *d1 __asm__("d1") = signals;

    __asm__ __volatile__ (
        "jsr -126(%%a6)"
        : "+r"(d0)
        : "r"(a6), "r"(d0), "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(d1)
        : "d1", "memory"
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

static LONG call_errno(VOID)
{
    register struct Library *a6 __asm__("a6") = SocketBase;
    register LONG d0 __asm__("d0");

    __asm__ __volatile__ (
        "jsr -162(%%a6)"
        : "=r"(d0)
        : "r"(a6)
        : "d1", "a0", "a1", "memory"
    );
    return d0;
}

static BOOL str_equal(const char *s1, const char *s2)
{
    if (s1 == NULL || s2 == NULL) return FALSE;
    while (*s1 && *s2) {
        if (*s1 != *s2) return FALSE;
        s1++;
        s2++;
    }
    return (*s1 == *s2);
}

int main(int argc, char *argv[])
{
    struct Library *DOSBase;
    BPTR log_fh = (BPTR)0;
    LONG sock, rc;
    STRPTR str_ip;
    in_addr_t addr;
    ULONG on = 1;
    ULONG bytes_avail = 999;
    int optval = 0;
    socklen_t optlen = sizeof(int);
    struct timeval tv;
    ULONG rfds = 0, wfds = 0, sigs = 0;
    BOOL all_passed = TRUE;
    (void)argc; (void)argv;

    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    log_fh = Open((CONST_STRPTR)"WORK:tolunnet-m3.log", MODE_NEWFILE);
    g_log_dos = DOSBase;
    g_log_file = log_fh;
    g_log_level = TN_LOG_VERBOSE;

    tn_log(TN_LOG_BASIC, "========================================\n");
    tn_log(TN_LOG_BASIC, "TestSocket: Milestone M6 Verification\n");
    tn_log(TN_LOG_BASIC, "========================================\n");

    /* 1. Open bsdsocket.library */
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (SocketBase == NULL) {
        tn_log(TN_LOG_BASIC, "FAIL: OpenLibrary(bsdsocket.library, 4)\n");
        if (log_fh) Close(log_fh);
        CloseLibrary(DOSBase);
        return 20;
    }
    tn_log(TN_LOG_BASIC, "PASS: OpenLibrary(bsdsocket.library, 4)\n");

    /* 2. Call socket(AF_INET, SOCK_STREAM, 0) */
    sock = call_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        tn_logf(TN_LOG_BASIC, "FAIL: socket() returned %ld\n", sock);
        all_passed = FALSE;
    } else {
        tn_logf(TN_LOG_BASIC, "PASS: socket() -> fd %ld\n", sock);
    }

    /* 3. Test Inet_NtoA & inet_addr */
    str_ip = call_inet_ntoa(0x0a00020fUL);
    if (str_equal((const char *)str_ip, "10.0.2.15")) {
        tn_logf(TN_LOG_BASIC, "PASS: Inet_NtoA(0x0a00020f) -> \"%s\"\n", str_ip);
    } else {
        tn_logf(TN_LOG_BASIC, "FAIL: Inet_NtoA(0x0a00020f) -> \"%s\"\n", str_ip);
        all_passed = FALSE;
    }

    addr = call_inet_addr((CONST_STRPTR)"10.0.2.15");
    if (addr == 0x0a00020fUL) {
        tn_log(TN_LOG_BASIC, "PASS: inet_addr(\"10.0.2.15\") -> 0x0a00020f\n");
    } else {
        all_passed = FALSE;
    }

    /* 4. Test IoctlSocket(FIONBIO) */
    on = 1;
    rc = call_ioctlsocket(sock, FIONBIO, &on);
    if (rc == 0) {
        tn_log(TN_LOG_BASIC, "PASS: IoctlSocket(FIONBIO, 1) -> ok\n");
    } else {
        tn_logf(TN_LOG_BASIC, "FAIL: IoctlSocket(FIONBIO) returned %ld\n", rc);
        all_passed = FALSE;
    }

    /* 5. Test IoctlSocket(FIONREAD) */
    bytes_avail = 999;
    rc = call_ioctlsocket(sock, FIONREAD, &bytes_avail);
    if (rc == 0 && bytes_avail == 0) {
        tn_log(TN_LOG_BASIC, "PASS: IoctlSocket(FIONREAD) -> 0 bytes queued\n");
    } else {
        tn_logf(TN_LOG_BASIC, "FAIL: IoctlSocket(FIONREAD) returned %ld (avail=%lu)\n", rc, bytes_avail);
        all_passed = FALSE;
    }

    /* 6. Test setsockopt(SO_REUSEADDR) & getsockopt */
    optval = 1;
    rc = call_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    if (rc == 0) {
        optval = 0;
        rc = call_getsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, &optlen);
        if (rc == 0 && optval == 1) {
            tn_log(TN_LOG_BASIC, "PASS: setsockopt/getsockopt(SO_REUSEADDR, 1) verified\n");
        } else {
            tn_logf(TN_LOG_BASIC, "FAIL: getsockopt(SO_REUSEADDR) returned %ld val=%d\n", rc, optval);
            all_passed = FALSE;
        }
    } else {
        tn_logf(TN_LOG_BASIC, "FAIL: setsockopt(SO_REUSEADDR) returned %ld\n", rc);
        all_passed = FALSE;
    }

    /* 7. Test setsockopt(TCP_NODELAY) */
    optval = 1;
    rc = call_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int));
    if (rc == 0) {
        optval = 0;
        rc = call_getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, &optlen);
        if (rc == 0 && optval == 1) {
            tn_log(TN_LOG_BASIC, "PASS: setsockopt/getsockopt(TCP_NODELAY, 1) verified\n");
        } else {
            tn_logf(TN_LOG_BASIC, "FAIL: getsockopt(TCP_NODELAY) returned %ld val=%d\n", rc, optval);
            all_passed = FALSE;
        }
    } else {
        tn_logf(TN_LOG_BASIC, "FAIL: setsockopt(TCP_NODELAY) returned %ld\n", rc);
        all_passed = FALSE;
    }

    /* 8. Test WaitSelect with 50 ms timeout on non-ready socket */
    rfds = (1UL << sock);
    wfds = 0;
    tv.tv_secs = 0;
    tv.tv_micro = 50000; /* 50 ms */
    sigs = 0;
    rc = call_waitselect(sock + 1, &rfds, &wfds, NULL, &tv, &sigs);
    if (rc == 0 && rfds == 0) {
        tn_log(TN_LOG_BASIC, "PASS: WaitSelect(50ms timeout) returned 0 (timeout ok)\n");
    } else {
        tn_logf(TN_LOG_BASIC, "FAIL: WaitSelect() returned %ld rfds=0x%08lx\n", rc, rfds);
        all_passed = FALSE;
    }

    /* 9. Call CloseSocket(sock) */
    if (sock >= 0) {
        if (call_closesocket(sock) != 0) {
            tn_logf(TN_LOG_BASIC, "FAIL: CloseSocket(fd=%ld)\n", sock);
            all_passed = FALSE;
        } else {
            tn_logf(TN_LOG_BASIC, "PASS: CloseSocket(fd=%ld)\n", sock);
        }
    }

    /* 10. Close bsdsocket.library */
    CloseLibrary(SocketBase);
    SocketBase = NULL;
    tn_log(TN_LOG_BASIC, "PASS: CloseLibrary(bsdsocket.library)\n");

    tn_log(TN_LOG_BASIC, "----------------------------------------\n");
    if (all_passed) {
        tn_log(TN_LOG_BASIC, "MILESTONE M6: ALL TESTS PASSED\n");
    } else {
        tn_log(TN_LOG_BASIC, "MILESTONE M6: SOME TESTS FAILED\n");
    }
    tn_log(TN_LOG_BASIC, "----------------------------------------\n");

    if (log_fh) {
        Close(log_fh);
    }
    CloseLibrary(DOSBase);
    return all_passed ? 0 : 20;
}
