/*
 * tolunnet — pure dotted-quad / classful address parser (host-testable).
 * Behaviour identical to the bsdsocket inet_addr() LVO (TNET-019/TNET-051).
 */

#include "inet_parse.h"

int tn_inet_addr_parse_ex(const char *cp, uint32_t *out_addr)
{
    uint32_t val[4];
    const char *p = cp;
    int parts = 0;

    if (cp == NULL || *cp == '\0') return 0;

    while (*p && parts < 4) {
        uint32_t num = 0;
        int base_radix = 10;
        int digits = 0;

        if (*p == '0') {
            if (*(p + 1) == 'x' || *(p + 1) == 'X') {
                base_radix = 16;
                p += 2;
            } else {
                base_radix = 8;
                p++;
                digits++;
            }
        }

        while (*p) {
            int d = -1;
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (base_radix == 16 && *p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (base_radix == 16 && *p >= 'A' && *p <= 'F') d = *p - 'A' + 10;

            if (d < 0 || d >= base_radix) break;
            num = num * (uint32_t)base_radix + (uint32_t)d;
            digits++;
            p++;
        }

        if (digits == 0) return 0;
        val[parts++] = num;

        if (*p == '.') {
            p++;
            if (*p == '\0') return 0;
        } else if (*p != '\0') {
            return 0;
        }
    }

    if (*p != '\0') return 0;

    switch (parts) {
    case 1:
        *out_addr = val[0];
        return 1;
    case 2:
        if (val[0] > 0xFF || val[1] > 0xFFFFFF) return 0;
        *out_addr = (val[0] << 24) | (val[1] & 0xFFFFFF);
        return 1;
    case 3:
        if (val[0] > 0xFF || val[1] > 0xFF || val[2] > 0xFFFF) return 0;
        *out_addr = (val[0] << 24) | ((val[1] & 0xFF) << 16) | (val[2] & 0xFFFF);
        return 1;
    case 4:
        if (val[0] > 0xFF || val[1] > 0xFF || val[2] > 0xFF || val[3] > 0xFF) return 0;
        *out_addr = (val[0] << 24) | ((val[1] & 0xFF) << 16) | ((val[2] & 0xFF) << 8) | (val[3] & 0xFF);
        return 1;
    default:
        return 0;
    }
}

uint32_t tn_inet_addr_parse(const char *cp)
{
    uint32_t addr;
    if (!tn_inet_addr_parse_ex(cp, &addr)) return TN_INADDR_NONE;
    return addr;
}
