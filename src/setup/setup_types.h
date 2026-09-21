/*
 * tolunnet — First-Run Network Wizard Types & Definitions
 *
 * Data models for stack detection, hardware scanning, wireless management,
 * and multi-page wizard state.
 */

#ifndef TOLUNNET_SETUP_TYPES_H
#define TOLUNNET_SETUP_TYPES_H

#ifdef __AMIGA__
#include <exec/types.h>
#else
#include <stdint.h>
typedef int32_t  LONG;
typedef uint32_t ULONG;
typedef int16_t  WORD;
typedef uint16_t UWORD;
typedef uint8_t  UBYTE;
typedef int      BOOL;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#endif

#define WIZARD_PAGE_REPLACE 0
#define WIZARD_PAGE_HW      1
#define WIZARD_PAGE_WIFI    2
#define WIZARD_PAGE_ADDRESS 3
#define WIZARD_PAGE_TEST    4
#define WIZARD_PAGE_COUNT   5

#define MAX_DETECTED_STACKS 8
#define MAX_DETECTED_HW     16
#define MAX_WIFI_NETWORKS   32

typedef struct DetectedStack {
    char name[32];          /* "Miami", "Roadshow", "AmiTCP", "Genesis" */
    char details[64];       /* "Active in Exec LibList", "Found in S:User-Startup" */
    BOOL is_running;        /* bsdsocket.library in LibList or port found */
    BOOL has_startup;       /* Startup command line found in S:User-Startup */
    BOOL has_disk_lib;      /* LIBS:bsdsocket.library exists on disk */
    BOOL has_wbstartup;     /* Icon found in SYS:WBStartup/ */
} DetectedStack;

typedef struct DetectedHw {
    char  device_name[32];   /* "wifipi.device", "uaenet.device" */
    char  device_path[64];   /* "DEVS:Networks/wifipi.device" */
    ULONG unit;              /* 0 */
    char  friendly_name[64]; /* "PiStorm WiFi — wifipi.device unit 0" */
    char  mac_str[20];       /* "00:80:10:20:30:40" */
    ULONG mtu;               /* 1500 */
    BOOL  is_wireless;       /* TRUE if S2_GETNETWORKS or wireless driver */
    BOOL  is_operational;    /* TRUE if OpenDevice + S2_DEVICEQUERY succeeded */
} DetectedHw;

typedef struct WifiNetwork {
    char  ssid[34];
    UBYTE bssid[6];
    LONG  signal_dbm;        /* dBm */
    LONG  noise_dbm;
    WORD  channel;
    WORD  encryption;        /* 0=Open, 1=WEP, 2=TKIP, 3=WPA2-CCMP */
    char  display_str[64];   /* "SSID [Chan X, 85%, WPA2]" */
} WifiNetwork;

typedef struct WizardState {
    int  current_page;
    BOOL replace_stacks;     /* Checkbox: Replace with tolunnet (default TRUE) */
    
    /* Page 1: Stacks */
    int           stack_count;
    DetectedStack stacks[MAX_DETECTED_STACKS];

    /* Page 2: Hardware */
    int        hw_count;
    DetectedHw hw[MAX_DETECTED_HW];
    int        selected_hw_idx;

    /* Page 3: WiFi */
    int         wifi_count;
    WifiNetwork wifi[MAX_WIFI_NETWORKS];
    int         selected_wifi_idx;
    char        wifi_pass[64];
    char        wifi_ssid_str[34];   /* TNET-110: editable SSID (hidden networks) */
    BOOL        wifi_show_pass;
    BOOL        wifi_associated;
    char        wifi_status_msg[64];

    /* Page 4: Address */
    int  ip_mode;            /* 0 = DHCP, 1 = Static */
    char ip_str[16];
    char nm_str[16];
    char gw_str[16];
    char dns1_str[16];
    char dns2_str[16];
    char mtu_str[8];        /* TNET-110: blank = driver default, 576..1500 */
    char host_str[32];
    char domain_str[64];
    BOOL write_roadshow;          /* Checkbox: write DEVS:NetInterfaces (default TRUE) */
    /* Advanced dialog options */
    LONG task_priority;           /* Priority (-128..127, default 5) */
    char log_file[64];            /* Log file path; empty = none */
    char database_order[40];      /* Lookup order: "hosts,dns" */
    BOOL write_roadshow_internet; /* Checkbox: write Roadshow DEVS:Internet/ files */
    BOOL dhcp_fallback_dns2;      /* Checkbox: Use DHCP DNS, fall back to DNS 2 */

    /* Page 5: Test & Finish */
    BOOL start_at_boot;           /* Checkbox: Start at boot (default TRUE) */
    BOOL open_prefs_after_finish; /* Checkbox: Open Prefs after finish */
    int  test_daemon_ok;          /* -1 = not run, 0 = failed, 1 = OK */
    int  test_dhcp_ok;
    int  test_ping_ok;
    int  test_dns_ok;
    int  test_http_ok;
    char test_details[5][64];
    char test_advice[5][64];
    BOOL needs_reboot;            /* TRUE if legacy stack did not exit cleanly within 10s */

    /* Navigation & Scripting control */
    BOOL rexx_done;
    BOOL rexx_cancel;
    struct Message *rexx_finish_msg;
} WizardState;

#endif /* TOLUNNET_SETUP_TYPES_H */
