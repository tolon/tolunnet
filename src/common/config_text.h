/*
 * tolunnet — pure KEY=VALUE config parse/format (host-testable).
 *
 * Round 3 §B.1: the text grammar (DEVS:tolunnet.config, master prompt §5.2)
 * split out of prefs.c so it can be unit-tested with host gcc. AmigaDOS file
 * I/O stays in prefs.c; only string handling lives here.
 */
#ifndef TOLUNNET_CONFIG_TEXT_H
#define TOLUNNET_CONFIG_TEXT_H

#include "prefs.h"

/*
 * Apply one "KEY=VALUE" pair (key/value already NUL-terminated strings as
 * extracted by the caller; leading whitespace of val already skipped).
 * Accepts both key dialects (NETMASK|MASK, GATEWAY|GW, DNS|DNS1|NAMESERVER,
 * IP|IP_ADDR, DHCP|USE_DHCP). Unknown keys are ignored (warned by caller).
 */
void tn_config_parse_line(TnPrefs *prefs, const char *key, const char *val);

/*
 * Format the whole TnPrefs as the canonical DEVS:tolunnet.config text into
 * buf (NUL-terminated). Returns the number of characters written (excluding
 * the terminator); if the buffer is too small, returns the number of
 * characters that WOULD be written and leaves buf contents undefined —
 * callers must retry with a larger buffer (TN_CONFIG_TEXT_MAX is always
 * sufficient).
 */
#define TN_CONFIG_TEXT_MAX 640
int tn_config_format(const TnPrefs *prefs, char *buf, int buf_size);

/* Case-insensitive equality + clean-copy helpers (shared with prefs.c). */
int  tn_str_equal_nocase(const char *s1, const char *s2);
void tn_str_copy_clean(char *dst, const char *src, int max_len);

#endif /* TOLUNNET_CONFIG_TEXT_H */
