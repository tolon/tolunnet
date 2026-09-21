/*
 * tolunnet — TolunnetControl command (CMD-1 item 4).
 * Control front-end: START|STOP|RESTART|STATUS|RECONFIG|VERSION
 * STATUS returns RC 0 (running) / 5 (not running) — scriptable.
 */
#include "cmdlib.h"
#include "../../include/version.h"
#include <string.h>

#define TEMPLATE "COMMAND/A"

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    char cmd[32];

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"TolunnetControl");
        return TN_CMD_USAGE;
    }

    strncpy(cmd, (const char *)opts[0], sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    {
        int i;
        for (i = 0; cmd[i]; i++) {
            if (cmd[i] >= 'a' && cmd[i] <= 'z') cmd[i] -= 32;
        }
    }

    if (strcmp(cmd, "VERSION") == 0) {
        tn_cmd_printf("tolunnet " TOLUNNET_VERSION " (bsdsocket.library v4.1)\n");
    } else if (strcmp(cmd, "START") == 0) {
        BPTR seg = LoadSeg((CONST_STRPTR)"C:tolunnet");
        if (seg != (BPTR)0) {
            LONG ret;
            tn_cmd_printf("TolunnetControl: starting C:tolunnet...\n");
            ret = RunCommand(seg, 32768, (CONST_STRPTR)"START\n", 6);
            UnLoadSeg(seg);
            if (ret != 0) rc = (int)ret;
        } else {
            tn_cmd_printf("TolunnetControl: cannot load C:tolunnet\n");
            rc = TN_CMD_FAIL;
        }
    } else if (strcmp(cmd, "STATUS") == 0) {
        /* Check if the daemon is running by opening bsdsocket.library */
        struct Library *lib = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
        if (lib != NULL) {
            tn_cmd_printf("tolunnet: running\n");
            CloseLibrary(lib);
            rc = TN_CMD_OK;
        } else {
            tn_cmd_printf("tolunnet: not running\n");
            rc = TN_CMD_WARN; /* RC 5 = not running */
        }
    } else if (strcmp(cmd, "STOP") == 0) {
        BPTR seg = LoadSeg((CONST_STRPTR)"C:tolunnet");
        if (seg != (BPTR)0) {
            LONG ret;
            tn_cmd_printf("TolunnetControl: STOP...\n");
            ret = RunCommand(seg, 32768, (CONST_STRPTR)"STOP\n", 5);
            UnLoadSeg(seg);
            if (ret != 0) rc = (int)ret;
        } else {
            tn_cmd_printf("TolunnetControl: cannot load C:tolunnet\n");
            rc = TN_CMD_FAIL;
        }
    } else if (strcmp(cmd, "RESTART") == 0) {
        BPTR seg = LoadSeg((CONST_STRPTR)"C:tolunnet");
        if (seg != (BPTR)0) {
            LONG ret;
            tn_cmd_printf("TolunnetControl: STOP...\n");
            ret = RunCommand(seg, 32768, (CONST_STRPTR)"STOP\n", 5);
            UnLoadSeg(seg);
            if (ret != 0) {
                rc = (int)ret;
            } else {
                seg = LoadSeg((CONST_STRPTR)"C:tolunnet");
                if (seg != (BPTR)0) {
                    tn_cmd_printf("TolunnetControl: starting C:tolunnet...\n");
                    ret = RunCommand(seg, 32768, (CONST_STRPTR)"START\n", 6);
                    UnLoadSeg(seg);
                    if (ret != 0) rc = (int)ret;
                } else {
                    tn_cmd_printf("TolunnetControl: cannot load C:tolunnet\n");
                    rc = TN_CMD_FAIL;
                }
            }
        } else {
            tn_cmd_printf("TolunnetControl: cannot load C:tolunnet\n");
            rc = TN_CMD_FAIL;
        }
    } else if (strcmp(cmd, "RECONFIG") == 0) {
        BPTR seg = LoadSeg((CONST_STRPTR)"C:tolunnet");
        if (seg != (BPTR)0) {
            LONG ret;
            tn_cmd_printf("TolunnetControl: RECONFIG...\n");
            ret = RunCommand(seg, 32768, (CONST_STRPTR)"RECONFIG\n", 9);
            UnLoadSeg(seg);
            if (ret != 0) rc = (int)ret;
        } else {
            tn_cmd_printf("TolunnetControl: cannot load C:tolunnet\n");
            rc = TN_CMD_FAIL;
        }
    } else {
        tn_cmd_printf("TolunnetControl: unknown command '%s'\n", cmd);
        tn_cmd_printf("Usage: TolunnetControl START|STOP|RESTART|STATUS|RECONFIG|VERSION\n");
        rc = TN_CMD_USAGE;
    }

    FreeArgs(rdargs);
    return rc;
}
