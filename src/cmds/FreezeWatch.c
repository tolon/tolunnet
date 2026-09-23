/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FreezeWatch — TNET-115 live capture without the WinUAE debugger
 * (TN-bugtrack-2 item 2). Runs as a background task next to the bench;
 * when WORK:bsdsocktest.log stops growing mid-suite (the known freeze),
 * it walks the Exec task lists under Forbid() and records each task's
 * name, state, saved SP and the innermost return addresses from its stack,
 * so the wedge point of both bsdsocktest and the daemon becomes visible
 * in WORK:freezewatch.log. Also dumps on demand when WORK:dumptasks
 * exists (deleted after the dump).
 *
 * Bench diagnostic tool only — not part of the shipped package.
 */

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <string.h>

#define LOGNAME   "WORK:freezewatch.log"
#define WATCHFILE "WORK:bsdsocktest.log"
#define TRIGGER   "WORK:dumptasks"

static struct ExecBase *EB;

static LONG file_size(const char *path)
{
    BPTR lock;
    struct FileInfoBlock *fib;
    LONG size = -1;

    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lock == (BPTR)0) return -1;
    fib = (struct FileInfoBlock *)AllocVec(sizeof(*fib), MEMF_PUBLIC | MEMF_CLEAR);
    if (fib != NULL) {
        if (Examine(lock, fib)) {
            size = (LONG)fib->fib_Size;
        }
        FreeVec(fib);
    }
    UnLock(lock);
    return size;
}

static void put(char **p, int cap, int *len, const char *s)
{
    while (*s && *len < cap - 1) *(*p)++ = *s++, (*len)++;
}

static void puthex(char **p, int cap, int *len, ULONG v)
{
    static const char h[] = "0123456789ABCDEF";
    int i;
    for (i = 28; i >= 0 && *len < cap - 1; i -= 4) *(*p)++ = h[(v >> i) & 0xF], *len += 1;
}

static void dump_tasks(BPTR fh, const char *why)
{
    static char buf[4096];
    char *p = buf;
    int len = 0;
    struct List *lists[2];
    int li;

    put(&p, sizeof(buf), &len, "=== FreezeWatch dump: ");
    put(&p, sizeof(buf), &len, why);
    put(&p, sizeof(buf), &len, " ===\n");

    lists[0] = &(EB->TaskWait);
    lists[1] = &(EB->TaskReady);
    for (li = 0; li < 2; li++) {
        struct Task *t;
        put(&p, sizeof(buf), &len, li == 0 ? "--- WAITING ---\n" : "--- READY ---\n");
        Forbid();
        for (t = (struct Task *)lists[li]->lh_Head;
             t != NULL && t->tc_Node.ln_Succ != NULL;
             t = (struct Task *)t->tc_Node.ln_Succ) {
            ULONG sp = (ULONG)t->tc_SPReg;
            ULONG r1 = 0, r2 = 0;
            const char *name = t->tc_Node.ln_Name ? t->tc_Node.ln_Name : "?";

            /* Innermost return addresses from the saved stack. Byte-safe:
             * the saved SP is long-aligned by ABI. */
            if (sp != 0 && (sp & 1) == 0 && sp > 256 && sp < 0x20000000) {
                memcpy(&r1, (const void *)sp, 4);
                memcpy(&r2, (const void *)(sp + 4), 4);
            }

            put(&p, sizeof(buf), &len, "  ");
            put(&p, sizeof(buf), &len, name);
            put(&p, sizeof(buf), &len, " type=");
            puthex(&p, sizeof(buf), &len, (ULONG)t->tc_Node.ln_Type);
            put(&p, sizeof(buf), &len, " SP=$");
            puthex(&p, sizeof(buf), &len, sp);
            put(&p, sizeof(buf), &len, " ret0=$");
            puthex(&p, sizeof(buf), &len, r1);
            put(&p, sizeof(buf), &len, " ret1=$");
            puthex(&p, sizeof(buf), &len, r2);
            put(&p, sizeof(buf), &len, " sig=$");
            puthex(&p, sizeof(buf), &len, (ULONG)t->tc_SigWait);
            put(&p, sizeof(buf), &len, "\n");
        }
        Permit();
    }

    Write(fh, (CONST APTR)buf, (LONG)len);
    Flush(fh);
}

int main(void)
{
    BPTR fh;
    LONG last_size = -1;
    int stall_secs = 0;
    int dumps = 0;
    int tick;

    EB = *(struct ExecBase **)4UL;
    if (EB == NULL) return 20;

    fh = Open((CONST_STRPTR)LOGNAME, MODE_NEWFILE);
    if (fh == (BPTR)0) {
        /* WORK: not up yet or read-only: fall back to RAM: so the boot
         * marker still proves we ran (copy RAM: file out afterwards). */
        fh = Open((CONST_STRPTR)"RAM:freezewatch.log", MODE_NEWFILE);
        if (fh == (BPTR)0) return 20;
    }
    Write(fh, (CONST APTR)"FreezeWatch started\n", 20);
    Flush(fh);

    for (tick = 0; tick < 40 * 60 * 2; tick++) {   /* ~2 hours max */
        LONG sz = file_size(WATCHFILE);

        /* manual trigger */
        if (file_size(TRIGGER) >= 0) {
            BPTR td = Open((CONST_STRPTR)TRIGGER, MODE_OLDFILE);
            if (td) Close(td);
            DeleteFile((CONST_STRPTR)TRIGGER);
            dump_tasks(fh, "manual trigger");
            dumps++;
        }

        /* freeze detection: file exists, is mid-suite (< 5000 bytes) and
         * has not grown for >= 45 s */
        if (sz >= 0 && sz < 5000) {
            if (sz == last_size) {
                stall_secs += 2;
                if (stall_secs >= 45) {
                    char why[64];
                    strcpy(why, "bsdsocktest stalled size=");
                    /* append size as decimal */
                    {
                        char num[12];
                        int n = 0;
                        LONG v = sz;
                        int i;
                        if (v == 0) num[n++] = '0';
                        while (v > 0 && n < 11) { num[n++] = (char)('0' + (v % 10)); v /= 10; }
                        for (i = 0; i < n / 2; i++) { char c = num[i]; num[i] = num[n-1-i]; num[n-1-i] = c; }
                        num[n] = '\0';
                        strcat(why, num);
                    }
                    dump_tasks(fh, why);
                    dumps++;
                    stall_secs = 0;
                    if (dumps >= 5) break;
                }
            } else {
                stall_secs = 0;
                last_size = sz;
            }
        } else {
            stall_secs = 0;
            last_size = sz;
            if (sz >= 5000) break;   /* suite completed — done watching */
        }

        Delay(100);   /* 2 s */
    }

    Write(fh, (CONST APTR)"FreezeWatch exit\n", 17);
    Close(fh);
    return 0;
}
