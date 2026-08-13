#!/bin/bash
# Write a minimal C99 stdint.h into the amiga-gcc toolchain (GCC 6.5 freestanding
# ships stdbool.h but not stdint.h; NDK exec/types.h #includes <stdint.h>).
# 68k AmigaOS ABI: 32-bit int/long/ptr, 8-bit char, big-endian.
set -e
GCCINC="$HOME/opt/m68k-amigaos/lib/gcc/m68k-amigaos/6.5.0b/include"
cat > "$GCCINC/stdint.h" <<'STDINT_EOF'
/* stdint.h - minimal C99 stdint for m68k-amigaos (GCC 6.5 freestanding). */
#ifndef _STDINT_H
#define _STDINT_H

typedef signed char         int8_t;
typedef unsigned char       uint8_t;
typedef short               int16_t;
typedef unsigned short      uint16_t;
typedef int                 int32_t;
typedef unsigned int        uint32_t;
typedef long long           int64_t;
typedef unsigned long long  uint64_t;
typedef int                 intptr_t;
typedef unsigned int        uintptr_t;

typedef signed char         int_least8_t;
typedef unsigned char       uint_least8_t;
typedef short               int_least16_t;
typedef unsigned short      uint_least16_t;
typedef int                 int_least32_t;
typedef unsigned int        uint_least32_t;
typedef long long           int_least64_t;
typedef unsigned long long  uint_least64_t;

typedef int                 int_fast8_t;
typedef unsigned int        uint_fast8_t;
typedef int                 int_fast16_t;
typedef unsigned int        uint_fast16_t;
typedef int                 int_fast32_t;
typedef unsigned int        uint_fast32_t;
typedef long long           int_fast64_t;
typedef unsigned long long  uint_fast64_t;

typedef long long           intmax_t;
typedef unsigned long long  uintmax_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32767-1)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647-1)
#define INT32_MAX  2147483647
#define UINT32_MAX 4294967295U
#define INT64_MIN  (-9223372036854775807LL-1)
#define INT64_MAX  9223372036854775807LL
#define UINT64_MAX 18446744073709551615ULL

#define INTPTR_MIN INT32_MIN
#define INTPTR_MAX INT32_MAX
#define UINTPTR_MAX UINT32_MAX
#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX

#define INT8_C(c)  c
#define UINT8_C(c) c
#define INT16_C(c) c
#define UINT16_C(c) c
#define INT32_C(c) c
#define UINT32_C(c) c##U
#define INT64_C(c) c##LL
#define UINT64_C(c) c##ULL

#endif /* _STDINT_H */
STDINT_EOF
echo "stdint.h written: $(wc -l < "$GCCINC/stdint.h") lines -> $GCCINC/stdint.h"
