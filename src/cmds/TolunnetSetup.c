/*
 * tolunnet — First-Run Network Setup Wizard (TolunnetSetup)
 *
 * Screen-font GadTools GUI fitting 640x200 NTSC Workbench.
 * 5-page wizard with ARexx automation, stack migration, SANA-II device
 * probing, WiFi scanning, and boot configuration.
 */

#include "../setup/setup_types.h"
#include "../setup/stack_detect.h"
#include "../setup/hw_detect.h"
#include "../setup/wifi_mgr.h"
#include "../setup/net_test.h"
#include "../setup/setup_rexx.h"
#include "../common/inet_parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <exec/execbase.h>

extern struct ExecBase      *SysBase;
extern struct DosLibrary    *DOSBase;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
struct Library       *GadToolsBase  = NULL;
unsigned long        __stack        = 32768;

/* Gadget IDs */
#define GID_CYCLE_PAGE      100
#define GID_BTN_BACK        101
#define GID_BTN_NEXT        102
#define GID_BTN_CANCEL      103

/* Page 1: Replace */
#define GID_P1_REPLACE_CHK  110

/* Page 2: Hardware */
#define GID_P2_HW_CYCLE     120
#define GID_P2_SCAN_BTN     121

/* Page 3: WiFi */
#define GID_P3_WIFI_LIST    130
#define GID_P3_RESCAN_BTN   131
#define GID_P3_PASS_STR     132
#define GID_P3_SHOWPASS_CHK 133

/* Page 4: Address */
#define GID_P4_IPMODE_CYCLE 140
#define GID_P4_IP_STR       141
#define GID_P4_NM_STR       142
#define GID_P4_GW_STR       143
#define GID_P4_DNS1_STR     144
#define GID_P4_DNS2_STR     145
#define GID_P4_HOST_STR     146
#define GID_P4_ROADSHOW_CHK 147

/* Page 5: Test */
#define GID_P5_TEST_BTN     150
#define GID_P5_BOOT_CHK     151

static const STRPTR g_page_names[] = {
    (STRPTR)"1. Replace Stacks",
    (STRPTR)"2. Hardware",
    (STRPTR)"3. WiFi Setup",
    (STRPTR)"4. IP Address",
    (STRPTR)"5. Test & Finish",
    NULL
};

static const STRPTR g_ipmode_names[] = {
    (STRPTR)"DHCP (Automatic Configuration)",
    (STRPTR)"Static IP (Manual Configuration)",
    NULL
};

static struct TextAttr g_gui_font = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

static WizardState g_ws;
static struct Window *g_win = NULL;
static struct Gadget *g_nav_glist = NULL;
static struct Gadget *g_page_glist = NULL;
static APTR g_vi = NULL;

static struct Gadget *g_gad_cycle = NULL;
static struct Gadget *g_gad_next = NULL;
static struct Gadget *g_gad_back = NULL;

static STRPTR g_hw_labels[MAX_DETECTED_HW + 1];

#ifdef __AMIGA__
#include <intuition/sghooks.h>

static char s_pass_display_buf[64];

static ULONG pass_edit_hook_fn(struct Hook *hook __asm__("a0"),
                               struct SGWork *sgw __asm__("a2"),
                               ULONG *msg __asm__("a1"))
{
    (void)hook;
    if (!msg || *msg != SGH_KEY) return 1;

    if (g_ws.wifi_show_pass) return 1;

    if (sgw->EditOp == EO_INSERTCHAR || sgw->EditOp == EO_REPLACECHAR) {
        UWORD code = sgw->Code;
        if (code >= 32 && code < 127 && code != '"') {
            WORD pos = sgw->BufferPos;
            if (pos > 0 && pos < (WORD)sizeof(g_ws.wifi_pass)) {
                g_ws.wifi_pass[pos - 1] = (char)code;
                g_ws.wifi_pass[pos] = '\0';
                sgw->WorkBuffer[pos - 1] = '*';
            }
        }
    } else if (sgw->EditOp == EO_DELBACKWARD) {
        WORD pos = sgw->BufferPos;
        if (pos >= 0 && pos < (WORD)sizeof(g_ws.wifi_pass)) {
            g_ws.wifi_pass[pos] = '\0';
        }
    } else if (sgw->EditOp == EO_RESET || sgw->EditOp == EO_CLEAR) {
        volatile char *vp = (volatile char *)g_ws.wifi_pass;
        for (size_t i = 0; i < sizeof(g_ws.wifi_pass); i++) vp[i] = 0;
    }
    return 1;
}

static struct Hook s_pass_hook = {
    { NULL, NULL },
    (ULONG (*)())pass_edit_hook_fn,
    NULL,
    NULL
};
#endif

static void rebuild_page_gadgets(void);

static void sync_page_gadgets_to_state(void)
{
    if (!g_page_glist) return;
    struct Gadget *g = g_page_glist;
    while (g) {
        if (g->GadgetType == GTYP_STRGADGET || (g->SpecialInfo != NULL && (g->GadgetType & 0x0F) == GTYP_STRGADGET)) {
            struct StringInfo *si = (struct StringInfo *)g->SpecialInfo;
            if (si && si->Buffer) {
                switch (g->GadgetID) {
                case GID_P3_PASS_STR:
                    if (g_ws.wifi_show_pass) {
                        strncpy(g_ws.wifi_pass, (const char *)si->Buffer, sizeof(g_ws.wifi_pass) - 1);
                        g_ws.wifi_pass[sizeof(g_ws.wifi_pass) - 1] = '\0';
                    }
                    break;
                case GID_P4_IP_STR:
                    strncpy(g_ws.ip_str, (const char *)si->Buffer, sizeof(g_ws.ip_str) - 1);
                    break;
                case GID_P4_NM_STR:
                    strncpy(g_ws.nm_str, (const char *)si->Buffer, sizeof(g_ws.nm_str) - 1);
                    break;
                case GID_P4_GW_STR:
                    strncpy(g_ws.gw_str, (const char *)si->Buffer, sizeof(g_ws.gw_str) - 1);
                    break;
                case GID_P4_DNS1_STR:
                    strncpy(g_ws.dns1_str, (const char *)si->Buffer, sizeof(g_ws.dns1_str) - 1);
                    break;
                }
            }
        }
        g = g->NextGadget;
    }
}

static void update_nav_buttons(void)
{
    if (!g_win || !g_gad_next || !g_gad_back || !g_gad_cycle) return;

    /* Update cycle gadget */
    GT_SetGadgetAttrs(g_gad_cycle, g_win, NULL,
                      GTCY_Active, g_ws.current_page,
                      TAG_END);

    /* Back disabled on first page */
    GT_SetGadgetAttrs(g_gad_back, g_win, NULL,
                      GA_Disabled, (g_ws.current_page == 0),
                      TAG_END);

    /* Label on next button */
    if (g_ws.current_page == WIZARD_PAGE_TEST) {
        GT_SetGadgetAttrs(g_gad_next, g_win, NULL,
                          GT_Underscore, '_',
                          TAG_END);
    }
}

static void advance_next_page(void)
{
    sync_page_gadgets_to_state();
    if (g_ws.current_page < WIZARD_PAGE_COUNT - 1) {
        g_ws.current_page++;
        /* Skip WiFi if hardware is wired */
        if (g_ws.current_page == WIZARD_PAGE_WIFI) {
            if (g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
                if (!g_ws.hw[g_ws.selected_hw_idx].is_wireless) {
                    g_ws.current_page = WIZARD_PAGE_ADDRESS;
                }
            }
        }
        rebuild_page_gadgets();
    } else {
        /* Finish! */
        g_ws.rexx_done = TRUE;
    }
}

static void retreat_back_page(void)
{
    sync_page_gadgets_to_state();
    if (g_ws.current_page > 0) {
        g_ws.current_page--;
        /* Skip WiFi backwards if wired */
        if (g_ws.current_page == WIZARD_PAGE_WIFI) {
            if (g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
                if (!g_ws.hw[g_ws.selected_hw_idx].is_wireless) {
                    g_ws.current_page = WIZARD_PAGE_HW;
                }
            }
        }
        rebuild_page_gadgets();
    }
}

static void draw_page_content(void)
{
    if (!g_win) return;
    struct RastPort *rp = g_win->RPort;

    /* Clear middle area */
    SetAPen(rp, 0);
    RectFill(rp, 15, 34, 605, 145);
    SetAPen(rp, 1);

    char buf[128];

    switch (g_ws.current_page) {
    case WIZARD_PAGE_REPLACE:
        Move(rp, 20, 50);
        Text(rp, (CONST_STRPTR)"Step 1: Replace Legacy Stacks", 29);
        Move(rp, 20, 65);
        if (g_ws.stack_count > 0) {
            snprintf(buf, sizeof(buf), "Detected %d existing network stack(s):", g_ws.stack_count);
            Text(rp, (CONST_STRPTR)buf, (WORD)strlen(buf));
            for (int i = 0; i < g_ws.stack_count && i < 3; i++) {
                Move(rp, 30, 80 + i * 15);
                snprintf(buf, sizeof(buf), "* %s: %s", g_ws.stacks[i].name, g_ws.stacks[i].details);
                Text(rp, (CONST_STRPTR)buf, (WORD)strlen(buf));
            }
        } else {
            Text(rp, (CONST_STRPTR)"No conflicting stacks detected. System is ready for tolunnet.", 61);
        }
        break;

    case WIZARD_PAGE_HW:
        Move(rp, 20, 50);
        Text(rp, (CONST_STRPTR)"Step 2: Select Network Interface Hardware", 41);
        if (g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
            DetectedHw *hw = &g_ws.hw[g_ws.selected_hw_idx];
            Move(rp, 30, 95);
            snprintf(buf, sizeof(buf), "Hardware Type: %s", hw->is_wireless ? "Wireless 802.11" : "Ethernet IEEE 802.3");
            Text(rp, (CONST_STRPTR)buf, (WORD)strlen(buf));
            Move(rp, 30, 110);
            snprintf(buf, sizeof(buf), "Hardware MAC:  %s     MTU: %lu bytes", hw->mac_str, (unsigned long)hw->mtu);
            Text(rp, (CONST_STRPTR)buf, (WORD)strlen(buf));
        }
        break;

    case WIZARD_PAGE_WIFI:
        Move(rp, 20, 50);
        Text(rp, (CONST_STRPTR)"Step 3: Wireless Network Configuration", 38);
        Move(rp, 20, 65);
        if (g_ws.wifi_count > 0) {
            snprintf(buf, sizeof(buf), "Found %d wireless network(s):", g_ws.wifi_count);
            Text(rp, (CONST_STRPTR)buf, (WORD)strlen(buf));
        } else {
            Text(rp, (CONST_STRPTR)"Click 'Rescan' to scan for nearby wireless networks.", 52);
        }
        if (g_ws.wifi_status_msg[0]) {
            Move(rp, 20, 140);
            Text(rp, (CONST_STRPTR)g_ws.wifi_status_msg, (WORD)strlen(g_ws.wifi_status_msg));
        }
        break;

    case WIZARD_PAGE_ADDRESS:
        Move(rp, 20, 50);
        Text(rp, (CONST_STRPTR)"Step 4: IP Address & DNS Configuration", 38);
        if (g_ws.ip_mode == 0) {
            Move(rp, 30, 90);
            Text(rp, (CONST_STRPTR)"DHCP will automatically obtain IP, netmask, gateway, and DNS", 60);
            Move(rp, 30, 105);
            Text(rp, (CONST_STRPTR)"servers upon interface bring-up.", 32);
        }
        break;

    case WIZARD_PAGE_TEST:
        Move(rp, 20, 50);
        Text(rp, (CONST_STRPTR)"Step 5: Diagnostic Test & Finish", 32);
        for (int i = 0; i < 4; i++) {
            Move(rp, 30, 70 + i * 14);
            const char *label = (i == 0) ? "Daemon: " : (i == 1) ? "Gateway:" : (i == 2) ? "DNS:    " : "HTTP:   ";
            int st = (i == 0) ? g_ws.test_daemon_ok : (i == 1) ? g_ws.test_ping_ok : (i == 2) ? g_ws.test_dns_ok : g_ws.test_http_ok;
            const char *res = (st == 1) ? "[OK]" : (st == 0) ? "[FAIL]" : "[READY]";
            snprintf(buf, sizeof(buf), "%s %-7s %s", label, res, g_ws.test_details[i]);
            Text(rp, (CONST_STRPTR)buf, (WORD)strlen(buf));
        }
        break;
    }
}

static void rebuild_page_gadgets(void)
{
    if (!g_win) return;
    sync_page_gadgets_to_state();

    if (g_page_glist) {
        RemoveGList(g_win, g_page_glist, -1);
        FreeGadgets(g_page_glist);
        g_page_glist = NULL;
    }

    struct NewGadget ng;
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = g_vi;
    ng.ng_TextAttr   = &g_gui_font;

    struct Gadget *prev = CreateContext(&g_page_glist);

    switch (g_ws.current_page) {
    case WIZARD_PAGE_REPLACE:
        ng.ng_LeftEdge   = 30;
        ng.ng_TopEdge    = 125;
        ng.ng_Width      = 26;
        ng.ng_Height     = 14;
        ng.ng_GadgetText = (STRPTR)"Replace with tolunnet (recommended, non-destructive)";
        ng.ng_GadgetID   = GID_P1_REPLACE_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.replace_stacks,
                            TAG_END);
        break;

    case WIZARD_PAGE_HW:
        for (int i = 0; i < g_ws.hw_count; i++) {
            g_hw_labels[i] = (STRPTR)g_ws.hw[i].friendly_name;
        }
        g_hw_labels[g_ws.hw_count] = NULL;

        ng.ng_LeftEdge   = 30;
        ng.ng_TopEdge    = 70;
        ng.ng_Width      = 440;
        ng.ng_Height     = 16;
        ng.ng_GadgetText = (STRPTR)"Adapter:";
        ng.ng_GadgetID   = GID_P2_HW_CYCLE;
        ng.ng_Flags      = PLACETEXT_LEFT;
        prev = CreateGadget(CYCLE_KIND, prev, &ng,
                            GTCY_Labels, (ULONG)g_hw_labels,
                            GTCY_Active, g_ws.selected_hw_idx,
                            TAG_END);

        ng.ng_LeftEdge   = 490;
        ng.ng_TopEdge    = 70;
        ng.ng_Width      = 90;
        ng.ng_Height     = 16;
        ng.ng_GadgetText = (STRPTR)"Rescan";
        ng.ng_GadgetID   = GID_P2_SCAN_BTN;
        ng.ng_Flags      = PLACETEXT_IN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng, TAG_END);
        break;

    case WIZARD_PAGE_WIFI:
        ng.ng_LeftEdge   = 490;
        ng.ng_TopEdge    = 65;
        ng.ng_Width      = 90;
        ng.ng_Height     = 16;
        ng.ng_GadgetText = (STRPTR)"Scan APs";
        ng.ng_GadgetID   = GID_P3_RESCAN_BTN;
        ng.ng_Flags      = PLACETEXT_IN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng, TAG_END);

        ng.ng_LeftEdge   = 110;
        ng.ng_TopEdge    = 115;
        ng.ng_Width      = 240;
        ng.ng_Height     = 16;
        ng.ng_GadgetText = (STRPTR)"Passphrase:";
        ng.ng_GadgetID   = GID_P3_PASS_STR;
        ng.ng_Flags      = PLACETEXT_LEFT;
        if (g_ws.wifi_show_pass) {
            strncpy(s_pass_display_buf, g_ws.wifi_pass, sizeof(s_pass_display_buf) - 1);
            s_pass_display_buf[sizeof(s_pass_display_buf) - 1] = '\0';
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)s_pass_display_buf,
                                GTST_MaxChars, 63,
                                TAG_END);
        } else {
            size_t plen = strlen(g_ws.wifi_pass);
            for (size_t i = 0; i < plen && i < sizeof(s_pass_display_buf) - 1; i++) {
                s_pass_display_buf[i] = '*';
            }
            s_pass_display_buf[plen < sizeof(s_pass_display_buf) ? plen : sizeof(s_pass_display_buf) - 1] = '\0';
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)s_pass_display_buf,
                                GTST_EditHook, (ULONG)&s_pass_hook,
                                GTST_MaxChars, 63,
                                TAG_END);
        }

        ng.ng_LeftEdge   = 370;
        ng.ng_TopEdge    = 116;
        ng.ng_Width      = 26;
        ng.ng_Height     = 14;
        ng.ng_GadgetText = (STRPTR)"Show";
        ng.ng_GadgetID   = GID_P3_SHOWPASS_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.wifi_show_pass,
                            TAG_END);
        break;

    case WIZARD_PAGE_ADDRESS:
        ng.ng_LeftEdge   = 110;
        ng.ng_TopEdge    = 65;
        ng.ng_Width      = 280;
        ng.ng_Height     = 16;
        ng.ng_GadgetText = (STRPTR)"IP Mode:";
        ng.ng_GadgetID   = GID_P4_IPMODE_CYCLE;
        ng.ng_Flags      = PLACETEXT_LEFT;
        prev = CreateGadget(CYCLE_KIND, prev, &ng,
                            GTCY_Labels, (ULONG)g_ipmode_names,
                            GTCY_Active, g_ws.ip_mode,
                            TAG_END);

        if (g_ws.ip_mode == 1) {
            /* Static IP fields */
            ng.ng_LeftEdge   = 110;
            ng.ng_TopEdge    = 85;
            ng.ng_Width      = 140;
            ng.ng_Height     = 16;
            ng.ng_GadgetText = (STRPTR)"IP:";
            ng.ng_GadgetID   = GID_P4_IP_STR;
            ng.ng_Flags      = PLACETEXT_LEFT;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.ip_str,
                                TAG_END);

            ng.ng_LeftEdge   = 330;
            ng.ng_TopEdge    = 85;
            ng.ng_Width      = 140;
            ng.ng_Height     = 16;
            ng.ng_GadgetText = (STRPTR)"Mask:";
            ng.ng_GadgetID   = GID_P4_NM_STR;
            ng.ng_Flags      = PLACETEXT_LEFT;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.nm_str,
                                TAG_END);

            ng.ng_LeftEdge   = 110;
            ng.ng_TopEdge    = 105;
            ng.ng_Width      = 140;
            ng.ng_Height     = 16;
            ng.ng_GadgetText = (STRPTR)"Gateway:";
            ng.ng_GadgetID   = GID_P4_GW_STR;
            ng.ng_Flags      = PLACETEXT_LEFT;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.gw_str,
                                TAG_END);

            ng.ng_LeftEdge   = 330;
            ng.ng_TopEdge    = 105;
            ng.ng_Width      = 140;
            ng.ng_Height     = 16;
            ng.ng_GadgetText = (STRPTR)"DNS:";
            ng.ng_GadgetID   = GID_P4_DNS1_STR;
            ng.ng_Flags      = PLACETEXT_LEFT;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.dns1_str,
                                TAG_END);
        }

        ng.ng_LeftEdge   = 30;
        ng.ng_TopEdge    = 128;
        ng.ng_Width      = 26;
        ng.ng_Height     = 14;
        ng.ng_GadgetText = (STRPTR)"Also write Roadshow-style DEVS:NetInterfaces/ for other tools";
        ng.ng_GadgetID   = GID_P4_ROADSHOW_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.write_roadshow,
                            TAG_END);
        break;

    case WIZARD_PAGE_TEST:
        ng.ng_LeftEdge   = 480;
        ng.ng_TopEdge    = 70;
        ng.ng_Width      = 100;
        ng.ng_Height     = 18;
        ng.ng_GadgetText = (STRPTR)"Run Tests";
        ng.ng_GadgetID   = GID_P5_TEST_BTN;
        ng.ng_Flags      = PLACETEXT_IN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng, TAG_END);

        ng.ng_LeftEdge   = 30;
        ng.ng_TopEdge    = 130;
        ng.ng_Width      = 26;
        ng.ng_Height     = 14;
        ng.ng_GadgetText = (STRPTR)"Start tolunnet TCP/IP stack automatically at boot";
        ng.ng_GadgetID   = GID_P5_BOOT_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.start_at_boot,
                            TAG_END);
        break;
    }

    if (g_page_glist) {
        AddGList(g_win, g_page_glist, -1, -1, NULL);
        RefreshGList(g_page_glist, g_win, NULL, -1);
    }
    update_nav_buttons();
    draw_page_content();
}

static void apply_wizard_finish(void)
{
    /* 1. Migrate / disable other stacks */
    if (g_ws.replace_stacks) {
        tn_stack_apply_replacement(&g_ws);
        tn_stack_request_quit(&g_ws);
    }

    /* 2. Write WiFi credentials if wireless */
    if (g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
        if (g_ws.hw[g_ws.selected_hw_idx].is_wireless) {
            const char *ssid = (g_ws.selected_wifi_idx >= 0 && g_ws.selected_wifi_idx < g_ws.wifi_count)
                               ? g_ws.wifi[g_ws.selected_wifi_idx].ssid : "DefaultAP";
            tn_wifi_write_prefs(ssid, g_ws.wifi_pass);
            tn_wifi_start_manager(g_ws.hw[g_ws.selected_hw_idx].device_name,
                                  g_ws.hw[g_ws.selected_hw_idx].unit);
        }
    }

    /* 3. Write tolunnet configuration */
    tn_write_tolunnet_config(&g_ws);

    /* 4. Write Roadshow interface if requested */
    if (g_ws.write_roadshow) {
        tn_write_roadshow_interface(&g_ws);
    }

    /* 5. Install boot block if requested */
    if (g_ws.start_at_boot) {
        tn_install_boot_block(TRUE);
    }

    /* Zero the memory containing passphrase immediately after writing */
    volatile char *vpass = (volatile char *)g_ws.wifi_pass;
    for (size_t i = 0; i < sizeof(g_ws.wifi_pass); i++) {
        vpass[i] = 0;
    }
#ifdef __AMIGA__
    volatile char *vdisp = (volatile char *)s_pass_display_buf;
    for (size_t i = 0; i < sizeof(s_pass_display_buf); i++) {
        vdisp[i] = 0;
    }
#endif
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    memset(&g_ws, 0, sizeof(g_ws));
    g_ws.replace_stacks = TRUE;
    g_ws.write_roadshow = TRUE;
    g_ws.start_at_boot  = TRUE;
    g_ws.ip_mode        = 0; /* DHCP default */
    strncpy(g_ws.ip_str, "192.168.1.100", sizeof(g_ws.ip_str) - 1);
    strncpy(g_ws.nm_str, "255.255.255.0", sizeof(g_ws.nm_str) - 1);
    strncpy(g_ws.gw_str, "192.168.1.1", sizeof(g_ws.gw_str) - 1);
    strncpy(g_ws.dns1_str, "1.1.1.1", sizeof(g_ws.dns1_str) - 1);
    strncpy(g_ws.host_str, "amiga", sizeof(g_ws.host_str) - 1);

    struct Process *pr = (struct Process *)FindTask(NULL);
    APTR old_win_ptr = NULL;
    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        old_win_ptr = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;
    }

    /* Initialize ARexx host port early so external scripts and bench can connect immediately */
    struct MsgPort *rexx_port = tn_setup_rexx_init();

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    GfxBase       = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    GadToolsBase  = OpenLibrary((CONST_STRPTR)"gadtools.library", 36);

    /* Run initial scans */
    tn_stack_detect_all(&g_ws);
    tn_hw_scan_all(&g_ws);

    struct Screen *scr = NULL;
    BOOL owns_screen = FALSE;

    if (IntuitionBase && GfxBase && GadToolsBase) {
        scr = LockPubScreen(NULL);
        if (!scr) {
            /* Open fallback screen if Workbench is not loaded yet (e.g. early User-Startup) */
            scr = OpenScreenTags(NULL,
                                 SA_Depth, 2,
                                 SA_DisplayID, DEFAULT_MONITOR_ID | HIRES_KEY,
                                 SA_Title, (ULONG)"tolunnet First-Run Network Setup Wizard",
                                 SA_Type, CUSTOMSCREEN,
                                 TAG_END);
            if (scr) owns_screen = TRUE;
        }
    }

    if (scr) {
        g_vi = GetVisualInfo(scr, TAG_END);
        if (g_vi) {
            /* Create persistent navigation gadgets */
            struct NewGadget ng;
            memset(&ng, 0, sizeof(ng));
            ng.ng_VisualInfo = g_vi;
            ng.ng_TextAttr   = &g_gui_font;

            struct Gadget *prev = CreateContext(&g_nav_glist);

            /* Top Page Cycle */
            ng.ng_LeftEdge   = 120;
            ng.ng_TopEdge    = 14;
            ng.ng_Width      = 380;
            ng.ng_Height     = 16;
            ng.ng_GadgetText = (STRPTR)"Wizard Step:";
            ng.ng_GadgetID   = GID_CYCLE_PAGE;
            ng.ng_Flags      = PLACETEXT_LEFT;
            prev = CreateGadget(CYCLE_KIND, prev, &ng,
                                GTCY_Labels, (ULONG)g_page_names,
                                GTCY_Active, g_ws.current_page,
                                TAG_END);
            g_gad_cycle = prev;

            /* Bottom Buttons */
            ng.ng_LeftEdge   = 140;
            ng.ng_TopEdge    = 154;
            ng.ng_Width      = 100;
            ng.ng_Height     = 18;
            ng.ng_GadgetText = (STRPTR)"< _Back";
            ng.ng_GadgetID   = GID_BTN_BACK;
            ng.ng_Flags      = PLACETEXT_IN;
            prev = CreateGadget(BUTTON_KIND, prev, &ng,
                                GT_Underscore, '_',
                                GA_Disabled, TRUE,
                                TAG_END);
            g_gad_back = prev;

            ng.ng_LeftEdge   = 260;
            ng.ng_TopEdge    = 154;
            ng.ng_Width      = 100;
            ng.ng_Height     = 18;
            ng.ng_GadgetText = (STRPTR)"_Next >";
            ng.ng_GadgetID   = GID_BTN_NEXT;
            ng.ng_Flags      = PLACETEXT_IN;
            prev = CreateGadget(BUTTON_KIND, prev, &ng,
                                GT_Underscore, '_',
                                TAG_END);
            g_gad_next = prev;

            ng.ng_LeftEdge   = 380;
            ng.ng_TopEdge    = 154;
            ng.ng_Width      = 100;
            ng.ng_Height     = 18;
            ng.ng_GadgetText = (STRPTR)"_Cancel";
            ng.ng_GadgetID   = GID_BTN_CANCEL;
            ng.ng_Flags      = PLACETEXT_IN;
            prev = CreateGadget(BUTTON_KIND, prev, &ng,
                                GT_Underscore, '_',
                                TAG_END);

            /* Open Window: 620 x 180 (NTSC 640x200 safe) */
            g_win = OpenWindowTags(NULL,
                                   WA_Left,         10,
                                   WA_Top,          12,
                                   WA_Width,        620,
                                   WA_Height,       180,
                                   WA_IDCMP,        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                                                    IDCMP_GADGETUP | IDCMP_RAWKEY,
                                   WA_Flags,        WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                                                    WFLG_CLOSEGADGET | WFLG_SMART_REFRESH |
                                                    WFLG_ACTIVATE,
                                   WA_Title,        (ULONG)"tolunnet First-Run Network Setup Wizard",
                                   WA_Gadgets,      (ULONG)g_nav_glist,
                                   WA_PubScreen,    (ULONG)scr,
                                   TAG_END);

            if (!owns_screen) {
                UnlockPubScreen(NULL, scr);
            }

            if (g_win) {
                GT_RefreshWindow(g_win, NULL);
                rebuild_page_gadgets();
            }
        }
    }

    if (!g_win) {
        /* Headless / script-driven mode via ARexx port */
        ULONG rexx_sig = rexx_port ? (1UL << rexx_port->mp_SigBit) : 0;
        BOOL running = TRUE;

        while (running) {
            ULONG sigs = Wait(rexx_sig | SIGBREAKF_CTRL_C);

            if (sigs & SIGBREAKF_CTRL_C) {
                break;
            }

            if (rexx_port && (sigs & rexx_sig)) {
                tn_setup_rexx_process(rexx_port, &g_ws, NULL);
                if (g_ws.rexx_done) {
                    apply_wizard_finish();
                    if (g_ws.rexx_finish_msg) {
                        ReplyMsg(g_ws.rexx_finish_msg);
                        g_ws.rexx_finish_msg = NULL;
                    }
                    break;
                }
                if (g_ws.rexx_cancel) {
                    break;
                }
            }
        }
    } else {
        ULONG win_sig = 1UL << g_win->UserPort->mp_SigBit;
        ULONG rexx_sig = rexx_port ? (1UL << rexx_port->mp_SigBit) : 0;

        BOOL running = TRUE;

        while (running) {
            ULONG sigs = Wait(win_sig | rexx_sig | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) {
            running = FALSE;
            break;
        }

        if (rexx_port && (sigs & rexx_sig)) {
            tn_setup_rexx_process(rexx_port, &g_ws, rebuild_page_gadgets);
            if (g_ws.rexx_done) {
                apply_wizard_finish();
                if (g_ws.rexx_finish_msg) {
                    ReplyMsg(g_ws.rexx_finish_msg);
                    g_ws.rexx_finish_msg = NULL;
                }
                running = FALSE;
                break;
            }
            if (g_ws.rexx_cancel) {
                running = FALSE;
                break;
            }
        }

        if (sigs & win_sig) {
            struct IntuiMessage *imsg;
            while ((imsg = GT_GetIMsg(g_win->UserPort))) {
                ULONG im_class = imsg->Class;
                UWORD im_code  = imsg->Code;
                APTR  im_iaddr = imsg->IAddress;
                GT_ReplyIMsg(imsg);

                switch (im_class) {
                case IDCMP_CLOSEWINDOW:
                    running = FALSE;
                    break;

                case IDCMP_REFRESHWINDOW:
                    GT_BeginRefresh(g_win);
                    draw_page_content();
                    GT_EndRefresh(g_win, TRUE);
                    break;

                case IDCMP_RAWKEY:
                    if (im_code == 0x44) { /* RETURN */
                        advance_next_page();
                        if (g_ws.rexx_done) {
                            apply_wizard_finish();
                            running = FALSE;
                        }
                    } else if (im_code == 0x45) { /* ESC */
                        running = FALSE;
                    }
                    break;

                case IDCMP_GADGETUP: {
                    struct Gadget *gad = (struct Gadget *)im_iaddr;
                    if (!gad) break;

                    switch (gad->GadgetID) {
                    case GID_CYCLE_PAGE:
                        g_ws.current_page = im_code;
                        rebuild_page_gadgets();
                        break;

                    case GID_BTN_NEXT:
                        advance_next_page();
                        if (g_ws.rexx_done) {
                            apply_wizard_finish();
                            running = FALSE;
                        }
                        break;

                    case GID_BTN_BACK:
                        retreat_back_page();
                        break;

                    case GID_BTN_CANCEL:
                        running = FALSE;
                        break;

                    case GID_P1_REPLACE_CHK:
                        g_ws.replace_stacks = !g_ws.replace_stacks;
                        break;

                    case GID_P2_HW_CYCLE:
                        g_ws.selected_hw_idx = im_code;
                        draw_page_content();
                        break;

                    case GID_P2_SCAN_BTN:
                        tn_hw_scan_all(&g_ws);
                        rebuild_page_gadgets();
                        break;

                    case GID_P3_RESCAN_BTN:
                        strncpy(g_ws.wifi_status_msg, "Scanning for access points...", sizeof(g_ws.wifi_status_msg) - 1);
                        draw_page_content();
                        tn_wifi_scan(&g_ws);
                        snprintf(g_ws.wifi_status_msg, sizeof(g_ws.wifi_status_msg),
                                 "Scan complete: %d network(s) found", g_ws.wifi_count);
                        rebuild_page_gadgets();
                        break;

                    case GID_P3_SHOWPASS_CHK:
                        g_ws.wifi_show_pass = !g_ws.wifi_show_pass;
                        rebuild_page_gadgets();
                        break;

                    case GID_P4_IPMODE_CYCLE:
                        g_ws.ip_mode = im_code;
                        rebuild_page_gadgets();
                        break;

                    case GID_P4_ROADSHOW_CHK:
                        g_ws.write_roadshow = !g_ws.write_roadshow;
                        break;

                    case GID_P5_TEST_BTN:
                        tn_run_network_tests(&g_ws);
                        draw_page_content();
                        break;

                    case GID_P5_BOOT_CHK:
                        g_ws.start_at_boot = !g_ws.start_at_boot;
                        break;
                    }
                    break;
                } /* case IDCMP_GADGETUP */
                } /* switch (im_class) */
            } /* while (imsg) */
        } /* if (sigs & win_sig) */
    } /* while (running) */
    } /* else */

    if (g_ws.rexx_finish_msg) {
        ReplyMsg(g_ws.rexx_finish_msg);
        g_ws.rexx_finish_msg = NULL;
    }

    if (rexx_port) {
        tn_setup_rexx_cleanup(rexx_port);
    }

    if (g_page_glist) {
        if (g_win) RemoveGList(g_win, g_page_glist, -1);
        FreeGadgets(g_page_glist);
    }

    if (g_win) {
        CloseWindow(g_win);
    }

    if (g_nav_glist) {
        FreeGadgets(g_nav_glist);
    }

    if (g_vi) {
        FreeVisualInfo(g_vi);
    }

    if (owns_screen && scr) {
        CloseScreen(scr);
    }

    if (GadToolsBase)  CloseLibrary(GadToolsBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);

    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        pr->pr_WindowPtr = old_win_ptr;
    }

    volatile char *vpass_clean = (volatile char *)g_ws.wifi_pass;
    for (size_t i = 0; i < sizeof(g_ws.wifi_pass); i++) {
        vpass_clean[i] = 0;
    }

    return 0;
}

#else /* Host test main stub */

int main(void)
{
    printf("TolunnetSetup is an AmigaOS GUI application.\n");
    return 0;
}

#endif
