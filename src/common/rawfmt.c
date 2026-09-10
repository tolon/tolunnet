/*
 * tolunnet — shared RawDoFmt PutChProc data (TNET-139).
 */
#include "rawfmt.h"

/* move.b d0,(a3)+ ; rts — word-aligned executable pair for RawDoFmt */
const unsigned short tn_rawfmt_putch[2] = {0x16C0, 0x4E75};
