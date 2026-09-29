/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — Real Test-page checks (z.ai step 11m item 1)
 *
 * Each check runs an existing C: command and judges its result:
 *   - address: C:GetNetStatus ADDRESS (prints the daemon's IPv4)
 *   - ping:    C:TolunnetPing <host> COUNT=1 TIMEOUT=5, rc 0 only
 *   - dns:     C:nslookup <name>, rc 0 and a printed IPv4 only
 *   - tcp:     C:nc <host> <port> TIMEOUT=5, rc 0 only
 * Command output is captured through T:tn-check.out; commands with
 * their own timeout option are bounded by it (ping TIMEOUT, nc
 * TIMEOUT, nslookup's built-in 3 s x 2 tries).
 */

#include "net_checks.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define TN_CHECK_TMP "T:tn-check.out"

/* Load the command and run it with RunCommand, capturing stdout.
 * This mirrors the conformance suite's run_cmd: System() starts a
 * fresh CLI process whose command line makes ReadArgs /N scans
 * misparse here (ping rc=20, dns/tcp rc=10 across the board in
 * bench 164439), while RunCommand in-process works for every C:
 * command the suite already drives. The command line MUST end with
 * a newline or /N numeric scans hit garbage. */
static LONG run_cmd_capture(const char *path, const char *args,
                            char *out, size_t outn)
{
    BPTR seg, old_out, out_fh, r;
    LONG ret = -1, got, total;
    char cmdline[160];

    out[0] = '\0';
    seg = LoadSeg((CONST_STRPTR)path);
    if (seg == (BPTR)0) return -100;
    out_fh = Open((CONST_STRPTR)TN_CHECK_TMP, MODE_NEWFILE);
    if (out_fh == (BPTR)0) {
        UnLoadSeg(seg);
        return -101;
    }
    old_out = SelectOutput(out_fh);
    snprintf(cmdline, sizeof(cmdline), "%s\n", args ? args : "");
    ret = RunCommand(seg, 32768, (CONST_STRPTR)cmdline, (LONG)strlen(cmdline));
    SelectOutput(old_out);
    Close(out_fh);
    UnLoadSeg(seg);

    r = Open((CONST_STRPTR)TN_CHECK_TMP, MODE_OLDFILE);
    if (r != (BPTR)0) {
        total = 0;
        while (total < (LONG)outn - 1 &&
               (got = Read(r, out + total, (LONG)outn - 1 - total)) > 0) {
            total += got;
        }
        Close(r);
        out[total] = '\0';
    }
    DeleteFile((CONST_STRPTR)TN_CHECK_TMP);
    return ret;
}

/* Same, with stdout sent to NIL (no output wanted). */
static LONG run_cmd_silent(const char *path, const char *args)
{
    BPTR seg, old_out, null_out;
    LONG ret = -1;
    char cmdline[160];

    seg = LoadSeg((CONST_STRPTR)path);
    if (seg == (BPTR)0) return -100;
    null_out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    old_out = SelectOutput(null_out != (BPTR)0 ? null_out : Output());
    snprintf(cmdline, sizeof(cmdline), "%s\n", args ? args : "");
    ret = RunCommand(seg, 32768, (CONST_STRPTR)cmdline, (LONG)strlen(cmdline));
    SelectOutput(old_out);
    if (null_out != (BPTR)0) Close(null_out);
    UnLoadSeg(seg);
    return ret;
}

/* digits and dots, or a plain host name (RFC-952-ish): no shell
 * metacharacter survives this, so the string is safe on a command
 * line. No leading/trailing dot, no empty label. */
static int tn_host_ok(const char *h)
{
    size_t i, n;

    if (h == NULL) return 0;
    n = strlen(h);
    if (n == 0 || n > 63) return 0;
    for (i = 0; i < n; i++) {
        char c = h[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || c == '.' || c == '-') {
            continue;
        }
        return 0;
    }
    if (h[0] == '.' || h[n - 1] == '.') return 0;
    for (i = 0; i + 1 < n; i++) {
        if (h[i] == '.' && h[i + 1] == '.') return 0;
    }
    return 1;
}

/* Scan text for a printable dotted quad; returns 1 and fills a-d. */
static int scan_ipv4(const char *s, unsigned *a, unsigned *b,
                     unsigned *c, unsigned *d)
{
    const char *p = s;

    while (*p) {
        if (isdigit((unsigned char)*p)) {
            unsigned v[4];
            int k, good = 1;
            const char *q = p;
            for (k = 0; k < 4 && good; k++) {
                int digits = 0;
                v[k] = 0;
                while (isdigit((unsigned char)*q)) {
                    v[k] = v[k] * 10 + (*q - '0');
                    q++;
                    digits++;
                    if (digits > 3 || v[k] > 255) { good = 0; break; }
                }
                if (digits == 0) good = 0;
                if (good && k < 3) {
                    if (*q != '.') good = 0;
                    else q++;
                }
            }
            if (good && !isdigit((unsigned char)*q) && *q != '.') {
                *a = v[0]; *b = v[1]; *c = v[2]; *d = v[3];
                return 1;
            }
            p++;
        } else {
            p++;
        }
    }
    return 0;
}

int tn_check_address(char *detail, size_t n)
{
    char out[128];
    unsigned a, b, c, d;
    LONG rc = run_cmd_capture("C:GetNetStatus", "ADDRESS", out, sizeof(out));

    if (rc != 0) {
        snprintf(detail, n, "no address: stack reports offline");
        return 0;
    }
    if (!scan_ipv4(out, &a, &b, &c, &d)) {
        snprintf(detail, n, "no usable address reported");
        return 0;
    }
    if (a == 0 && b == 0 && c == 0 && d == 0) {
        snprintf(detail, n, "address is 0.0.0.0 - no lease yet");
        return 0;
    }
    if (a == 169 && b == 254) {
        snprintf(detail, n, "link-local %u.%u.%u.%u - DHCP got no answer",
                 a, b, c, d);
        return 0;
    }
    snprintf(detail, n, "address %u.%u.%u.%u", a, b, c, d);
    return 1;
}

int tn_check_ping(const char *host, char *detail, size_t n)
{
    char args[120];
    LONG rc;

    if (!tn_host_ok(host)) {
        snprintf(detail, n, "invalid host string");
        return 0;
    }
    snprintf(args, sizeof(args), "%s COUNT 1 TIMEOUT 5", host);
    rc = run_cmd_silent("C:TolunnetPing", args);
    if (rc == 0) {
        snprintf(detail, n, "ping %s replied", host);
        return 1;
    }
    snprintf(detail, n, "ping %s got no reply (rc=%ld)", host, (long)rc);
    return 0;
}

int tn_check_dns(const char *name, char *detail, size_t n)
{
    char args[120];
    char out[512];
    unsigned a, b, c, d;
    LONG rc;

    if (!tn_host_ok(name)) {
        snprintf(detail, n, "invalid name string");
        return 0;
    }
    snprintf(args, sizeof(args), "%s", name);
    rc = run_cmd_capture("C:nslookup", args, out, sizeof(out));
    if (rc == 0 && scan_ipv4(out, &a, &b, &c, &d)) {
        snprintf(detail, n, "resolved %s to %u.%u.%u.%u",
                 name, a, b, c, d);
        return 1;
    }
    snprintf(detail, n, "cannot resolve %s (rc=%ld)", name, (long)rc);
    return 0;
}

int tn_check_tcp(const char *host, unsigned port, char *detail, size_t n)
{
    char args[120];
    LONG rc;

    if (!tn_host_ok(host) || port == 0 || port > 65535) {
        snprintf(detail, n, "invalid connect target");
        return 0;
    }
    snprintf(args, sizeof(args), "%s %u TIMEOUT 5", host, port);
    rc = run_cmd_silent("C:nc", args);
    if (rc == 0) {
        snprintf(detail, n, "tcp %s:%u connected", host, port);
        return 1;
    }
    snprintf(detail, n, "tcp %s:%u refused or failed (rc=%ld)",
             host, port, (long)rc);
    return 0;
}
