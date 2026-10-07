/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — keyed, non-invertible RNG (audit run-1). See tn_csprng.h.
 *
 * SipHash-2-4 by Jean-Philippe Aumasson and Daniel J. Bernstein
 * (public domain reference). Reimplemented here with byte-wise loads so
 * it is safe on the unaligned-hostile 68000.
 */
#include "tn_csprng.h"

#define ROTL64(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

static uint64_t load_le64(const uint8_t *p, size_t n)
{
    uint64_t r = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        r |= (uint64_t)p[i] << (8 * i);
    }
    return r;
}

#define SIPROUND                        \
    do {                                \
        v0 += v1; v1 = ROTL64(v1, 13); v1 ^= v0; v0 = ROTL64(v0, 32); \
        v2 += v3; v3 = ROTL64(v3, 16); v3 ^= v2;                      \
        v0 += v3; v3 = ROTL64(v3, 21); v3 ^= v0;                      \
        v2 += v1; v1 = ROTL64(v1, 17); v1 ^= v2; v2 = ROTL64(v2, 32); \
    } while (0)

uint64_t tn_siphash24(const uint8_t key[16], const void *in, size_t inlen)
{
    const uint8_t *data = (const uint8_t *)in;
    uint64_t k0 = load_le64(key, 8);
    uint64_t k1 = load_le64(key + 8, 8);
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;
    uint64_t b;
    uint64_t m;
    size_t left = inlen & 7;
    const uint8_t *end = data + (inlen - left);

    for (; data != end; data += 8) {
        m = load_le64(data, 8);
        v3 ^= m;
        SIPROUND;
        SIPROUND;
        v0 ^= m;
    }

    b = ((uint64_t)inlen) << 56;
    b |= load_le64(data, left);
    v3 ^= b;
    SIPROUND;
    SIPROUND;
    v0 ^= b;
    v2 ^= 0xff;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

void tn_csprng_init(TnCsprng *g, const uint8_t key[16])
{
    size_t i;
    for (i = 0; i < 16; i++) {
        g->key[i] = key[i];
    }
    g->counter = 0;
}

uint32_t tn_csprng_next(TnCsprng *g)
{
    uint8_t ctr[8];
    uint64_t c = g->counter++;
    size_t i;
    for (i = 0; i < 8; i++) {
        ctr[i] = (uint8_t)(c >> (8 * i));
    }
    /* low 32 bits of the keyed hash of the counter */
    return (uint32_t)tn_siphash24(g->key, ctr, sizeof(ctr));
}

uint32_t tn_tcp_isn_compute(const uint8_t key[16],
                            uint32_t laddr, uint16_t lport,
                            uint32_t raddr, uint16_t rport,
                            uint32_t clock_ms)
{
    uint8_t tuple[12];
    tuple[0]  = (uint8_t)(laddr >> 24); tuple[1]  = (uint8_t)(laddr >> 16);
    tuple[2]  = (uint8_t)(laddr >> 8);  tuple[3]  = (uint8_t)(laddr);
    tuple[4]  = (uint8_t)(raddr >> 24); tuple[5]  = (uint8_t)(raddr >> 16);
    tuple[6]  = (uint8_t)(raddr >> 8);  tuple[7]  = (uint8_t)(raddr);
    tuple[8]  = (uint8_t)(lport >> 8);  tuple[9]  = (uint8_t)(lport);
    tuple[10] = (uint8_t)(rport >> 8);  tuple[11] = (uint8_t)(rport);
    /* RFC 6528 §3: keyed hash of the 4-tuple plus a fast monotonic clock.
     * The RFC uses a ~4 us tick; clock_ms << 8 is ~3.9 us units (u32 wrap is
     * fine) so two connections on the same 4-tuple within one millisecond
     * still get distinct ISNs. */
    return (uint32_t)tn_siphash24(key, tuple, sizeof(tuple)) + (clock_ms << 8);
}
