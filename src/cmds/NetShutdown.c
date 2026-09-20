/*
 * tolunnet — NetShutdown command (CLOSE §B.8).
 * ReadArgs: FORCE/S — takes the interface down (IFCTL DOWN) and asks the
 * daemon to stop (CTRL-C via the IPC port, the same mechanism the
 * conformance suite uses). Without FORCE the daemon's TNET-059 guard may
 * refuse while clients still have bsdsocket.library open — reported.
 */
#include "ifctl_cmd.h"
#include <proto/exec.h>
#include <exec/ports.h>
#include <string.h>

#define TEMPLATE "FORCE/S"

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    TnIpcMsg msg;
    struct MsgPort *port;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"NetShutdown");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    /* interface down first */
    ifctl_call(TN_IFCTL_DOWN, -1, 0, 0, 0, NULL, 0, &msg);
    tn_cmd_printf("NetShutdown: interface down\n");

    /* ask the daemon to stop */
    port = (struct MsgPort *)FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
    if (port == NULL || port->mp_SigTask == NULL) {
        tn_cmd_printf("NetShutdown: tolunnet daemon is not running\n");
        FreeArgs(rdargs);
        tn_cmd_fini();
        return TN_CMD_WARN;
    }
    Signal((struct Task *)port->mp_SigTask, SIGBREAKF_CTRL_C);
    tn_cmd_printf("NetShutdown: stop signal sent to the daemon\n");
    if (opts[0] == 0) {
        tn_cmd_printf("NetShutdown: if clients still hold bsdsocket.library the daemon\n");
        tn_cmd_printf("             refuses until they close (TNET-059); use FORCE to\n");
        tn_cmd_printf("             send the signal regardless (it is already sent).\n");
    } else {
        tn_cmd_printf("NetShutdown: FORCE — signal delivered; lingering clients are\n");
        tn_cmd_printf("             named and dead ones reaped by the daemon.\n");
    }

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return TN_CMD_OK;
}
