/* SPDX-License-Identifier: GPL-3.0-or-later */
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
/* TNET-151: a selector entry survives its owning client process only if
 * that process died without CloseLibrary (the wizard child does). Its
 * base pointer is then FREED memory - dereferencing fd_map/dtablesize is
 * undefined and the bookkeeping desyncs. Validation is pointer-only: the
 * base must be in the open-bases registry (TNET-150). Stale entries are
 * disarmed on sight (self-healing scan). */
static BOOL tn_selector_base_alive(const TnDaemon *d, const void *base)
{
    int b;
    if (base == NULL) return FALSE;
    for (b = 0; b < TN_CLIENT_BASES_MAX; b++) {
        if (d->open_bases[b] == base) return TRUE;
    }
    return FALSE;
}

/* 2.3: the selector/WaitSelect bookkeeping covers two fd_set words (fds
 * 0-63). 4.4BSD select clamps nd to the descriptor table; we clamp to that
 * and to 64. */
#define TN_SELECT_MAX_FDS 64

static LONG tn_select_clamp_nfds(const TnSocketBase *base, LONG nfds)
{
    if (nfds > TN_SELECT_MAX_FDS) nfds = TN_SELECT_MAX_FDS;
    if (base != NULL && nfds > (LONG)base->dtablesize) nfds = (LONG)base->dtablesize;
    if (nfds < 0) nfds = 0;
    return nfds;
}

/* 2.3: result words the daemon does not evaluate (fds >= 64, or the hi
 * word when nfds was clamped to <= 32) must not keep the caller's request
 * bits - they would read back as "ready". The caller's fd_set has at least
 * (req_nfds + 31) / 32 words. */
#define TN_SELECT_FDSET_WORDS (256 / 32) /* netinclude default FD_SETSIZE */

/* YENI-8: the app's fd_set may sit at an odd address - its words are
 * copied byte-wise, never loaded/stored through a ULONG pointer. */
static ULONG tn_fdset_word_get(const void *set, LONG w)
{
    ULONG v;
    memcpy(&v, (const UBYTE *)set + w * (LONG)sizeof(ULONG), sizeof(v));
    return v;
}

static void tn_fdset_word_put(void *set, LONG w, ULONG v)
{
    memcpy((UBYTE *)set + w * (LONG)sizeof(ULONG), &v, sizeof(v));
}

/* result words: lo always, hi only when it was evaluated (nfds > 32) */
static void tn_fdset_put_result(void *set, LONG nfds, ULONG lo, ULONG hi)
{
    if (set == NULL) return;
    tn_fdset_word_put(set, 0, lo);
    if (nfds > 32) tn_fdset_word_put(set, 1, hi);
}

static void tn_select_clear_tail(void *set, LONG clamped, LONG req_nfds)
{
    LONG w = (clamped > 32) ? 2 : 1;
    LONG nw = (req_nfds + 31) / 32;
    /* never past a default-size fd_set: nfds itself is client-controlled */
    if (nw > TN_SELECT_FDSET_WORDS) nw = TN_SELECT_FDSET_WORDS;
    if (set == NULL) return;
    for (; w < nw; w++) tn_fdset_word_put(set, w, 0);
}

/* TNET-151: keep selector_count true - recompute from the table instead
 * of trusting increment/decrement pairs across grow/disarm/arm churn. */
static void tn_selector_count_resync(TnDaemon *d)
{
    int i, n = 0;
    if (d == NULL || d->selectors == NULL) return;
    for (i = 0; i < (int)d->max_selectors; i++) {
        if (d->selectors[i].in_use) n++;
    }
    d->selector_count = (uint8_t)n;
}

void tn_signal_socket(TnDaemon *d, TnSocketSlot *slot)
{
    int s_idx;
    int sel_i;

    if (slot == NULL || !slot->in_use) return;

    /* Legacy SIGIO delivery. 3.7: only to a base still in the open-bases
     * registry - otherwise base and task may both be freed memory. */
    if (slot->owner_base != NULL && d != NULL && !tn_selector_base_alive(d, slot->owner_base)) {
        slot->owner_base = NULL;
        slot->owner_task = NULL;
    }
    if (slot->owner_task != NULL && slot->owner_base != NULL) {
        ULONG sig_io = slot->owner_base->sig_io;
        if (sig_io != 0) {
            Signal(slot->owner_task, sig_io);
            if (d != NULL) d->sigio_sent++;
        }
    }

    /* Event-driven selectors (§D) */
    if (d == NULL || d->selector_count == 0 || d->selectors == NULL) return;
    s_idx = (int)(slot - d->sockets);
    if (s_idx < 0 || s_idx >= TN_MAX_GLOBAL_SOCKETS) return;

    for (sel_i = 0; sel_i < (int)d->max_selectors; sel_i++) {
        TnSelector *sel = &d->selectors[sel_i];
        if (!sel->in_use || sel->base == NULL || sel->task == NULL || sel->sig_select == 0) continue;
        if (!tn_selector_base_alive(d, sel->base)) {
            /* TNET-151: owner died without CloseLibrary - drop the stale
             * entry instead of dereferencing freed memory */
            sel->in_use = FALSE;
            sel->base   = NULL;
            sel->task   = NULL;
            tn_selector_count_resync(d);
            continue;
        }

        BOOL match = FALSE;
        int fd;
        for (fd = 0; fd < sel->nfds && fd < sel->base->dtablesize; fd++) {
            ULONG mask = (1UL << (fd & 31));
            int hi = (fd >= 32);
            ULONG r = hi ? sel->read_mask_hi  : sel->read_mask;
            ULONG w = hi ? sel->write_mask_hi : sel->write_mask;
            ULONG e = hi ? sel->except_mask_hi : sel->except_mask;
            if (sel->base->fd_map[fd] == s_idx) {
                if ((r & mask) && tn_select_can_read(slot)) {
                    match = TRUE;
                    break;
                }
                if ((w & mask) && tn_select_can_write(slot)) {
                    match = TRUE;
                    break;
                }
                if ((e & mask) && tn_select_has_except(slot)) {
                    match = TRUE;
                    break;
                }
            }
        }
        if (match) {
            tn_logf(TN_LOG_VERBOSE, "tolunnet: signal_socket slot=%d woke task 0x%p sig=0x%lx\n",
                    s_idx, sel->task, sel->sig_select);
            Signal(sel->task, sel->sig_select);
            d->selector_wakeups++;
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
    if (slot->tcp_state == TN_TCP_STATE_ERROR) return TRUE;
    if (slot->type == 2 /* UDP */ || slot->type == 3 /* RAW */) return TRUE;
    /* 4.6: 4.4BSD sowriteable - connected (CLOSE_WAIT included) with send
     * space, or CANTSENDMORE (send fails at once with EPIPE). */
    if (slot->tcp_state == TN_TCP_STATE_ESTABLISHED ||
        slot->tcp_state == TN_TCP_STATE_PEER_CLOSED) {
        if (slot->shut_wr) return TRUE;
        if (slot->tcp_pcb == NULL || slot->pending_send_msg != NULL) return FALSE;
        return (tcp_sndbuf(slot->tcp_pcb) > 0 &&
                tcp_sndqueuelen(slot->tcp_pcb) < TCP_SND_QUEUELEN) ? TRUE : FALSE;
    }
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
    ULONG arm_hi_r = 0, arm_hi_w = 0, arm_hi_e = 0;
    void *rfds;                 /* client fd_sets: tn_fdset_word_get/put */
    void *wfds;
    void *efds;
    LONG ready_cnt = 0;
    int chk;
    int i;
    int sel_slot = -1;
    LONG req_nfds;
    (void)slot;

    if (d == NULL || imsg == NULL) return 0;
    base = (TnSocketBase *)imsg->socket_base;
    if (base == NULL) {
        imsg->result = -1;
        imsg->err_no = EINVAL;
        return 0;
    }
    if (d->selectors == NULL || d->max_selectors == 0) {
        imsg->result = -1;
        imsg->err_no = ENOBUFS;
        return 0;
    }

    nfds = imsg->args[0];
    in_r = (ULONG)imsg->args[1];
    in_w = (ULONG)imsg->args[2];
    in_e = (ULONG)imsg->args[3];
    rfds = imsg->ptrs[0];
    wfds = imsg->ptrs[1];
    efds = imsg->ptrs[2];

    chk = tn_fdset_check_nfds((int)nfds);
    if (chk != 0) {
        imsg->result = -1;
        imsg->err_no = chk;
        return 0;
    }
    req_nfds = nfds;
    nfds = tn_select_clamp_nfds(base, nfds); /* 2.3 */

    /* z.ai step 6 item 4: fd_set carries TWO words (64 fds). The LO word
     * rides args[1..3]; the HI word is read from / written to the client
     * fd_set memory directly (fds_bits[1]). */
    {
        ULONG hi_r = 0, hi_w = 0, hi_e = 0;      /* input (client request) */
        ULONG hi_out_r = 0, hi_out_w = 0, hi_out_e = 0; /* readiness output */
        if (nfds > 32) {
            if (rfds) hi_r = tn_fdset_word_get(rfds, 1);
            if (wfds) hi_w = tn_fdset_word_get(wfds, 1);
            if (efds) hi_e = tn_fdset_word_get(efds, 1);
        }

        /* Validate descriptors and evaluate immediate readiness */
        for (i = 0; i < nfds; i++) {
            ULONG mask = (1UL << (i & 31));
            ULONG *lo;
            if (i < 32) {
                lo = &in_r; if ((in_r | in_w | in_e) & mask) {
                    int slot_idx;
                    TnSocketSlot *s = tn_slot_lookup(d, base, i, &slot_idx);
                    if (s == NULL) {
                        imsg->result = -1;
                        imsg->err_no = EBADF;
                        return 0;
                    }
                    if ((in_r & mask) && tn_select_can_read(s)) { out_r |= mask; ready_cnt++; }
                    if ((in_w & mask) && tn_select_can_write(s)) { out_w |= mask; ready_cnt++; }
                    if ((in_e & mask) && tn_select_has_except(s)) { out_e |= mask; ready_cnt++; }
                }
                (void)lo;
            } else {
                ULONG g = (ULONG)i - 32;
                ULONG m2 = (1UL << (g & 31));
                if (((hi_r | hi_w | hi_e) & m2) == 0) continue;
                int slot_idx;
                TnSocketSlot *s = tn_slot_lookup(d, base, i, &slot_idx);
                if (s == NULL) {
                    imsg->result = -1;
                    imsg->err_no = EBADF;
                    return 0;
                }
                if ((hi_r & m2) && tn_select_can_read(s))  { hi_out_r |= m2; ready_cnt++; }
                if ((hi_w & m2) && tn_select_can_write(s)) { hi_out_w |= m2; ready_cnt++; }
                if ((hi_e & m2) && tn_select_has_except(s)) { hi_out_e |= m2; ready_cnt++; }
            }
        }

        /* If descriptors are already ready, return count and sets immediately */
        if (ready_cnt > 0) {
            tn_fdset_put_result(rfds, nfds, out_r, hi_out_r);
            tn_fdset_put_result(wfds, nfds, out_w, hi_out_w);
            tn_fdset_put_result(efds, nfds, out_e, hi_out_e);
            tn_select_clear_tail(rfds, nfds, req_nfds);
            tn_select_clear_tail(wfds, nfds, req_nfds);
            tn_select_clear_tail(efds, nfds, req_nfds);
            imsg->result = ready_cnt;
            imsg->err_no = 0;
            return 0;
        }

        /* remember the HI request masks on the armed selector (wake scan
         * compares requests, not readiness) */
        arm_hi_r = hi_r;
        arm_hi_w = hi_w;
        arm_hi_e = hi_e;
    }

    /* Check if base already has an armed selector; otherwise find empty slot */
    for (i = 0; i < (int)d->max_selectors; i++) {
        if (d->selectors[i].in_use && d->selectors[i].base == base) {
            sel_slot = i;
            break;
        }
    }
    if (sel_slot < 0) {
        for (i = 0; i < (int)d->max_selectors; i++) {
            if (!d->selectors[i].in_use) {
                sel_slot = i;
                break;
            }
        }
    }

    if (sel_slot < 0) {
        tn_logf(TN_LOG_BASIC, "tolunnet: selector table exhausted (max %lu)\n",
                (ULONG)d->max_selectors);
        imsg->result = -1;
        imsg->err_no = ENOBUFS;
        return 0;
    }

    if (!d->selectors[sel_slot].in_use) {
        d->selector_count++;
    }
    tn_selector_count_resync(d);

    d->selectors[sel_slot].in_use      = TRUE;
    d->selectors[sel_slot].base        = base;
    d->selectors[sel_slot].task        = imsg->client_task;
    d->selectors[sel_slot].sig_select  = base->sig_select;
    d->selectors[sel_slot].nfds        = nfds;
    d->selectors[sel_slot].read_mask   = in_r;
    d->selectors[sel_slot].write_mask  = in_w;
    d->selectors[sel_slot].except_mask = in_e;
    d->selectors[sel_slot].read_mask_hi   = arm_hi_r;
    d->selectors[sel_slot].write_mask_hi  = arm_hi_w;
    d->selectors[sel_slot].except_mask_hi = arm_hi_e;

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
    if (base != NULL && d->selectors != NULL) {
        for (i = 0; i < (int)d->max_selectors; i++) {
            if (d->selectors[i].in_use && d->selectors[i].base == base) {
                d->selectors[i].in_use = FALSE;
                d->selectors[i].base   = NULL;
                d->selectors[i].task   = NULL;
                if (d->selector_count > 0) d->selector_count--;
                tn_logf(TN_LOG_VERBOSE, "tolunnet: select_disarm slot=%d\n", i);
                    tn_selector_count_resync(d);
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
    if (d == NULL || base == NULL || d->selectors == NULL) return;
    for (i = 0; i < (int)d->max_selectors; i++) {
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
    void *rfds;                 /* client fd_sets: tn_fdset_word_get/put */
    void *wfds;
    void *efds;
    ULONG in_r, in_w, in_e;
    ULONG out_r = 0, out_w = 0, out_e = 0;
    LONG ready_cnt = 0;
    LONG req_nfds;
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
    rfds = imsg->ptrs[0];
    wfds = imsg->ptrs[1];
    efds = imsg->ptrs[2];
    in_r = rfds ? tn_fdset_word_get(rfds, 0) : 0;
    in_w = wfds ? tn_fdset_word_get(wfds, 0) : 0;
    in_e = efds ? tn_fdset_word_get(efds, 0) : 0;

    chk = tn_fdset_check_nfds((int)nfds);
    if (chk != 0) {
        imsg->result = -1;
        imsg->err_no = chk;
        return 0; /* TN_IPC_REPLY_NOW */
    }
    req_nfds = nfds;
    nfds = tn_select_clamp_nfds(base, nfds); /* 2.3 */

    /* z.ai step 6 item 4: HI word (fds 32-63) read from client fd_sets */
    ULONG hi_r = 0, hi_w = 0, hi_e = 0;      /* input (client request) */
    ULONG hi_out_r = 0, hi_out_w = 0, hi_out_e = 0; /* readiness output */
    if (nfds > 32) {
        if (rfds) hi_r = tn_fdset_word_get(rfds, 1);
        if (wfds) hi_w = tn_fdset_word_get(wfds, 1);
        if (efds) hi_e = tn_fdset_word_get(efds, 1);
    }

    for (i = 0; i < nfds; i++) {
        ULONG mask = (1UL << (i & 31));
        int hi = (i >= 32);
        ULONG ir = hi ? hi_r : in_r;
        ULONG iw = hi ? hi_w : in_w;
        ULONG ie = hi ? hi_e : in_e;
        if ((ir | iw | ie) & mask) {
            int slot_idx;
            TnSocketSlot *s = tn_slot_lookup(d, base, i, &slot_idx);
            if (s == NULL) {
                imsg->result = -1;
                imsg->err_no = EBADF;
                return 0; /* TN_IPC_REPLY_NOW */
            }
            /* Read readiness */
            if (ir & mask) {
                if (tn_select_can_read(s)) {
                    if (hi) hi_out_r |= mask; else out_r |= mask;
                    ready_cnt++;
                }
            }
            /* Write readiness */
            if (iw & mask) {
                if (tn_select_can_write(s)) {
                    if (hi) hi_out_w |= mask; else out_w |= mask;
                    ready_cnt++;
                }
            }
            /* Exception readiness */
            if (ie & mask) {
                if (tn_select_has_except(s)) {
                    if (hi) hi_out_e |= mask; else out_e |= mask;
                    ready_cnt++;
                }
            }
        }
    }

    tn_fdset_put_result(rfds, nfds, out_r, hi_out_r);
    tn_fdset_put_result(wfds, nfds, out_w, hi_out_w);
    tn_fdset_put_result(efds, nfds, out_e, hi_out_e);
    tn_select_clear_tail(rfds, nfds, req_nfds);
    tn_select_clear_tail(wfds, nfds, req_nfds);
    tn_select_clear_tail(efds, nfds, req_nfds);

    imsg->result = ready_cnt;
    imsg->err_no = 0;
    tn_logf(TN_LOG_VERBOSE, "tolunnet: waitselect_query ready=%ld r=0x%lx w=0x%lx e=0x%lx\n",
            ready_cnt, out_r, out_w, out_e);
    return 0; /* TN_IPC_REPLY_NOW */
}