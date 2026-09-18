/*
 * tolunnet — whois command (CMD-1). ReadArgs: QUERY/A,SERVER
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "QUERY/A,SERVER"
#define WHOIS_PORT 43
#define WHOIS_DEFAULT_SERVER "whois.iana.org"
#define RX_BUF 2048

int main(int argc, char **argv)
{
    LONG opts[2] = { 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    struct sockaddr_in dst;
    static char rxbuf[RX_BUF];
    static char query[512];
    ULONG addr;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"whois");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *q = (const char *)opts[0];
    const char *server = (opts[1] != 0) ? (const char *)opts[1] : WHOIS_DEFAULT_SERVER;

    addr = tn_cmd_resolve(server);
    if (addr == INADDR_NONE) {
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    fd = tn_call_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    memset(&dst, 0, sizeof(dst));
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons(WHOIS_PORT);
    dst.sin_addr.s_addr = addr;

    tn_cmd_printf("Connecting to %s...\n", server);
    if (tn_call_connect(fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
        tn_cmd_printf("whois: connect failed (errno=%ld)\n", tn_call_errno());
        tn_call_closesocket(fd);
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    {
        int n = 0;
        while (q[n] && n < (int)sizeof(query) - 3) { query[n] = q[n]; n++; }
        query[n++] = '\r';
        query[n++] = '\n';
        query[n] = '\0';
        tn_call_send(fd, (const void *)query, n, 0);
    }

    {
        LONG got;
        LONG total = 0;
        while (!tn_cmd_check_ctrlc()) {
            got = tn_call_recv(fd, (void *)rxbuf, RX_BUF - 1, 0);
            if (got <= 0) break;
            rxbuf[got] = '\0';
            Write(Output(), (CONST APTR)rxbuf, got);
            total += got;
        }
        if (total == 0) {
            tn_cmd_printf("(no response)\n");
            rc = TN_CMD_WARN;
        }
    }

    tn_call_closesocket(fd);
    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
