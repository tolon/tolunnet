/*
 * TolunetStatus — M1 raw-frame logger (UNPROVEN).
 *
 * Master prompt §M1 exit test: "broadcast sent, incoming frames logged with
 * types." This tool is the M1 artefact: it opens the configured SANA-II
 * device, brings it online, sends one broadcast, then polls >=4 armed CMD_READ
 * slots and logs each incoming frame's length + EtherType to
 * WORK:tolunet-sana2.log for a few seconds, then shuts down cleanly.
 *
 * UNPROVEN: built on top of src/sana2/sana2_netif.c which itself is unproven.
 * Device/unit come from argv in M1 (config-file parsing lands with the task in
 * M2). Do not assume this runs until the M1 exit test is pasted in STATUS.md.
 *
 * Usage:  TolunetStatus <device> <unit>  [e.g. TolunetStatus a2065.device 0]
 */

#include "../sana2/sana2_netif.h"
#include "../common/log.h"

#include <proto/exec.h>
#include <proto/dos.h>

#include <exec/types.h>

/* A short receive window (seconds-ish) implemented by polling, since the M1
 * tool has no task loop yet. The real network task does this with Wait(). */
#define TN_STATUS_SECONDS  8

/* Forward declarations. */
static void log_line(struct Library *dos, BPTR fh, const char *s);
static ULONG ether_type(const UBYTE *frame, ULONG len);
static void format_line(char *out, ULONG outcap,
                        const char *pfx, ULONG declen,
                        const char *mid, ULONG hexval);

static void log_line(struct Library *dos, BPTR fh, const char *s)
{
    LONG len = 0;
    while (s[len]) len++;
    Write(fh, (APTR)s, len);
    if (dos != NULL) Write(Output(), (APTR)s, len);  /* echo */
}

/* Pretty-print a frame's EtherType (big-endian on the wire: bytes 12,13). */
static ULONG ether_type(const UBYTE *frame, ULONG len)
{
    if (len < 14) return 0;
    return ((ULONG)frame[12] << 8) | (ULONG)frame[13];
}

int main(int argc, char *argv[])
{
    struct ExecBase *SysBase;
    struct Library  *DOSBase;
    TnSana2If nif;
    CONST_STRPTR device = "a2065.device";  /* default; argv overrides */
    ULONG unit = 0;
    BPTR fh = (BPTR)0;
    UBYTE rbuf[1600];   /* a full ethernet frame + headroom */
    int rc = 20;        /* RETURN_FAIL default */
    TnS2Result r;

    SysBase = *(struct ExecBase **)4UL;
    DOSBase = OpenLibrary((CONST_STRPTR)"dos.library", 0);
    if (DOSBase == NULL) return 20;

    /* Wire the logger so tn_log() inside sana2_netif.c actually writes. */
    g_log_dos = DOSBase;
    g_log_level = TN_LOG_VERBOSE;

    if (argc >= 2) device = (CONST_STRPTR)argv[1];
    if (argc >= 3) {
        /* DOS StrToLong(CONST_STRPTR string, LONG *value) returns chars consumed;
         * the parsed value is stored in *value. */
        LONG parsed = 0;
        LONG consumed = StrToLong((CONST_STRPTR)argv[2], &parsed);
        if (consumed > 0) unit = (ULONG)parsed;
    }

    log_line(DOSBase, (BPTR)0, "tolunet M1: TolunetStatus starting\n");

    fh = Open((CONST_STRPTR)"WORK:tolunet-sana2.log", MODE_NEWFILE);
    if (fh == (BPTR)0) {
        log_line(DOSBase, (BPTR)0,
                 "tolunet M1: could not open WORK:tolunet-sana2.log\n");
        rc = 5;  /* RETURN_WARN */
        goto out;
    }

    r = tn_s2_open(&nif, device, unit);
    if (r != TN_S2_OK) {
        log_line(DOSBase, fh, "tolunet M1: tn_s2_open failed\n");
        rc = 5;
        goto close_log;
    }
    log_line(DOSBase, fh, "tolunet M1: device opened\n");

    r = tn_s2_online(&nif, NULL);
    if (r != TN_S2_OK) {
        log_line(DOSBase, fh, "tolunet M1: tn_s2_online failed\n");
        tn_s2_offline_close(&nif);
        rc = 5;
        goto close_log;
    }
    log_line(DOSBase, fh, "tolunet M1: online\n");

    /* Arm the read pump (>=4 outstanding). */
    r = tn_s2_arm_reads(&nif);
    if (r != TN_S2_OK) {
        log_line(DOSBase, fh, "tolunet M1: arm_reads failed\n");
        tn_s2_offline_close(&nif);
        rc = 5;
        goto close_log;
    }

    /* Send one broadcast so the host/slirp side sees traffic (M1 exit). */
    {
        static const UBYTE bcast[64] = {0};  /* minimal filler frame */
        LONG sent = tn_s2_send(&nif, bcast, sizeof(bcast), TRUE, 0x1234);
        if (sent < 0)
            log_line(DOSBase, fh, "tolunet M1: broadcast send failed\n");
        else
            log_line(DOSBase, fh, "tolunet M1: broadcast sent\n");
    }

    /* Poll incoming frames for a short window. M1 just logs type+length. */
    {
        ULONG spin;
        UBYTE src[SANA2_MAX_ADDR_BYTES];
        ULONG frames = 0;
        /* Crude bounded poll; the real task uses Wait() on the reply port. */
        for (spin = 0; spin < (ULONG)(TN_STATUS_SECONDS * 100); spin++) {
            ULONG idx;
            for (idx = 0; idx < nif.n_read_ios; idx++) {
                LONG flen = tn_s2_recv(&nif, rbuf, sizeof(rbuf), idx, src);
                if (flen > 0) {
                    ULONG et = ether_type(rbuf, (ULONG)flen);
                    char line[80];
                    /* Tiny manual formatter (no sprintf in resident code). */
                    format_line(line, sizeof(line),
                                "tolunet M1: frame len=", (ULONG)flen,
                                " type=0x", et);
                    log_line(DOSBase, fh, line);
                    frames++;
                }
            }
            Delay(50);   /* VERIFY: DOS Delay, ~1s at 50 VBlank ticks */
        }
        if (frames == 0)
            log_line(DOSBase, fh, "tolunet M1: no frames received in window\n");
    }

    tn_s2_offline_close(&nif);
    log_line(DOSBase, fh, "tolunet M1: shut down cleanly\n");
    rc = 0;  /* RETURN_OK */

close_log:
    Close(fh);
out:
    CloseLibrary(DOSBase);
    return rc;
}

/* --- minimal decimal/hex formatter (no stdio) ----------------------------- */
static void format_line(char *out, ULONG outcap,
                 const char *pfx, ULONG declen,
                 const char *mid, ULONG hexval)
{
    ULONG o = 0, k;
    char tmp[16];
    /* prefix */
    for (k = 0; pfx[k] && o + 1 < outcap; k++) out[o++] = pfx[k];
    /* decimal */
    {
        ULONG n = declen, i = 0;
        if (n == 0) tmp[i++] = '0';
        while (n > 0 && i < sizeof(tmp)) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
        while (i > 0 && o + 1 < outcap) out[o++] = tmp[--i];
    }
    for (k = 0; mid[k] && o + 1 < outcap; k++) out[o++] = mid[k];
    /* hex (4 digits) */
    {
        ULONG shift;
        for (shift = 0; shift < 4; shift++) {
            UBYTE nyb = (UBYTE)((hexval >> ((3 - shift) * 4)) & 0xF);
            if (o + 1 < outcap)
                out[o++] = (char)(nyb < 10 ? '0' + nyb : 'A' + nyb - 10);
        }
    }
    if (o + 1 < outcap) out[o++] = '\n';
    if (o < outcap) out[o] = '\0';
}
