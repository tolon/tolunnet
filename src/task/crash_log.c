/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — DIAG crash capture implementation (TNET-139).
 *
 * The handler runs in trap context: no allocation, no printf, no library
 * calls. It only formats into a static buffer; tn_crash_flush_pending()
 * writes the file later from task context. Everything it needs is
 * pre-recorded at tn_crash_arm() time (segment table, log ring pointer).
 */
#include "crash_log.h"

#ifdef __AMIGA__

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <dos/dos.h>

#define TN_CRASH_LOG_PATH "RAM:tolunnet-crash.log"
#define TN_MAX_SEGS 32

/* 11a item 2: per-task log path; tn_crash_arm keeps the RAM: default. */
static const char *g_crash_path = TN_CRASH_LOG_PATH;

/* Recorded at arm time; read-only in trap context. tn_crash_old_trap is
 * read by crash_trap.s (8.1: must be the symbol the asm chains through). */
APTR  tn_crash_old_trap;
static ULONG g_seg_base[TN_MAX_SEGS];
static ULONG g_seg_size[TN_MAX_SEGS];
static int   g_seg_count;

/* 8.3: the trap only formats here; tn_crash_flush_pending() writes the file
 * from task context. Fixed symbol so a monitor can find it if the task
 * never runs again (the usual case after a Guru: the file is only written
 * if the task survives the chained handler). */
char tn_crash_report[4096];
static volatile LONG g_report_len;
static volatile int  g_report_pending;
static volatile int  g_in_trap;   /* a fault inside the formatter just chains */

/* Exception frame as seen by tc_TrapCode (8.2): Exec pushes the trap
 * number longword on top of the CPU frame.
 *   68000 vectors 2/3 (group 0): SSW(w0) ACCESS(w1-2) IR(w3) SR(w4) PC(w5-6)
 *   68000 other vectors:         SR(w0) PC(w1-2)
 *   68010+:                      SR(w0) PC(w1-2) FORMAT/VECOFS(w3) ...
 * The raw words are dumped too. */
#define FRAME_WORDS 16

static void put(char *dst, int cap, int *len, const char *s)
{
    while (*s && *len < cap - 1) dst[(*len)++] = *s++;
}

static void puthex(char *dst, int cap, int *len, ULONG v, int digits)
{
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = digits - 1; i >= 0 && *len < cap - 1; i--) {
        dst[(*len)++] = h[(v >> (i * 4)) & 0xF];
    }
}

/* Log ring accessor implemented in log.c (trap-safe read of a static
 * buffer; the writer may be mid-line but each slot is NUL-terminated). */
extern const char *const *tn_log_ring_snapshot(void);
extern int tn_log_ring_count(void);

extern void tn_crash_trap_asm(void);

/* 68010+ fault address word index per stack-frame format (vectors 2/3):
 * fmt 2 (040/060 address) / 4 (060 access) at +8, fmt 8 (68010, after
 * its SSW) at +$0A, fmt A/B (020/030) at +$10, fmt 7 (040 access) at +$14. -1 = none. */
static int fault_index010(unsigned fmt)
{
    switch (fmt) {
    case 0x2: case 0x4:           return 4;
    case 0x8:                     return 5;
    case 0xA: case 0xB:           return 8;
    case 0x7:                     return 10;
    default:                      return -1;
    }
}

/* Called from crash_trap.s in supervisor mode. regs = d0-d7/a0-a6/USP
 * (16 longs); stk = Exec's trap-number longword, CPU frame follows. */
void tn_crash_entry(const unsigned short *regs, const unsigned short *stk)
{
    char *buf = tn_crash_report;
    const int cap = (int)sizeof(tn_crash_report);
    const unsigned short *frame = stk + 2;
    struct ExecBase *sys = *(struct ExecBase **)4UL;
    int len = 0;
    int i;
    int is010 = (sys != NULL && (sys->AttnFlags & AFF_68010) != 0);
    ULONG trapno = ((ULONG)stk[0] << 16) | stk[1];
    ULONG pc, sr, fault = 0;
    int have_fault = 0;
    const char *const *ring;
    int ring_n;

    /* First crash wins until the task flushes it; never recurse. */
    if (g_report_pending || g_in_trap) return;
    g_in_trap = 1;

    if (is010) {
        unsigned fmt = (unsigned)(frame[3] >> 12);
        int fi = fault_index010(fmt);
        sr = frame[0];
        pc = ((ULONG)frame[1] << 16) | frame[2];
        if ((trapno == 2 || trapno == 3) && fi >= 0) {
            fault = ((ULONG)frame[fi] << 16) | frame[fi + 1];
            have_fault = 1;
        }
    } else if (trapno == 2 || trapno == 3) {
        sr = frame[4];
        pc = ((ULONG)frame[5] << 16) | frame[6];
        fault = ((ULONG)frame[1] << 16) | frame[2];
        have_fault = 1;
    } else {
        sr = frame[0];
        pc = ((ULONG)frame[1] << 16) | frame[2];
    }

    put(buf, cap, &len, "tolunnet DIAG crash report\nCPU: ");
    put(buf, cap, &len, is010 ? "68010+ frame\n" : "68000 frame\n");
    /* 11a item 2: tc_TrapCode routes the CPU exceptions: bus (2),
     * address (3), illegal (4), div0 (5), CHK (6), TRAPV (7),
     * privilege (8), trace (9), Line-A (10), Line-F (11), TRAP #n (32+n). */
    put(buf, cap, &len, "TRAP=$"); puthex(buf, cap, &len, trapno, 8);
    if (trapno == 2) put(buf, cap, &len, " (bus error)");
    if (trapno == 3) put(buf, cap, &len, " (address error)");
    if (trapno == 4) put(buf, cap, &len, " (illegal)");
    if (trapno == 10) put(buf, cap, &len, " (Line-A)");
    if (trapno == 11) put(buf, cap, &len, " (Line-F)");
    if (is010) {
        put(buf, cap, &len, " FORMAT/VEC=$"); puthex(buf, cap, &len, frame[3], 4);
    }
    put(buf, cap, &len, "\nPC=$"); puthex(buf, cap, &len, pc, 8);
    put(buf, cap, &len, " SR=$"); puthex(buf, cap, &len, sr, 4);
    if (have_fault) {
        put(buf, cap, &len, " FAULT=$"); puthex(buf, cap, &len, fault, 8);
        if (!is010) {
            put(buf, cap, &len, " SSW=$"); puthex(buf, cap, &len, frame[0], 4);
            put(buf, cap, &len, " IR=$"); puthex(buf, cap, &len, frame[3], 4);
        }
    }
    put(buf, cap, &len, "\n");

    put(buf, cap, &len, "FRAME words:");
    for (i = 0; i < FRAME_WORDS; i++) {
        put(buf, cap, &len, " ");
        puthex(buf, cap, &len, frame[i], 4);
    }
    put(buf, cap, &len, "\n");

    put(buf, cap, &len, "REGS d0-d7:");
    for (i = 0; i < 8; i++) {
        ULONG v = ((ULONG)regs[i * 2] << 16) | regs[i * 2 + 1];
        put(buf, cap, &len, " ");
        puthex(buf, cap, &len, v, 8);
    }
    put(buf, cap, &len, "\nREGS a0-a6,USP:");
    for (i = 0; i < 8; i++) {
        ULONG v = ((ULONG)regs[16 + i * 2] << 16) | regs[16 + i * 2 + 1];
        put(buf, cap, &len, " ");
        puthex(buf, cap, &len, v, 8);
    }
    put(buf, cap, &len, "\n");

    put(buf, cap, &len, "SEGMENTS (hunk base/size; PC-base = objdump offset):\n");
    for (i = 0; i < g_seg_count; i++) {
        put(buf, cap, &len, "  seg ");
        puthex(buf, cap, &len, (ULONG)i, 2);
        put(buf, cap, &len, " base=$"); puthex(buf, cap, &len, g_seg_base[i], 8);
        put(buf, cap, &len, " size=$");  puthex(buf, cap, &len, g_seg_size[i], 8);
        put(buf, cap, &len, "\n");
    }

    put(buf, cap, &len, "LAST LOG LINES:\n");
    ring = tn_log_ring_snapshot();
    ring_n = tn_log_ring_count();
    for (i = 0; i < ring_n && ring != NULL && ring[i] != NULL; i++) {
        put(buf, cap, &len, "  ");
        put(buf, cap, &len, ring[i]);
        put(buf, cap, &len, "\n");
    }

    buf[len] = 0; /* put() keeps len <= cap - 1 */

    /* 8.3: no DOS here (supervisor/trap context). */
    g_report_len = len;
    g_report_pending = 1;
    g_in_trap = 0;
}

/* Task context only (DOS). Cheap no-op unless a trap formatted a report. */
void tn_crash_flush_pending(void)
{
    BPTR fh;
    LONG len;

    if (!g_report_pending) return;
    len = g_report_len;
    fh = Open((CONST_STRPTR)g_crash_path, MODE_NEWFILE);
    if (fh != (BPTR)0) {
        Write(fh, (CONST APTR)tn_crash_report, len);
        Close(fh);
    }
    g_report_pending = 0;
}

/* Previous-handler chain lives in asm: we must re-enter it with the exact
 * trap-entry state so the normal Software Failure still happens. */

void tn_crash_arm(void)
{
    tn_crash_arm_path(TN_CRASH_LOG_PATH);
}

void tn_crash_arm_path(const char *path)
{
    struct Process *pr = (struct Process *)FindTask(NULL);
    BPTR seg;
    int n = 0;

    if (pr == NULL) return;

    /* 8.4: a CLI-started program's seglist is cli_Module; pr_SegList is
     * a SegArray (count at [0], the program at [3] for CreateNewProc). */
    seg = (BPTR)0;
    if (pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        struct CommandLineInterface *cli =
            (struct CommandLineInterface *)BADDR(pr->pr_CLI);
        if (cli != NULL) seg = cli->cli_Module;
        if (seg == (BPTR)0 && pr->pr_SegList != (BPTR)0) {
            BPTR *arr = (BPTR *)BADDR(pr->pr_SegList);
            if ((ULONG)arr[0] >= 3) seg = arr[3];
        }
    }

    /* Segment: size (bytes, incl. header) at [-1], next BPTR at [0],
     * code/data from [1]. */
    for (; seg != (BPTR)0 && n < TN_MAX_SEGS; n++) {
        BPTR *bptr = (BPTR *)BADDR(seg);
        g_seg_size[n] = (ULONG)bptr[-1];
        g_seg_base[n] = (ULONG)&bptr[1];
        seg = bptr[0];
    }
    g_seg_count = n;

    if (path != NULL) g_crash_path = path;

    /* Never chain to NULL: fall back to Exec's default task trap. */
    tn_crash_old_trap = pr->pr_Task.tc_TrapCode;
    if (tn_crash_old_trap == NULL) tn_crash_old_trap = SysBase->TaskTrapCode;
    pr->pr_Task.tc_TrapCode = tn_crash_trap_asm;
}

const char *tn_crash_last(void)
{
    return g_crash_path;
}

#else /* !__AMIGA__ */

void tn_crash_arm(void) {}
void tn_crash_arm_path(const char *path) { (void)path; }
void tn_crash_flush_pending(void) {}
const char *tn_crash_last(void) { return ""; }

#endif
