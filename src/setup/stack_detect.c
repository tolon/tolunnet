/*
 * tolunnet — Stack Detection & Migration Engine
 *
 * Scans for Miami, Roadshow, AmiTCP, and Genesis.
 * Provides safe, non-destructive migration and undo.
 */

#include "stack_detect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dostags.h>
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
#endif

static int case_str_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            char c1 = tolower((unsigned char)haystack[i + j]);
            char c2 = tolower((unsigned char)needle[j]);
            if (c1 != c2) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static int is_stack_keyword_line(const char *line)
{
    /* Ignore lines that are already pure comments unless they are disabled markers */
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == ';' || *p == '#') {
        return 0;
    }

    if (case_str_contains(line, "miamidx") ||
        case_str_contains(line, "miami") ||
        case_str_contains(line, "miamiinit") ||
        case_str_contains(line, "amitcp") ||
        case_str_contains(line, "addnetinterface") ||
        case_str_contains(line, "configurenetinterface") ||
        case_str_contains(line, "netshutdown") ||
        case_str_contains(line, "genesis") ||
        case_str_contains(line, "startnet") ||
        case_str_contains(line, "stopnet")) {
        return 1;
    }
    return 0;
}

int tn_parse_startup_script(const char *content, char *out_buf, int out_max, int *disabled_count)
{
    if (!content || !out_buf || out_max <= 0) return 0;

    int dis_count = 0;
    int out_len = 0;
    const char *p = content;

    out_buf[0] = '\0';

    while (*p) {
        /* Extract line */
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        int line_len = (int)(p - line_start);
        if (*p == '\n') p++;

        char line[512];
        if (line_len >= (int)sizeof(line)) line_len = (int)sizeof(line) - 1;
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        /* Strip trailing CR */
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
            line_len--;
        }

        if (is_stack_keyword_line(line)) {
            dis_count++;
            int written = snprintf(out_buf + out_len, out_max - out_len,
                                   "; tolunnet-disabled: %s\n", line);
            if (written > 0 && written < out_max - out_len) {
                out_len += written;
            }
        } else {
            int written = snprintf(out_buf + out_len, out_max - out_len,
                                   "%s\n", line);
            if (written > 0 && written < out_max - out_len) {
                out_len += written;
            }
        }
    }

    if (disabled_count) *disabled_count = dis_count;
    return out_len;
}

int tn_uncomment_startup_script(const char *content, char *out_buf, int out_max, int *restored_count)
{
    if (!content || !out_buf || out_max <= 0) return 0;

    int res_count = 0;
    int out_len = 0;
    const char *p = content;

    out_buf[0] = '\0';

    while (*p) {
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        int line_len = (int)(p - line_start);
        if (*p == '\n') p++;

        char line[512];
        if (line_len >= (int)sizeof(line)) line_len = (int)sizeof(line) - 1;
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
            line_len--;
        }

        const char *prefix = "; tolunnet-disabled: ";
        size_t plen = strlen(prefix);
        if (strncmp(line, prefix, plen) == 0) {
            res_count++;
            int written = snprintf(out_buf + out_len, out_max - out_len,
                                   "%s\n", line + plen);
            if (written > 0 && written < out_max - out_len) {
                out_len += written;
            }
        } else {
            int written = snprintf(out_buf + out_len, out_max - out_len,
                                   "%s\n", line);
            if (written > 0 && written < out_max - out_len) {
                out_len += written;
            }
        }
    }

    if (restored_count) *restored_count = res_count;
    return out_len;
}

#ifdef __AMIGA__

static BOOL file_exists(const char *path)
{
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return TRUE;
    }
    return FALSE;
}

static BOOL scan_script_file(const char *path)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return FALSE;

    char buf[1024];
    LONG bytes;
    BOOL found = FALSE;

    while ((bytes = Read(fh, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        if (case_str_contains(buf, "miami") ||
            case_str_contains(buf, "amitcp") ||
            case_str_contains(buf, "addnetinterface") ||
            case_str_contains(buf, "genesis")) {
            found = TRUE;
            break;
        }
    }
    Close(fh);
    return found;
}

static void try_import_roadshow(WizardState *ws)
{
    /* Check DEVS:NetInterfaces */
    BPTR fh = Open((CONST_STRPTR)"DEVS:NetInterfaces/WiFiPi", MODE_OLDFILE);
    if (!fh) fh = Open((CONST_STRPTR)"DEVS:NetInterfaces/Ethernet", MODE_OLDFILE);
    if (!fh) fh = Open((CONST_STRPTR)"DEVS:NetInterfaces/uaenet", MODE_OLDFILE);

    if (fh) {
        char line[128];
        while (FGets(fh, (STRPTR)line, sizeof(line))) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *key = line;
                char *val = eq + 1;
                while (*key == ' ' || *key == '#') key++;
                char *nl = strpbrk(val, "\r\n");
                if (nl) *nl = '\0';

                if (strcasecmp(key, "device") == 0) {
                    /* found device name */
                } else if (strcasecmp(key, "configure") == 0) {
                    if (strcasecmp(val, "dhcp") == 0) ws->ip_mode = 0;
                    else ws->ip_mode = 1;
                } else if (strcasecmp(key, "address") == 0) {
                    strncpy(ws->ip_str, val, sizeof(ws->ip_str) - 1);
                    ws->ip_mode = 1;
                } else if (strcasecmp(key, "netmask") == 0) {
                    strncpy(ws->nm_str, val, sizeof(ws->nm_str) - 1);
                }
            }
        }
        Close(fh);
        ws->imported_settings = TRUE;
    }

    /* Check routes */
    fh = Open((CONST_STRPTR)"DEVS:Internet/routes", MODE_OLDFILE);
    if (fh) {
        char line[128];
        while (FGets(fh, (STRPTR)line, sizeof(line))) {
            char word[32], gw[32];
            if (sscanf(line, "%31s %31s", word, gw) == 2) {
                if (strcasecmp(word, "default") == 0) {
                    strncpy(ws->gw_str, gw, sizeof(ws->gw_str) - 1);
                }
            }
        }
        Close(fh);
    }

    /* Check name resolution */
    fh = Open((CONST_STRPTR)"DEVS:Internet/name_resolution", MODE_OLDFILE);
    if (fh) {
        char line[128];
        while (FGets(fh, (STRPTR)line, sizeof(line))) {
            char word[32], ip[32];
            if (sscanf(line, "%31s %31s", word, ip) == 2) {
                if (strcasecmp(word, "nameserver") == 0) {
                    if (ws->dns1_str[0] == '\0') {
                        strncpy(ws->dns1_str, ip, sizeof(ws->dns1_str) - 1);
                    } else if (ws->dns2_str[0] == '\0') {
                        strncpy(ws->dns2_str, ip, sizeof(ws->dns2_str) - 1);
                    }
                } else if (strcasecmp(word, "domain") == 0) {
                    strncpy(ws->domain_str, ip, sizeof(ws->domain_str) - 1);
                }
            }
        }
        Close(fh);
    }
}

static void try_import_amitcp(WizardState *ws)
{
    if (!ws) return;

    /* 1. HostName from ENV:HostName or ENVARC:HostName */
    BPTR fh = Open((CONST_STRPTR)"ENV:HostName", MODE_OLDFILE);
    if (!fh) fh = Open((CONST_STRPTR)"ENVARC:HostName", MODE_OLDFILE);
    if (fh) {
        char line[64];
        if (FGets(fh, (STRPTR)line, sizeof(line))) {
            char *nl = strpbrk(line, "\r\n");
            if (nl) *nl = '\0';
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\0') {
                strncpy(ws->host_str, p, sizeof(ws->host_str) - 1);
                ws->imported_settings = TRUE;
            }
        }
        Close(fh);
    }

    /* 2. Check AmiTCP:db/interfaces */
    fh = Open((CONST_STRPTR)"AmiTCP:db/interfaces", MODE_OLDFILE);
    if (fh) {
        char line[256];
        while (FGets(fh, (STRPTR)line, sizeof(line))) {
            const char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == ';' || *p == '\0' || *p == '\r' || *p == '\n') continue;

            char dev[64] = "";
            int unit = 0;
            char *dev_pos = strstr(line, "dev=");
            if (!dev_pos) dev_pos = strstr(line, "DEV=");
            if (dev_pos) {
                sscanf(dev_pos + 4, "%63s", dev);
                char *comma = strchr(dev, ',');
                if (comma) *comma = '\0';
            }
            char *unit_pos = strstr(line, "unit=");
            if (!unit_pos) unit_pos = strstr(line, "UNIT=");
            if (unit_pos) {
                sscanf(unit_pos + 5, "%d", &unit);
            }
            if (dev[0] != '\0') {
                ws->imported_settings = TRUE;
            }
        }
        Close(fh);
    }

    /* 3. Check AmiTCP:db/hosts for static IP matching host_str */
    fh = Open((CONST_STRPTR)"AmiTCP:db/hosts", MODE_OLDFILE);
    if (fh) {
        char line[256];
        while (FGets(fh, (STRPTR)line, sizeof(line))) {
            const char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == ';' || *p == '\0' || *p == '\r' || *p == '\n') continue;

            char ip[32], hname[64];
            if (sscanf(line, "%31s %63s", ip, hname) >= 2) {
                if (ws->host_str[0] != '\0' && strcasecmp(hname, ws->host_str) == 0) {
                    if (strcmp(ip, "127.0.0.1") != 0) {
                        strncpy(ws->ip_str, ip, sizeof(ws->ip_str) - 1);
                        ws->ip_mode = 1; /* static */
                        ws->imported_settings = TRUE;
                    }
                }
            }
        }
        Close(fh);
    }

    /* 4. Check AmiTCP:db/name_resolution or AmiTCP:db/resolv.conf */
    fh = Open((CONST_STRPTR)"AmiTCP:db/name_resolution", MODE_OLDFILE);
    if (!fh) fh = Open((CONST_STRPTR)"AmiTCP:db/resolv.conf", MODE_OLDFILE);
    if (fh) {
        char line[128];
        while (FGets(fh, (STRPTR)line, sizeof(line))) {
            char word[32], ip[32];
            if (sscanf(line, "%31s %31s", word, ip) == 2) {
                if (strcasecmp(word, "nameserver") == 0) {
                    if (ws->dns1_str[0] == '\0') {
                        strncpy(ws->dns1_str, ip, sizeof(ws->dns1_str) - 1);
                    } else if (ws->dns2_str[0] == '\0') {
                        strncpy(ws->dns2_str, ip, sizeof(ws->dns2_str) - 1);
                    }
                } else if (strcasecmp(word, "domain") == 0) {
                    strncpy(ws->domain_str, ip, sizeof(ws->domain_str) - 1);
                }
            }
        }
        Close(fh);
    }
}

void tn_stack_detect_all(WizardState *ws)
{
    if (!ws) return;
    ws->stack_count = 0;

    struct Process *pr = (struct Process *)FindTask(NULL);
    APTR old_win = NULL;
    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        old_win = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;
    }

    /* 1. Check Exec LibList for bsdsocket.library */
    Forbid();
    struct Library *lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)"bsdsocket.library");
    Permit();

    if (lib) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Active bsdsocket.library", sizeof(st->name) - 1);
        snprintf(st->details, sizeof(st->details), "v%d.%d running in RAM (ID: %s)",
                 lib->lib_Version, lib->lib_Revision,
                 lib->lib_IdString ? (char *)lib->lib_IdString : "unknown");
        st->is_running = TRUE;
    }

    /* 2. Check Miami / MiamiDx (do not import binary prefs; detect & disable only) */
    BOOL miami_found = FALSE;
    if (file_exists("Miami:") || file_exists("ENVARC:MiamiDx") || file_exists("ENVARC:Miami") ||
        file_exists("SYS:WBStartup/Miami.info") ||
        scan_script_file("S:User-Startup") || scan_script_file("S:Startup-Sequence")) {
        miami_found = TRUE;
    }
    if (miami_found && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Miami / MiamiDx", sizeof(st->name) - 1);
        strncpy(st->details, "Installed (Miami:, ENVARC:Miami*, WBStartup)", sizeof(st->details) - 1);
        st->has_startup = scan_script_file("S:User-Startup") || scan_script_file("S:Startup-Sequence");
    }

    /* 3. Check Roadshow */
    BOOL roadshow_found = FALSE;
    if (file_exists("DEVS:NetInterfaces") || file_exists("DEVS:Internet")) {
        roadshow_found = TRUE;
    }
    if (roadshow_found && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Roadshow", sizeof(st->name) - 1);
        strncpy(st->details, "Installed (DEVS:NetInterfaces, DEVS:Internet)", sizeof(st->details) - 1);
        st->has_startup = scan_script_file("S:User-Startup") || scan_script_file("S:Network-Startup");
    }

    /* 4. Check AmiTCP / Genesis */
    BOOL amitcp_found = FALSE;
    if (file_exists("AmiTCP:") || file_exists("AmiTCP:db/") ||
        file_exists("SYS:WBStartup/Genesis.info")) {
        amitcp_found = TRUE;
    }
    if (amitcp_found && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "AmiTCP / Genesis", sizeof(st->name) - 1);
        strncpy(st->details, "Installed on disk (AmiTCP:, AmiTCP:db/)", sizeof(st->details) - 1);
        st->has_startup = scan_script_file("S:User-Startup") || scan_script_file("S:Startup-Sequence");
    }

    /* 5. Check LIBS:bsdsocket.library */
    if (file_exists("LIBS:bsdsocket.library") && ws->stack_count < MAX_DETECTED_STACKS) {
        DetectedStack *st = &ws->stacks[ws->stack_count++];
        strncpy(st->name, "Disk bsdsocket.library", sizeof(st->name) - 1);
        strncpy(st->details, "File present in LIBS:", sizeof(st->details) - 1);
        st->has_disk_lib = TRUE;
    }

    /* Import settings ONLY from Roadshow and AmiTCP; NEVER from Miami */
    try_import_roadshow(ws);
    try_import_amitcp(ws);

    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        pr->pr_WindowPtr = old_win;
    }
}

static BOOL rewrite_file_with_parser(const char *filepath, int (*parser)(const char *, char *, int, int *))
{
    BPTR fh = Open((CONST_STRPTR)filepath, MODE_OLDFILE);
    if (!fh) return FALSE;

    /* Read entire file up to 64 KB */
    char *in_buf = (char *)malloc(65536);
    char *out_buf = (char *)malloc(98304);
    if (!in_buf || !out_buf) {
        if (in_buf) free(in_buf);
        if (out_buf) free(out_buf);
        Close(fh);
        return FALSE;
    }

    LONG r = Read(fh, in_buf, 65535);
    Close(fh);
    if (r <= 0) {
        free(in_buf);
        free(out_buf);
        return FALSE;
    }
    in_buf[r] = '\0';

    int count = 0;
    int out_len = parser(in_buf, out_buf, 98300, &count);
    free(in_buf);

    if (count > 0 && out_len > 0) {
        fh = Open((CONST_STRPTR)filepath, MODE_NEWFILE);
        if (fh) {
            Write(fh, out_buf, out_len);
            Close(fh);
        }
    }
    free(out_buf);
    return TRUE;
}

BOOL tn_stack_apply_replacement(WizardState *ws)
{
    if (!ws || !ws->replace_stacks) return TRUE;

    /* 1. Backup S:User-Startup once */
    if (file_exists("S:User-Startup") && !file_exists("S:User-Startup.tolunnet-bak")) {
        BPTR in_fh = Open((CONST_STRPTR)"S:User-Startup", MODE_OLDFILE);
        BPTR out_fh = Open((CONST_STRPTR)"S:User-Startup.tolunnet-bak", MODE_NEWFILE);
        if (in_fh && out_fh) {
            char chunk[1024];
            LONG n;
            while ((n = Read(in_fh, chunk, sizeof(chunk))) > 0) {
                Write(out_fh, chunk, n);
            }
        }
        if (in_fh) Close(in_fh);
        if (out_fh) Close(out_fh);
    }

    /* 2. Comment out stack lines in S:User-Startup and S:Network-Startup */
    rewrite_file_with_parser("S:User-Startup", tn_parse_startup_script);
    if (file_exists("S:Network-Startup")) {
        rewrite_file_with_parser("S:Network-Startup", tn_parse_startup_script);
    }

    /* 3. Rename LIBS:bsdsocket.library -> LIBS:bsdsocket.library.pre-tolunnet */
    if (file_exists("LIBS:bsdsocket.library")) {
        Rename((CONST_STRPTR)"LIBS:bsdsocket.library",
               (CONST_STRPTR)"LIBS:bsdsocket.library.pre-tolunnet");
    }

    /* 4. Disable WBStartup icons */
    if (file_exists("SYS:WBStartup/Miami.info")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Miami.info",
               (CONST_STRPTR)"SYS:WBStartup/Miami.info.pre-tolunnet");
    }
    if (file_exists("SYS:WBStartup/Genesis.info")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Genesis.info",
               (CONST_STRPTR)"SYS:WBStartup/Genesis.info.pre-tolunnet");
    }

    return TRUE;
}

BOOL tn_stack_undo_replacement(void)
{
    /* 1. Restore commented lines in S:User-Startup */
    if (file_exists("S:User-Startup")) {
        rewrite_file_with_parser("S:User-Startup", tn_uncomment_startup_script);
    }
    if (file_exists("S:Network-Startup")) {
        rewrite_file_with_parser("S:Network-Startup", tn_uncomment_startup_script);
    }

    /* 2. Restore LIBS:bsdsocket.library.pre-tolunnet */
    if (file_exists("LIBS:bsdsocket.library.pre-tolunnet")) {
        Rename((CONST_STRPTR)"LIBS:bsdsocket.library.pre-tolunnet",
               (CONST_STRPTR)"LIBS:bsdsocket.library");
    }

    /* 3. Restore WBStartup icons */
    if (file_exists("SYS:WBStartup/Miami.info.pre-tolunnet")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Miami.info.pre-tolunnet",
               (CONST_STRPTR)"SYS:WBStartup/Miami.info");
    }
    if (file_exists("SYS:WBStartup/Genesis.info.pre-tolunnet")) {
        Rename((CONST_STRPTR)"SYS:WBStartup/Genesis.info.pre-tolunnet",
               (CONST_STRPTR)"SYS:WBStartup/Genesis.info");
    }

    return TRUE;
}

void tn_stack_request_quit(WizardState *ws)
{
    if (!ws) return;

    /* 1. Ask Miami / MiamiDx to quit via ARexx port */
    Forbid();
    struct MsgPort *miami_port = FindPort((CONST_STRPTR)"MIAMI");
    struct MsgPort *miamidx_port = FindPort((CONST_STRPTR)"MIAMIDX");
    Permit();

    if (miami_port) {
        Execute((CONST_STRPTR)"rx \"address MIAMI 'QUIT'\" >NIL: <NIL:", 0, 0);
    }
    if (miamidx_port) {
        Execute((CONST_STRPTR)"rx \"address MIAMIDX 'QUIT'\" >NIL: <NIL:", 0, 0);
    }

    /* 2. Roadshow shutdown */
    if (file_exists("C:NetShutdown")) {
        Execute((CONST_STRPTR)"C:NetShutdown >NIL: <NIL:", 0, 0);
    } else if (file_exists("NetShutdown")) {
        Execute((CONST_STRPTR)"NetShutdown >NIL: <NIL:", 0, 0);
    }

    /* 3. AmiTCP shutdown */
    if (file_exists("AmiTCP:bin/stopnet")) {
        Execute((CONST_STRPTR)"AmiTCP:bin/stopnet >NIL: <NIL:", 0, 0);
    }

    /* 4. Wait loop up to 10s for legacy stack ports to terminate */
    BOOL any_running = FALSE;
    for (int i = 0; i < 20; i++) {
        Delay(25); /* 500ms */
        Forbid();
        any_running = (FindPort((CONST_STRPTR)"MIAMI") != NULL ||
                       FindPort((CONST_STRPTR)"MIAMIDX") != NULL ||
                       FindPort((CONST_STRPTR)"AmiTCP") != NULL ||
                       FindPort((CONST_STRPTR)"AmiTCP_DAEMON") != NULL);
        Permit();
        if (!any_running) break;
    }

    if (any_running) {
        ws->needs_reboot = TRUE;
    }
}

#else /* Host test stubs */

void tn_stack_detect_all(WizardState *ws) { (void)ws; }
BOOL tn_stack_apply_replacement(WizardState *ws) { (void)ws; return TRUE; }
BOOL tn_stack_undo_replacement(void) { return TRUE; }
void tn_stack_request_quit(WizardState *ws) { (void)ws; }

#endif
