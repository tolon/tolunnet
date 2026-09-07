/*
 * tolunnet — SANA-II Wireless Manager & Association Controller
 *
 * Scans for access points using S2_GETNETWORKS, writes ENVARC:Sys/Wireless.prefs,
 * launches WirelessManager, and verifies association.
 */

#ifndef TOLUNNET_WIFI_MGR_H
#define TOLUNNET_WIFI_MGR_H

#include "setup_types.h"

/* Scan available wireless networks on the selected hardware device */
void tn_wifi_scan(WizardState *ws);

/* Write ENVARC:Sys/Wireless.prefs and ENV:Sys/Wireless.prefs */
BOOL tn_wifi_write_prefs(const char *ssid, const char *passphrase);

/* Launch or restart WirelessManager for the given device */
BOOL tn_wifi_start_manager(const char *device_name, ULONG unit);

/* Await wireless association with timeout */
BOOL tn_wifi_wait_association(const char *device_name, ULONG unit, int timeout_secs,
                              char *err_msg, int err_max);

/* Pure string formatting helper (for host unit tests) */
int tn_format_wireless_block(const char *ssid, const char *passphrase, char *out_buf, int out_max);

#endif /* TOLUNNET_WIFI_MGR_H */
