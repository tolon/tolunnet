/*
 * tolunnet — shared RawDoFmt PutChProc (TNET-139).
 *
 * The classic idiom passes the inline string "\x16\xc0\x4e\x75" (move.b
 * d0,(a3)+ ; rts) cast to a function pointer. A char* -> function-pointer
 * cast is exactly what -Wcast-align flags, and the literal buys nothing:
 * use one named, word-aligned pair instead so the CI alignment gate stays
 * zero-warning without weakening the check.
 */
#ifndef TOLUNNET_RAWFMT_H
#define TOLUNNET_RAWFMT_H

/* move.b d0,(a3)+ ; rts */
extern const unsigned short tn_rawfmt_putch[2];

#ifdef __AMIGA__
#define TN_RAWFMT_PUTCH ((VOID (*)())tn_rawfmt_putch)
#endif

#endif /* TOLUNNET_RAWFMT_H */
