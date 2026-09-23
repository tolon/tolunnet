/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — portable network and OS error string tables.
 *
 * Provides human-readable error descriptions for SocketBaseTagList()
 * string pointer tags:
 *   - SBTC_ERRNOSTRPTR   -> tn_strerror()
 *   - SBTC_HERRNOSTRPTR  -> tn_hstrerror()
 *   - SBTC_IOERRNOSTRPTR -> tn_ioerror()
 *   - SBTC_S2ERRNOSTRPTR -> tn_s2error()
 *   - SBTC_S2WERRNOSTRPTR-> tn_s2werror()
 */
#ifndef TOLUNNET_ERRSTR_H
#define TOLUNNET_ERRSTR_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns description for BSD errno (1..81).
 * Returns static fallback string if unrecognised.
 */
const char *tn_strerror(int err);

/*
 * Returns description for resolver h_errno (HOST_NOT_FOUND, etc.).
 * Returns static fallback string if unrecognised.
 */
const char *tn_hstrerror(int herr);

/*
 * Returns description for AmigaOS Exec IOErrors (IOERR_OPENFAIL..IOERR_SELFTEST).
 * Accepts either negative (-1..-7) or positive (1..7) error codes.
 */
const char *tn_ioerror(int ioerr);

/*
 * Returns description for SANA-II device error codes (S2ERR_*).
 */
const char *tn_s2error(int s2err);

/*
 * Returns description for SANA-II wire error codes (S2WERR_*).
 */
const char *tn_s2werror(int s2werr);

#ifdef __cplusplus
}
#endif

#endif /* TOLUNNET_ERRSTR_H */
