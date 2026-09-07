/*
 * tolunnet — SANA-II Hardware Scanner & Device Prober
 *
 * Scans DEVS:Networks devices, wifipi.device, prism2.device.
 * Queries MAC, MTU, device name, and probes wireless capabilities.
 */

#include "hw_detect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/sana2.h>
#include <dos/dos.h>

#define S2_GETNETWORKS 0xc011

static const char * const g_known_devices[] = {
    "DEVS:Networks/uaenet.device",
    "DEVS:Networks/wifipi.device",
    "DEVS:Networks/prism2.device",
    "DEVS:Networks/a2065.device",
    "DEVS:Networks/cnet.device",
    "DEVS:Networks/ariadne.device",
    "DEVS:Networks/3c589.device",
    "DEVS:wifipi.device",
    "DEVS:prism2.device",
    "wifipi.device",
    "prism2.device",
    "uaenet.device",
    NULL
};

static int local_case_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return 0;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j;
        for (j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) break;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static void probe_one_device(const char *dev_path, WizardState *ws)
{
    if (!dev_path || !ws || ws->hw_count >= MAX_DETECTED_HW) return;

    /* Extract base device name */
    const char *p = strrchr(dev_path, '/');
    if (!p) p = strrchr(dev_path, ':');
    const char *base_name = p ? p + 1 : dev_path;

    /* Check if already in list */
    for (int i = 0; i < ws->hw_count; i++) {
        if (strcasecmp(ws->hw[i].device_name, base_name) == 0) {
            return;
        }
    }

    struct MsgPort *port = CreateMsgPort();
    if (!port) return;

    struct IOSana2Req *req = (struct IOSana2Req *)CreateIORequest(port, sizeof(struct IOSana2Req));
    if (!req) {
        DeleteMsgPort(port);
        return;
    }

    static const struct TagItem s_bm_tags[] = { {TAG_END, 0} };
    req->ios2_BufferManagement = (struct TagItem *)s_bm_tags;

    BYTE err = OpenDevice((CONST_STRPTR)dev_path, 0, (struct IORequest *)req, 0);
    if (err != 0) {
        /* If full path failed, try base name */
        err = OpenDevice((CONST_STRPTR)base_name, 0, (struct IORequest *)req, 0);
    }

    if (err == 0) {
        DetectedHw *hw = &ws->hw[ws->hw_count];
        strncpy(hw->device_name, base_name, sizeof(hw->device_name) - 1);
        strncpy(hw->device_path, dev_path, sizeof(hw->device_path) - 1);
        hw->unit = 0;
        hw->mtu = 1500;
        hw->is_operational = TRUE;

        /* 1. Device Query */
        struct Sana2DeviceQuery query;
        memset(&query, 0, sizeof(query));
        query.SizeAvailable = sizeof(query);
        req->ios2_Req.io_Command = S2_DEVICEQUERY;
        req->ios2_StatData = &query;
        if (DoIO((struct IORequest *)req) == 0) {
            if (query.MTU > 0) hw->mtu = query.MTU;
        }

        /* 2. Station Address (MAC) */
        req->ios2_Req.io_Command = S2_GETSTATIONADDRESS;
        req->ios2_StatData = NULL;
        if (DoIO((struct IORequest *)req) == 0) {
            snprintf(hw->mac_str, sizeof(hw->mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     req->ios2_SrcAddr[0], req->ios2_SrcAddr[1], req->ios2_SrcAddr[2],
                     req->ios2_SrcAddr[3], req->ios2_SrcAddr[4], req->ios2_SrcAddr[5]);
        } else {
            strncpy(hw->mac_str, "00:00:00:00:00:00", sizeof(hw->mac_str) - 1);
        }

        /* 3. Probe wireless */
        BOOL is_wifi = FALSE;
        if (local_case_contains(base_name, "wifi") || local_case_contains(base_name, "prism")) {
            is_wifi = TRUE;
        }

        req->ios2_Req.io_Command = S2_GETNETWORKS;
        req->ios2_StatData = NULL;
        req->ios2_Data = NULL;
        req->ios2_DataLength = 0;
        BYTE s2_err = DoIO((struct IORequest *)req);
        if (s2_err == 0 || req->ios2_Req.io_Error == 0) {
            is_wifi = TRUE;
        }
        hw->is_wireless = is_wifi;

        /* 4. Build friendly description */
        if (strcasecmp(base_name, "wifipi.device") == 0) {
            snprintf(hw->friendly_name, sizeof(hw->friendly_name),
                     "PiStorm WiFi (%s, unit %lu)", base_name, (unsigned long)hw->unit);
        } else if (strcasecmp(base_name, "prism2.device") == 0) {
            snprintf(hw->friendly_name, sizeof(hw->friendly_name),
                     "Prism II PCMCIA Wireless (%s, unit %lu)", base_name, (unsigned long)hw->unit);
        } else if (strcasecmp(base_name, "uaenet.device") == 0) {
            snprintf(hw->friendly_name, sizeof(hw->friendly_name),
                     "WinUAE Virtual Ethernet (%s, unit %lu)", base_name, (unsigned long)hw->unit);
        } else if (strcasecmp(base_name, "a2065.device") == 0) {
            snprintf(hw->friendly_name, sizeof(hw->friendly_name),
                     "Commodore A2065 Ethernet (%s, unit %lu)", base_name, (unsigned long)hw->unit);
        } else {
            snprintf(hw->friendly_name, sizeof(hw->friendly_name),
                     "%s (%s, unit %lu)", hw->is_wireless ? "Wireless Adapter" : "Ethernet Adapter",
                     base_name, (unsigned long)hw->unit);
        }

        CloseDevice((struct IORequest *)req);
        ws->hw_count++;
    }

    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(port);
}

void tn_hw_scan_all(WizardState *ws)
{
    if (!ws) return;
    ws->hw_count = 0;

    /* Scan known device paths */
    for (int i = 0; g_known_devices[i] != NULL; i++) {
        probe_one_device(g_known_devices[i], ws);
    }

    /* Fallback if nothing was openable */
    if (ws->hw_count == 0) {
        DetectedHw *hw = &ws->hw[ws->hw_count++];
        strncpy(hw->device_name, "uaenet.device", sizeof(hw->device_name) - 1);
        strncpy(hw->device_path, "DEVS:Networks/uaenet.device", sizeof(hw->device_path) - 1);
        hw->unit = 0;
        hw->mtu = 1500;
        strncpy(hw->mac_str, "00:80:10:00:00:01", sizeof(hw->mac_str) - 1);
        hw->is_wireless = FALSE;
        hw->is_operational = TRUE;
        strncpy(hw->friendly_name, "Standard SANA-II Ethernet (uaenet.device unit 0)",
                sizeof(hw->friendly_name) - 1);
    }

    ws->selected_hw_idx = 0;
}

#else /* Host test stub */

void tn_hw_scan_all(WizardState *ws)
{
    if (!ws) return;
    ws->hw_count = 1;
    strncpy(ws->hw[0].device_name, "uaenet.device", sizeof(ws->hw[0].device_name) - 1);
    strncpy(ws->hw[0].device_path, "DEVS:Networks/uaenet.device", sizeof(ws->hw[0].device_path) - 1);
    ws->hw[0].unit = 0;
    ws->hw[0].mtu = 1500;
    strncpy(ws->hw[0].mac_str, "00:80:10:00:00:01", sizeof(ws->hw[0].mac_str) - 1);
    ws->hw[0].is_wireless = FALSE;
    ws->hw[0].is_operational = TRUE;
    strncpy(ws->hw[0].friendly_name, "WinUAE Virtual Ethernet (uaenet.device unit 0)",
            sizeof(ws->hw[0].friendly_name) - 1);
    ws->selected_hw_idx = 0;
}

#endif
