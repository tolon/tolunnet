/*
 * tolunnet — Persistent Preferences Engine (ENVARC: / ENV:)
 */

#ifndef TOLUNNET_PREFS_H
#define TOLUNNET_PREFS_H

#include <exec/types.h>

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
} TnPrefs;

/* Set factory defaults */
void tn_prefs_default(TnPrefs *prefs);

/* Load preferences from ENV: or ENVARC:; returns TRUE if loaded successfully */
BOOL tn_prefs_load(TnPrefs *prefs);

/* Save preferences to both ENV: and ENVARC:; returns TRUE on success */
BOOL tn_prefs_save(const TnPrefs *prefs);

#endif /* TOLUNNET_PREFS_H */
