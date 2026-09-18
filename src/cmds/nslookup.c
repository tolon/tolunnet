/*
 * tolunnet — nslookup / host command (CMD-1). ReadArgs: NAME/A,SERVER
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "NAME/A,SERVER"

int main(int argc, char **argv)
{
    LONG opts[2] = { 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    ULONG addr;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"nslookup");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *name = (const char *)opts[0];

    addr = tn_call_inet_addr(name);
    if (addr != INADDR_NONE) {
        struct in_addr ia;
        ia.s_addr = addr;
        tn_cmd_printf("Name:      %s\n", name);
        tn_cmd_printf("Address:   %s\n", tn_call_inet_ntoa(ia));

        {
            struct hostent *he = tn_call_gethostbyaddr((const char *)&addr, 4, AF_INET);
            if (he != NULL && he->h_name != NULL) {
                tn_cmd_printf("Hostname:  %s\n", he->h_name);
            } else {
                tn_cmd_printf("(no PTR record)\n");
            }
        }
    } else {
        tn_cmd_printf("Name:      %s\n", name);
        {
            struct hostent *he = tn_call_gethostbyname(name);
            if (he == NULL || he->h_addr_list[0] == NULL) {
                tn_cmd_printf("** %s doesn't exist\n", name);
                rc = TN_CMD_FAIL;
            } else {
                int i;
                for (i = 0; he->h_addr_list[i] != NULL && i < 4; i++) {
                    struct in_addr a;
                    memcpy(&a, he->h_addr_list[i], 4);
                    tn_cmd_printf("Address %d:  %s\n", i + 1, tn_call_inet_ntoa(a));
                }
                if (he->h_name != NULL) {
                    tn_cmd_printf("Canonical: %s\n", he->h_name);
                }
            }
        }
    }

    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
