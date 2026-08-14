/*
 * tolunnet — Native Workbench GadTools Preferences Panel (TolunnetPrefs)
 *
 * Implements a full-featured AmigaOS Intuition & GadTools GUI:
 * - SANA-II device & unit configuration
 * - DHCP vs Static IP addressing
 * - Live Save to ENVARC: / ENV:
 * - Ping Diagnostic Trigger
 */

#include "../common/prefs.h"
#include "../common/log.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/dos.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <dos/dos.h>

struct IntuitionBase *IntuitionBase = NULL;
struct Library       *GadToolsBase  = NULL;
struct DosLibrary   *DOSBase       = NULL;

/* Gadget IDs */
#define GID_DEVICE    1
#define GID_UNIT      2
#define GID_MODE      3
#define GID_IP        4
#define GID_NETMASK   5
#define GID_GATEWAY   6
#define GID_DNS       7
#define GID_SAVE      8
#define GID_PING      9
#define GID_CANCEL    10

static const STRPTR g_mode_labels[] = {
    (STRPTR)"DHCP (Automatic)",
    (STRPTR)"Static IP (Manual)",
    NULL
};

int main(int argc, char *argv[])
{
    struct Screen *scr = NULL;
    struct Window *win = NULL;
    APTR vi = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *gad;
    struct NewGadget ng;
    struct StringInfo *sinfo_dev, *sinfo_ip, *sinfo_nm, *sinfo_gw, *sinfo_dns;
    struct Gadget *gad_dev, *gad_unit, *gad_mode, *gad_ip, *gad_nm, *gad_gw, *gad_dns;
    struct IntuiMessage *imsg;
    ULONG class;
    UWORD code;
    BOOL running = TRUE;
    TnPrefs prefs;
    (void)argc; (void)argv;

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 36);

    if (!DOSBase || !IntuitionBase || !GadToolsBase) {
        if (GadToolsBase) CloseLibrary(GadToolsBase);
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    /* Load current or default preferences */
    tn_prefs_load(&prefs);

    scr = LockPubScreen(NULL);
    if (!scr) goto cleanup;

    vi = GetVisualInfo(scr, TAG_END);
    if (!vi) goto cleanup;

    /* Create GadTools gadget chain */
    gad = CreateContext(&glist);
    if (!gad) goto cleanup;

    /* 1. Device Name String Gadget */
    ng.ng_LeftEdge   = 110;
    ng.ng_TopEdge    = 24;
    ng.ng_Width      = 180;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"SANA-II Dev:";
    ng.ng_TextAttr   = scr->Font;
    ng.ng_GadgetID   = GID_DEVICE;
    ng.ng_Flags      = PLACETEXT_LEFT;
    ng.ng_VisualInfo = vi;
    ng.ng_UserData   = NULL;
    gad_dev = CreateGadget(STRING_KIND, gad, &ng,
                           GTST_String, (ULONG)prefs.device,
                           GTST_MaxChars, 63,
                           TAG_END);

    /* 2. Unit Number Integer Gadget */
    ng.ng_LeftEdge   = 350;
    ng.ng_TopEdge    = 24;
    ng.ng_Width      = 40;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Unit:";
    ng.ng_GadgetID   = GID_UNIT;
    gad_unit = CreateGadget(INTEGER_KIND, gad_dev, &ng,
                            GTIN_Number, prefs.unit,
                            GTIN_MaxChars, 3,
                            TAG_END);

    /* 3. Addressing Mode Cycle Gadget */
    ng.ng_LeftEdge   = 110;
    ng.ng_TopEdge    = 44;
    ng.ng_Width      = 180;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"IP Mode:";
    ng.ng_GadgetID   = GID_MODE;
    gad_mode = CreateGadget(CYCLE_KIND, gad_unit, &ng,
                            GTCY_Labels, (ULONG)g_mode_labels,
                            GTCY_Active, prefs.use_dhcp ? 0 : 1,
                            TAG_END);

    /* 4. IP Address String Gadget */
    ng.ng_LeftEdge   = 110;
    ng.ng_TopEdge    = 66;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"IP Address:";
    ng.ng_GadgetID   = GID_IP;
    gad_ip = CreateGadget(STRING_KIND, gad_mode, &ng,
                          GTST_String, (ULONG)prefs.ip_addr,
                          GTST_MaxChars, 19,
                          TAG_END);

    /* 5. Subnet Mask String Gadget */
    ng.ng_LeftEdge   = 110;
    ng.ng_TopEdge    = 84;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Netmask:";
    ng.ng_GadgetID   = GID_NETMASK;
    gad_nm = CreateGadget(STRING_KIND, gad_ip, &ng,
                          GTST_String, (ULONG)prefs.netmask,
                          GTST_MaxChars, 19,
                          TAG_END);

    /* 6. Gateway String Gadget */
    ng.ng_LeftEdge   = 110;
    ng.ng_TopEdge    = 102;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"Gateway:";
    ng.ng_GadgetID   = GID_GATEWAY;
    gad_gw = CreateGadget(STRING_KIND, gad_nm, &ng,
                          GTST_String, (ULONG)prefs.gateway,
                          GTST_MaxChars, 19,
                          TAG_END);

    /* 7. DNS Server String Gadget */
    ng.ng_LeftEdge   = 110;
    ng.ng_TopEdge    = 120;
    ng.ng_Width      = 140;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = (STRPTR)"DNS Server:";
    ng.ng_GadgetID   = GID_DNS;
    gad_dns = CreateGadget(STRING_KIND, gad_gw, &ng,
                           GTST_String, (ULONG)prefs.dns_server,
                           GTST_MaxChars, 19,
                           TAG_END);

    /* 8. Save Button */
    ng.ng_LeftEdge   = 20;
    ng.ng_TopEdge    = 148;
    ng.ng_Width      = 110;
    ng.ng_Height     = 16;
    ng.ng_GadgetText = (STRPTR)"Save Prefs";
    ng.ng_Flags      = PLACETEXT_IN;
    ng.ng_GadgetID   = GID_SAVE;
    gad = CreateGadget(BUTTON_KIND, gad_dns, &ng, TAG_END);

    /* 9. Ping Test Button */
    ng.ng_LeftEdge   = 145;
    ng.ng_TopEdge    = 148;
    ng.ng_Width      = 110;
    ng.ng_Height     = 16;
    ng.ng_GadgetText = (STRPTR)"Test Ping";
    ng.ng_GadgetID   = GID_PING;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);

    /* 10. Cancel Button */
    ng.ng_LeftEdge   = 270;
    ng.ng_TopEdge    = 148;
    ng.ng_Width      = 110;
    ng.ng_Height     = 16;
    ng.ng_GadgetText = (STRPTR)"Cancel";
    ng.ng_GadgetID   = GID_CANCEL;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);

    /* Open Intuition Window */
    win = OpenWindowTags(NULL,
                         WA_Left,          60,
                         WA_Top,           30,
                         WA_Width,         410,
                         WA_Height,        176,
                         WA_IDCMP,         IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW,
                         WA_Flags,         WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
                         WA_Gadgets,       (ULONG)glist,
                         WA_Title,         (ULONG)"tolunnet Preferences v1.1",
                         WA_PubScreen,     (ULONG)scr,
                         TAG_END);

    if (!win) goto cleanup;

    GT_RefreshWindow(win, NULL);

    /* Event Message Loop */
    while (running) {
        Wait(1UL << win->UserPort->mp_SigBit);

        while ((imsg = GT_GetIMsg(win->UserPort)) != NULL) {
            class = imsg->Class;
            code  = imsg->Code;

            if (class == IDCMP_GADGETUP) {
                struct Gadget *g = (struct Gadget *)imsg->IAddress;
                switch (g->GadgetID) {
                case GID_MODE:
                    prefs.use_dhcp = (code == 0);
                    break;

                case GID_SAVE:
                    {
                        /* Extract string and integer values (TNET-042) */
                        struct StringInfo *sinfo_unit = (struct StringInfo *)gad_unit->SpecialInfo;
                        sinfo_dev = (struct StringInfo *)gad_dev->SpecialInfo;
                        sinfo_ip  = (struct StringInfo *)gad_ip->SpecialInfo;
                        sinfo_nm  = (struct StringInfo *)gad_nm->SpecialInfo;
                        sinfo_gw  = (struct StringInfo *)gad_gw->SpecialInfo;
                        sinfo_dns = (struct StringInfo *)gad_dns->SpecialInfo;

                        if (sinfo_unit) {
                            prefs.unit = (ULONG)sinfo_unit->LongInt;
                        }
                        if (sinfo_dev && sinfo_dev->Buffer) {
                            CopyMem(sinfo_dev->Buffer, prefs.device, sizeof(prefs.device) - 1);
                            prefs.device[sizeof(prefs.device) - 1] = '\0';
                        }
                        if (sinfo_ip && sinfo_ip->Buffer) {
                            CopyMem(sinfo_ip->Buffer, prefs.ip_addr, sizeof(prefs.ip_addr) - 1);
                            prefs.ip_addr[sizeof(prefs.ip_addr) - 1] = '\0';
                        }
                        if (sinfo_nm && sinfo_nm->Buffer) {
                            CopyMem(sinfo_nm->Buffer, prefs.netmask, sizeof(prefs.netmask) - 1);
                            prefs.netmask[sizeof(prefs.netmask) - 1] = '\0';
                        }
                        if (sinfo_gw && sinfo_gw->Buffer) {
                            CopyMem(sinfo_gw->Buffer, prefs.gateway, sizeof(prefs.gateway) - 1);
                            prefs.gateway[sizeof(prefs.gateway) - 1] = '\0';
                        }
                        if (sinfo_dns && sinfo_dns->Buffer) {
                            CopyMem(sinfo_dns->Buffer, prefs.dns_server, sizeof(prefs.dns_server) - 1);
                            prefs.dns_server[sizeof(prefs.dns_server) - 1] = '\0';
                        }

                        tn_prefs_save(&prefs);
                        running = FALSE;
                    }
                    break;

                case GID_PING:
                    {
                        char ping_cmd[160];
                        CONST_STRPTR target = (prefs.gateway[0] != '\0') ? (CONST_STRPTR)prefs.gateway : (CONST_STRPTR)"1.1.1.1";
                        char *p = ping_cmd;
                        CONST_STRPTR s1 = (CONST_STRPTR)"ping ";
                        CONST_STRPTR s2 = (CONST_STRPTR)" 3 >\"CON:50/50/450/150/tolunnet Ping Test/AUTO/CLOSE/WAIT\"";
                        while (*s1) *p++ = *s1++;
                        while (*target) *p++ = *target++;
                        while (*s2) *p++ = *s2++;
                        *p = '\0';
                        Execute((CONST_STRPTR)ping_cmd, (BPTR)0, (BPTR)0);
                    }
                    break;

                case GID_CANCEL:
                    running = FALSE;
                    break;
                }
            } else if (class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
            } else if (class == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
            }

            GT_ReplyIMsg(imsg);
        }
    }

cleanup:
    if (win) CloseWindow(win);
    if (glist) FreeGadgets(glist);
    if (vi) FreeVisualInfo(vi);
    if (scr) UnlockPubScreen(NULL, scr);

    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);

    return 0;
}
