/*
 * tolunnet — CheckNetConfig command (CLOSE §B.8).
 * ReadArgs: FILE — validates a tolunnet config file (default
 * DEVS:tolunnet.config): every non-comment line must be KEY=VALUE with a
 * key the parser accepts and a value of the right class (dotted quad /
 * number / boolean-ish). Problems print with LINE numbers. RC 0 clean,
 * 10 problems found, 5 unreadable file.
 */
#include "cmdlib.h"
#include "../common/config_text.h"
#include <string.h>

#define TEMPLATE "FILE"

static char g_buf[4096];

static int read_file(const char *path, char *buf, int size)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    LONG n;
    if (fh == (BPTR)0) return -1;
    n = Read(fh, (APTR)buf, size - 1);
    Close(fh);
    if (n < 0) return -1;
    buf[n] = '\0';
    return (int)n;
}

static int looks_like_ip(const char *v)
{
    /* dotted quad: 4 numeric groups 0..255 separated by dots */
    ULONG o[4] = { 0, 0, 0, 0 };
    int part = 0, digits = 0;
    const char *p = v;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            if (digits > 2) return 0;
            o[part] = o[part] * 10 + (ULONG)(*p - '0');
            digits++;
            p++;
        } else if (*p == '.') {
            if (digits == 0 || part >= 3 || o[part] > 255) return 0;
            part++;
            digits = 0;
            p++;
        } else {
            return 0;
        }
    }
    return (part == 3 && digits > 0 && o[part] <= 255);
}

static int looks_like_number(const char *v)
{
    int digits = 0;
    const char *p = v;
    if (*p == '-') p++;
    while (*p >= '0' && *p <= '9') { digits++; p++; }
    return (digits > 0 && *p == '\0');
}

int main(int argc, char **argv)
{
    LONG opts[1] = { 0 };
    struct RDArgs *rdargs;
    const char *path = "DEVS:tolunnet.config";
    char *line;
    int lineno = 0;
    int problems = 0;

    if (tn_cmd_init() != TN_CMD_OK) return TN_CMD_FAIL;

    rdargs = ReadArgs((CONST_STRPTR)TEMPLATE, opts, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR)"CheckNetConfig");
        tn_cmd_fini();
        return TN_CMD_USAGE;
    }
    if (opts[0] != 0) path = (const char *)opts[0];

    if (read_file(path, g_buf, sizeof(g_buf)) < 0) {
        tn_cmd_printf("CheckNetConfig: cannot read %s\n", path);
        FreeArgs(rdargs); tn_cmd_fini();
        return TN_CMD_WARN;
    }

    line = g_buf;
    while (line != NULL && *line != '\0') {
        char *eol = line;
        char key[32];
        char val[64];
        int len, klen = 0, eq = -1, i;
        char clean_line[96];
        int cl = 0;

        while (*eol && *eol != '\n') eol++;
        len = (int)(eol - line);
        if (*eol == '\n') *eol++ = '\0';

        lineno++;

        /* trimmed working copy */
        {
            int s = 0;
            while (s < len && (line[s] == ' ' || line[s] == '\t' || line[s] == '\r')) s++;
            while (len > s && (line[len-1] == ' ' || line[len-1] == '\t' || line[len-1] == '\r')) len--;
            for (i = s; i < len && cl < (int)sizeof(clean_line) - 1; i++) {
                clean_line[cl++] = line[i];
            }
            clean_line[cl] = '\0';
        }

        if (cl == 0 || clean_line[0] == ';' || clean_line[0] == '#') {
            line = eol;
            continue;
        }

        for (i = 0; i < cl; i++) {
            if (clean_line[i] == '=') { eq = i; break; }
        }
        if (eq <= 0) {
            tn_cmd_printf("LINE %d: not KEY=VALUE: \"%s\"\n", lineno, clean_line);
            problems++;
            line = eol;
            continue;
        }

        klen = (eq < (int)sizeof(key) - 1) ? eq : (int)sizeof(key) - 1;
        for (i = 0; i < klen; i++) key[i] = clean_line[i];
        key[klen] = '\0';
        {
            int vl = cl - eq - 1;
            if (vl > (int)sizeof(val) - 1) vl = (int)sizeof(val) - 1;
            for (i = 0; i < vl; i++) val[i] = clean_line[eq + 1 + i];
            val[vl] = '\0';
        }

        if (!tn_config_key_known(key)) {
            tn_cmd_printf("LINE %d: unknown key \"%s\"\n", lineno, key);
            problems++;
        } else {
            int cls = tn_config_value_class(key);
            if (val[0] == '\0') {
                tn_cmd_printf("LINE %d: empty value for \"%s\"\n", lineno, key);
                problems++;
            } else if (cls == 1) {
                /* IP-class: dotted quad, or hostname (letters) for
                 * DNS/NAMESERVER/SYSLOG — only strict for MASK/NETMASK/IP */
                if ((strncmp(key, "NETMASK", 7) == 0 || strncmp(key, "MASK", 4) == 0 ||
                     strncmp(key, "IP", 2) == 0 || strncmp(key, "IP_ADDR", 7) == 0 ||
                     strncmp(key, "GATEWAY", 7) == 0 || strncmp(key, "GW", 2) == 0) &&
                    !looks_like_ip(val)) {
                    tn_cmd_printf("LINE %d: \"%s\" expects a dotted quad, got \"%s\"\n",
                                  lineno, key, val);
                    problems++;
                }
            } else if (cls == 2 && !looks_like_number(val)) {
                tn_cmd_printf("LINE %d: \"%s\" expects a number, got \"%s\"\n",
                              lineno, key, val);
                problems++;
            } else if (cls == 3) {
                if (!(val[0] == 'Y' || val[0] == 'y' || val[0] == 'N' || val[0] == 'n' ||
                      val[0] == '1' || val[0] == '0' || val[0] == 'T' || val[0] == 't' ||
                      val[0] == 'F' || val[0] == 'f' || val[0] == 'O' || val[0] == 'o')) {
                    tn_cmd_printf("LINE %d: \"%s\" expects YES/NO, got \"%s\"\n",
                                  lineno, key, val);
                    problems++;
                }
            }
        }
        line = eol;
    }

    if (problems == 0) {
        tn_cmd_printf("CheckNetConfig: %s OK (%d lines)\n", path, lineno);
    } else {
        tn_cmd_printf("CheckNetConfig: %d problem(s) in %s\n", problems, path);
    }

    (void)argc; (void)argv;
    FreeArgs(rdargs);
    tn_cmd_fini();
    return (problems == 0) ? TN_CMD_OK : TN_CMD_FAIL;
}
