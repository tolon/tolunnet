/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tolunnet — SANA-II Wireless Manager & Association Controller
 *
 * Scans for access points using S2_GETNETWORKS, writes ENVARC:Sys/Wireless.prefs,
 * launches WirelessManager, and verifies association.
 */

#include "wifi_mgr.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define S2INFO_SSID           (0x80000000 + 0)
#define S2INFO_BSSID          (0x80000000 + 1)
#define S2INFO_Encryption     (0x80000000 + 4)
#define S2INFO_Channel        (0x80000000 + 7)
#define S2INFO_Signal         (0x80000000 + 8)
#define S2INFO_Noise          (0x80000000 + 9)
#define S2INFO_Capabilities   (0x80000000 + 10)

#define S2_GETNETWORKS        0xc011
#define S2_GETNETWORKINFO     0xc014

static int is_safe_ssid_char(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return 1;
    }
    if (c == ' ' || c == '.' || c == '_' || c == '-') {
        return 1;
    }
    return 0;
}

int tn_format_wireless_block_priority(const char *ssid, const char *passphrase, int priority, char *out_buf, int out_max)
{
    if (!ssid || !out_buf || out_max <= 0) return 0;

    /* Check if SSID needs hex formatting */
    int needs_hex = 0;
    size_t slen = strlen(ssid);
    if (slen == 0 || slen > 32) return 0;

    for (size_t i = 0; i < slen; i++) {
        if (!is_safe_ssid_char(ssid[i])) {
            needs_hex = 1;
            break;
        }
    }

    char ssid_line[128];
    if (needs_hex) {
        char hex_ssid[65];
        for (size_t i = 0; i < slen; i++) {
            snprintf(&hex_ssid[i * 2], 3, "%02x", (unsigned char)ssid[i]);
        }
        hex_ssid[slen * 2] = '\0';
        snprintf(ssid_line, sizeof(ssid_line), "    ssid=%s\n", hex_ssid);
    } else {
        snprintf(ssid_line, sizeof(ssid_line), "    ssid=\"%s\"\n", ssid);
    }

    char prio_line[32] = "";
    if (priority > 0) {
        snprintf(prio_line, sizeof(prio_line), "    priority=%d\n", priority);
    }

    int written = 0;
    if (passphrase && passphrase[0] != '\0') {
        size_t plen = strlen(passphrase);
        if (plen < 8 || plen > 63) return 0;

        /* Reject quotes and control characters */
        for (size_t i = 0; i < plen; i++) {
            unsigned char uc = (unsigned char)passphrase[i];
            if (uc < 32 || uc == 127 || uc == '"') {
                return 0;
            }
        }

        /* WPA/WPA2 with passphrase */
        written = snprintf(out_buf, out_max,
                           "network={\n"
                           "%s"
                           "    psk=\"%s\"\n"
                           "%s"
                           "    scan_ssid=1\n"
                           "}\n",
                           ssid_line, passphrase, prio_line);
    } else {
        /* Open network */
        written = snprintf(out_buf, out_max,
                           "network={\n"
                           "%s"
                           "    key_mgmt=NONE\n"
                           "%s"
                           "    scan_ssid=1\n"
                           "}\n",
                           ssid_line, prio_line);
    }
    return written;
}

int tn_format_wireless_block(const char *ssid, const char *passphrase, char *out_buf, int out_max)
{
    return tn_format_wireless_block_priority(ssid, passphrase, 0, out_buf, out_max);
}


BOOL tn_wifi_validate_devname(const char *devname)
{
    if (!devname) return FALSE;
    size_t len = strlen(devname);
    if (len == 0 || len > 31) return FALSE;

    for (size_t i = 0; i < len; i++) {
        char c = devname[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL tn_parse_wifi_tagitem(const void *tags_ptr, WifiNetwork *out_net)
{
    if (!tags_ptr || !out_net) return FALSE;

    const struct TagItem *tags = (const struct TagItem *)tags_ptr;
    memset(out_net, 0, sizeof(*out_net));

    out_net->channel = 1;
    out_net->signal_dbm = -70;
    out_net->noise_dbm = -90;
    out_net->encryption = 3; /* WPA2 default */

    const char *ssid_ptr = NULL;
    const UBYTE *bssid_ptr = NULL;

    for (const struct TagItem *t = tags; t->ti_Tag != TAG_DONE; t++) {
        switch (t->ti_Tag) {
        case S2INFO_SSID:
            ssid_ptr = (const char *)(uintptr_t)t->ti_Data;
            break;
        case S2INFO_BSSID:
            bssid_ptr = (const UBYTE *)(uintptr_t)t->ti_Data;
            break;
        case S2INFO_Channel:
            out_net->channel = (WORD)t->ti_Data;
            break;
        case S2INFO_Signal:
            out_net->signal_dbm = (LONG)t->ti_Data;
            break;
        case S2INFO_Noise:
            out_net->noise_dbm = (LONG)t->ti_Data;
            break;
        case S2INFO_Encryption:
            out_net->encryption = (WORD)t->ti_Data;
            break;
        default:
            break;
        }
    }

    if (bssid_ptr) {
        memcpy(out_net->bssid, bssid_ptr, 6);
    }

    if (ssid_ptr && ssid_ptr[0] != '\0') {
        size_t slen = 0;
        while (slen < 32 && ssid_ptr[slen] != '\0') {
            out_net->ssid[slen] = ssid_ptr[slen];
            slen++;
        }
        out_net->ssid[slen] = '\0';
    } else if (bssid_ptr && bssid_ptr[8] != '\0') {
        /* Fallback for drivers where SSID was packed into BSSID buffer +8 */
        strncpy(out_net->ssid, (const char *)&bssid_ptr[8], sizeof(out_net->ssid) - 1);
        out_net->ssid[sizeof(out_net->ssid) - 1] = '\0';
    } else {
        strncpy(out_net->ssid, "Unknown AP", sizeof(out_net->ssid) - 1);
        out_net->ssid[sizeof(out_net->ssid) - 1] = '\0';
    }

    /* Signal percentage mapping:
     * -100 dBm = 0%, -50 dBm = 100%
     * Linear mapping formula: (signal_dbm + 100) * 2, clamped to [0, 100]
     */
    int pct = (int)((out_net->signal_dbm + 100) * 2);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    const char *enc_name = "Open";
    if (out_net->encryption == 3) enc_name = "WPA2";
    else if (out_net->encryption == 2) enc_name = "WPA";
    else if (out_net->encryption == 1) enc_name = "WEP";

    snprintf(out_net->display_str, sizeof(out_net->display_str),
             "%-18.18s Ch:%-2d %3d%%  [%s]",
             out_net->ssid, out_net->channel, pct, enc_name);

    return TRUE;
}

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/sana2.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <dos/dostags.h>

static void sort_wifi_by_signal(WizardState *ws)
{
    /* Bubble sort descending by signal_dbm */
    for (int i = 0; i < ws->wifi_count - 1; i++) {
        for (int j = 0; j < ws->wifi_count - i - 1; j++) {
            if (ws->wifi[j].signal_dbm < ws->wifi[j + 1].signal_dbm) {
                WifiNetwork tmp = ws->wifi[j];
                ws->wifi[j] = ws->wifi[j + 1];
                ws->wifi[j + 1] = tmp;
            }
        }
    }
}

void tn_wifi_scan(WizardState *ws)
{
    if (!ws || ws->selected_hw_idx < 0 || ws->selected_hw_idx >= ws->hw_count) return;
    DetectedHw *hw = &ws->hw[ws->selected_hw_idx];
    if (!hw->is_wireless) return;

    ws->wifi_count = 0;

    struct MsgPort *port = CreateMsgPort();
    if (!port) return;

    struct IOSana2Req *req = (struct IOSana2Req *)CreateIORequest(port, sizeof(struct IOSana2Req));
    if (!req) {
        DeleteMsgPort(port);
        return;
    }

    static const struct TagItem s_bm_tags[] = { {TAG_END, 0} };
    req->ios2_BufferManagement = (struct TagItem *)s_bm_tags;

    BYTE err = OpenDevice((CONST_STRPTR)hw->device_name, hw->unit, (struct IORequest *)req, 0);
    if (err == 0) {
        static const struct TagItem apParams[] = {
            {S2INFO_SSID, 0},
            {S2INFO_BSSID, 0},
            {S2INFO_Channel, 0},
            {S2INFO_Capabilities, 0},
            {S2INFO_Signal, 0},
            {S2INFO_Noise, 0},
            {S2INFO_Encryption, 0},
            {TAG_END, 0}
        };

        char *mem_pool = (char *)AllocMem(8192, MEMF_PUBLIC | MEMF_CLEAR);
        if (mem_pool) {
            req->ios2_Req.io_Command = S2_GETNETWORKS;
            req->ios2_StatData = (APTR)apParams;
            req->ios2_Data = mem_pool;
            req->ios2_DataLength = 8192;
            req->ios2_WireError = 0;

            if (DoIO((struct IORequest *)req) == 0 && req->ios2_DataLength > 0) {
                ULONG count = req->ios2_DataLength;
                if (count > MAX_WIFI_NETWORKS) count = MAX_WIFI_NETWORKS;

                struct TagItem **tag_lists = (struct TagItem **)req->ios2_StatData;
                if (tag_lists) {
                    for (ULONG i = 0; i < count; i++) {
                        struct TagItem *tags = tag_lists[i];
                        if (!tags) continue;

                        WifiNetwork *net = &ws->wifi[ws->wifi_count];
                        if (tn_parse_wifi_tagitem(tags, net)) {
                            ws->wifi_count++;
                        }
                    }
                }
            }
            FreeMem(mem_pool, 8192);
        }
        CloseDevice((struct IORequest *)req);
    }

    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(port);

    if (ws->wifi_count > 0) {
        sort_wifi_by_signal(ws);
        ws->selected_wifi_idx = 0;
    }
}

static BOOL write_text_to_file(const char *path, const char *text, int len)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!fh) return FALSE;
    LONG w = Write(fh, (CONST_APTR)text, len);
    Close(fh);
    return (w == len);
}

BOOL tn_wifi_write_prefs(const char *ssid, const char *passphrase)
{
    if (!ssid || ssid[0] == '\0') return FALSE;

    char block[512];
    int len = tn_format_wireless_block(ssid, passphrase, block, sizeof(block));
    if (len <= 0) return FALSE;

    /* Ensure parent directories exist */
    BPTR lock = CreateDir((CONST_STRPTR)"ENVARC:Sys");
    if (lock) UnLock(lock);
    lock = CreateDir((CONST_STRPTR)"ENV:Sys");
    if (lock) UnLock(lock);

    BOOL ok1 = write_text_to_file("ENVARC:Sys/Wireless.prefs", block, len);
    BOOL ok2 = write_text_to_file("ENV:Sys/Wireless.prefs", block, len);

    /* Zero the memory containing passphrase after writing */
    volatile char *vblk = (volatile char *)block;
    for (size_t i = 0; i < sizeof(block); i++) {
        vblk[i] = 0;
    }

    return (ok1 || ok2);
}

BOOL tn_wifi_write_prefs_multi(const WizardState *ws)
{
    if (!ws) return FALSE;

    /* If only primary ssid is present */
    const char *ssid = (ws->selected_wifi_idx >= 0 && ws->selected_wifi_idx < ws->wifi_count)
                       ? ws->wifi[ws->selected_wifi_idx].ssid : ws->wifi_ssid_str;
    if (!ssid || ssid[0] == '\0') {
        ssid = ws->wifi_ssid_str;
    }
    if (!ssid || ssid[0] == '\0') return FALSE;

    char combined[2048];
    int total_len = 0;

    /* Write selected network with priority 4 */
    int len = tn_format_wireless_block_priority(ssid, ws->wifi_pass, 4,
                                                combined + total_len,
                                                sizeof(combined) - total_len);
    if (len > 0) total_len += len;

    /* Include up to 3 additional scanned networks with lower priorities if needed */
    int added = 1;
    for (int i = 0; i < ws->wifi_count && added < 4; i++) {
        if (i == ws->selected_wifi_idx) continue;
        if (ws->wifi[i].ssid[0] == '\0') continue;
        if (strcmp(ws->wifi[i].ssid, ssid) == 0) continue;
        if (ws->wifi[i].encryption == 0) { /* Open networks can be written without pass */
            len = tn_format_wireless_block_priority(ws->wifi[i].ssid, NULL, 4 - added,
                                                    combined + total_len,
                                                    sizeof(combined) - total_len);
            if (len > 0) {
                total_len += len;
                added++;
            }
        }
    }

    if (total_len <= 0) return FALSE;

    BPTR lock = CreateDir((CONST_STRPTR)"ENVARC:Sys");
    if (lock) UnLock(lock);
    lock = CreateDir((CONST_STRPTR)"ENV:Sys");
    if (lock) UnLock(lock);

    BOOL ok1 = write_text_to_file("ENVARC:Sys/Wireless.prefs", combined, total_len);
    BOOL ok2 = write_text_to_file("ENV:Sys/Wireless.prefs", combined, total_len);

    volatile char *vc = (volatile char *)combined;
    for (size_t i = 0; i < sizeof(combined); i++) {
        vc[i] = 0;
    }
    return (ok1 || ok2);
}


BOOL tn_wifi_start_manager(const char *device_name, ULONG unit)
{
    if (!device_name) return FALSE;
    if (!tn_wifi_validate_devname(device_name)) return FALSE;

    char cmd[256];
    int ret;
    if (strcasecmp(device_name, "wifipi.device") == 0) {
        ret = snprintf(cmd, sizeof(cmd),
                       "Run <>NIL: C:WirelessManager DEVICE=\"%s\" UNIT=%lu CONFIG=\"ENVARC:Sys/Wireless.prefs\"",
                       device_name, (unsigned long)unit);
    } else {
        ret = snprintf(cmd, sizeof(cmd), "Run <>NIL: C:WirelessManager %s", device_name);
    }

    if (ret < 0 || (size_t)ret >= sizeof(cmd)) {
        return FALSE;
    }

    LONG rc = SystemTags((CONST_STRPTR)cmd,
                         NP_StackSize, 32768,
                         TAG_END);
    return (rc == 0);
}

BOOL tn_wifi_wait_association(const char *device_name, ULONG unit, int timeout_secs,
                              char *err_msg, int err_max)
{
    if (!device_name) return FALSE;

    struct MsgPort *port = CreateMsgPort();
    if (!port) return FALSE;

    struct IOSana2Req *req = (struct IOSana2Req *)CreateIORequest(port, sizeof(struct IOSana2Req));
    if (!req) {
        DeleteMsgPort(port);
        return FALSE;
    }

    static const struct TagItem s_bm_tags[] = { {TAG_END, 0} };
    req->ios2_BufferManagement = (struct TagItem *)s_bm_tags;

    BYTE err = OpenDevice((CONST_STRPTR)device_name, unit, (struct IORequest *)req, 0);
    if (err != 0) {
        DeleteIORequest((struct IORequest *)req);
        DeleteMsgPort(port);
        if (err_msg) snprintf(err_msg, err_max, "Cannot open %s unit %lu", device_name, (unsigned long)unit);
        return FALSE;
    }

    BOOL associated = FALSE;
    for (int i = 0; i < timeout_secs; i++) {
        Delay(50); /* 1 second */

        req->ios2_Req.io_Command = S2_ONLINE;
        if (DoIO((struct IORequest *)req) == 0) {
            associated = TRUE;
            break;
        }
    }

    CloseDevice((struct IORequest *)req);
    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(port);

    if (!associated && err_msg) {
        snprintf(err_msg, err_max, "Association failed: verify passphrase or 2.4 GHz AP");
    }
    return associated;
}

#else /* Host test stubs */

void tn_wifi_scan(WizardState *ws) { (void)ws; }
BOOL tn_wifi_write_prefs(const char *ssid, const char *passphrase) { (void)ssid; (void)passphrase; return TRUE; }
BOOL tn_wifi_write_prefs_multi(const WizardState *ws) { (void)ws; return TRUE; }
BOOL tn_wifi_start_manager(const char *device_name, ULONG unit)
{
    (void)unit;
    if (!device_name) return FALSE;
    return tn_wifi_validate_devname(device_name);
}
BOOL tn_wifi_wait_association(const char *device_name, ULONG unit, int timeout_secs,
                              char *err_msg, int err_max)
{
    (void)device_name; (void)unit; (void)timeout_secs; (void)err_msg; (void)err_max;
    return TRUE;
}

#endif
