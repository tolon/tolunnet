/*
 * tolunnet — lwIP compiler and architecture definitions for 68k AmigaOS.
 *
 * Included by lwIP headers via #include "arch/cc.h".
 * Target: Motorola 68020+ (Big-Endian), GCC (amiga-gcc / -m68020 -noixemul).
 */

#ifndef TOLUNNET_ARCH_CC_H
#define TOLUNNET_ARCH_CC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef BYTE_ORDER
#define BYTE_ORDER BIG_ENDIAN
#endif

/* Basic integer types used by lwIP */
typedef uint8_t    u8_t;
typedef int8_t     s8_t;
typedef uint16_t   u16_t;
typedef int16_t    s16_t;
typedef uint32_t   u32_t;
typedef int32_t    s32_t;
typedef uintptr_t  mem_ptr_t;

/* Printf format specifiers for lwIP types */
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

/* Structure packing macros for GCC on 68k */
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_FLD_8(x) PACK_STRUCT_FIELD(x)
#define PACK_STRUCT_FLD_S(x) PACK_STRUCT_FIELD(x)

/* Memory alignment macro for GCC on 68k */
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) u8_t variable_name[size] __attribute__((aligned(4)))

/* Random number generator for DNS/TCP */
uint32_t tn_rand(void);
#define LWIP_RAND() tn_rand()

/* Diagnostics and assertions (AUDIT-2 TNET-018) */
void tn_logf(int tier, const char *fmt, ...);

#ifdef TOLUNNET_DEBUG
#define LWIP_PLATFORM_DIAG(x)   do { tn_logf(2, "%s", x); } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { tn_logf(1, "tolunnet: ASSERTION FAILED: %s (%s:%d)\n", x, __FILE__, __LINE__); } while(0)
#else
#define LWIP_PLATFORM_DIAG(x)   do { } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { } while(0)
#endif

#endif /* TOLUNNET_ARCH_CC_H */
