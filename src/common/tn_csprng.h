/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — keyed, non-invertible RNG for security-relevant values.
 *
 * Audit run-1 (confirmed: tolunnet/tn_rand-xorshift32-shared-stream-weak-prng
 * and tolunnet/lwip-tcp-isn-not-randomized-default-counter).
 *
 * The old tn_rand() was a single xorshift32 whose output equalled its
 * whole state, so one observed DNS (port,txid) pair revealed every later
 * value. This module replaces it with a counter-based keyed stream built
 * on SipHash-2-4: output = SipHash(key, counter). Observing outputs does
 * not reveal the 128-bit key or future outputs. A separate key drives the
 * RFC 6528 TCP ISN.
 *
 * Byte-wise only (no unaligned loads) so it is safe on the 68000 and
 * passes -Werror=cast-align. Pure C: host-testable.
 */
#ifndef TOLUNNET_CSPRNG_H
#define TOLUNNET_CSPRNG_H

#include <stdint.h>
#include <stddef.h>

typedef struct TnCsprng {
    uint8_t  key[16];
    uint64_t counter;
} TnCsprng;

/* SipHash-2-4 of `in` (inlen bytes) under the 16-byte key. */
uint64_t tn_siphash24(const uint8_t key[16], const void *in, size_t inlen);

/* Seed a keyed counter generator. */
void tn_csprng_init(TnCsprng *g, const uint8_t key[16]);

/* Next 32-bit output; non-invertible, counter advances. */
uint32_t tn_csprng_next(TnCsprng *g);

/* RFC 6528 ISN: SipHash(key, laddr|lport|raddr|rport) + (clock_ms >> 6).
 * Addresses are host-order u32; the function serialises them canonically. */
uint32_t tn_tcp_isn_compute(const uint8_t key[16],
                            uint32_t laddr, uint16_t lport,
                            uint32_t raddr, uint16_t rport,
                            uint32_t clock_ms);

#endif /* TOLUNNET_CSPRNG_H */
