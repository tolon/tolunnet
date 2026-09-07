/*
 * tolunnet ? Select & Async Signaling Implementation (ipc_select.c).
 *
 * ROUND4b ?B (Modular Daemon Refactor).
 */
#include "ipc_select.h"
#include "slot_table.h"

#include "../common/log.h"
#include "../common/fdset_util.h"

/* Deliver SIGIO to socket owner task and notify active selectors (TNET-067, §D) */
void tn_signal_socket(TnDaemon *d, TnSocketSlot *slot)
{
    int s_idx;
    int sel_i;

    if (slot == NULL || !slot->in_use) return;

    /* Legacy SIGIO delivery */
    if (slot->owner_task != NULL && slot->owner_base != NULL) {
        ULONG sig_io = slot->owner_base->sig_io;
        if (sig_io != 0) {
            Signal(slot->owner_task, sig_io);
        }
    }

    /* Event-driven selectors (§D) */
    if (d == NULL || d->selector_count == 0) return;
    s_idx = (int)(slot - d->sockets);
    if (s_idx < 0 || s_idx >= TN_MAX_GLOBAL_SOCKETS) return;

    for (sel_i = 0; sel_i < TN_MAX_SELECTORS; sel_i++) {
        TnSelector *sel = &d->selectors[sel_i];
        if (!sel->in_use || sel->base == NULL || sel->task == NULL || sel->sig_select == 0) continue;

        BOOL match = FALSE;
        int fd;
        for (fd = 0; fd < sel->nfds && fd < TN_MAX_FDS_PER_TASK; fd++) {
            if (sel->base->fd_map[fd] == s_idx) {
                ULONG mask = (1UL << fd);
                if ((sel->read_mask & mask) && tn_select_can_read(slot)) {
                    match = TRUE;
                    break;
                }
                if ((sel->write_mask & mask) && tn_select_can_write(slot)) {
                    match = TRUE;
                    break;
                }
                if ((sel->except_mask & mask) && tn_select_has_except(slot)) {
                    match = TRUE;
                    break;
                }
            }
        }
        if (match) {
            tn_logf(TN_LOG_VERBOSE, "tolunnet: signal_socket slot=%d woke task 0x%p sig=0x%lx\n",
                    s_idx, sel->task, sel->sig_select);
            Signal(sel->task, sel->sig_select);
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

int tn_ipc_cmd_select_arm(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnSocketBase *base;
    LONG nfds;
    ULONG in_r, in_w, in_e;
    ULONG out_r = 0, out_w = 0, out_e = 0;
    ULONG *rfds;
    ULONG *wfds;
    ULONG *efds;
    LONG ready_cnt = 0;
    int chk;
    int i;
    int sel_slot = -1;
    (void)slot;

    if (d == NULL || imsg == NULL) return 0;
    base = (TnSocketBase *)imsg->socket_base;
    if (base == NULL) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }

    nfds = imsg->args[0];
    in_r = (ULONG)imsg->args[1];
    in_w = (ULONG)imsg->args[2];
    in_e = (ULONG)imsg->args[3];
    rfds = (ULONG *)imsg->ptrs[0];
    wfds = (ULONG *)imsg->ptrs[1];
    efds = (ULONG *)imsg->ptrs[2];

    chk = tn_fdset_check_nfds((int)nfds);
    if (chk != 0) {
        imsg->result = -1;
        imsg->err_no = chk;
        return 0;
    }

    /* Validate descriptors and evaluate immediate readiness */
    for (i = 0; i < nfds; i++) {
        ULONG mask = (1UL << i);
        if ((in_r | in_w | in_e) & mask) {
            int slot_idx;
            TnSocketSlot *s = tn_slot_lookup(d, base, i, &slot_idx);
            if (s == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return 0;
            }
            if ((in_r & mask) && tn_select_can_read(s)) {
                out_r |= mask;
                ready_cnt++;
            }
            if ((in_w & mask) && tn_select_can_write(s)) {
                out_w |= mask;
                ready_cnt++;
            }
            if ((in_e & mask) && tn_select_has_except(s)) {
                out_e |= mask;
                ready_cnt++;
            }
        }
    }

    /* If descriptors are already ready, return count and sets immediately */
    if (ready_cnt > 0) {
        if (rfds) *rfds = out_r;
        if (wfds) *wfds = out_w;
        if (efds) *efds = out_e;
        imsg->result = ready_cnt;
        imsg->err_no = 0;
        return 0;
    }

    /* Check if base already has an armed selector; otherwise find empty slot */
    for (i = 0; i < TN_MAX_SELECTORS; i++) {
        if (d->selectors[i].in_use && d->selectors[i].base == base) {
            sel_slot = i;
            break;
        }
    }
    if (sel_slot < 0) {
        for (i = 0; i < TN_MAX_SELECTORS; i++) {
            if (!d->selectors[i].in_use) {
                sel_slot = i;
                break;
            }
        }
    }

    if (sel_slot < 0) {
        tn_logf(TN_LOG_BASIC, "tolunnet: selector table exhausted (max %d)\n", TN_MAX_SELECTORS);
        imsg->result = -1;
        imsg->err_no = ENOBUFS;
        return 0;
    }

    if (!d->selectors[sel_slot].in_use) {
        d->selector_count++;
    }

    d->selectors[sel_slot].in_use      = TRUE;
    d->selectors[sel_slot].base        = base;
    d->selectors[sel_slot].task        = imsg->client_task;
    d->selectors[sel_slot].sig_select  = base->sig_select;
    d->selectors[sel_slot].nfds        = nfds;
    d->selectors[sel_slot].read_mask   = in_r;
    d->selectors[sel_slot].write_mask  = in_w;
    d->selectors[sel_slot].except_mask = in_e;

    tn_logf(TN_LOG_VERBOSE, "tolunnet: select_arm sel_slot=%d task=0x%p sig=0x%lx nfds=%ld w=0x%lx\n",
            sel_slot, imsg->client_task, base->sig_select, nfds, in_w);

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

int tn_ipc_cmd_select_disarm(TnDaemon *d, TnIpcMsg *imsg, TnSocketSlot *slot)
{
    TnSocketBase *base;
    int i;
    (void)slot;

    if (d == NULL || imsg == NULL) return 0;
    base = (TnSocketBase *)imsg->socket_base;
    if (base != NULL) {
        for (i = 0; i < TN_MAX_SELECTORS; i++) {
            if (d->selectors[i].in_use && d->selectors[i].base == base) {
                d->selectors[i].in_use = FALSE;
                d->selectors[i].base   = NULL;
                d->selectors[i].task   = NULL;
                if (d->selector_count > 0) d->selector_count--;
                tn_logf(TN_LOG_VERBOSE, "tolunnet: select_disarm slot=%d\n", i);
            }
        }
    }

    imsg->result = 0;
    imsg->err_no = 0;
    return 0;
}

void tn_selector_disarm_all_for_base(TnDaemon *d, const TnSocketBase *base)
{
    int i;
    if (d == NULL || base == NULL) return;
    for (i = 0; i < TN_MAX_SELECTORS; i++) {
        if (d->selectors[i].in_use && d->selectors[i].base == base) {
            d->selectors[i].in_use = FALSE;
            d->selectors[i].base   = NULL;
            d->selectors[i].task   = NULL;
            if (d->selector_count > 0) d->selector_count--;
        }
    }
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
    tn_logf(TN_LOG_VERBOSE, "tolunnet: waitselect_query ready=%ld r=0x%lx w=0x%lx e=0x%lx\n",
            ready_cnt, out_r, out_w, out_e);
    return 0; /* TN_IPC_REPLY_NOW */
}