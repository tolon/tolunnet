/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * log_format — the pure printf-style formatter behind tn_logf
 * (z.ai step 9b item 1).
 *
 * Supported conversions: %[-][0][width](s | ld | lu | lx | lX | c | %),
 * plus the legacy bare d/i/u/x/X/p forms. '-' left-aligns, '0' zero-pads
 * numeric conversions, width is multi-digit and applies to 's' as space
 * padding. A trailing '%' ends the output instead of reading past it.
 *
 * Pure C: no AmigaOS headers, host-testable.
 */

#include "log_format.h"

#include <stdarg.h>
#include <stdint.h>

void tn_logf_vformat(char *buf, unsigned long cap, const char *fmt, va_list ap)
{
    unsigned long o = 0;
    const char *p = fmt;

    if (buf == NULL || cap == 0 || fmt == NULL) return;

    while (*p && o + 1 < cap) {
        if (*p != '%') {
            buf[o++] = *p++;
            continue;
        }

        p++;                                /* skip '%' */
        if (*p == '\0') break;              /* trailing '%': stop cleanly */
        if (*p == '%') {
            buf[o++] = '%';
            p++;
            continue;
        }

        int left = 0;
        int zero = 0;
        for (;;) {
            if (*p == '-') { left = 1; p++; }
            else if (*p == '0') { zero = 1; p++; }
            else break;
        }

        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        if (*p == 'l') p++;

        if (*p == 's') {
            const char *s = va_arg(ap, const char *);
            unsigned long len = 0;
            unsigned long pad;
            if (s == NULL) s = "(null)";
            while (s[len] != '\0') len++;
            pad = (len < (unsigned long)width) ? (unsigned long)width - len : 0;
            if (!left) {
                while (pad-- > 0 && o + 1 < cap) buf[o++] = ' ';
            }
            while (*s && o + 1 < cap) buf[o++] = *s++;
            if (left) {
                while (pad-- > 0 && o + 1 < cap) buf[o++] = ' ';
            }
            p++;
        } else if (*p == 'd' || *p == 'i' || *p == 'u' ||
                   *p == 'x' || *p == 'X' || *p == 'p') {
            unsigned long uv;
            int neg = 0;
            char tmp[20];
            unsigned long i = 0;
            unsigned long digits;
            unsigned long total;
            unsigned long pad;
            unsigned long k;

            if (*p == 'd' || *p == 'i') {
                long v = va_arg(ap, long);
                if (v < 0) { neg = 1; uv = (unsigned long)(-(v + 1)) + 1; }
                else uv = (unsigned long)v;
            } else if (*p == 'p') {
                uv = (unsigned long)(uintptr_t)va_arg(ap, void *);
            } else {
                uv = va_arg(ap, unsigned long);
            }

            if (uv == 0) tmp[i++] = '0';
            if (*p == 'x' || *p == 'p') {
                while (uv > 0 && i < sizeof(tmp)) { tmp[i++] = "0123456789abcdef"[uv & 0xF]; uv >>= 4; }
            } else if (*p == 'X') {
                while (uv > 0 && i < sizeof(tmp)) { tmp[i++] = "0123456789ABCDEF"[uv & 0xF]; uv >>= 4; }
            } else {
                while (uv > 0 && i < sizeof(tmp)) { tmp[i++] = (char)('0' + (uv % 10)); uv /= 10; }
            }
            digits = i;
            total = digits + (neg ? 1u : 0u);
            pad = (total < (unsigned long)width) ? (unsigned long)width - total : 0;

            if (!left) {
                if (zero) {
                    if (neg && o + 1 < cap) buf[o++] = '-';
                    while (pad-- > 0 && o + 1 < cap) buf[o++] = '0';
                } else {
                    while (pad-- > 0 && o + 1 < cap) buf[o++] = ' ';
                    if (neg && o + 1 < cap) buf[o++] = '-';
                }
            } else if (neg && o + 1 < cap) {
                buf[o++] = '-';
            }
            for (k = 0; k < digits && o + 1 < cap; k++) buf[o++] = tmp[--i];
            if (left) {
                while (pad-- > 0 && o + 1 < cap) buf[o++] = ' ';
            }
            p++;
        } else if (*p == 'c') {
            int c = va_arg(ap, int);
            unsigned long pad = (width > 1) ? (unsigned long)width - 1 : 0;
            if (!left) {
                while (pad-- > 0 && o + 1 < cap) buf[o++] = ' ';
            }
            if (o + 1 < cap) buf[o++] = (char)c;   /* 5.9: keep the NUL in bounds */
            if (left) {
                while (pad-- > 0 && o + 1 < cap) buf[o++] = ' ';
            }
            p++;
        } else {
            /* unknown conversion: emit verbatim */
            buf[o++] = *p++;
        }
    }

    buf[o] = '\0';
}
