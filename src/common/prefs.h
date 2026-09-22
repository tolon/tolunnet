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

/* TNET-150: deferred gethostbyname tracking slots (daemon-side table;
 * defined here so the config parser and the daemon share one cap). */
#define TN_DNS_PENDING_MAX 8

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
    char  log_file[64];       /* W3 item 6: log file path; empty = no file log */
    /* TNET-108: RECONFIG hot-reload keys */
    LONG  log_level;          /* LOGLEVEL 0..2 direct tier; -1 = derive from DEBUG */
    char  database_order[40]; /* netdb lookup order (§D1); empty = default */
    ULONG selectors;          /* selector table size; 0 = TN_MAX_SELECTORS (16), max 128 */
    BOOL  stats;              /* NO = counters frozen/reported as zero */
    char  syslog_host[48];    /* RFC3164 UDP-514 forward target (§D3); empty = off */
    ULONG s2events;           /* S2EVENTS= mask (TNET-109); 0 = ONLINE|OFFLINE|ERROR */
    ULONG dns_port;           /* DNS_PORT= resolver port (TNET-111); 0 = 53 */
    ULONG dns_pending_max;    /* TNET-150: DNS_PENDING= deferred lookup slots; 0 = 8 */
    ULONG dns_retries;
    ULONG tx_queue;           /* TNET-106 §C: TX pool slots (TX_QUEUE=);
                               * 0/absent = synchronous DoIO (safe default —
                               * the emulated a2065 interleaves spurious link
                               * events with async TX completions) */        /* TNET-150: DNS_RETRIES= (must match compiled
                               * DNS_MAX_RETRIES); 0 = 4. Client watchdogs use
                               * this to outlive the daemon's DNS attempt. */
    char  font[24];           /* FONT=name/size for TolunnetSetup; empty = screen font */
    ULONG diag;              /* DIAG=YES: trap handler + crash log + step logging (TNET-139) */
    BOOL  autoip;            /* AUTOIP=YES/NO (default TRUE): RFC 3927 link-local fallback */
    BOOL  mdns;              /* MDNS=YES/NO (default TRUE): Zeroconf mDNS responder */
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
