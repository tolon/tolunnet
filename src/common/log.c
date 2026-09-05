/*
 * tolunnet logging implementation — DOS Write/Output (master prompt §3: no
 * stdio in resident code).
 *
 * Provides tn_log() and tn_logf() without libc printf/vprintf dependencies.
 */

#include "log.h"

#include <proto/dos.h>
#include <stdarg.h>

struct Library *g_log_dos   = NULL;
BPTR            g_log_file  = (BPTR)0;
int             g_log_level = TN_LOG_OFF;

void tn_log(int tier, const char *msg)
{
    LONG len;
    BPTR out;

    if (msg == NULL) return;
    if (tier > g_log_level) return;
    if (g_log_dos == NULL) return;

    len = 0;
    while (msg[len] != '\0') len++;

    out = Output();
    if (out != (BPTR)0) {
        Write(out, (CONST APTR)msg, len);
    }

    if (g_log_file != (BPTR)0) {
        Write(g_log_file, (CONST APTR)msg, len);
        Flush(g_log_file);
    }
}

static void tn_format_internal(char *buf, ULONG cap, const char *fmt, va_list ap)
{
    ULONG o = 0;
    const char *p = fmt;

    if (buf == NULL || cap == 0 || fmt == NULL) return;

    while (*p && o + 1 < cap) {
        if (*p != '%') {
            buf[o++] = *p++;
            continue;
        }

        p++; /* skip '%' */
        if (*p == '%') {
            buf[o++] = '%';
            p++;
            continue;
        }

        int zero_pad = 0;
        int width = 0;
        if (*p == '0') {
            zero_pad = 1;
            p++;
            if (*p >= '0' && *p <= '9') {
                width = *p - '0';
                p++;
            }
        }

        if (*p == 'l') {
            p++;
        }

        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            while (*s && o + 1 < cap) {
                buf[o++] = *s++;
            }
            p++;
        } else if (*p == 'd' || *p == 'i') {
            LONG v = va_arg(ap, LONG);
            char tmp[16];
            ULONG i = 0;
            LONG av = v < 0 ? -v : v;
            if (v < 0 && o + 1 < cap) buf[o++] = '-';
            if (av == 0) tmp[i++] = '0';
            while (av > 0 && i < sizeof(tmp)) {
                tmp[i++] = (char)('0' + (av % 10));
                av /= 10;
            }
            while (i > 0 && o + 1 < cap) buf[o++] = tmp[--i];
            p++;
        } else if (*p == 'u') {
            ULONG v = va_arg(ap, ULONG);
            char tmp[16];
            ULONG i = 0;
            if (v == 0) tmp[i++] = '0';
            while (v > 0 && i < sizeof(tmp)) {
                tmp[i++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (i > 0 && o + 1 < cap) buf[o++] = tmp[--i];
            p++;
        } else if (*p == 'x' || *p == 'X' || *p == 'p') {
            ULONG v = (*p == 'p') ? (ULONG)va_arg(ap, void *) : va_arg(ap, ULONG);
            char tmp[16];
            ULONG i = 0;
            const char hex_chars[] = "0123456789abcdef";
            if (v == 0) tmp[i++] = '0';
            while (v > 0 && i < sizeof(tmp)) {
                tmp[i++] = hex_chars[v & 0xF];
                v >>= 4;
            }
            while (zero_pad && (int)i < width && i < sizeof(tmp)) {
                tmp[i++] = '0';
            }
            while (i > 0 && o + 1 < cap) buf[o++] = tmp[--i];
            p++;
        } else if (*p == 'c') {
            int c = va_arg(ap, int);
            buf[o++] = (char)c;
            p++;
        } else {
            buf[o++] = *p++;
        }
    }

    buf[o] = '\0';
}

void tn_logf(int tier, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    if (fmt == NULL) return;
    if (tier > g_log_level) return;
    if (g_log_dos == NULL) return;

    va_start(ap, fmt);
    tn_format_internal(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    tn_log(tier, buf);
}
