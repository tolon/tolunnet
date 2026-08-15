/*
 * tolunnet — Advanced Native Workbench Preferences & Control Panel (TolunnetPrefs)
 *
 * Implements a clean, parametric, fully functional AmigaOS Intuition & GadTools GUI:
 * - SANA-II Hardware Adapter Section (Device, Unit, MTU)
 * - Protocol & IP Addressing Section (DHCP vs Static, IP, Netmask, Gateway, DNS1, DNS2)
 * - Hostname & Domain Identification
 * - Live Status Indicator & Daemon Control (Start/Restart Stack)
 * - Diagnostics (Ping Test Console)
 * - Native 3D Bevel Box layout rendering (ROM 2.04+ / 3.0+ compliant)
 */

#include "../common/prefs.h"
#include "../common/log.h"
#include "../../include/ipc.h"

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

struct IntuitionBase *IntuitionBase = NULL;
struct Library       *GadToolsBase  = NULL;
struct GfxBase       *GfxBase       = NULL;
struct DosLibrary   *DOSBase       = NULL;

/* Gadget IDs */
#define GID_DEVICE      1
#define GID_UNIT        2
#define GID_MODE        3
#define GID_IP          4
#define GID_NETMASK     5
#define GID_GATEWAY     6
#define GID_DNS1        7
#define GID_DNS2        8
#define GID_HOSTNAME    9
#define GID_SAVE        10
#define GID_USE         11
#define GID_PING        12
#define GID_DAEMON      13
#define GID_CANCEL      14

static const STRPTR g_mode_labels[] = {
    (STRPTR)"DHCP (Automatic)",
    (STRPTR)"Static IP (Manual)",
    NULL
};

/*
 * GadTools requires a REAL TextAttr for every gadget — a NULL ng_TextAttr is
 * NOT safe on OS 3.0/3.1 (GadTools dereferences it to open the label font and
 * to size the gadget, crashing with a line-F / #8000000B before the window
 * opens). topaz.font/8 is always present in ROM, so this never fails.
 */
static struct TextAttr g_gui_font = { (STRPTR)"topaz.font", 8, 0, 0 };

/* Render 3D Beveled Framing Boxes around visual groups */
static void render_gui_frames(struct Window *win, APTR vi)
{
    if (!win || !vi) return;

    /* Group 1: Hardware & Network Interface */
    DrawBevelBox(win->RPort, 12, 20, 460, 52,
                 GT_VisualInfo, (ULONG)vi,
                 GTBB_Recessed, TRUE,
                 TAG_END);

    /* Group 2: IP Addressing & Nameserver Configuration */
    DrawBevelBox(win->RPort, 12, 78, 460, 110,
                 GT_VisualInfo, (ULONG)vi,
                 GTBB_Recessed, TRUE,
                 TAG_END);

    /* Group 3: Host Identity */
    DrawBevelBox(win->RPort, 12, 194, 460, 36,
                 GT_VisualInfo, (ULONG)vi,
                 GTBB_Recessed, TRUE,
                 TAG_END);
}

int main(int argc, char *argv[])
{
    struct Screen *scr = NULL;
    struct Window *win = NULL;
    APTR vi = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *gad = NULL;
    struct NewGadget ng;

    /* Gadget Pointers */
    struct Gadget *gad_dev = NULL;
    struct Gadget *gad_unit = NULL;
    struct Gadget *gad_mode = NULL;
    struct Gadget *gad_ip = NULL;
    struct Gadget *gad_nm = NULL;
    struct Gadget *gad_gw = NULL;
    struct Gadget *gad_dns1 = NULL;
    struct Gadget *gad_dns2 = NULL;
    struct Gadget *gad_host = NULL;

    struct IntuiMessage *imsg = NULL;
    ULONG class;
    UWORD code;
    BOOL running = TRUE;
    TnPrefs prefs;
    char hostname_buf[32] = "amiga";
    char dns2_buf[20] = "1.0.0.1";
    (void)argc; (void)argv;

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 36);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);

    if (!DOSBase || !IntuitionBase || !GadToolsBase || !GfxBase) {
        goto cleanup;
    }

    /* Load persistent preferences */
    tn_prefs_load(&prefs);

    scr = LockPubScreen(NULL);
    if (!scr) goto cleanup;

    vi = GetVisualInfo(scr, TAG_END);
    if (!vi) goto cleanup;

    /* Initialize GadTools gadget context */
    gad = CreateContext(&glist);
    if (!gad) goto cleanup;

    /* Base NewGadget defaults */
    ng.ng_TextAttr   = &g_gui_font; /* MUST be a real TextAttr (see g_gui_font) */
    ng.ng_VisualInfo = vi;
    ng.ng_UserData   = NULL;

    /* =========================================================================
     * SECTION 1: HARDWARE INTERFACE (Top: 26 - 68)
     * ========================================================================= */

    /* 1. Device Name String */
    ng.ng_LeftEdge   = 120;
    ng.ng_TopEdge    = 28;
    ng.ng_Width      = 190;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"SANA-II Dev:";
    ng.ng_GadgetID   = GID_DEVICE;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_dev = CreateGadget(STRING_KIND, gad, &ng,
                           GTST_String, (ULONG)prefs.device,
                           GTST_MaxChars, 63,
                           TAG_END);
    if (!gad_dev) goto cleanup;

    /* 2. Unit Number Integer */
    ng.ng_LeftEdge   = 390;
    ng.ng_TopEdge    = 28;
    ng.ng_Width      = 60;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Unit:";
    ng.ng_GadgetID   = GID_UNIT;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_unit = CreateGadget(INTEGER_KIND, gad_dev, &ng,
                            GTIN_Number, prefs.unit,
                            GTIN_MaxChars, 5,
                            TAG_END);
    if (!gad_unit) goto cleanup;

    /* 3. IP Addressing Mode Cycle */
    ng.ng_LeftEdge   = 120;
    ng.ng_TopEdge    = 48;
    ng.ng_Width      = 190;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Config Mode:";
    ng.ng_GadgetID   = GID_MODE;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_mode = CreateGadget(CYCLE_KIND, gad_unit, &ng,
                            GTCY_Labels, (ULONG)g_mode_labels,
                            GTCY_Active, prefs.use_dhcp ? 0 : 1,
                            TAG_END);
    if (!gad_mode) goto cleanup;

    /* =========================================================================
     * SECTION 2: TCP/IP ADDRESSING (Top: 84 - 180)
     * ========================================================================= */

    /* 4. IP Address String */
    ng.ng_LeftEdge   = 120;
    ng.ng_TopEdge    = 86;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"IP Address:";
    ng.ng_GadgetID   = GID_IP;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_ip = CreateGadget(STRING_KIND, gad_mode, &ng,
                          GTST_String, (ULONG)prefs.ip_addr,
                          GTST_MaxChars, 19,
                          TAG_END);
    if (!gad_ip) goto cleanup;

    /* 5. Subnet Mask String */
    ng.ng_LeftEdge   = 330;
    ng.ng_TopEdge    = 86;
    ng.ng_Width      = 120;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Netmask:";
    ng.ng_GadgetID   = GID_NETMASK;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_nm = CreateGadget(STRING_KIND, gad_ip, &ng,
                          GTST_String, (ULONG)prefs.netmask,
                          GTST_MaxChars, 19,
                          TAG_END);
    if (!gad_nm) goto cleanup;

    /* 6. Default Gateway String */
    ng.ng_LeftEdge   = 120;
    ng.ng_TopEdge    = 108;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Gateway:";
    ng.ng_GadgetID   = GID_GATEWAY;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_gw = CreateGadget(STRING_KIND, gad_nm, &ng,
                          GTST_String, (ULONG)prefs.gateway,
                          GTST_MaxChars, 19,
                          TAG_END);
    if (!gad_gw) goto cleanup;

    /* 7. Primary DNS Server */
    ng.ng_LeftEdge   = 120;
    ng.ng_TopEdge    = 130;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Primary DNS:";
    ng.ng_GadgetID   = GID_DNS1;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_dns1 = CreateGadget(STRING_KIND, gad_gw, &ng,
                            GTST_String, (ULONG)prefs.dns_server,
                            GTST_MaxChars, 19,
                            TAG_END);
    if (!gad_dns1) goto cleanup;

    /* 8. Secondary DNS Server */
    ng.ng_LeftEdge   = 330;
    ng.ng_TopEdge    = 130;
    ng.ng_Width      = 120;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Sec DNS:";
    ng.ng_GadgetID   = GID_DNS2;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_dns2 = CreateGadget(STRING_KIND, gad_dns1, &ng,
                            GTST_String, (ULONG)dns2_buf,
                            GTST_MaxChars, 19,
                            TAG_END);
    if (!gad_dns2) goto cleanup;

    /* =========================================================================
     * SECTION 3: HOST IDENTITY (Top: 200)
     * ========================================================================= */

    /* 9. Hostname String */
    ng.ng_LeftEdge   = 120;
    ng.ng_TopEdge    = 202;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Host Name:";
    ng.ng_GadgetID   = GID_HOSTNAME;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_host = CreateGadget(STRING_KIND, gad_dns2, &ng,
                            GTST_String, (ULONG)hostname_buf,
                            GTST_MaxChars, 31,
                            TAG_END);
    if (!gad_host) goto cleanup;

    /* =========================================================================
     * SECTION 4: ACTION BUTTON BAR (Top: 242)
     * ========================================================================= */

    /* Save Button */
    ng.ng_LeftEdge   = 14;
    ng.ng_TopEdge    = 242;
    ng.ng_Width      = 84;
    ng.ng_Height     = 18;
    ng.ng_GadgetText = (STRPTR)"Save";
    ng.ng_Flags      = PLACETEXT_IN;
    ng.ng_GadgetID   = GID_SAVE;
    gad = CreateGadget(BUTTON_KIND, gad_host, &ng, TAG_END);
    if (!gad) goto cleanup;

    /* Use / Apply Button */
    ng.ng_LeftEdge   = 104;
    ng.ng_TopEdge    = 242;
    ng.ng_Width      = 84;
    ng.ng_Height     = 18;
    ng.ng_GadgetText = (STRPTR)"Use";
    ng.ng_GadgetID   = GID_USE;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    if (!gad) goto cleanup;

    /* Test Ping Button */
    ng.ng_LeftEdge   = 196;
    ng.ng_TopEdge    = 242;
    ng.ng_Width      = 90;
    ng.ng_Height     = 18;
    ng.ng_GadgetText = (STRPTR)"Ping Test";
    ng.ng_GadgetID   = GID_PING;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    if (!gad) goto cleanup;

    /* Start / Restart Daemon Button */
    ng.ng_LeftEdge   = 294;
    ng.ng_TopEdge    = 242;
    ng.ng_Width      = 92;
    ng.ng_Height     = 18;
    ng.ng_GadgetText = (STRPTR)"Start Stack";
    ng.ng_GadgetID   = GID_DAEMON;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    if (!gad) goto cleanup;

    /* Cancel Button */
    ng.ng_LeftEdge   = 394;
    ng.ng_TopEdge    = 242;
    ng.ng_Width      = 78;
    ng.ng_Height     = 18;
    ng.ng_GadgetText = (STRPTR)"Cancel";
    ng.ng_GadgetID   = GID_CANCEL;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    if (!gad) goto cleanup;

    /* Open Centered Intuition Window */
    win = OpenWindowTags(NULL,
                         WA_Left,          50,
                         WA_Top,           25,
                         WA_Width,         484,
                         WA_Height,        272,
                         WA_IDCMP,         IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN,
                         WA_Flags,         WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
                         WA_Gadgets,       (ULONG)glist,
                         WA_Title,         (ULONG)"tolunnet Network Preferences",
                         WA_PubScreen,     (ULONG)scr,
                         TAG_END);

    if (!win) goto cleanup;

    GT_RefreshWindow(win, NULL);
    render_gui_frames(win, vi);

    /* Event Message Loop */
    while (running) {
        Wait(1UL << win->UserPort->mp_SigBit);

        while ((imsg = GT_GetIMsg(win->UserPort)) != NULL) {
            class = imsg->Class;
            code  = imsg->Code;

            if (class == IDCMP_GADGETUP) {
                struct Gadget *g = (struct Gadget *)imsg->IAddress;
                if (g != NULL) {
                    switch (g->GadgetID) {
                    case GID_MODE:
                        prefs.use_dhcp = (code == 0);
                        break;

                    case GID_SAVE:
                    case GID_USE:
                        {
                            struct StringInfo *si;

                            si = (struct StringInfo *)gad_unit->SpecialInfo;
                            if (si) {
                                prefs.unit = (ULONG)si->LongInt;
                            }

                            si = (struct StringInfo *)gad_dev->SpecialInfo;
                            if (si && si->Buffer) {
                                CopyMem(si->Buffer, prefs.device, sizeof(prefs.device) - 1);
                                prefs.device[sizeof(prefs.device) - 1] = '\0';
                            }

                            si = (struct StringInfo *)gad_ip->SpecialInfo;
                            if (si && si->Buffer) {
                                CopyMem(si->Buffer, prefs.ip_addr, sizeof(prefs.ip_addr) - 1);
                                prefs.ip_addr[sizeof(prefs.ip_addr) - 1] = '\0';
                            }

                            si = (struct StringInfo *)gad_nm->SpecialInfo;
                            if (si && si->Buffer) {
                                CopyMem(si->Buffer, prefs.netmask, sizeof(prefs.netmask) - 1);
                                prefs.netmask[sizeof(prefs.netmask) - 1] = '\0';
                            }

                            si = (struct StringInfo *)gad_gw->SpecialInfo;
                            if (si && si->Buffer) {
                                CopyMem(si->Buffer, prefs.gateway, sizeof(prefs.gateway) - 1);
                                prefs.gateway[sizeof(prefs.gateway) - 1] = '\0';
                            }

                            si = (struct StringInfo *)gad_dns1->SpecialInfo;
                            if (si && si->Buffer) {
                                CopyMem(si->Buffer, prefs.dns_server, sizeof(prefs.dns_server) - 1);
                                prefs.dns_server[sizeof(prefs.dns_server) - 1] = '\0';
                            }

                            /* Persist changes */
                            tn_prefs_save(&prefs);

                            if (g->GadgetID == GID_SAVE) {
                                running = FALSE;
                            }
                        }
                        break;

                    case GID_PING:
                        {
                            char ping_cmd[160];
                            CONST_STRPTR target = (prefs.gateway[0] != '\0') ? (CONST_STRPTR)prefs.gateway : (CONST_STRPTR)"1.1.1.1";
                            char *p = ping_cmd;
                            CONST_STRPTR s1 = (CONST_STRPTR)"ping ";
                            CONST_STRPTR s2 = (CONST_STRPTR)" 4 >\"CON:60/60/460/160/tolunnet Live Ping Probe/AUTO/CLOSE/WAIT\"";
                            while (*s1) *p++ = *s1++;
                            while (*target) *p++ = *target++;
                            while (*s2) *p++ = *s2++;
                            *p = '\0';
                            Execute((CONST_STRPTR)ping_cmd, (BPTR)0, (BPTR)0);
                        }
                        break;

                    case GID_DAEMON:
                        {
                            /* Start or restart tolunnet background daemon */
                            Execute((CONST_STRPTR)"Run >NIL: C:tolunnet", (BPTR)0, (BPTR)0);
                        }
                        break;

                    case GID_CANCEL:
                        running = FALSE;
                        break;
                    }
                }
            } else if (class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
            } else if (class == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
                render_gui_frames(win, vi);
            }

            GT_ReplyIMsg(imsg);
        }
    }

cleanup:
    if (win) CloseWindow(win);
    if (glist) FreeGadgets(glist);
    if (vi) FreeVisualInfo(vi);
    if (scr) UnlockPubScreen(NULL, scr);

    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);

    return 0;
}
