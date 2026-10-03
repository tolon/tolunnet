/* combine-clr peephole2 cases. Expected with the fix (-O2):
 *   hi_si    : clr.w 4(a0) ; clr.l 6(a0)      (mixed modes: must NOT merge)
 *   hi_hi    : clr.l 4(a0)                    (two words: merge kept)
 *   si_hi    : clr.l 4(a0) ; clr.w 8(a0)      (first is long: rule needs a word on 68000)
 *   hi_vsi   : clr.w 4(a0) ; clr.l 6(a0)      (second store volatile: must NOT merge)
 *   si_si    : 68000 two clr.l; -m68080: one 8-byte clear (merge kept)
 *   si_hi_80 : -m68080: clr.l 4 ; clr.w 8     (mixed modes: must NOT merge) */
struct hs { long pad; short a; long b; };
struct hh { long pad; short a; short b; };
struct sh { long pad; long a; short b; };
struct hv { long pad; short a; volatile long b; };
struct ss { long pad; long a; long b; };

void hi_si(struct hs *p)  { p->a = 0; p->b = 0; }
void hi_hi(struct hh *p)  { p->a = 0; p->b = 0; }
void si_hi(struct sh *p)  { p->a = 0; p->b = 0; }
void hi_vsi(struct hv *p) { p->a = 0; p->b = 0; }
void si_si(struct ss *p)  { p->a = 0; p->b = 0; }
void si_hi_80(struct sh *p) { p->a = 0; p->b = 0; }

/* stack-slot shape of the tolunnet hit (tn_boot_block_apply) */
extern void use(short *, long *);
long frame(void)
{
    short inside;
    long out_len;
    inside = 0;
    out_len = 0;
    use(&inside, &out_len);
    return out_len;
}
