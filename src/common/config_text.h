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
#define TN_CONFIG_TEXT_MAX 768
int tn_config_format(const TnPrefs *prefs, char *buf, int buf_size);

/* Case-insensitive equality + clean-copy helpers (shared with prefs.c). */
int  tn_str_equal_nocase(const char *s1, const char *s2);
void tn_str_copy_clean(char *dst, const char *src, int max_len);

/* TNET-109: S2EVENT_* bit values mirrored here because devices/sana2.h is
 * Amiga-only while this parser is host-built. sana2_netif.c statically
 * asserts these match the real S2EVENT_* constants. */
#define TN_S2EV_ERROR    (1UL << 0)
#define TN_S2EV_TX       (1UL << 1)
#define TN_S2EV_RX       (1UL << 2)
#define TN_S2EV_ONLINE   (1UL << 3)
#define TN_S2EV_OFFLINE  (1UL << 4)
#define TN_S2EV_BUFF     (1UL << 5)
#define TN_S2EV_HARDWARE (1UL << 6)
#define TN_S2EV_SOFTWARE (1UL << 7)
#define TN_S2EV_DEFAULT  (TN_S2EV_ONLINE | TN_S2EV_OFFLINE | TN_S2EV_ERROR)

/* TNET-108: RECONFIG classification (pure, host-testable). */
#include <stdint.h>

/* Effective log tier: LOGLEVEL= when set, else the DEBUG-derived tier. */
int tn_recfg_effective_loglevel(const TnPrefs *p);

/* Classify changed keys between two snapshots into the restart-only and
 * hot-reloadable TN_RECFG_* masks (bits defined in include/ipc.h). */
void tn_recfg_diff(const TnPrefs *oldp, const TnPrefs *newp,
                   uint32_t *needs_restart, uint32_t *live);

/* Printable name of one TN_RECFG_* bit, or NULL. */
const char *tn_recfg_key_name(uint32_t bit);

/* CLOSE §B.8: CheckNetConfig vocabulary. */
int tn_config_key_known(const char *key);
int tn_config_value_class(const char *key);

#endif /* TOLUNNET_CONFIG_TEXT_H */
