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

int tn_format_wireless_block(const char *ssid, const char *passphrase, char *out_buf, int out_max)
{
    if (!ssid || !out_buf || out_max <= 0) return 0;

    int written = 0;
    if (passphrase && passphrase[0] != '\0') {
        /* WPA/WPA2 with passphrase */
        written = snprintf(out_buf, out_max,
                           "network={\n"
                           "    ssid=\"%s\"\n"
                           "    psk=\"%s\"\n"
                           "    scan_ssid=1\n"
                           "}\n",
                           ssid, passphrase);
    } else {
        /* Open network */
        written = snprintf(out_buf, out_max,
                           "network={\n"
                           "    ssid=\"%s\"\n"
                           "    key_mgmt=NONE\n"
                           "    scan_ssid=1\n"
                           "}\n",
                           ssid);
    }
    return written;
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
                        char *bssid_ptr = (char *)GetTagData(S2INFO_BSSID, 0, tags);
                        if (bssid_ptr) {
                            memcpy(net->bssid, bssid_ptr, 6);
                            /* SSID starts at byte 8 in SANA-II standard */
                            strncpy(net->ssid, &bssid_ptr[8], sizeof(net->ssid) - 1);
                        } else {
                            strncpy(net->ssid, "Unknown AP", sizeof(net->ssid) - 1);
                        }

                        net->channel = (WORD)GetTagData(S2INFO_Channel, 1, tags);
                        net->signal_dbm = (LONG)GetTagData(S2INFO_Signal, -70, tags);
                        net->noise_dbm = (LONG)GetTagData(S2INFO_Noise, -90, tags);
                        net->encryption = (WORD)GetTagData(S2INFO_Encryption, 3, tags);

                        const char *enc_name = "Open";
                        if (net->encryption == 3) enc_name = "WPA2";
                        else if (net->encryption == 2) enc_name = "WPA";
                        else if (net->encryption == 1) enc_name = "WEP";

                        /* Estimate percentage: -100 dBm = 0%, -50 dBm = 100% */
                        int pct = (int)((net->signal_dbm + 100) * 2);
                        if (pct < 0) pct = 0;
                        if (pct > 100) pct = 100;

                        snprintf(net->display_str, sizeof(net->display_str),
                                 "%-18.18s Ch:%-2d %3d%%  [%s]",
                                 net->ssid, net->channel, pct, enc_name);

                        ws->wifi_count++;
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

    return (ok1 || ok2);
}

BOOL tn_wifi_start_manager(const char *device_name, ULONG unit)
{
    if (!device_name) return FALSE;

    char cmd[128];
    if (strcasecmp(device_name, "wifipi.device") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "Run <>NIL: C:WirelessManager DEVICE=\"%s\" UNIT=%lu CONFIG=\"ENVARC:Sys/Wireless.prefs\"",
                 device_name, (unsigned long)unit);
    } else {
        snprintf(cmd, sizeof(cmd), "Run <>NIL: C:WirelessManager %s", device_name);
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
BOOL tn_wifi_start_manager(const char *device_name, ULONG unit) { (void)device_name; (void)unit; return TRUE; }
BOOL tn_wifi_wait_association(const char *device_name, ULONG unit, int timeout_secs,
                              char *err_msg, int err_max)
{
    (void)device_name; (void)unit; (void)timeout_secs; (void)err_msg; (void)err_max;
    return TRUE;
}

#endif
