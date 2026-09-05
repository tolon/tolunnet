/*
 * tolunnet — Native Workbench Preferences & Control Panel (TolunnetPrefs)
 *
 * TNET-062: window geometry is derived from the default public screen
 *           (scr->Height / scr->Font / border sizes) and always fits a
 *           640x200 NTSC Workbench. WA_Top/WA_Left are computed, never
 *           hard-coded.
 * TNET-063: Hostname / Secondary DNS / MTU gadgets are bound to TnPrefs and
 *           persisted (HOSTNAME=, DNS2=, MTU=, DEBUG= keys).
 * TNET-064: Amiga Prefs convention - Use = ENV: only (until reboot),
 *           Save = ENV: + ENVARC: + DEVS:tolunnet.config. Both notify a
 *           running daemon via TN_IPC_CMD_RECONFIG.
 * TNET-065: Start/Stop stack control: Stop signals the daemon task found via
 *           the public port (no blind second-instance spawn).
 *
 * GadTools/Intuition only, ROM 2.04+.
 */

#include "../common/prefs.h"
#include "../common/log.h"
#include "../common/ipc_client.h"
#include "../../include/ipc.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/icon.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
struct Library       *GadToolsBase  = NULL;
struct Library       *IconBase      = NULL;
struct DosLibrary    *DOSBase       = NULL;

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
#define GID_START       12
#define GID_STOP        13
#define GID_PING        14
#define GID_CANCEL      15
#define GID_MTU         16

static const STRPTR g_mode_labels[] = {
    (STRPTR)"DHCP (Automatic)",
    (STRPTR)"Static IP (Manual)",
    NULL
};

/*
 * GadTools requires a REAL TextAttr for every gadget. The screen font is
 * preferred (TNET-062); topaz.font/8 with FPF_ROMFONT (always in ROM) is the
 * fallback when the screen has no font or a nonsensical one.
 */
static struct TextAttr g_gui_font = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };

/* Computed layout (all values in window-relative pixels) */
typedef struct PrefsLayout {
    struct TextAttr *font;      /* TextAttr actually used */
    UWORD  fh;                  /* font height            */
    UWORD  gh;                  /* gadget height          */
    UWORD  pitch;               /* row pitch              */
    UWORD  win_w, win_h;        /* window outer size      */
    WORD  win_left, win_top;    /* window position on screen */
    UWORD  col1_x, col2_x;      /* gadget column left edges */
    UWORD  col1_w, col2_w;      /* gadget column widths   */
    UWORD  row_y[5];            /* y of data rows 0..4    */
    UWORD  btn_y, btn_w, btn_h; /* button bar geometry    */
} PrefsLayout;

/* ------------------------------------------------------------------ helpers */

static struct MsgPort *find_daemon_port(void)
{
    return FindPort((CONST_STRPTR)TOLUNNET_PORT_NAME);
}

static BOOL daemon_running(void)
{
    return (find_daemon_port() != NULL) ? TRUE : FALSE;
}

static void str_copy_len(char *dst, const char *src, ULONG dst_sz)
{
    ULONG i = 0;
    if (dst == NULL || dst_sz == 0) return;
    while (src && src[i] && i < dst_sz - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Read a STRING_KIND gadget's buffer into dst */
static void gad_get_str(struct Gadget *g, char *dst, ULONG dst_sz)
{
    struct StringInfo *si;
    if (g == NULL) return;
    si = (struct StringInfo *)g->SpecialInfo;
    if (si != NULL && si->Buffer != NULL) {
        str_copy_len(dst, (const char *)si->Buffer, dst_sz);
    }
}

/* Read an INTEGER_KIND gadget's value (fallback when conversion failed) */
static LONG gad_get_int(struct Gadget *g, LONG fallback)
{
    struct StringInfo *si;
    if (g == NULL) return fallback;
    si = (struct StringInfo *)g->SpecialInfo;
    if (si != NULL && (si->Buffer == NULL || si->Buffer[0] != '\0')) {
        return si->LongInt;
    }
    return fallback;
}

/* TNET-064/082: tell a running daemon to reload its configuration */
static void notify_daemon_reconfig(void)
{
    tn_ipc_oneshot(TN_IPC_CMD_RECONFIG, NULL, 0, NULL);
}

/* TNET-065/080: start the daemon with guaranteed 32 KB stack (non-blocking) */
static void daemon_start(void)
{
    if (daemon_running()) return;
    PutStr((CONST_STRPTR)"TolunnetPrefs: starting daemon with 32 KB stack...\n");
    SystemTags((CONST_STRPTR)"C:tolunnet",
               SYS_Asynch, TRUE,
               SYS_Input, (BPTR)0,
               SYS_Output, (BPTR)0,
               NP_StackSize, 32768,
               TAG_END);
}

/* TNET-065/079/081: ask the daemon task to exit (non-blocking, no Delay loops) */
static void daemon_stop(struct Window *win)
{
    struct MsgPort *port = find_daemon_port();

    if (port == NULL) return;
    PutStr((CONST_STRPTR)"TolunnetPrefs: sending stop signal to daemon...\n");
    if (port->mp_SigTask != NULL) {
        Signal((struct Task *)port->mp_SigTask, SIGBREAKF_CTRL_C);
    }
    (void)win;
}

/* TNET-079/081: toggle Start/Stop buttons via GT_SetGadgetAttrs (GA_Disabled) */
static void update_daemon_buttons(struct Window *win, struct Gadget *gad_start, struct Gadget *gad_stop)
{
    BOOL running = daemon_running();
    if (gad_start != NULL) {
        GT_SetGadgetAttrs(gad_start, win, NULL,
                          GA_Disabled, running ? TRUE : FALSE,
                          TAG_END);
    }
    if (gad_stop != NULL) {
        GT_SetGadgetAttrs(gad_stop, win, NULL,
                          GA_Disabled, running ? FALSE : TRUE,
                          TAG_END);
    }
}

/* TNET-064: after Save/Use, notify the daemon; if it is not running,
 * offer to start it (Amiga Prefs "Use now" behaviour). */
static void after_save_use(struct Window *win, struct Gadget *gad_start, struct Gadget *gad_stop)
{
    if (daemon_running()) {
        notify_daemon_reconfig();
    } else {
        struct EasyStruct es;
        es.es_StructSize   = sizeof(struct EasyStruct);
        es.es_Flags        = 0;
        es.es_Title        = (STRPTR)"tolunnet";
        es.es_TextFormat   = (STRPTR)"Configuration written.\n\nStart the network stack now?";
        es.es_GadgetFormat = (STRPTR)"Start|Not now";
        if (EasyRequestArgs(win, &es, NULL, NULL) == 1) {
            daemon_start();
            update_daemon_buttons(win, gad_start, gad_stop);
        }
    }
}

static LONG strlen_local(const char *s)
{
    LONG n = 0;
    while (s && s[n]) n++;
    return n;
}

/* ------------------------------------------------------------ layout engine */

#define TN_LABEL_PAD   8
#define TN_BORDER_PAD  8
#define TN_COL_GAP     16

/*
 * TNET-062: derive the whole layout from the locked public screen.
 * All label widths come from TextLength() of the longest label; every row is
 * font-height based; the result fits a 640x200 NTSC screen with topaz/8.
 */
static void compute_layout(PrefsLayout *lo, struct Screen *scr)
{
    static const char *const labels[] = {
        "Device:", "Unit:", "Config Mode:", "IP Address:", "Netmask:",
        "Gateway:", "DNS 1:", "DNS 2:", "Host Name:", "MTU:", NULL
    };

    struct TextAttr *sattr = scr->Font;
    struct RastPort *rp    = &scr->RastPort;
    UWORD lab_w = 0;
    int i;

    /* Screen font when sane; ROM topaz/8 otherwise */
    if (sattr != NULL && sattr->ta_Name != NULL &&
        sattr->ta_YSize >= 6 && sattr->ta_YSize <= 24) {
        lo->font = sattr;
    } else {
        lo->font = &g_gui_font;
    }

    lo->fh    = lo->font->ta_YSize;
    lo->gh    = lo->fh + 6;
    lo->pitch = lo->fh + 12;

    if (rp != NULL) {
        for (i = 0; labels[i] != NULL; i++) {
            UWORD w = (UWORD)TextLength(rp, (STRPTR)labels[i], (LONG)strlen_local(labels[i]));
            if (w > lab_w) lab_w = w;
        }
    } else {
        lab_w = (UWORD)(14 * lo->fh / 2); /* no rastport: rough topaz estimate */
    }
    lab_w += TN_LABEL_PAD;

    lo->col1_w = (UWORD)(17 * lo->fh / 2);   /* "ethernet.device" = 15 chars */
    lo->col2_w = (UWORD)(17 * lo->fh / 2);
    lo->col1_x = TN_BORDER_PAD + lab_w;
    lo->col2_x = lo->col1_x + lo->col1_w + TN_COL_GAP + lab_w;

    for (i = 0; i < 5; i++) {
        lo->row_y[i] = (UWORD)(6 + i * lo->pitch);
    }

    lo->btn_h = lo->gh + 2;
    lo->btn_y = (UWORD)(6 + 5 * lo->pitch + 4);
    lo->win_w = lo->col2_x + lo->col2_w + TN_BORDER_PAD;
    if (lo->win_w < 440) {
        lo->win_w = 440;
    }

    /* Six equal buttons across the bottom (TNET-079/081) */
    lo->btn_w = (UWORD)((lo->win_w - 2 * TN_BORDER_PAD - 5 * 8) / 6);

    /* Window OUTER height: rows + button bar + Intuition chrome estimate
     * (title bar + bottom border) so OpenWindow never overflows NTSC. */
    lo->win_h = (UWORD)(lo->btn_y + lo->btn_h + 8 +   /* interior bottom pad */
                        (scr->WBorTop + lo->fh + 1) + scr->WBorBottom + 4);

    /* Centre on the visible area; never hard-code WA_Top (TNET-062) */
    {
        WORD vis_top    = (WORD)(scr->WBorTop + lo->fh + 1);
        WORD vis_left   = (WORD)scr->WBorLeft;
        WORD vis_h      = (WORD)scr->Height - vis_top - (WORD)scr->WBorBottom;
        WORD vis_w      = (WORD)scr->Width - vis_left - (WORD)scr->WBorRight;

        if (vis_h < (WORD)lo->win_h) {
            lo->win_top  = vis_top;
            lo->win_h    = (UWORD)(vis_h < 0 ? 0 : vis_h);
        } else {
            lo->win_top  = (WORD)(vis_top + (vis_h - (WORD)lo->win_h) / 2);
        }

    if (vis_w < (WORD)lo->win_w) {
        lo->win_left = vis_left;
        lo->win_w    = (UWORD)(vis_w < 0 ? 0 : vis_w);
    } else {
        lo->win_left = (WORD)(vis_left + (vis_w - (WORD)lo->win_w) / 2);
    }
    }
}

/* Render 3D bevelled group frames (geometry follows the computed layout) */
static void render_gui_frames(struct Window *win, APTR vi, const PrefsLayout *lo)
{
    if (!win || !vi) return;

    /* Group 1: Interface (rows 0-1) */
    DrawBevelBox(win->RPort, 4, (WORD)lo->row_y[0] - 3,
                 (WORD)lo->win_w - 8, (WORD)(2 * lo->pitch + 4),
                 GT_VisualInfo, (ULONG)vi,
                 GTBB_Recessed, TRUE,
                 TAG_END);

    /* Group 2: Addressing & Identity (rows 2-4) */
    DrawBevelBox(win->RPort, 4, (WORD)lo->row_y[2] - 3,
                 (WORD)lo->win_w - 8, (WORD)(3 * lo->pitch + 4),
                 GT_VisualInfo, (ULONG)vi,
                 GTBB_Recessed, TRUE,
                 TAG_END);
}

/* -------------------------------------------------------------------- main */

int main(int argc, char *argv[])
{
    struct Screen *scr = NULL;
    struct Window *win = NULL;
    APTR vi = NULL;
    struct Gadget *glist = NULL;
    struct Gadget *gad = NULL;
    struct NewGadget ng;
    PrefsLayout lo;

    /* Gadget Pointers */
    struct Gadget *gad_dev   = NULL;
    struct Gadget *gad_unit  = NULL;
    struct Gadget *gad_mode  = NULL;
    struct Gadget *gad_ip    = NULL;
    struct Gadget *gad_nm    = NULL;
    struct Gadget *gad_gw    = NULL;
    struct Gadget *gad_dns1  = NULL;
    struct Gadget *gad_dns2  = NULL;
    struct Gadget *gad_host  = NULL;
    struct Gadget *gad_mtu   = NULL;
    struct Gadget *gad_start = NULL;
    struct Gadget *gad_stop  = NULL;

    struct MsgPort     *timer_port = NULL;
    struct timerequest *timer_io   = NULL;
    BOOL                timer_active = FALSE;
    ULONG               timer_sig = 0;

    struct IntuiMessage *imsg = NULL;
    ULONG class;
    UWORD code;
    BOOL running = TRUE;
    TnPrefs prefs;
    CONST_STRPTR pubscreen_name = NULL;

    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 36);
    if (!DOSBase) return 20;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    if (!IntuitionBase) {
        CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    GadToolsBase = OpenLibrary((CONST_STRPTR)"gadtools.library", 36);
    if (!GadToolsBase) {
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        CloseLibrary((struct Library *)DOSBase);
        return 20;
    }

    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36);

    /* TNET-072: WBStartup and ToolTypes handling */
    if (IconBase != NULL) {
        struct DiskObject *dobj = NULL;
        BPTR old_dir = (BPTR)0;

        if (argc == 0 && argv != NULL) {
            struct WBStartup *wbmsg = (struct WBStartup *)argv;
            if (wbmsg->sm_NumArgs > 0 && wbmsg->sm_ArgList != NULL) {
                old_dir = CurrentDir(wbmsg->sm_ArgList[0].wa_Lock);
                dobj = GetDiskObject(wbmsg->sm_ArgList[0].wa_Name);
            }
        } else {
            dobj = GetDiskObject((CONST_STRPTR)"PROGDIR:TolunnetPrefs");
        }

        if (dobj != NULL) {
            if (dobj->do_ToolTypes != NULL) {
                STRPTR tt;
                if ((tt = (STRPTR)FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"PUBSCREEN")) != NULL) {
                    pubscreen_name = (CONST_STRPTR)tt;
                }
                if ((tt = (STRPTR)FindToolType((CONST_STRPTR *)dobj->do_ToolTypes, (CONST_STRPTR)"TOOLPRI")) != NULL) {
                    LONG pri = 0;
                    if (StrToLong((CONST_STRPTR)tt, &pri)) {
                        SetTaskPri(FindTask(NULL), (BYTE)pri);
                    }
                }
            }
            FreeDiskObject(dobj);
        }

        if (old_dir != (BPTR)0) {
            CurrentDir(old_dir);
        }
    }

    /* Load persistent preferences */
    tn_prefs_load(&prefs);

    if (pubscreen_name != NULL) {
        scr = LockPubScreen(pubscreen_name);
    }
    if (!scr) {
        scr = LockPubScreen(NULL);
    }
    if (!scr) goto cleanup;

    compute_layout(&lo, scr);

    vi = GetVisualInfo(scr, TAG_END);
    if (!vi) goto cleanup;

    /* Initialize GadTools gadget context */
    gad = CreateContext(&glist);
    if (!gad) goto cleanup;

    /* Base NewGadget defaults */
    ng.ng_TextAttr   = lo.font;
    ng.ng_VisualInfo = vi;
    ng.ng_UserData   = NULL;
    ng.ng_Height     = lo.gh;

    /* =========================================================================
     * GROUP 1: INTERFACE
     * ========================================================================= */

    /* Device Name */
    ng.ng_LeftEdge   = (WORD)lo.col1_x;
    ng.ng_TopEdge    = (WORD)lo.row_y[0];
    ng.ng_Width      = lo.col1_w;
    ng.ng_GadgetText = (STRPTR)"Device:";
    ng.ng_GadgetID   = GID_DEVICE;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_dev = CreateGadget(STRING_KIND, gad, &ng,
                           GTST_String, (ULONG)prefs.device,
                           GTST_MaxChars, 63,
                           TAG_END);
    if (!gad_dev) goto cleanup;

    /* Unit Number */
    ng.ng_LeftEdge   = (WORD)lo.col2_x;
    ng.ng_Width      = (UWORD)(6 * lo.fh / 2);
    ng.ng_GadgetText = (STRPTR)"Unit:";
    ng.ng_GadgetID   = GID_UNIT;
    gad_unit = CreateGadget(INTEGER_KIND, gad_dev, &ng,
                            GTIN_Number, prefs.unit,
                            GTIN_MaxChars, 5,
                            TAG_END);
    if (!gad_unit) goto cleanup;

    /* Addressing Mode Cycle */
    ng.ng_LeftEdge   = (WORD)lo.col1_x;
    ng.ng_TopEdge    = (WORD)lo.row_y[1];
    ng.ng_Width      = lo.col1_w;
    ng.ng_GadgetText = (STRPTR)"Config Mode:";
    ng.ng_GadgetID   = GID_MODE;
    gad_mode = CreateGadget(CYCLE_KIND, gad_unit, &ng,
                            GTCY_Labels, (ULONG)g_mode_labels,
                            GTCY_Active, prefs.use_dhcp ? 0 : 1,
                            TAG_END);
    if (!gad_mode) goto cleanup;

    /* MTU (TNET-063) */
    ng.ng_LeftEdge   = (WORD)lo.col2_x;
    ng.ng_Width      = (UWORD)(6 * lo.fh / 2);
    ng.ng_GadgetText = (STRPTR)"MTU:";
    ng.ng_GadgetID   = GID_MTU;
    gad_mtu = CreateGadget(INTEGER_KIND, gad_mode, &ng,
                           GTIN_Number, prefs.mtu,
                           GTIN_MaxChars, 5,
                           TAG_END);
    if (!gad_mtu) goto cleanup;

    /* =========================================================================
     * GROUP 2: ADDRESSING & IDENTITY
     * ========================================================================= */

    /* IP Address */
    ng.ng_LeftEdge   = (WORD)lo.col1_x;
    ng.ng_TopEdge    = (WORD)lo.row_y[2];
    ng.ng_Width      = lo.col1_w;
    ng.ng_GadgetText = (STRPTR)"IP Address:";
    ng.ng_GadgetID   = GID_IP;
    gad_ip = CreateGadget(STRING_KIND, gad_mtu, &ng,
                          GTST_String, (ULONG)prefs.ip_addr,
                          GTST_MaxChars, 19,
                          TAG_END);
    if (!gad_ip) goto cleanup;

    /* Subnet Mask */
    ng.ng_LeftEdge   = (WORD)lo.col2_x;
    ng.ng_Width      = lo.col2_w;
    ng.ng_GadgetText = (STRPTR)"Netmask:";
    ng.ng_GadgetID   = GID_NETMASK;
    gad_nm = CreateGadget(STRING_KIND, gad_ip, &ng,
                          GTST_String, (ULONG)prefs.netmask,
                          GTST_MaxChars, 19,
                          TAG_END);
    if (!gad_nm) goto cleanup;

    /* Default Gateway */
    ng.ng_LeftEdge   = (WORD)lo.col1_x;
    ng.ng_TopEdge    = (WORD)lo.row_y[3];
    ng.ng_Width      = lo.col1_w;
    ng.ng_GadgetText = (STRPTR)"Gateway:";
    ng.ng_GadgetID   = GID_GATEWAY;
    gad_gw = CreateGadget(STRING_KIND, gad_nm, &ng,
                          GTST_String, (ULONG)prefs.gateway,
                          GTST_MaxChars, 19,
                          TAG_END);
    if (!gad_gw) goto cleanup;

    /* Primary DNS */
    ng.ng_LeftEdge   = (WORD)lo.col2_x;
    ng.ng_Width      = lo.col2_w;
    ng.ng_GadgetText = (STRPTR)"DNS 1:";
    ng.ng_GadgetID   = GID_DNS1;
    gad_dns1 = CreateGadget(STRING_KIND, gad_gw, &ng,
                            GTST_String, (ULONG)prefs.dns_server,
                            GTST_MaxChars, 19,
                            TAG_END);
    if (!gad_dns1) goto cleanup;

    /* Secondary DNS (TNET-063: bound to TnPrefs, persisted as DNS2=) */
    ng.ng_LeftEdge   = (WORD)lo.col1_x;
    ng.ng_TopEdge    = (WORD)lo.row_y[4];
    ng.ng_Width      = lo.col1_w;
    ng.ng_GadgetText = (STRPTR)"DNS 2:";
    ng.ng_GadgetID   = GID_DNS2;
    gad_dns2 = CreateGadget(STRING_KIND, gad_dns1, &ng,
                            GTST_String, (ULONG)prefs.dns2,
                            GTST_MaxChars, 19,
                            TAG_END);
    if (!gad_dns2) goto cleanup;

    /* Host Name (TNET-063: bound to TnPrefs, DHCP option 12 + gethostname) */
    ng.ng_LeftEdge   = (WORD)lo.col2_x;
    ng.ng_Width      = lo.col2_w;
    ng.ng_GadgetText = (STRPTR)"Host Name:";
    ng.ng_GadgetID   = GID_HOSTNAME;
    gad_host = CreateGadget(STRING_KIND, gad_dns2, &ng,
                            GTST_String, (ULONG)prefs.hostname,
                            GTST_MaxChars, 63,
                            TAG_END);
    if (!gad_host) goto cleanup;

    /* =========================================================================
     * BUTTON BAR (TNET-064/065)
     * ========================================================================= */

    ng.ng_TopEdge    = (WORD)lo.btn_y;
    ng.ng_Height     = lo.btn_h;
    ng.ng_Flags      = PLACETEXT_IN;

    ng.ng_LeftEdge   = (WORD)(TN_BORDER_PAD);
    ng.ng_Width      = lo.btn_w;
    ng.ng_GadgetText = (STRPTR)"Save";
    ng.ng_GadgetID   = GID_SAVE;
    gad = CreateGadget(BUTTON_KIND, gad_host, &ng, TAG_END);
    if (!gad) goto cleanup;

    ng.ng_LeftEdge  += (WORD)(lo.btn_w + 8);
    ng.ng_GadgetText = (STRPTR)"Use";
    ng.ng_GadgetID   = GID_USE;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    if (!gad) goto cleanup;

    ng.ng_LeftEdge  += (WORD)(lo.btn_w + 8);
    ng.ng_GadgetText = (STRPTR)"Start";
    ng.ng_GadgetID   = GID_START;
    gad_start = CreateGadget(BUTTON_KIND, gad, &ng,
                             GA_Disabled, daemon_running() ? TRUE : FALSE,
                             TAG_END);
    if (!gad_start) goto cleanup;

    ng.ng_LeftEdge  += (WORD)(lo.btn_w + 8);
    ng.ng_GadgetText = (STRPTR)"Stop";
    ng.ng_GadgetID   = GID_STOP;
    gad_stop = CreateGadget(BUTTON_KIND, gad_start, &ng,
                            GA_Disabled, daemon_running() ? FALSE : TRUE,
                            TAG_END);
    if (!gad_stop) goto cleanup;

    ng.ng_LeftEdge  += (WORD)(lo.btn_w + 8);
    ng.ng_GadgetText = (STRPTR)"Ping";
    ng.ng_GadgetID   = GID_PING;
    gad = CreateGadget(BUTTON_KIND, gad_stop, &ng, TAG_END);
    if (!gad) goto cleanup;

    ng.ng_LeftEdge  += (WORD)(lo.btn_w + 8);
    ng.ng_GadgetText = (STRPTR)"Cancel";
    ng.ng_GadgetID   = GID_CANCEL;
    gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_END);
    if (!gad) goto cleanup;

    /* Open window: position/size computed from the screen (TNET-062) */
    win = OpenWindowTags(NULL,
                         WA_Left,          (ULONG)lo.win_left,
                         WA_Top,           (ULONG)lo.win_top,
                         WA_Width,         (ULONG)lo.win_w,
                         WA_Height,        (ULONG)lo.win_h,
                         WA_IDCMP,         IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                                           IDCMP_GADGETUP | IDCMP_VANILLAKEY,
                         WA_Flags,         WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                                           WFLG_ACTIVATE | WFLG_SMART_REFRESH,
                         WA_Gadgets,       (ULONG)glist,
                         WA_Title,         (ULONG)"tolunnet Network Preferences",
                         WA_PubScreen,     (ULONG)scr,
                         TAG_END);

    if (!win) goto cleanup;

    /* Set up 1-second non-blocking timer for polling daemon state (TNET-079) */
    timer_port = CreateMsgPort();
    if (timer_port != NULL) {
        timer_io = (struct timerequest *)CreateIORequest(timer_port, sizeof(struct timerequest));
        if (timer_io != NULL) {
            if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)timer_io, 0) == 0) {
                timer_sig = 1UL << timer_port->mp_SigBit;
                timer_io->tr_node.io_Command = TR_ADDREQUEST;
                timer_io->tr_time.tv_secs = 1;
                timer_io->tr_time.tv_micro = 0;
                SendIO((struct IORequest *)timer_io);
                timer_active = TRUE;
            }
        }
    }

    update_daemon_buttons(win, gad_start, gad_stop);

    GT_RefreshWindow(win, NULL);
    render_gui_frames(win, vi, &lo);

    /* Event Message Loop (TNET-079: non-blocking timer in Wait mask) */
    while (running) {
        ULONG sigs = Wait((1UL << win->UserPort->mp_SigBit) | timer_sig);

        if (timer_active && (sigs & timer_sig)) {
            WaitIO((struct IORequest *)timer_io);
            update_daemon_buttons(win, gad_start, gad_stop);
            timer_io->tr_node.io_Command = TR_ADDREQUEST;
            timer_io->tr_time.tv_secs = 1;
            timer_io->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)timer_io);
        }

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
                            LONG mtu;

                            prefs.unit = (ULONG)gad_get_int(gad_unit, (LONG)prefs.unit);

                            gad_get_str(gad_dev, prefs.device, sizeof(prefs.device));
                            gad_get_str(gad_ip, prefs.ip_addr, sizeof(prefs.ip_addr));
                            gad_get_str(gad_nm, prefs.netmask, sizeof(prefs.netmask));
                            gad_get_str(gad_gw, prefs.gateway, sizeof(prefs.gateway));
                            gad_get_str(gad_dns1, prefs.dns_server, sizeof(prefs.dns_server));
                            gad_get_str(gad_dns2, prefs.dns2, sizeof(prefs.dns2));       /* TNET-063 */
                            gad_get_str(gad_host, prefs.hostname, sizeof(prefs.hostname));/* TNET-063 */

                            /* TNET-063: MTU key - 0 keeps the driver default;
                             * out-of-range values fall back to 0 */
                            mtu = gad_get_int(gad_mtu, 0);
                            if (mtu >= 576 && mtu <= 1500) {
                                prefs.mtu = (ULONG)mtu;
                            } else {
                                prefs.mtu = 0;
                            }

                            /* TNET-064: Use = ENV: only; Save = ENV:+ENVARC:+DEVS: */
                            tn_prefs_save(&prefs,
                                          (g->GadgetID == GID_SAVE) ? TN_PREFS_SAVE : TN_PREFS_USE);
                            after_save_use(win, gad_start, gad_stop);

                            if (g->GadgetID == GID_SAVE) {
                                running = FALSE;
                            }
                        }
                        break;

                    case GID_PING:
                        {
                            char ping_cmd[192];
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

                    case GID_START:
                        daemon_start();
                        update_daemon_buttons(win, gad_start, gad_stop);
                        break;

                    case GID_STOP:
                        daemon_stop(win);
                        update_daemon_buttons(win, gad_start, gad_stop);
                        break;

                    case GID_CANCEL:
                        running = FALSE;
                        break;
                    }
                }
            } else if (class == IDCMP_CLOSEWINDOW) {
                running = FALSE;
            } else if (class == IDCMP_VANILLAKEY) {
                if (code == 27) running = FALSE;   /* ESC = Cancel */
            } else if (class == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
                render_gui_frames(win, vi, &lo);
            }

            GT_ReplyIMsg(imsg);
        }
    }

cleanup:
    if (timer_active && timer_io != NULL) {
        if (!CheckIO((struct IORequest *)timer_io)) {
            AbortIO((struct IORequest *)timer_io);
            WaitIO((struct IORequest *)timer_io);
        }
        CloseDevice((struct IORequest *)timer_io);
    }
    if (timer_io != NULL) DeleteIORequest((struct IORequest *)timer_io);
    if (timer_port != NULL) DeleteMsgPort(timer_port);

    if (win) CloseWindow(win);
    if (glist) FreeGadgets(glist);
    if (vi) FreeVisualInfo(vi);
    if (scr) UnlockPubScreen(NULL, scr);

    if (IconBase) CloseLibrary(IconBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);

    return 0;
}
