/*
 * tolunnet — SANA-II Hardware Scanner & Device Prober
 *
 * Scans DEVS:Networks devices, wifipi.device, prism2.device.
 * Queries MAC, MTU, device name, and probes wireless capabilities.
 */

#ifndef TOLUNNET_HW_DETECT_H
#define TOLUNNET_HW_DETECT_H

#include "setup_types.h"

/* Scan all available network devices and populate ws->hw array */
void tn_hw_scan_all(WizardState *ws);

#endif /* TOLUNNET_HW_DETECT_H */
