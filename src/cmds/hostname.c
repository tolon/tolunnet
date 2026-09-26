/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — hostname command (CMD-1). ReadArgs: NAME
 *
 * z.ai step 9b item 2: NAME (RFC 1123: 1-63 chars per label,
 * [A-Za-z0-9-]) is written to the config via tn_prefs_save and applied
 * live with IPC RECONFIG. RC 0 only if both worked.
 */
#include "cmdlib.h"
#include "../common/prefs.h"
#include "../common/ipc_client.h"

#define TEMPLATE "NAME"

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    char name[64];

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"hostname");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    if (opts[0] == 0) {
        if (tn_call_gethostname((STRPTR)name, (LONG)sizeof(name) - 1) == 0) {
            name[sizeof(name) - 1] = '\0';
            tn_cmd_printf("%s\n", name);
        } else {
            tn_cmd_printf("hostname: unable to read hostname\n");
            rc = TN_CMD_FAIL;
        }
        FreeArgs(rdargs);
        tn_cmd_fini();
        return rc;
    }

    /* set: validate RFC 1123, persist, reconfigure live */
    {
        const char *name_arg = (const char *)opts[0];
        const char *p = name_arg;
        int labels = 0, ok = 1;
        TnPrefs prefs;
        TnIpcMsg msg;
        LONG rargs[5];
        APTR rptrs[1];
        TnReconfigResponse resp;
        int saved, reconf;

        while (*p && ok) {
            int n = 0;
            while (*p != 0 && *p != '.') {
                char c = *p;
                if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') || c == '-') || n >= 63) {
                    ok = 0;
                    break;
                }
                n++;
                p++;
            }
            if (n == 0) ok = 0;
            labels++;
            if (*p == '.') p++;
            else break;
        }
        if (!ok || labels < 1) {
            tn_cmd_printf("hostname: invalid name (RFC 1123 labels, 1-63 chars)\n");
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_FAIL;
        }

        tn_prefs_load(&prefs);
        {
            int i = 0;
            p = name_arg;
            while (*p && i < (int)sizeof(prefs.hostname) - 1) prefs.hostname[i++] = *p++;
            prefs.hostname[i] = '\0';
        }
        saved = tn_prefs_save(&prefs, TN_PREFS_SAVE);

        rargs[0] = 0; rargs[1] = 0; rargs[2] = 0; rargs[3] = 0;
        rargs[4] = (LONG)sizeof(TnReconfigResponse);
        rptrs[0] = (APTR)&resp;
        reconf = (tn_ipc_oneshot_ex(TN_IPC_CMD_RECONFIG, rargs, 5,
                                    rptrs, 1, &msg) == 0 && msg.result == 0);

        if (saved && reconf) {
            tn_cmd_printf("%s\n", name_arg);
            FreeArgs(rdargs); tn_cmd_fini();
            return TN_CMD_OK;
        }
        tn_cmd_printf("hostname: %s failed\n", saved ? "RECONFIG" : "save");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }
}
