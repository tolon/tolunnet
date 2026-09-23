/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Roadshow-style DEVS:Internet/interfaces reader (CLOSE §B.7).
 *
 * Format (per Roadshow): one interface per block — the block starts with
 * the interface name at column 0 followed by KEY=VALUE pairs; indented
 * continuation lines belong to the same interface. ';' and '#' comments.
 * Recognised keys: DEVICE, UNIT, ADDRESS (dotted quad or CIDR a.b.c.d/n),
 * NETMASK, GATEWAY, DHCP (YES/NO). Unknown keys are ignored (tolerant).
 *
 * Parse core is host-testable (tn_if_parse_lines); the file wrapper uses
 * DOS Open/Read on the Amiga only.
 */
#ifndef TOLUNNET_IFREADER_H
#define TOLUNNET_IFREADER_H

#define TN_IF_MAX 4

typedef struct TnIfEntry {
    char name[16];
    char device[40];
    long unit;              /* -1 = unset */
    char address[24];       /* dotted quad, or CIDR a.b.c.d/n */
    char netmask[24];       /* dotted quad */
    char gateway[24];       /* dotted quad */
    int  dhcp;              /* 0/1 */
} TnIfEntry;

/* Parse a whole file image. Returns entry count (>=0) or -1 on bad args. */
int tn_if_parse_lines(const char *text, TnIfEntry *out, int max);

#endif /* TOLUNNET_IFREADER_H */
