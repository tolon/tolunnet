/*
 * tolunnet — SANA-II Wireless Manager & Association Controller
 *
 * Scans for access points using S2_GETNETWORKS, writes ENVARC:Sys/Wireless.prefs,
 * launches WirelessManager, and verifies association.
 */

#ifndef TOLUNNET_WIFI_MGR_H
#define TOLUNNET_WIFI_MGR_H

#include "setup_types.h"

#ifndef TAG_DONE
#define TAG_DONE   (0UL)
#define TAG_END    (0UL)
#endif

#define S2INFO_SSID           (0x80000000 + 0)
#define S2INFO_BSSID          (0x80000000 + 1)
#define S2INFO_Encryption     (0x80000000 + 4)
#define S2INFO_Channel        (0x80000000 + 7)
#define S2INFO_Signal         (0x80000000 + 8)
#define S2INFO_Noise          (0x80000000 + 9)
#define S2INFO_Capabilities   (0x80000000 + 10)

#ifdef __AMIGA__
#include <utility/tagitem.h>
#else
#include <stdint.h>
struct TagItem {
    ULONG ti_Tag;
    uintptr_t ti_Data;
};
#endif

/* Scan available wireless networks on the selected hardware device */
void tn_wifi_scan(WizardState *ws);

/* Write ENVARC:Sys/Wireless.prefs and ENV:Sys/Wireless.prefs */
BOOL tn_wifi_write_prefs(const char *ssid, const char *passphrase);

/* Launch or restart WirelessManager for the given device */
BOOL tn_wifi_start_manager(const char *device_name, ULONG unit);

/* Await wireless association with timeout */
BOOL tn_wifi_wait_association(const char *device_name, ULONG unit, int timeout_secs,
                              char *err_msg, int err_max);

/* Validate device name against [A-Za-z0-9._-] and max 31 chars */
BOOL tn_wifi_validate_devname(const char *devname);

/* Pure tag-item parser helper for SANA-II WiFi scan */
BOOL tn_parse_wifi_tagitem(const void *tags, WifiNetwork *out_net);

/* Pure string formatting helper (for host unit tests) */
int tn_format_wireless_block(const char *ssid, const char *passphrase, char *out_buf, int out_max);

#endif /* TOLUNNET_WIFI_MGR_H */
