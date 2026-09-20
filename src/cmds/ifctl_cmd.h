/*
 * tolunnet — shared IFCTL client helpers for the interface commands
 * (CLOSE §B.7): one-line IPC wrappers + dotted-quad formatting.
 */
#ifndef TOLUNNET_IFCTL_CMD_H
#define TOLUNNET_IFCTL_CMD_H

#include "cmdlib.h"
#include "../common/ipc_client.h"
#include "../../include/ipc.h"

/* Send one IFCTL op. Returns 0 on IPC transport success (msg.result
 * carries the op result). */
static LONG ifctl_call(LONG op, LONG idx, ULONG a2, ULONG a3, ULONG a4,
                       TnIfInfo *rows, LONG cap, TnIpcMsg *out)
{
    LONG args[6];
    APTR ptrs[1];

    memset(out, 0, sizeof(*out));
    memset(args, 0, sizeof(args));
    args[0] = op;
    args[1] = idx;
    args[2] = (LONG)a2;
    args[3] = (LONG)a3;
    args[4] = (LONG)a4;
    ptrs[0] = rows;
    return tn_ipc_oneshot_ex(TN_IPC_CMD_IFCTL, args, 5, ptrs, 1, out);
}

/* dotted quad of a network-order address */
static void ifctl_fmt_ip(char *out, ULONG a_net)
{
    ULONG a = ntohl(a_net);
    ULONG o[4];
    int k, pos = 0;
    o[0] = (a >> 24) & 0xFF; o[1] = (a >> 16) & 0xFF;
    o[2] = (a >> 8) & 0xFF;  o[3] = a & 0xFF;
    for (k = 0; k < 4; k++) {
        if (o[k] >= 100) {
            out[pos++] = (char)('0' + (o[k] / 100) % 10);
            out[pos++] = (char)('0' + (o[k] / 10) % 10);
            out[pos++] = (char)('0' + o[k] % 10);
        } else if (o[k] >= 10) {
            out[pos++] = (char)('0' + (o[k] / 10) % 10);
            out[pos++] = (char)('0' + o[k] % 10);
        } else {
            out[pos++] = (char)('0' + o[k]);
        }
        if (k < 3) out[pos++] = '.';
    }
    out[pos] = '\0';
}

#endif /* TOLUNNET_IFCTL_CMD_H */
