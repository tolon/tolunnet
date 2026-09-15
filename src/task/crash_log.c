/*
 * tolunnet — DIAG crash capture implementation (TNET-139).
 *
 * The handler runs in trap context: no allocation, no printf, no library
 * calls beyond raw Open/Write/Close on RAM:. Everything it needs is
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

/* Recorded at arm time; read-only in trap context. */
static APTR  g_old_trap;
static ULONG g_seg_base[TN_MAX_SEGS];
static ULONG g_seg_size[TN_MAX_SEGS];
static int   g_seg_count;

/* Exception-frame word dump + PC/fault extraction. 68000 bus/address error
 * frame: SR(w@0) PC(l@2) op(w@6) addr(l@8); 68010+ has a format word:
 * fmt(w@0) ... SR(w@4) PC(l@6). We dump raw words and print BOTH readings;
 * the AttnFlags 68010 bit says which interpretation to trust. */
#define FRAME_WORDS 12

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

void tn_crash_entry(const unsigned short *regs, const unsigned short *frame)
{
    static char buf[4096];
    struct ExecBase *sys = *(struct ExecBase **)4UL;
    int len = 0;
    int i;
    int is010 = (sys != NULL && (sys->AttnFlags & AFF_68010) != 0);
    ULONG pc000, fault000, pc010, fault010;
    BPTR fh;
    const char *const *ring;
    int ring_n;

    pc000   = ((ULONG)frame[2] << 16) | frame[3];
    fault000= ((ULONG)frame[8] << 16) | frame[9];
    pc010   = ((ULONG)frame[6] << 16) | frame[7];
    fault010= ((ULONG)frame[10] << 16) | frame[11];

    put(buf, sizeof(buf), &len, "tolunnet DIAG crash report\nCPU: ");
    put(buf, sizeof(buf), &len, is010 ? "68010+ frame\n" : "68000 frame\n");

    put(buf, sizeof(buf), &len, "PC(68000 read) =$");  puthex(buf, sizeof(buf), &len, pc000, 8);
    put(buf, sizeof(buf), &len, " PC(68010+ read) =$"); puthex(buf, sizeof(buf), &len, pc010, 8);
    put(buf, sizeof(buf), &len, "\nSR=$"); puthex(buf, sizeof(buf), &len, frame[is010 ? 4 : 0], 4);
    put(buf, sizeof(buf), &len, " FAULT(68000 read) =$"); puthex(buf, sizeof(buf), &len, fault000, 8);
    put(buf, sizeof(buf), &len, " FAULT(68010+ read) =$"); puthex(buf, sizeof(buf), &len, fault010, 8);
    put(buf, sizeof(buf), &len, "\n");

    put(buf, sizeof(buf), &len, "FRAME words:");
    for (i = 0; i < FRAME_WORDS; i++) {
        put(buf, sizeof(buf), &len, " ");
        puthex(buf, sizeof(buf), &len, frame[i], 4);
    }
    put(buf, sizeof(buf), &len, "\n");

    put(buf, sizeof(buf), &len, "REGS d0-d7:");
    for (i = 0; i < 8; i++) {
        ULONG v = ((ULONG)regs[i * 2] << 16) | regs[i * 2 + 1];
        put(buf, sizeof(buf), &len, " ");
        puthex(buf, sizeof(buf), &len, v, 8);
    }
    put(buf, sizeof(buf), &len, "\nREGS a0-a7:");
    for (i = 0; i < 8; i++) {
        ULONG v = ((ULONG)regs[16 + i * 2] << 16) | regs[16 + i * 2 + 1];
        put(buf, sizeof(buf), &len, " ");
        puthex(buf, sizeof(buf), &len, v, 8);
    }
    put(buf, sizeof(buf), &len, "\n");

    put(buf, sizeof(buf), &len, "SEGMENTS (hunk base/size; PC-base = objdump offset):\n");
    for (i = 0; i < g_seg_count; i++) {
        put(buf, sizeof(buf), &len, "  seg ");
        puthex(buf, sizeof(buf), &len, (ULONG)i, 2);
        put(buf, sizeof(buf), &len, " base=$"); puthex(buf, sizeof(buf), &len, g_seg_base[i], 8);
        put(buf, sizeof(buf), &len, " size=$");  puthex(buf, sizeof(buf), &len, g_seg_size[i], 8);
        put(buf, sizeof(buf), &len, "\n");
    }

    put(buf, sizeof(buf), &len, "LAST LOG LINES:\n");
    ring = tn_log_ring_snapshot();
    ring_n = tn_log_ring_count();
    for (i = 0; i < ring_n && ring != NULL && ring[i] != NULL; i++) {
        put(buf, sizeof(buf), &len, "  ");
        put(buf, sizeof(buf), &len, ring[i]);
        put(buf, sizeof(buf), &len, "\n");
    }

    /* Best-effort write; RAM: needs no interaction. */
    fh = Open((CONST_STRPTR)TN_CRASH_LOG_PATH, MODE_NEWFILE);
    if (fh != (BPTR)0) {
        Write(fh, (CONST APTR)buf, (LONG)len);
        Close(fh);
    }
}

/* Previous-handler chain lives in asm: we must re-enter it with the exact
 * trap-entry state so the normal Software Failure still happens. */

void tn_crash_arm(void)
{
    struct Process *pr = (struct Process *)FindTask(NULL);
    BPTR seg;
    int n = 0;

    if (pr == NULL) return;

    for (seg = pr->pr_SegList; seg != (BPTR)0 && n < TN_MAX_SEGS; n++) {
        BPTR *bptr = (BPTR *)(void *)BADDR(seg);
        if (bptr == NULL) break;
        g_seg_size[n] = (ULONG)bptr[0];          /* first longword = size */
        g_seg_base[n] = (ULONG)&bptr[1];         /* segment data follows  */
        seg = bptr[1];                           /* next BPTR             */
    }
    g_seg_count = n;

    g_old_trap = pr->pr_Task.tc_TrapCode;
    pr->pr_Task.tc_TrapCode = tn_crash_trap_asm;
}

const char *tn_crash_last(void)
{
    return TN_CRASH_LOG_PATH;
}

#else /* !__AMIGA__ */

void tn_crash_arm(void) {}
const char *tn_crash_last(void) { return ""; }

#endif
