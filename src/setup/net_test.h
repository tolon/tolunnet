/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Network Testing & Finalization Engine
 *
 * Starts daemon, pings gateway, tests DNS and HTTP,
 * writes configuration and User-Startup boot blocks.
 */

#ifndef TOLUNNET_NET_TEST_H
#define TOLUNNET_NET_TEST_H

#include "setup_types.h"

/* Run all test stages sequentially */
void tn_run_network_tests(WizardState *ws);

/* Write DEVS:tolunnet.config (and ENV:/ENVARC: twins) */
BOOL tn_write_tolunnet_config(const WizardState *ws);

/* Write Roadshow-style DEVS:NetInterfaces/<ifname> */
BOOL tn_write_roadshow_interface(const WizardState *ws);

/* Write Roadshow-style DEVS:Internet/name_resolution and routes */
BOOL tn_write_roadshow_internet(const WizardState *ws);

/* Add or remove the marked tolunnet boot block in S:User-Startup */
BOOL tn_install_boot_block(BOOL enable);

/* Pure string formatting helper for Roadshow interfaces (for host unit test) */
int tn_format_roadshow_interface(const char *dev, ULONG unit, BOOL is_dhcp,
                                 const char *ip, const char *mask,
                                 char *out_buf, int out_max);

#endif /* TOLUNNET_NET_TEST_H */
