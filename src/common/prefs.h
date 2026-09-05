/*
 * tolunnet — Persistent Preferences Engine (ENVARC: / ENV:)
 *
 * Host-tolerant (Round 3 §B.1): the text parse/format logic lives in
 * config_text.c and is unit-tested with host gcc; on Amiga builds we use the
 * real Exec types, on host builds minimal stdint typedefs with identical
 * 32-bit layout.
 */

#ifndef TOLUNNET_PREFS_H
#define TOLUNNET_PREFS_H

#if defined(__AMIGA__) || defined(__amigaos__) || defined(TN_AMIGA_BUILD)
#include <exec/types.h>
#else
#include <stdint.h>
#include <stddef.h>
typedef uint32_t ULONG;
typedef int32_t  LONG;
typedef int      BOOL;
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#endif

#define TN_CONFIG_FILE_DEVS  "DEVS:tolunnet.config"
#define TN_PREFS_FILE_ENVARC "ENVARC:tolunnet.prefs"
#define TN_PREFS_FILE_ENV    "ENV:tolunnet.prefs"

typedef struct TnPrefs {
    char  device[64];
    ULONG unit;
    BOOL  use_dhcp;
    char  ip_addr[20];
    char  netmask[20];
    char  gateway[20];
    char  dns_server[20];
    /* TNET-063: previously cosmetic keys, now first-class config */
    char  hostname[64];       /* RFC-952 charset; DHCP option 12 + gethostname */
    char  dns2[20];           /* secondary resolver; empty = unset */
    ULONG mtu;                /* 0 = driver default; 576..1500 honoured */
    ULONG debug;              /* 0..2 log tier */
    LONG  priority;           /* task priority (-128..127, default 5) (TNET-066) */
} TnPrefs;

/* TNET-064: Amiga Prefs convention.
 * TN_PREFS_USE  = ENV: only  (current session; reverted by reboot).
 * TN_PREFS_SAVE = ENV: + ENVARC: + DEVS:tolunnet.config (persistent). */
typedef enum TnPrefsSaveMode {
    TN_PREFS_USE  = 0,
    TN_PREFS_SAVE = 1
} TnPrefsSaveMode;

/* Set factory defaults */
void tn_prefs_default(TnPrefs *prefs);

/* Load preferences. Precedence (classic ENV: session semantics, TNET-064):
 * 1. ENV:tolunnet.prefs binary blob (live "Use" state; refreshed from ENVARC: at boot)
 * 2. DEVS:tolunnet.config text    (ART integration API, master prompt §5.2)
 * 3. DEVS:tolunet.config text     (pre-rename legacy install)
 * 4. ENVARC:tolunnet.prefs blob   (last "Save")
 * Returns TRUE if any store was found and parsed. */
BOOL tn_prefs_load(TnPrefs *prefs);

/* Save preferences per Amiga Prefs convention (TNET-064). */
BOOL tn_prefs_save(const TnPrefs *prefs, TnPrefsSaveMode mode);

#endif /* TOLUNNET_PREFS_H */
