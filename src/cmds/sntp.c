/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — sntp command (CMD-1). ReadArgs: HOST,SET/S,OFFSET/N
 * Pure thin-client: UDP socket to port 123, NTPv3 packet, no daemon changes.
 */
#include "cmdlib.h"
#include <string.h>

#define TEMPLATE "HOST,SET/S,OFFSET/N"
#define NTP_PORT 123
#define NTP_PKT_LEN 48

/* NTP timestamp: seconds since 1900 + fraction */
struct ntp_ts {
    ULONG sec;
    ULONG frac;
};

/* SNTP client packet (NTPv3) */
struct ntp_pkt {
    UBYTE li_vn_mode;    /* LI(2) VN(3) Mode(3) = 0x1B */
    UBYTE stratum;
    UBYTE poll;
    UBYTE precision;
    ULONG root_delay;
    ULONG root_dispersion;
    UBYTE ref_id[4];
    struct ntp_ts ref_ts;
    struct ntp_ts orig_ts;
    struct ntp_ts recv_ts;
    struct ntp_ts xmit_ts;
};

/* Unix epoch (1970) to NTP epoch (1900) offset */
#define NTP_UNIX_OFFSET 2208988800UL

/* Day count from Unix epoch to AmigaDOS epoch (1978-01-01) */
#define UNIX_TO_AMIGA_OFFSET 2922UL  /* days */
#define AMIGA_EPOCH_UNIX 28173600UL /* 1978-01-01 00:00:00 UTC */

int main(int argc, char **argv)
{
    LONG opts[3] = { 0, 0, 0 };
    struct RDArgs *rdargs;
    int rc = TN_CMD_OK;
    LONG fd;
    struct sockaddr_in dst;
    struct ntp_pkt pkt;
    struct ntp_pkt reply;
    LONG got;
    ULONG server_addr;
    const char *default_server = "pool.ntp.org";
    fd_set rfds;
    struct timeval tv;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"sntp");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }

    const char *host = (opts[0] != 0) ? (const char *)opts[0] : default_server;
    LONG do_set = opts[1];
    LONG offset_min = opts[2];

    server_addr = tn_cmd_resolve(host);
    if (server_addr == INADDR_NONE) {
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    fd = tn_call_socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { FreeArgs(rdargs); tn_cmd_fini(); return TN_CMD_FAIL; }

    memset(&dst, 0, sizeof(dst));
    dst.sin_len = sizeof(dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons(NTP_PORT);
    dst.sin_addr.s_addr = server_addr;

    /* Build NTP request */
    memset(&pkt, 0, sizeof(pkt));
    pkt.li_vn_mode = 0x1B; /* LI=0, VN=3, Mode=3 (client) */

    tn_cmd_printf("sntp: querying %s...\n", host);

    if (tn_call_send(fd, &pkt, NTP_PKT_LEN, 0) != NTP_PKT_LEN) {
        tn_cmd_printf("sntp: send failed (errno=%ld)\n", tn_call_errno());
        tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    /* Wait up to 5 seconds */
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_secs = 5;
    tv.tv_micro = 0;
    if (tn_call_waitselect(fd + 1, &rfds, NULL, NULL, &tv, NULL) <= 0) {
        tn_cmd_printf("sntp: no response within 5s\n");
        tn_call_closesocket(fd); FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_WARN;
    }

    got = tn_call_recv(fd, &reply, sizeof(reply), 0);
    tn_call_closesocket(fd);

    if (got < NTP_PKT_LEN) {
        tn_cmd_printf("sntp: short response (%ld bytes)\n", got);
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_FAIL;
    }

    if ((reply.li_vn_mode >> 6) == 3) {
        tn_cmd_printf("sntp: server clock not synchronized\n");
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_WARN;
    }

    /* Extract transmit timestamp */
    {
        ULONG ntp_secs = reply.xmit_ts.sec;
        ULONG unix_secs = ntp_secs - NTP_UNIX_OFFSET;

        /* Apply offset if specified (minutes) */
        if (offset_min != 0) {
            unix_secs += (ULONG)(offset_min * 60);
        }

        /* Convert to readable date (simplified: just show unix timestamp + offset) */
        tn_cmd_printf("Server time: %lu (NTP) = %lu (Unix)\n", ntp_secs, unix_secs);

        /* Show as date: seconds to Y/M/D H:M:S (simplified Gregorian) */
        {
            ULONG days = unix_secs / 86400;
            ULONG secs_today = unix_secs % 86400;
            ULONG hh = secs_today / 3600;
            ULONG mm = (secs_today % 3600) / 60;
            ULONG ss = secs_today % 60;
            ULONG yy, mo, dd;
            ULONG era, doe, yoe, y, doy, mp;

            /* Civil-from-days algorithm (Howard Hinnant) */
            LONG z = (LONG)days + 719468;
            era = (z >= 0 ? z : z - 146096) / 146097;
            doe = (ULONG)(z - (LONG)era * 146097);
            yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
            y = yoe + (ULONG)era * 400;
            doy = doe - (365*yoe + yoe/4 - yoe/100);
            mp = (5*doy + 2)/153;
            dd = doy - (153*mp+2)/5 + 1;
            mo = (mp < 10 ? mp+3 : mp-9);
            yy = y + (mo <= 2 ? 1 : 0);

            tn_cmd_printf("Date: %02lu-%02lu-%04lu %02lu:%02lu:%02lu UTC\n",
                          dd, mo, yy, hh, mm, ss);
        }

        if (do_set) {
            tn_cmd_printf("(SET not yet implemented — use Preferences/Time instead)\n");
            rc = TN_CMD_WARN;
        }
    }

    FreeArgs(rdargs);
    tn_cmd_fini();
    return rc;
}
