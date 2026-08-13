/*
 * tolunet — config file grammar constants.
 *
 * Binding source: TOLUNET-master-prompt.md §5.2 (CONFIG FILE). The parser
 * (src/task/, M1+) reads DEVS:tolunet.config with this exact key set. Keys are
 * case-insensitive. Unknown keys: warn once, preserve on rewrite, never crash.
 * Missing file: polite error naming the file.
 *
 * WiFi credentials are NOT here — the driver owns them (M8-WiFi).
 */
#ifndef TOLUNET_CONFIG_H
#define TOLUNET_CONFIG_H

/* Default config path (assign-resolved at parse time). */
#define TN_CONFIG_PATH  "DEVS:tolunet.config"

/* Key names, exactly as they appear in §5.2. Comparison is case-insensitive. */
#define TN_KEY_DEVICE    "DEVICE"
#define TN_KEY_UNIT      "UNIT"
#define TN_KEY_DHCP      "DHCP"
#define TN_KEY_IP        "IP"
#define TN_KEY_MASK      "MASK"
#define TN_KEY_GATEWAY   "GATEWAY"
#define TN_KEY_DNS1      "DNS1"
#define TN_KEY_DNS2      "DNS2"
#define TN_KEY_HOSTNAME  "HOSTNAME"
#define TN_KEY_MTU       "MTU"
#define TN_KEY_DEBUG     "DEBUG"

/* Default MTU when the key is absent: defer to the SANA-II driver's query
 * (Sana2DeviceQuery.MTU). Non-zero here only overrides an explicit key. */
#define TN_MTU_DEFAULT   0

/* Debug tiers per §9 / §5.2 (ENV:TOLUNET_DEBUG mirrors this). */
#define TN_DEBUG_OFF     0
#define TN_DEBUG_TIER1   1
#define TN_DEBUG_TIER2   2

#endif /* TOLUNET_CONFIG_H */
