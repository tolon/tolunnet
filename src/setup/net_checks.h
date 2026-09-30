/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Real Test-page checks (z.ai step 11m item 1)
 *
 * Four self-contained checks the wizard Test page runs instead of
 * reporting fake success. Each returns 1 ok / 0 failed and writes a
 * short one-line detail (what was tested, what came back). No
 * wizard/GUI includes - only dos.library and the C: commands.
 */

#ifndef TOLUNNET_NET_CHECKS_H
#define TOLUNNET_NET_CHECKS_H

#include <stddef.h>

/* Non-zero IPv4 that is not 0.0.0.0 and not 169.254.x.x, reported
 * by C:GetNetStatus ADDRESS (IPC snapshot from the running daemon). */
int tn_check_address(char *detail, size_t n);

/* C:TolunnetPing <host> COUNT=1 TIMEOUT=5; ok only on return code 0.
 * The host is validated (digits and dots, or a plain host name)
 * before it reaches a command line. */
int tn_check_ping(const char *host, char *detail, size_t n);

/* C:nslookup <name> SERVER <server> [PORT <port>]; ok only on rc 0
 * AND a printed IPv4. The resolved IPv4 text is returned via ip_out
 * (for the TCP check). The configured server is queried directly:
 * lwIP's resolver resolves on UDP port 53 only, so on a bench whose
 * DNS lives elsewhere only this path works. */
int tn_check_dns(const char *name, const char *server, unsigned port,
                 char *detail, size_t n, char *ip_out, size_t ipn);

/* C:nc <host> <port> TIMEOUT=5; ok only if the connection opens. */
int tn_check_tcp(const char *host, unsigned port, char *detail, size_t n);

/* 11x item 3 T2: what the stack itself reports for GATEWAY / DNS
 * (C:GetNetStatus <keyword>) — not the wizard's text fields. "none"
 * and 0.0.0.0 count as "not set". Returns 1 with out filled, 0 with
 * out set to "" (also when the daemon is down). */
int tn_stack_value(const char *keyword, char *out, size_t n);

#endif /* TOLUNNET_NET_CHECKS_H */
