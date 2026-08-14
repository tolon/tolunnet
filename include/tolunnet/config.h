/*
 * tolunnet — config file grammar constants.
 *
 * Primary config path is DEVS:tolunnet.config (with fallback to DEVS:tolunet.config).
 * Comparison is case-insensitive.
 */
#ifndef TOLUNNET_CONFIG_H
#define TOLUNNET_CONFIG_H

/* Default config path */
#define TN_CONFIG_PATH  "DEVS:tolunnet.config"

/* Key names. Comparison is case-insensitive. */
#define TN_KEY_DEVICE    "DEVICE"
#define TN_KEY_UNIT      "UNIT"
#define TN_KEY_DHCP      "DHCP"
#define TN_KEY_IP        "IP"
#define TN_KEY_MASK      "MASK"
#define TN_KEY_NETMASK   "NETMASK"
#define TN_KEY_GATEWAY   "GATEWAY"
#define TN_KEY_GW        "GW"
#define TN_KEY_DNS1      "DNS1"
#define TN_KEY_DNS2      "DNS2"
#define TN_KEY_DNS       "DNS"
#define TN_KEY_HOSTNAME  "HOSTNAME"
#define TN_KEY_MTU       "MTU"
#define TN_KEY_DEBUG     "DEBUG"

/* Default MTU when the key is absent */
#define TN_MTU_DEFAULT   0

/* Debug tiers */
#define TN_DEBUG_OFF     0
#define TN_DEBUG_TIER1   1
#define TN_DEBUG_TIER2   2

#endif /* TOLUNNET_CONFIG_H */
