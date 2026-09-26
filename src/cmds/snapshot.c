/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — snapshot helper (z.ai step 9a item 1).
 * Kept out of cmdlib.o: linking ipc_client into every command changes
 * the layout of binaries that never call it (bench 20260926-075052:
 * the wizard's live-apply silently stopped after the relink), so only
 * the status tools pay for it.
 */
#include "cmdlib.h"
#include "../common/ipc_client.h"

void tn_cmd_ip_to_str(ULONG ip, char *buf)
{
    /* 68k: a ULONG holding a network-order address is the a.b.c.d value */
    ULONG v[4];
    char tmp[24];
    LONG o = 0, k, i;
    v[0] = (ip >> 24) & 0xFF; v[1] = (ip >> 16) & 0xFF;
    v[2] = (ip >> 8) & 0xFF; v[3] = ip & 0xFF;
    for (i = 0; i < 4; i++) {
        char rev[4];
        LONG rl = 0;
        ULONG x = v[i];
        if (i > 0) tmp[o++] = 46; /* '.' */
        if (x == 0) {
            rev[rl++] = 48; /* '0' */
        } else {
            while (x > 0) { rev[rl++] = (char)(48 + (x % 10)); x /= 10; }
        }
        while (rl > 0) tmp[o++] = rev[--rl];
    }
    tmp[o] = 0;
    for (k = 0; k <= o; k++) buf[k] = tmp[k];
}

int tn_cmd_snapshot(TnSnapshot *snap)
{
    LONG st_args[5], if_args[5], rt_args[5];
    APTR st_ptrs[1], if_ptrs[1], rt_ptrs[1];
    TnIpcMsg msg;

    if (snap == NULL) return -1;
    memset(snap, 0, sizeof(*snap));
    snap->if_count = -1;
    snap->route_count = -1;

    /* tn_ipc_oneshot_ex returns the daemon's result: 0 for GETSTATUS,
     * the ROW COUNT for the IFCTL/ROUTECTL LIST calls. */
    st_args[0] = 0; st_args[1] = 0; st_args[2] = 0; st_args[3] = 0;
    st_args[4] = (LONG)sizeof(TnStatusInfoV2);
    st_ptrs[0] = (APTR)&snap->status;
    if (tn_ipc_oneshot_ex(TN_IPC_CMD_GETSTATUS, st_args, 5, st_ptrs, 1, &msg) != 0)
        return -1;
    snap->socket_count = (LONG)snap->status.active_sockets;

    if_args[0] = TN_IFCTL_LIST; if_args[1] = 0; if_args[2] = 0; if_args[3] = 0;
    if_args[4] = TN_SNAP_MAX_IFS;
    if_ptrs[0] = (APTR)snap->ifs;
    {
        int rc = tn_ipc_oneshot_ex(TN_IPC_CMD_IFCTL, if_args, 5, if_ptrs, 1, &msg);
        snap->if_count = (rc >= 0) ? rc : -1;
    }

    rt_args[0] = TN_ROUTECTL_LIST; rt_args[1] = 0; rt_args[2] = 0; rt_args[3] = 0;
    rt_args[4] = TN_SNAP_MAX_ROUTES;
    rt_ptrs[0] = (APTR)snap->routes;
    {
        int rc = tn_ipc_oneshot_ex(TN_IPC_CMD_ROUTECTL, rt_args, 5, rt_ptrs, 1, &msg);
        snap->route_count = (rc >= 0) ? rc : -1;
    }

    return 0;
}
