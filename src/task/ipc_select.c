/*
 * tolunnet ? Select & Async Signaling Implementation (ipc_select.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_select.h"
#include "slot_table.h"

/* Deliver SIGIO to socket owner task if mask is set (TNET-067) */
void tn_signal_socket(TnSocketSlot *slot)
{
    if (slot != NULL && slot->in_use && slot->owner_task != NULL && slot->owner_base != NULL) {
        ULONG sig_io = slot->owner_base->sig_io;
        if (sig_io != 0) {
            Signal(slot->owner_task, sig_io);
        }
    }
}

BOOL tn_select_can_read(const TnSocketSlot *slot)
{
    if (slot == NULL || !slot->in_use) return FALSE;
    if (slot->rx_head != NULL) return TRUE;
    if (slot->tcp_state == TN_TCP_STATE_PEER_CLOSED || slot->tcp_state == TN_TCP_STATE_ERROR) return TRUE;
    if (slot->tcp_state == TN_TCP_STATE_LISTENING && slot->accept_head != NULL) return TRUE;
    return FALSE;
}

BOOL tn_select_can_write(const TnSocketSlot *slot)
{
    if (slot == NULL || !slot->in_use) return FALSE;
    if (slot->tcp_state == TN_TCP_STATE_ESTABLISHED || slot->tcp_state == TN_TCP_STATE_ERROR) return TRUE;
    if (slot->type == 2 /* UDP */ || slot->type == 3 /* RAW */) return TRUE;
    return FALSE;
}

BOOL tn_select_has_except(const TnSocketSlot *slot)
{
    if (slot == NULL || !slot->in_use) return FALSE;
    if (slot->tcp_state == TN_TCP_STATE_ERROR || slot->last_error != 0) return TRUE;
    return FALSE;
}

int tn_ipc_cmd_waitselect(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnSocketBase *base;
    LONG nfds;
    ULONG *rfds;
    ULONG *wfds;
    ULONG *efds;
    ULONG in_r, in_w, in_e;
    ULONG out_r = 0, out_w = 0, out_e = 0;
    LONG ready_cnt = 0;
    int chk;
    int i;
    (void)slot;

    base = (TnSocketBase *)imsg->socket_base;
    if (base == NULL) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0; /* TN_IPC_REPLY_NOW */
    }

    nfds = imsg->args[0];
    rfds = (ULONG *)imsg->ptrs[0];
    wfds = (ULONG *)imsg->ptrs[1];
    efds = (ULONG *)imsg->ptrs[2];
    in_r = rfds ? *rfds : 0;
    in_w = wfds ? *wfds : 0;
    in_e = efds ? *efds : 0;

    chk = tn_fdset_check_nfds((int)nfds);
    if (chk != 0) {
        imsg->result = -1;
        imsg->err_no = chk;
        return 0; /* TN_IPC_REPLY_NOW */
    }

    for (i = 0; i < nfds; i++) {
        ULONG mask = (1UL << i);
        if ((in_r | in_w | in_e) & mask) {
            int slot_idx;
            TnSocketSlot *s = tn_slot_lookup(d, base, i, &slot_idx);
            if (s == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return 0; /* TN_IPC_REPLY_NOW */
            }
            /* Read readiness */
            if (in_r & mask) {
                if (tn_select_can_read(s)) {
                    out_r |= mask;
                    ready_cnt++;
                }
            }
            /* Write readiness */
            if (in_w & mask) {
                if (tn_select_can_write(s)) {
                    out_w |= mask;
                    ready_cnt++;
                }
            }
            /* Exception readiness */
            if (in_e & mask) {
                if (tn_select_has_except(s)) {
                    out_e |= mask;
                    ready_cnt++;
                }
            }
        }
    }

    if (rfds) *rfds = out_r;
    if (wfds) *wfds = out_w;
    if (efds) *efds = out_e;

    imsg->result = ready_cnt;
    imsg->err_no = 0;
    return 0; /* TN_IPC_REPLY_NOW */
}