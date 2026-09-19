/*
 * tolunnet — ROUTECTL IPC handler (CLOSE §B.5).
 *
 * SHOW/ADD/DELETE against the static route table (src/task/route.c) that
 * the lwIP routing hooks consult (src/task/route_hook.c).
 */
#include "ipc_route.h"
#include "route.h"
#include <string.h>

int tn_ipc_cmd_routectl(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    LONG op = imsg->args[0];
    (void)d;
    (void)slot;

    if (op == TN_ROUTECTL_LIST) {
        TnRouteInfo *out = (TnRouteInfo *)imsg->ptrs[0];
        LONG cap = imsg->args[4];
        LONG count = 0;
        int i;

        if (out == NULL || cap <= 0) {
            imsg->result = -1;
            imsg->err_no = EINVAL;
            return 0; /* TN_IPC_REPLY_NOW */
        }
        for (i = 0; i < TN_MAX_ROUTES && count < cap; i++) {
            const TnRoute *r = tn_route_at(i);
            if (r != NULL) {
                TnRouteInfo *ent = &out[count];
                memset(ent, 0, sizeof(TnRouteInfo));
                ent->struct_size = (uint16_t)sizeof(TnRouteInfo);
                ent->in_use = 1;
                ent->dest = r->dest;
                ent->mask = r->mask;
                ent->gw = r->gw;
                count++;
            }
        }
        imsg->result = count;
        imsg->err_no = 0;
        return 0;
    } else if (op == TN_ROUTECTL_ADD) {
        int eno = 0;
        int rc = tn_route_add((uint32_t)imsg->args[1], (uint32_t)imsg->args[2],
                              (uint32_t)imsg->args[3], &eno);
        imsg->err_no = eno;
        imsg->result = rc;
        return 0;
    } else if (op == TN_ROUTECTL_DELETE) {
        int eno = 0;
        int rc = tn_route_delete((uint32_t)imsg->args[1], (uint32_t)imsg->args[2],
                                 &eno);
        imsg->err_no = eno;
        imsg->result = rc;
        return 0;
    }

    imsg->result = -1;
    imsg->err_no = EINVAL;
    return 0;
}
