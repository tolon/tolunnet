/*
 * tolunnet — pure dotted-quad / classful address parser (host-testable).
 *
 * Extracted from the bsdsocket inet_addr() LVO (TNET-019/TNET-051) so the
 * parser can be unit-tested with host gcc (Round 3 §B.1). No AmigaOS or NDK
 * includes here.
 */
#ifndef TOLUNNET_INET_PARSE_H
#define TOLUNNET_INET_PARSE_H

#include <stdint.h>
#include <stddef.h>

#define TN_INADDR_NONE 0xFFFFFFFFu

/*
 * Parse a v4 address in BSD inet_addr() grammar: 1–4 parts, decimal, octal
 * (leading 0) or hex (0x) parts, classful short forms. Address is returned
 * in NETWORK byte order (big-endian on 68k, i.e. host order there).
 * Returns 1 on success (including "255.255.255.255" — use the _ex form to
 * distinguish it from the error sentinel), 0 on failure.
 */
int tn_inet_addr_parse_ex(const char *cp, uint32_t *out_addr);

/*
 * BSD-compatible wrapper: returns the address or TN_INADDR_NONE on error.
 * Note the classic wart inherited from BSD: "255.255.255.255" also equals
 * TN_INADDR_NONE — callers that must disambiguate use the _ex form
 * (this is why inet_aton() exists).
 */
uint32_t tn_inet_addr_parse(const char *cp);

#endif /* TOLUNNET_INET_PARSE_H */
