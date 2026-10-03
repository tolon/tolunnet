/* m68k-amigaos-gcc -O2 -S repro.c
 * expected: clr.w 4(a0) ; clr.l 6(a0)   (6 bytes)
 * actual:   clr.l 4(a0)                 (4 bytes - s->b's low word kept) */
struct s { long pad; short a; long b; };   /* a at 4, b at 6 (m68k: 2-byte long alignment) */

void clear(struct s *p)
{
    p->a = 0;
    p->b = 0;
}
