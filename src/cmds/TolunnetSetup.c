/*
 * tolunnet — First-Run Network Setup Wizard (TolunnetSetup), UI v2
 *
 * TNET-110 part 1 — v2 window chrome on a font-derived layout engine:
 *   • left step rail (110 px): done / active (FILLPEN box) / pending
 *   • right recessed pane with group titles, status line + 1 s timer tick
 *   • Cancel left, < Back / Next > right (Next→Finish on the last page)
 *   • screen font by default, FONT=name/size CLI arg or ToolType
 *     (diskfont.library), topaz/8 fallback; NTSC compact when < 240 lines
 * Page internals and the ARexx port (PAGE/SELECT/NEXT/FINISH) are carried
 * over unchanged from v1 — the per-page LISTVIEW rewrite is part 2.
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
#include <stdarg.h>

#ifdef __AMIGA__
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/diskfont.h>
#include <proto/icon.h>
#include <proto/asl.h>

#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <libraries/diskfont.h>
#include <workbench/startup.h>
#include <workbench/icon.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <intuition/screens.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/execbase.h>

extern struct ExecBase      *SysBase;
extern struct DosLibrary    *DOSBase;
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
struct Library       *GadToolsBase  = NULL;
struct Library       *DiskfontBase  = NULL;
struct Library       *IconBase      = NULL;
struct Library       *AslBase       = NULL;
unsigned long        __stack        = 32768;

/* Gadget IDs */
#define GID_BTN_BACK        101
#define GID_BTN_NEXT        102
#define GID_BTN_CANCEL      103
#define GID_STATUS_TX       104

/* Page 1: Replace */
#define GID_P1_REPLACE_CHK  110
#define GID_P1_IMPORT_CHK   112

/* Page 2: Hardware */
#define GID_P2_HW_CYCLE     120
#define GID_P2_SCAN_BTN     121
#define GID_P2_LIST         122
#define GID_P2_TEST_BTN     123

/* Page 3: WiFi */
#define GID_P3_WIFI_LIST    130
#define GID_P3_RESCAN_BTN   131
#define GID_P3_PASS_STR     132
#define GID_P3_SHOWPASS_CHK 133
#define GID_P3_NETLIST      134
#define GID_P3_SSID_STR     135
#define GID_P3_ADD_HIDDEN_BTN 136
#define GID_P3_FORGET_BTN     137

/* Page 4: Address */
#define GID_P4_IPMODE_CYCLE 140
#define GID_P4_IP_STR       141
#define GID_P4_NM_STR       142
#define GID_P4_GW_STR       143
#define GID_P4_DNS1_STR     144
#define GID_P4_DNS2_STR     145
#define GID_P4_HOST_STR     146
#define GID_P4_ROADSHOW_CHK 147
#define GID_P4_MTU_STR      148
#define GID_P4_DOMAIN_STR   149
#define GID_P4_ADVANCED_BTN 160

/* Page 5: Test */
#define GID_P5_TEST_BTN     150
#define GID_P5_BOOT_CHK     151
#define GID_P5_CHECKLIST    152
#define GID_P5_SAVELOG_BTN  153
#define GID_P5_PREFS_CHK    154

/* Sibling Advanced Window Gadgets */
#define GID_ADV_PRIO_STR         201
#define GID_ADV_LOG_STR          202
#define GID_ADV_DB_STR           203
#define GID_ADV_ROADSHOW_CHK     204
#define GID_ADV_DNS2FALLBACK_CHK 205
#define GID_ADV_OK_BTN           206
#define GID_ADV_CANCEL_BTN       207


static const STRPTR g_page_names[] = {
    (STRPTR)"1. Replace Stacks",
    (STRPTR)"2. Hardware",
    (STRPTR)"3. WiFi Setup",
    (STRPTR)"4. IP Address",
    (STRPTR)"5. Test & Finish",
    NULL
};

static const char *g_page_titles[] = {
    "Replace legacy stacks",
    "Network hardware",
    "Wireless network",
    "IP address",
    "Test & finish",
};

static const STRPTR g_ipmode_names[] = {
    (STRPTR)"Automatic (DHCP)",
    (STRPTR)"Manual (Static IP)",
    NULL
};

/* ----------------------------------------------------- v2 layout engine */

typedef struct UiMetrics {
    LONG fx, fy;        /* font cell size */
    LONG pitch;         /* row pitch: fy + 6 (compact: 14 min) */
    LONG win_w, win_h;
    LONG rail_w;        /* left step rail width */
    LONG pane_l, pane_t, pane_w, pane_h;
    LONG status_t;      /* status line top (window-relative) */
    LONG btn_t, btn_h;
    BOOL compact;
    LONG pen_text, pen_fill, pen_filltext, pen_bg, pen_shine, pen_shadow;
} UiMetrics;

static UiMetrics g_m;
static struct TextAttr g_gui_font = { (STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
static struct TextFont *g_font = NULL;

static WizardState g_ws;
static struct Window *g_win = NULL;
static struct Gadget *g_nav_glist = NULL;
static struct Gadget *g_page_glist = NULL;
static APTR g_vi = NULL;
static struct DrawInfo *g_dri = NULL;

static struct Gadget *g_gad_next = NULL;
static struct Gadget *g_gad_back = NULL;
static struct Gadget *g_gad_cancel = NULL;
static struct Gadget *g_gad_status = NULL;
static struct Gadget *g_gad_listview = NULL;

static struct Window *g_adv_win = NULL;
static struct Gadget *g_adv_glist = NULL;

static char g_adv_prio_buf[16];
static char g_adv_log_buf[64];
static char g_adv_db_buf[40];
static BOOL g_adv_write_roadshow = FALSE;
static BOOL g_adv_dhcp_fallback  = FALSE;

#ifndef NewList
#define NewList(l) do { \
    (l)->lh_Head = (struct Node *)&(l)->lh_Tail; \
    (l)->lh_Tail = NULL; \
    (l)->lh_TailPred = (struct Node *)&(l)->lh_Head; \
} while (0)
#endif

static struct List g_hw_list;
static struct Node g_hw_nodes[MAX_DETECTED_HW + 1];
static char        g_hw_lines[MAX_DETECTED_HW + 1][96];

static struct List g_stack_list;
static struct Node g_stack_nodes[MAX_DETECTED_STACKS + 1];
static char        g_stack_lines[MAX_DETECTED_STACKS + 1][96];

static struct List g_wifi_list;
static struct Node g_wifi_nodes[MAX_WIFI_NETWORKS + 1];
static char        g_wifi_lines[MAX_WIFI_NETWORKS + 1][96];

static struct List g_check_list;
static struct Node g_check_nodes[6];
static char        g_check_lines[6][128];

static char g_status_text[96] = "Ready.";
static char g_scr_title[64];


/* 1 s status tick (TNET-110): async progress without Delay() */
static struct MsgPort  *g_tick_port = NULL;
static struct timerequest *g_tick_io = NULL;
static ULONG g_tick_sig = 0;
static ULONG g_last_secs = 0;   /* seconds counter for elapsed displays */

/* FONT=name/size — override chain (TNET-110): CLI argument, then the WB
 * icon ToolType, then the FONT= key in DEVS:tolunnet.config; with no
 * override the wizard follows the screen font (derive_metrics). */
static BOOL g_font_spec_set = FALSE;   /* TRUE = g_gui_font carries a spec */

static BOOL font_from_config(char *spec, size_t speclen)
{
    BPTR fh = Open((CONST_STRPTR)"DEVS:tolunnet.config", MODE_OLDFILE);
    if (!fh) return FALSE;

    char line[160];
    while (FGets(fh, (STRPTR)line, (LONG)sizeof(line))) {
        if (strncmp(line, "FONT=", 5) == 0) {
            size_t n = strlen(line);
            while (n > 5 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
            strncpy(spec, line + 5, speclen - 1);
            spec[speclen - 1] = '\0';
            Close(fh);
            return TRUE;
        }
    }
    Close(fh);
    return FALSE;
}

/* Owner-side diagnostics (2026-09-11): phase progress to the console.
 * Harmless without one (Run <>NIL:), decisive with one: the last printed
 * phase in a crash photo pinpoints the failing step. */
static void wlog(const char *phase)
{
    if (DOSBase == NULL) return;
    if (Output() != (BPTR)0) {
        PutStr((CONST_STRPTR)"TolunnetSetup: ");
        PutStr((CONST_STRPTR)phase);
        PutStr((CONST_STRPTR)"\n");
    }
}

static void setup_font(int argc, char **argv)
{
    char spec[64] = "";
    const char *src = NULL;
    int i;

    for (i = 1; argv && argv[i]; i++) {
        if (strncmp(argv[i], "FONT=", 5) == 0) {
            src = argv[i] + 5;
            break;
        }
    }

    /* Workbench launch: FONT= ToolType of our own icon */
    if (src == NULL && argc == 0) {
        struct WBStartup *wbmsg = (struct WBStartup *)argv;
        if (wbmsg && wbmsg->sm_NumArgs > 0 && wbmsg->sm_ArgList &&
            wbmsg->sm_ArgList[0].wa_Name && wbmsg->sm_ArgList[0].wa_Name[0]) {
            if (IconBase == NULL) {
                IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36);
            }
            if (IconBase) {
                struct DiskObject *dobj = GetDiskObject(
                    (CONST_STRPTR)wbmsg->sm_ArgList[0].wa_Name);
                if (dobj) {
                    UBYTE *tt = (UBYTE *)FindToolType(
                        (CONST_STRPTR *)dobj->do_ToolTypes,
                        (CONST_STRPTR)"FONT");
                    if (tt) {
                        strncpy(spec, (const char *)tt, sizeof(spec) - 1);
                        spec[sizeof(spec) - 1] = '\0';
                        src = spec;
                    }
                    FreeDiskObject(dobj);
                }
            }
        }
    }

    /* DEVS:tolunnet.config FONT= key (written by TolunnetPrefs) */
    if (src == NULL) {
        if (font_from_config(spec, sizeof(spec)) && spec[0]) {
            src = spec;
        }
    }

    if (src != NULL && src[0]) {
        static char fname[32];
        char *slash = strchr(src, '/');
        ULONG size = 8;
        if (slash) {
            *slash = '\0';
            size = (ULONG)atol(slash + 1);
            if (size < 6 || size > 24) size = 8;
        }
        strncpy(fname, src, sizeof(fname) - 6);
        fname[sizeof(fname) - 6] = '\0';
        strcat(fname, ".font");
        g_gui_font.ta_Name = (STRPTR)fname;
        g_gui_font.ta_YSize = size;
        g_gui_font.ta_Style = FS_NORMAL;
        g_gui_font.ta_Flags = FPF_DISKFONT;
        g_font_spec_set = TRUE;
    }
}

static void derive_metrics(struct Screen *scr)
{
    memset(&g_m, 0, sizeof(g_m));
    g_m.compact = (scr->Height < 240);

    if (g_font_spec_set) {
        g_font = OpenDiskFont(&g_gui_font);
    } else {
        /* default: follow the screen font (TNET-110); it is already open */
        if (scr->Font && scr->Font->ta_Name) {
            g_font = OpenFont(scr->Font);
        }
    }
    if (g_font == NULL) {
        g_gui_font.ta_Name = (STRPTR)"topaz.font";
        g_gui_font.ta_YSize = 8;
        g_gui_font.ta_Style = FS_NORMAL;
        g_gui_font.ta_Flags = FPF_ROMFONT;
        g_font = OpenFont(&g_gui_font);
    }
    /* if even topaz/8 failed, metrics stay at the 8x8 default and the
     * window font is used for rendering */

    if (g_font != NULL) {
        g_m.fx = g_font->tf_XSize;
        g_m.fy = g_font->tf_YSize;
    } else {
        g_m.fx = 8;
        g_m.fy = 8;
    }

    /* readability floor: pitch >= fy + 6, compact >= 14 */
    g_m.pitch = g_m.fy + 6;
    if (g_m.compact && g_m.pitch < 14) g_m.pitch = 14;

    g_m.win_w = 632;
    g_m.win_h = g_m.compact ? 176 : 240;
    if (g_m.win_h > scr->Height - 4) g_m.win_h = scr->Height - 4;
    if (g_m.win_w > scr->Width - 4) g_m.win_w = scr->Width - 4;

    g_m.rail_w = 110;
    g_m.pane_l = g_m.rail_w + 8;
    g_m.pane_w = g_m.win_w - g_m.pane_l - 8;
    g_m.btn_h = g_m.fy + 6;

    /* chrome heights approximated from the window border data */
    {
        LONG border_t = scr->WBorTop + scr->Font->ta_YSize + 1;
        LONG border_b = scr->WBorBottom + 2;
        LONG title_h = border_t + 1;
        g_m.pane_t = title_h + (g_m.compact ? 3 : 6);
        g_m.btn_t = g_m.win_h - border_b - g_m.btn_h - 3;
        g_m.status_t = g_m.btn_t - g_m.fy - 6;
        g_m.pane_h = g_m.status_t - g_m.pane_t - 4;
    }


    if (g_dri) {
        g_m.pen_text = g_dri->dri_Pens[TEXTPEN];
        g_m.pen_fill = g_dri->dri_Pens[FILLPEN];
        g_m.pen_filltext = g_dri->dri_Pens[FILLTEXTPEN];
        g_m.pen_bg = g_dri->dri_Pens[BACKGROUNDPEN];
        g_m.pen_shine = g_dri->dri_Pens[SHINEPEN];
        g_m.pen_shadow = g_dri->dri_Pens[SHADOWPEN];
    }
}

static void set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status_text, sizeof(g_status_text), fmt, ap);
    va_end(ap);
    if (g_gad_status && g_win) {
        GT_SetGadgetAttrs(g_gad_status, g_win, NULL,
                          GTTX_Text, (ULONG)g_status_text,
                          TAG_END);
    }
}

/* v2 step rail: done / active / pending (TNET-110). The done marker is a
 * small filled block rather than a font glyph — topaz has no checkmark at
 * a portable code point. */
static void render_rail(void)
{
    struct RastPort *rp;
    int i;
    if (!g_win) return;
    rp = g_win->RPort;

    SetAPen(rp, g_m.pen_bg);
    RectFill(rp, 4, g_m.pane_t, g_m.rail_w - 4, g_m.status_t - 2);
    SetFont(rp, g_font ? g_font : g_win->RPort->Font);

    for (i = 0; i < WIZARD_PAGE_COUNT; i++) {
        LONG y = g_m.pane_t + 4 + i * g_m.pitch;
        const char *name = (const char *)g_page_names[i];
        LONG len = (LONG)strlen(name);

        if (i == g_ws.current_page) {
            SetAPen(rp, g_m.pen_fill);
            RectFill(rp, 6, y - 2, g_m.rail_w - 8, y + g_m.fy);
            SetAPen(rp, g_m.pen_filltext);
            Move(rp, 10, y + g_m.fy - 1);
            Text(rp, (CONST_STRPTR)name, (WORD)len);
        } else {
            if (i < g_ws.current_page) {
                /* done: filled marker + text */
                SetAPen(rp, g_m.pen_text);
                RectFill(rp, 10, y + g_m.fy - 3, 15, y + g_m.fy - 1);
            } else {
                SetAPen(rp, g_m.pen_text);
            }
            Move(rp, 20, y + g_m.fy - 1);
            Text(rp, (CONST_STRPTR)name, (WORD)len);
        }
    }
}

/* pane frame + bold group title on the top edge */
static void render_pane_frame(void)
{
    struct RastPort *rp;
    char title[48];
    if (!g_win) return;
    rp = g_win->RPort;

    /* Step SEC item 1: clear title row along top frame edge before drawing title */
    SetAPen(rp, g_m.pen_bg);
    RectFill(rp, g_m.pane_l + 8, g_m.pane_t - g_m.fy / 2 - 2,
             g_m.pane_l + g_m.pane_w - 8, g_m.pane_t + g_m.fy / 2 + 2);

    /* recessed bevel: shadow top/left, shine bottom/right */
    SetAPen(rp, g_m.pen_shadow);
    RectFill(rp, g_m.pane_l, g_m.pane_t,
             g_m.pane_l + g_m.pane_w - 1, g_m.pane_t);
    RectFill(rp, g_m.pane_l, g_m.pane_t,
             g_m.pane_l, g_m.pane_t + g_m.pane_h - 1);
    SetAPen(rp, g_m.pen_shine);
    RectFill(rp, g_m.pane_l, g_m.pane_t + g_m.pane_h - 1,
             g_m.pane_l + g_m.pane_w - 1, g_m.pane_t + g_m.pane_h - 1);
    RectFill(rp, g_m.pane_l + g_m.pane_w - 1, g_m.pane_t,
             g_m.pane_l + g_m.pane_w - 1, g_m.pane_t + g_m.pane_h - 1);

    snprintf(title, sizeof(title), " %s ", g_page_titles[g_ws.current_page]);
    {
        LONG tw = TextLength(rp, (CONST_STRPTR)title, (WORD)strlen(title));
        LONG tx = g_m.pane_l + 14;
        SetAPen(rp, g_m.pen_bg);
        RectFill(rp, tx - 2, g_m.pane_t - g_m.fy / 2 - 1,
                 tx + tw + 2, g_m.pane_t + g_m.fy / 2);
        SetAPen(rp, g_m.pen_text);
        SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
        Move(rp, tx, g_m.pane_t + 2);
        Text(rp, (CONST_STRPTR)title, (WORD)strlen(title));
        SetSoftStyle(rp, FS_NORMAL, AskSoftStyle(rp));
    }
}

static void render_frames(void)
{
    if (!g_win) return;
    render_rail();
    render_pane_frame();
}

/* n / 5 progress in the screen title bar */
static void update_screen_title(void)
{
    snprintf(g_scr_title, sizeof(g_scr_title), "tolunnet Network Setup 1.2 - %d / 5",
             g_ws.current_page + 1);
    if (g_win) {
        SetWindowTitles(g_win, (CONST_STRPTR)"Network Setup", (CONST_STRPTR)g_scr_title);
    }
}


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
                case GID_P4_DNS2_STR:
                    strncpy(g_ws.dns2_str, (const char *)si->Buffer, sizeof(g_ws.dns2_str) - 1);
                    break;
                case GID_P4_MTU_STR:
                    strncpy(g_ws.mtu_str, (const char *)si->Buffer, sizeof(g_ws.mtu_str) - 1);
                    break;
                case GID_P4_HOST_STR:
                    strncpy(g_ws.host_str, (const char *)si->Buffer, sizeof(g_ws.host_str) - 1);
                    break;
                case GID_P4_DOMAIN_STR:
                    strncpy(g_ws.domain_str, (const char *)si->Buffer, sizeof(g_ws.domain_str) - 1);
                    break;
                case GID_P3_SSID_STR:
                    strncpy(g_ws.wifi_ssid_str, (const char *)si->Buffer, sizeof(g_ws.wifi_ssid_str) - 1);
                    break;
                }

            }
        }
        g = g->NextGadget;
    }
}

static void update_nav_buttons(void)
{
    if (!g_win || !g_gad_next || !g_gad_back) return;

    GT_SetGadgetAttrs(g_gad_back, g_win, NULL,
                      GA_Disabled, (g_ws.current_page == 0),
                      TAG_END);

    /* Next becomes Finish on the last page */
    GT_SetGadgetAttrs(g_gad_next, g_win, NULL,
                      GA_Text, (ULONG)((g_ws.current_page == WIZARD_PAGE_TEST)
                                       ? "_Finish" : "_Next >"),
                      TAG_END);
}

/* TNET-110: validate the Address page on Next — bad field names itself in
 * an EasyRequest and gets the focus afterwards. */
static BOOL validate_address_page(void)
{
    static const char *labels[4] = { "IP address", "Netmask", "Gateway", "DNS server" };
    const char *vals[4];
    vals[0] = g_ws.ip_str;
    vals[1] = g_ws.nm_str;
    vals[2] = g_ws.gw_str;
    vals[3] = g_ws.dns1_str;

    if (g_ws.ip_mode == 1) {
        int i;
        for (i = 0; i < 4; i++) {
            uint32_t addr;
            if (tn_inet_addr_parse_ex(vals[i], &addr) != 1) {
                char msg[96];
                struct EasyStruct es = {
                    sizeof(struct EasyStruct), 0,
                    (STRPTR)"Network Setup", (STRPTR)"", (STRPTR)"OK" };
                snprintf(msg, sizeof(msg),
                         "The %s is not a valid dotted-quad\naddress: \"%s\"",
                         labels[i], vals[i]);
                es.es_TextFormat = (STRPTR)msg;
                EasyRequestArgs(g_win, &es, NULL, NULL);
                return FALSE;
            }
        }
    }
    if (g_ws.mtu_str[0] != '\0') {
        long mtu = atol(g_ws.mtu_str);
        if (mtu < 576 || mtu > 1500) {
            struct EasyStruct es = {
                sizeof(struct EasyStruct), 0,
                (STRPTR)"Network Setup",
                (STRPTR)"MTU must be between 576 and 1500,\nor empty for the driver default",
                (STRPTR)"OK" };
            EasyRequestArgs(g_win, &es, NULL, NULL);
            return FALSE;
        }
    }
    return TRUE;
}

/* ---------------- TNET-110 part 3: Advanced sibling window + Save log --- */

static void update_nav_buttons(void);

/* While the Advanced window is open the main navigation is frozen */
static void set_nav_lock(BOOL lock)
{
    if (!g_win) return;
    if (g_gad_cancel) {
        GT_SetGadgetAttrs(g_gad_cancel, g_win, NULL,
                          GA_Disabled, lock, TAG_END);
    }
    if (g_gad_next) {
        GT_SetGadgetAttrs(g_gad_next, g_win, NULL,
                          GA_Disabled, lock, TAG_END);
    }
    if (g_gad_back) {
        GT_SetGadgetAttrs(g_gad_back, g_win, NULL,
                          GA_Disabled, lock || (g_ws.current_page == 0),
                          TAG_END);
    }
}

static void open_advanced_window(void)
{
    if (g_adv_win || !g_win) return;

    snprintf(g_adv_prio_buf, sizeof(g_adv_prio_buf), "%ld",
             (long)(g_ws.task_priority ? g_ws.task_priority : 5));
    strncpy(g_adv_log_buf, g_ws.log_file, sizeof(g_adv_log_buf) - 1);
    g_adv_log_buf[sizeof(g_adv_log_buf) - 1] = '\0';
    strncpy(g_adv_db_buf, g_ws.database_order, sizeof(g_adv_db_buf) - 1);
    g_adv_db_buf[sizeof(g_adv_db_buf) - 1] = '\0';
    g_adv_write_roadshow = g_ws.write_roadshow_internet;
    g_adv_dhcp_fallback  = g_ws.dhcp_fallback_dns2;

    struct NewGadget ng;
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = g_vi;
    ng.ng_TextAttr   = &g_gui_font;

    struct Gadget *prev = CreateContext(&g_adv_glist);
    if (!prev) return;

    const LONG labw = 14 * g_m.fx;             /* "DATABASE_ORDER:" */
    const LONG strw = 24 * g_m.fx;
    const char *chk_roadshow_lbl = "Also write Roadshow-style DEVS:Internet files";
    const char *chk_fallback_lbl = "Use DHCP DNS, fall back to DNS 2";
    LONG chk_w = 10 + 26 + 6 + (LONG)strlen(chk_roadshow_lbl) * g_m.fx + 10;
    {
        LONG alt = 10 + 26 + 6 + (LONG)strlen(chk_fallback_lbl) * g_m.fx + 10;
        if (alt > chk_w) chk_w = alt;
    }
    const LONG win_w = (10 + labw + 6 + strw + 10 > chk_w)
                       ? 10 + labw + 6 + strw + 10 : chk_w;
    LONG y = 10;

    ng.ng_LeftEdge   = 10 + labw;
    ng.ng_Width      = strw;
    ng.ng_Height     = g_m.fy + 8;
    ng.ng_Flags      = PLACETEXT_LEFT;

    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = (STRPTR)"Priority:";
    ng.ng_GadgetID   = GID_ADV_PRIO_STR;
    prev = CreateGadget(STRING_KIND, prev, &ng,
                        GTST_String, (ULONG)g_adv_prio_buf,
                        GTST_MaxChars, (LONG)sizeof(g_adv_prio_buf) - 1,
                        GA_TabCycle, TRUE,
                        TAG_END);
    y += g_m.pitch;

    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = (STRPTR)"Log file:";
    ng.ng_GadgetID   = GID_ADV_LOG_STR;
    prev = CreateGadget(STRING_KIND, prev, &ng,
                        GTST_String, (ULONG)g_adv_log_buf,
                        GTST_MaxChars, (LONG)sizeof(g_adv_log_buf) - 1,
                        GA_TabCycle, TRUE,
                        TAG_END);
    y += g_m.pitch;

    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = (STRPTR)"DATABASE_ORDER:";
    ng.ng_GadgetID   = GID_ADV_DB_STR;
    prev = CreateGadget(STRING_KIND, prev, &ng,
                        GTST_String, (ULONG)g_adv_db_buf,
                        GTST_MaxChars, (LONG)sizeof(g_adv_db_buf) - 1,
                        GA_TabCycle, TRUE,
                        TAG_END);
    y += g_m.pitch;

    ng.ng_LeftEdge   = 10;
    ng.ng_Width      = 26;
    ng.ng_Height     = g_m.fy + 6;
    ng.ng_Flags      = PLACETEXT_RIGHT;

    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = (STRPTR)"Also write Roadshow-style DEVS:_Internet files";
    ng.ng_GadgetID   = GID_ADV_ROADSHOW_CHK;
    prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                        GTCB_Checked, g_adv_write_roadshow,
                        TAG_END);
    y += g_m.pitch;

    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = (STRPTR)"Use DHCP DNS, fall back to _DNS 2";
    ng.ng_GadgetID   = GID_ADV_DNS2FALLBACK_CHK;
    prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                        GTCB_Checked, g_adv_dhcp_fallback,
                        TAG_END);
    y += g_m.pitch + 4;

    /* OK / Cancel, bottom right */
    {
        LONG bw = 10 * g_m.fx + 8;
        ng.ng_Flags      = PLACETEXT_IN;
        ng.ng_Width      = bw;
        ng.ng_Height     = g_m.btn_h;
        ng.ng_LeftEdge   = win_w - 10 - 2 * bw - 8;
        ng.ng_TopEdge    = y;
        ng.ng_GadgetText = (STRPTR)"_OK";
        ng.ng_GadgetID   = GID_ADV_OK_BTN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_', TAG_END);

        ng.ng_LeftEdge   = win_w - 10 - bw;
        ng.ng_GadgetText = (STRPTR)"_Cancel";
        ng.ng_GadgetID   = GID_ADV_CANCEL_BTN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_', TAG_END);
        y += g_m.btn_h;
    }

    const LONG win_h = y + 10;

    g_adv_win = OpenWindowTags(NULL,
                               WA_Left,         g_win->LeftEdge + 40,
                               WA_Top,          g_win->TopEdge + 40,
                               WA_Width,        win_w,
                               WA_Height,       win_h,
                               WA_IDCMP,        IDCMP_CLOSEWINDOW | IDCMP_GADGETUP |
                                                IDCMP_RAWKEY,
                               WA_Flags,        WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                                                WFLG_CLOSEGADGET | WFLG_SMART_REFRESH |
                                                WFLG_ACTIVATE,
                               WA_Title,        (ULONG)"Network Setup - Advanced",
                               WA_Gadgets,      (ULONG)g_adv_glist,
                               WA_PubScreen,    (ULONG)g_win->WScreen,
                               TAG_END);
    if (!g_adv_win) {
        FreeGadgets(g_adv_glist);
        g_adv_glist = NULL;
        return;
    }

    set_nav_lock(TRUE);
    set_status("Advanced: OK keeps the values, Cancel discards them");
}

static void trim_tail(char *s)
{
    size_t n = s ? strlen(s) : 0;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static void close_advanced_window(BOOL commit)
{
    if (!g_adv_win) return;

    if (commit) {
        long p = atol(g_adv_prio_buf);
        if (g_adv_prio_buf[0] == '\0') p = 5;
        if (p < -128) p = -128;
        if (p > 127)  p = 127;
        g_ws.task_priority = (LONG)p;
        trim_tail(g_adv_log_buf);
        strncpy(g_ws.log_file, g_adv_log_buf, sizeof(g_ws.log_file) - 1);
        g_ws.log_file[sizeof(g_ws.log_file) - 1] = '\0';
        trim_tail(g_adv_db_buf);
        strncpy(g_ws.database_order, g_adv_db_buf, sizeof(g_ws.database_order) - 1);
        g_ws.database_order[sizeof(g_ws.database_order) - 1] = '\0';
        g_ws.write_roadshow_internet = g_adv_write_roadshow;
        g_ws.dhcp_fallback_dns2      = g_adv_dhcp_fallback;
        set_status("Advanced settings kept - written at Finish");
    } else {
        set_status("Advanced settings discarded");
    }

    if (g_adv_glist) {
        RemoveGList(g_adv_win, g_adv_glist, -1);
        FreeGadgets(g_adv_glist);
        g_adv_glist = NULL;
    }
    CloseWindow(g_adv_win);
    g_adv_win = NULL;
    set_nav_lock(FALSE);
    update_nav_buttons();
}

static BOOL all_tests_passed(void)
{
    return g_ws.test_daemon_ok == 1 && g_ws.test_dhcp_ok == 1 &&
           g_ws.test_ping_ok   == 1 && g_ws.test_dns_ok  == 1 &&
           g_ws.test_http_ok   == 1;
}

static void run_page_tests(void)
{
    set_status("Running network tests...");
    tn_run_network_tests(&g_ws);
    set_status(all_tests_passed() ? "All 5 tests passed"
                                  : "Tests complete - see the list below");
    rebuild_page_gadgets();
}

/* "Save log…" — ASL save requester; without asl.library (or after Cancel)
 * the report still lands in RAM:tolunnet-setup.log and the path is shown
 * in an EasyRequest (TN-decision W4p3 #3: never crash, never skip silently) */
static void save_test_log(void)
{
    char path[128];
    char report[1024];
    int off = 0;
    int i;
    BPTR fh;
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0,
        (STRPTR)"Network Setup", (STRPTR)"", (STRPTR)"OK" };

    static const char *names[5] = { "Start stack", "DHCP lease", "Ping gateway",
                                    "DNS lookup", "HTTP HEAD" };
    const int st[5] = { g_ws.test_daemon_ok, g_ws.test_dhcp_ok, g_ws.test_ping_ok,
                        g_ws.test_dns_ok, g_ws.test_http_ok };

    strcpy(path, "RAM:tolunnet-setup.log");
    if (AslBase) {
        struct FileRequester *fr = (struct FileRequester *)AllocAslRequestTags(
            ASL_FileRequest,
            ASLFR_DoSaveMode,   TRUE,
            ASLFR_TitleText,    (ULONG)"Save tolunnet setup log",
            ASLFR_InitialFile,  (ULONG)"tolunnet-setup.log",
            TAG_END);
        if (fr) {
            if (AslRequest(fr, NULL)) {
                const char *drawer = (const char *)fr->fr_Drawer;
                const char *file   = (const char *)fr->fr_File;
                size_t dlen = drawer ? strlen(drawer) : 0;
                size_t flen = file ? strlen(file) : 0;
                if (dlen + flen + 2 < sizeof(path)) {
                    if (dlen) {
                        memcpy(path, drawer, dlen);
                        if (path[dlen - 1] != ':' && path[dlen - 1] != '/') {
                            path[dlen++] = '/';
                        }
                    }
                    if (flen == 0) {
                        file = "tolunnet-setup.log";
                        flen = strlen(file);
                    }
                    memcpy(path + dlen, file, flen);
                    path[dlen + flen] = '\0';
                }
            }
            FreeAslRequest(fr);
        }
    }

    off += snprintf(report + off, sizeof(report) - off,
                    "tolunnet Network Setup 1.2 - test log\r\n"
                    "----------------------------------\r\n");
    for (i = 0; i < 5; i++) {
        off += snprintf(report + off, sizeof(report) - off,
                        "%-14s %-6s %s%s%s\r\n",
                        names[i],
                        (st[i] == 1) ? "OK" : (st[i] == 0) ? "FAILED" : "..",
                        g_ws.test_details[i],
                        (st[i] != 1 && g_ws.test_advice[i][0]) ? " -> " : "",
                        (st[i] != 1 && g_ws.test_advice[i][0]) ? g_ws.test_advice[i] : "");
    }
    off += snprintf(report + off, sizeof(report) - off, "\r\nConfiguration:\r\n");
    if (g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
        off += snprintf(report + off, sizeof(report) - off,
                        "  Device : %s unit %lu (%s)\r\n",
                        g_ws.hw[g_ws.selected_hw_idx].device_name,
                        (unsigned long)g_ws.hw[g_ws.selected_hw_idx].unit,
                        g_ws.hw[g_ws.selected_hw_idx].is_wireless ? "wireless" : "wired");
    }
    off += snprintf(report + off, sizeof(report) - off,
                    "  Mode   : %s\r\n  Host   : %s\r\n",
                    (g_ws.ip_mode == 0) ? "DHCP" : "Manual",
                    g_ws.host_str[0] ? g_ws.host_str : "amiga");
    (void)off;

    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        strcpy(path, "RAM:tolunnet-setup.log");
        fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    }
    if (fh) {
        Write(fh, (CONST_APTR)report, strlen(report));
        Close(fh);
        char msg[160];
        snprintf(msg, sizeof(msg), "Test log written to:\n%s", path);
        es.es_TextFormat = (STRPTR)msg;
    } else {
        es.es_TextFormat = (STRPTR)"Could not write the test log\nto RAM: - disk full?";
    }
    EasyRequestArgs(g_win, &es, NULL, NULL);
}

/* Layout self-report for tc_wizard_ntsc: window + lowest gadget edge in
 * ENV:TolunnetSetup.geom after every rebuild (TNET-110 proof hook) */
static void write_layout_geom(void)
{
    if (!g_win || !g_win->WScreen) return;

    LONG max_bottom = 0;
    const struct Gadget *g;
    for (g = g_nav_glist; g; g = g->NextGadget) {
        if (g->TopEdge + g->Height > max_bottom) max_bottom = g->TopEdge + g->Height;
    }
    for (g = g_page_glist; g; g = g->NextGadget) {
        if (g->TopEdge + g->Height > max_bottom) max_bottom = g->TopEdge + g->Height;
    }

    char line[96];
    snprintf(line, sizeof(line),
             "page=%d winw=%ld winh=%ld wintop=%ld scrw=%ld scrh=%ld maxbottom=%ld compact=%d\n",
             g_ws.current_page,
             (long)g_win->Width, (long)g_win->Height, (long)g_win->TopEdge,
             (long)g_win->WScreen->Width, (long)g_win->WScreen->Height,
             (long)max_bottom, g_m.compact ? 1 : 0);
    BPTR fh = Open((CONST_STRPTR)"ENV:TolunnetSetup.geom", MODE_NEWFILE);
    if (fh) {
        Write(fh, (CONST_APTR)line, strlen(line));
        Close(fh);
    }
}


/* TNET-110: leaving the WiFi page associates (<= 30 s) with actionable
 * failure text in the status line; Next is held back on failure. */
static BOOL associate_wifi_page(void)
{
    char err[80];
    DetectedHw *hw;
    if (g_ws.selected_hw_idx < 0 || g_ws.selected_hw_idx >= g_ws.hw_count) {
        return TRUE;
    }
    hw = &g_ws.hw[g_ws.selected_hw_idx];
    if (!hw->is_wireless) return TRUE;
    if (g_ws.wifi_ssid_str[0] == '\0') {
        set_status("Pick a network or type a hidden SSID first");
        return FALSE;
    }

    set_status("Associating with %s (<= 30 s)...", g_ws.wifi_ssid_str);
    tn_wifi_write_prefs(g_ws.wifi_ssid_str, g_ws.wifi_pass);
    tn_wifi_start_manager(hw->device_name, hw->unit);
    err[0] = '\0';
    if (tn_wifi_wait_association(hw->device_name, hw->unit, 30, err, sizeof(err))) {
        g_ws.wifi_associated = TRUE;
        set_status("Associated with %s", g_ws.wifi_ssid_str);
        return TRUE;
    }
    if (err[0] != '\0' && strstr(err, "auth") != NULL) {
        set_status("Association failed: Wrong password?");
    } else if (g_ws.wifi_count == 0) {
        set_status("Association failed: Network not found - 2.4 GHz only?");
    } else {
        set_status("Association failed: %s", err[0] ? err : "no response from AP");
    }
    return FALSE;
}

static void advance_next_page(void)
{
    sync_page_gadgets_to_state();
    if (g_ws.current_page == WIZARD_PAGE_WIFI && !associate_wifi_page()) {
        return;
    }
    if (g_ws.current_page == WIZARD_PAGE_ADDRESS && !validate_address_page()) {
        return;
    }
    if (g_ws.current_page < WIZARD_PAGE_COUNT - 1) {
        g_ws.current_page++;
        if (g_ws.current_page == WIZARD_PAGE_WIFI) {
            if (g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
                if (!g_ws.hw[g_ws.selected_hw_idx].is_wireless) {
                    g_ws.current_page = WIZARD_PAGE_ADDRESS;
                }
            }
        }
        rebuild_page_gadgets();
    } else {
        g_ws.rexx_done = TRUE;
    }
}

static void retreat_back_page(void)
{
    sync_page_gadgets_to_state();
    if (g_ws.current_page > 0) {
        g_ws.current_page--;
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

/* Only the Address page paints custom text; the listview pages render
 * themselves (TNET-110 part 3 — drawing over them would clobber gadgets) */
static void draw_page_content(void)
{
    if (!g_win) return;
    if (g_ws.current_page != WIZARD_PAGE_ADDRESS) return;

    struct RastPort *rp = g_win->RPort;
    const char *note = "DHCP obtains IP address, netmask, gateway and DNS servers automatically.";
    LONG note_y = g_m.pane_t + 4 + g_m.pitch + g_m.pitch;  /* row ct+1 */

    SetAPen(rp, g_m.pen_bg);
    RectFill(rp, g_m.pane_l + 14, note_y - g_m.fy,
             g_m.pane_l + g_m.pane_w - 14, note_y);
    SetAPen(rp, g_m.pen_text);
    SetFont(rp, g_font ? g_font : rp->Font);
    Move(rp, g_m.pane_l + 14, note_y);
    Text(rp, (CONST_STRPTR)note, (WORD)strlen(note));
}

static void rebuild_page_gadgets(void)
{
    if (!g_win) return;
    sync_page_gadgets_to_state();

    if (g_gad_listview && g_win) {
        GT_SetGadgetAttrs(g_gad_listview, g_win, NULL,
                          GTLV_Labels, ~0,
                          TAG_DONE);
        g_gad_listview = NULL;
    }

    if (g_page_glist) {
        RemoveGList(g_win, g_page_glist, -1);
        FreeGadgets(g_page_glist);
        g_page_glist = NULL;
    }

    /* Step SEC item 1: clear entire pane area and refresh window frame */
    {
        struct RastPort *rp = g_win->RPort;
        SetAPen(rp, g_m.pen_bg);
        RectFill(rp, g_m.pane_l, g_m.pane_t,
                 g_m.pane_l + g_m.pane_w - 1, g_m.pane_t + g_m.pane_h - 1);
        RefreshWindowFrame(g_win);
    }

    struct NewGadget ng;
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = g_vi;
    ng.ng_TextAttr   = &g_gui_font;

    struct Gadget *prev = CreateContext(&g_page_glist);

    /* pane-relative helper coords */
    const LONG cl = g_m.pane_l + 14;              /* content left */
    const LONG cw = g_m.pane_w - 28;              /* content width */
    const LONG ct = g_m.pane_t + 4 + g_m.pitch;   /* first gadget row */

    switch (g_ws.current_page) {
    case WIZARD_PAGE_REPLACE: {
        int rows = g_m.compact ? 3 : 5;
        int i;
        NewList(&g_stack_list);
        for (i = 0; i < g_ws.stack_count && i < MAX_DETECTED_STACKS; i++) {
            snprintf(g_stack_lines[i], sizeof(g_stack_lines[i]), "%-10s %s",
                     g_ws.stacks[i].name, g_ws.stacks[i].details);
            memset(&g_stack_nodes[i], 0, sizeof(struct Node));
            g_stack_nodes[i].ln_Name = g_stack_lines[i];
            AddTail(&g_stack_list, &g_stack_nodes[i]);
        }
        if (g_ws.stack_count == 0) {
            snprintf(g_stack_lines[0], sizeof(g_stack_lines[0]), "%s",
                     "(no other network stacks found)");
            memset(&g_stack_nodes[0], 0, sizeof(struct Node));
            g_stack_nodes[0].ln_Name = g_stack_lines[0];
            AddTail(&g_stack_list, &g_stack_nodes[0]);
        }

        ng.ng_LeftEdge   = cl;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = cw;
        ng.ng_Height     = g_m.pitch * rows + 6;
        ng.ng_GadgetText = (STRPTR)"Found on this system:";
        ng.ng_GadgetID   = GID_P2_LIST + 100; /* display-only list */
        ng.ng_Flags      = PLACETEXT_ABOVE;
        prev = CreateGadget(LISTVIEW_KIND, prev, &ng,
                            GTLV_Labels, (ULONG)&g_stack_list,
                            GTLV_ReadOnly, TRUE,
                            TAG_END);
        g_gad_listview = prev;

        ng.ng_LeftEdge   = cl;
        ng.ng_TopEdge    = ct + g_m.pitch * rows + 10 + g_m.pitch + 4;
        ng.ng_Width      = 26;
        ng.ng_Height     = g_m.fy + 6;
        ng.ng_GadgetText = (STRPTR)"_Replace with tolunnet (recommended, non-destructive)";
        ng.ng_GadgetID   = GID_P1_REPLACE_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.replace_stacks,
                            TAG_END);

        ng.ng_TopEdge    += g_m.pitch;
        ng.ng_GadgetText = (STRPTR)"_Import Roadshow interface settings (when found)";
        ng.ng_GadgetID   = GID_P1_IMPORT_CHK;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.imported_settings,
                            TAG_END);
        break;
    }

    case WIZARD_PAGE_HW: {
        int rows = g_m.compact ? 4 : 6;
        int i;
        NewList(&g_hw_list);
        for (i = 0; i < g_ws.hw_count && i < MAX_DETECTED_HW; i++) {
            char mark = (i == g_ws.selected_hw_idx) ? '>' : ' ';
            snprintf(g_hw_lines[i], sizeof(g_hw_lines[i]),
                     "%c %-26s u%-2lu  %s  MTU %lu%s",
                     mark,
                     g_ws.hw[i].friendly_name,
                     (unsigned long)g_ws.hw[i].unit,
                     g_ws.hw[i].mac_str,
                     (unsigned long)g_ws.hw[i].mtu,
                     g_ws.hw[i].is_operational ? "" : "  (unusable)");
            memset(&g_hw_nodes[i], 0, sizeof(struct Node));
            g_hw_nodes[i].ln_Name = g_hw_lines[i];
            AddTail(&g_hw_list, &g_hw_nodes[i]);
        }
        if (g_ws.hw_count == 0) {
            snprintf(g_hw_lines[0], sizeof(g_hw_lines[0]), "%s",
                     "(no SANA-II adapters found — press Rescan)");
            memset(&g_hw_nodes[0], 0, sizeof(struct Node));
            g_hw_nodes[0].ln_Name = g_hw_lines[0];
            AddTail(&g_hw_list, &g_hw_nodes[0]);
        }

        ng.ng_LeftEdge   = cl;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = cw;
        ng.ng_Height     = g_m.pitch * rows + 6;
        ng.ng_GadgetText = (STRPTR)"Adapter:";
        ng.ng_GadgetID   = GID_P2_LIST;
        ng.ng_Flags      = PLACETEXT_ABOVE;
        prev = CreateGadget(LISTVIEW_KIND, prev, &ng,
                            GTLV_Labels, (ULONG)&g_hw_list,
                            GTLV_Selected, g_ws.selected_hw_idx,
                            TAG_END);
        g_gad_listview = prev;

        ng.ng_LeftEdge   = cl + cw - 2 * 110 - 8;
        ng.ng_TopEdge    = ct + g_m.pitch * rows + 10 + g_m.pitch + 4;
        ng.ng_Width      = 110;
        ng.ng_Height     = g_m.btn_h;
        ng.ng_GadgetText = (STRPTR)"_Rescan";
        ng.ng_GadgetID   = GID_P2_SCAN_BTN;
        ng.ng_Flags      = PLACETEXT_IN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_',
                            TAG_END);

        ng.ng_LeftEdge   = cl + cw - 110;
        ng.ng_GadgetText = (STRPTR)"_Test adapter";
        ng.ng_GadgetID   = GID_P2_TEST_BTN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_',
                            TAG_END);
        break;
    }

    case WIZARD_PAGE_WIFI: {
        int rows = g_m.compact ? 4 : 6;
        int i;
        int shown = 0;
        NewList(&g_wifi_list);
        for (i = 0; i < g_ws.wifi_count && i < MAX_WIFI_NETWORKS; i++) {
            char bars[8];
            int pct = (int)((g_ws.wifi[i].signal_dbm + 100) * 2);
            int nb = (pct >= 80) ? 5 : (pct >= 60) ? 4 : (pct >= 40) ? 3 : (pct >= 20) ? 2 : 1;
            int b;
            const char *sec;
            if (nb < 1) nb = 1;
            if (nb > 5) nb = 5;
            for (b = 0; b < 5; b++) bars[b] = (b < nb) ? '#' : '.';
            bars[5] = '\0';
            sec = (g_ws.wifi[i].encryption == 0) ? "Open"
                : (g_ws.wifi[i].encryption == 1) ? "WEP" : "WPA";
            snprintf(g_wifi_lines[i], sizeof(g_wifi_lines[i]),
                     "%c %-24s Ch:%-3d %s [%s]",
                     (i == g_ws.selected_wifi_idx) ? '>' : ' ',
                     g_ws.wifi[i].ssid[0] ? g_ws.wifi[i].ssid : "<hidden>",
                     (int)g_ws.wifi[i].channel, bars, sec);
            memset(&g_wifi_nodes[i], 0, sizeof(struct Node));
            g_wifi_nodes[i].ln_Name = g_wifi_lines[i];
            AddTail(&g_wifi_list, &g_wifi_nodes[i]);
            shown++;
        }
        if (shown == 0) {
            snprintf(g_wifi_lines[0], sizeof(g_wifi_lines[0]), "%s",
                     "(press Scan APs — 2.4 GHz networks only)");
            memset(&g_wifi_nodes[0], 0, sizeof(struct Node));
            g_wifi_nodes[0].ln_Name = g_wifi_lines[0];
            AddTail(&g_wifi_list, &g_wifi_nodes[0]);
            shown = 1;
        }

        ng.ng_LeftEdge   = cl;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = cw;
        ng.ng_Height     = g_m.pitch * rows + 6;
        ng.ng_GadgetText = (STRPTR)"Networks:";
        ng.ng_GadgetID   = GID_P3_NETLIST;
        ng.ng_Flags      = PLACETEXT_ABOVE;
        prev = CreateGadget(LISTVIEW_KIND, prev, &ng,
                            GTLV_Labels, (ULONG)&g_wifi_list,
                            GTLV_Selected, g_ws.selected_wifi_idx,
                            TAG_END);
        g_gad_listview = prev;

        ng.ng_LeftEdge   = cl + cw - 100;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = 100;
        ng.ng_Height     = g_m.btn_h;
        ng.ng_GadgetText = (STRPTR)"_Scan APs";
        ng.ng_GadgetID   = GID_P3_RESCAN_BTN;
        ng.ng_Flags      = PLACETEXT_IN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_',
                            TAG_END);

        /* SSID row (editable: hidden networks) */
        {
            LONG row_y = ct + g_m.pitch * rows + 10 + g_m.pitch + 4;
            if (g_ws.wifi_ssid_str[0] == '\0' &&
                g_ws.selected_wifi_idx >= 0 && g_ws.selected_wifi_idx < g_ws.wifi_count) {
                strncpy(g_ws.wifi_ssid_str, g_ws.wifi[g_ws.selected_wifi_idx].ssid,
                        sizeof(g_ws.wifi_ssid_str) - 1);
                g_ws.wifi_ssid_str[sizeof(g_ws.wifi_ssid_str) - 1] = '\0';
            }
            ng.ng_LeftEdge   = cl + 60;
            ng.ng_TopEdge    = row_y;
            ng.ng_Width      = 200;
            ng.ng_Height     = g_m.fy + 8;
            ng.ng_GadgetText = (STRPTR)"SSID:";
            ng.ng_GadgetID   = GID_P3_SSID_STR;
            ng.ng_Flags      = PLACETEXT_LEFT;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.wifi_ssid_str,
                                GTST_MaxChars, 33,
                                GA_TabCycle, TRUE,
                                TAG_END);

            ng.ng_LeftEdge   = cl + 60;
            ng.ng_TopEdge    = row_y + g_m.pitch;
            ng.ng_Width      = 200;
            ng.ng_Height     = g_m.fy + 8;
            ng.ng_GadgetText = (STRPTR)"Passphrase:";
            ng.ng_GadgetID   = GID_P3_PASS_STR;
            ng.ng_Flags      = PLACETEXT_LEFT;
        if (g_ws.wifi_show_pass) {
            strncpy(s_pass_display_buf, g_ws.wifi_pass, sizeof(s_pass_display_buf) - 1);
            s_pass_display_buf[sizeof(s_pass_display_buf) - 1] = '\0';
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)s_pass_display_buf,
                                GTST_MaxChars, 63,
                                GA_TabCycle, TRUE,
                                TAG_END);
        } else {
            size_t plen = strlen(g_ws.wifi_pass);
            {
                size_t pi;
                for (pi = 0; pi < plen && pi < sizeof(s_pass_display_buf) - 1; pi++) {
                    s_pass_display_buf[pi] = '*';
                }
            }
            s_pass_display_buf[plen < sizeof(s_pass_display_buf) ? plen : sizeof(s_pass_display_buf) - 1] = '\0';
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)s_pass_display_buf,
                                GTST_EditHook, (ULONG)&s_pass_hook,
                                GTST_MaxChars, 63,
                                GA_TabCycle, TRUE,
                                TAG_END);
        }

        ng.ng_LeftEdge   = cl + 300;
        ng.ng_TopEdge    = ct + (g_m.compact ? 6 : 8) * g_m.pitch + 1;
        ng.ng_Width      = 26;
        ng.ng_Height     = g_m.fy + 6;
        ng.ng_GadgetText = (STRPTR)"Show";
        ng.ng_GadgetID   = GID_P3_SHOWPASS_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.wifi_show_pass,
                            TAG_END);
        }
        break;
    }

    case WIZARD_PAGE_ADDRESS:
        ng.ng_LeftEdge   = cl + 70;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = 230;
        ng.ng_Height     = g_m.fy + 8;
        ng.ng_GadgetText = (STRPTR)"IP Mode:";
        ng.ng_GadgetID   = GID_P4_IPMODE_CYCLE;
        ng.ng_Flags      = PLACETEXT_LEFT;
        prev = CreateGadget(CYCLE_KIND, prev, &ng,
                            GTCY_Labels, (ULONG)g_ipmode_names,
                            GTCY_Active, g_ws.ip_mode,
                            TAG_END);

        /* Advanced… opens the sibling window (part 3) */
        ng.ng_LeftEdge   = cl + cw - 100;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = 100;
        ng.ng_Height     = g_m.btn_h;
        ng.ng_GadgetText = (STRPTR)"Adv_anced...";
        ng.ng_GadgetID   = GID_P4_ADVANCED_BTN;
        ng.ng_Flags      = PLACETEXT_IN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_',
                            TAG_END);

        if (g_ws.ip_mode == 1) {
            ng.ng_TopEdge    = ct + 2 * g_m.pitch;
            ng.ng_Width      = 130;
            ng.ng_Height     = g_m.fy + 8;
            ng.ng_Flags      = PLACETEXT_LEFT;
            ng.ng_GadgetID   = GID_P4_IP_STR;
            ng.ng_GadgetText = (STRPTR)"IP:";
            ng.ng_LeftEdge   = cl + 70;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.ip_str,
                                GTST_MaxChars, 15, GA_TabCycle, TRUE, TAG_END);
            ng.ng_GadgetID   = GID_P4_NM_STR;
            ng.ng_GadgetText = (STRPTR)"Mask:";
            ng.ng_LeftEdge   = cl + 290;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.nm_str,
                                GTST_MaxChars, 15, GA_TabCycle, TRUE, TAG_END);
            ng.ng_GadgetID   = GID_P4_GW_STR;
            ng.ng_GadgetText = (STRPTR)"Gateway:";
            ng.ng_LeftEdge   = cl + 70;
            ng.ng_TopEdge    = ct + 3 * g_m.pitch;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.gw_str,
                                GTST_MaxChars, 15, GA_TabCycle, TRUE, TAG_END);
            ng.ng_GadgetID   = GID_P4_DNS1_STR;
            ng.ng_GadgetText = (STRPTR)"DNS 1:";
            ng.ng_LeftEdge   = cl + 290;
            prev = CreateGadget(STRING_KIND, prev, &ng,
                                GTST_String, (ULONG)g_ws.dns1_str,
                                GTST_MaxChars, 15, GA_TabCycle, TRUE, TAG_END);
        }

        /* DNS 2 + MTU are mode-independent (TNET-110 v2.1) */
        ng.ng_TopEdge    = ct + (g_ws.ip_mode == 1 ? 4 : 2) * g_m.pitch;
        ng.ng_Width      = 130;
        ng.ng_Height     = g_m.fy + 8;
        ng.ng_Flags      = PLACETEXT_LEFT;
        ng.ng_GadgetID   = GID_P4_DNS2_STR;
        ng.ng_GadgetText = (STRPTR)"DNS 2:";
        ng.ng_LeftEdge   = cl + 70;
        prev = CreateGadget(STRING_KIND, prev, &ng,
                            GTST_String, (ULONG)g_ws.dns2_str,
                            GTST_MaxChars, 15, GA_TabCycle, TRUE, TAG_END);
        ng.ng_GadgetID   = GID_P4_MTU_STR;
        ng.ng_GadgetText = (STRPTR)"MTU:";
        ng.ng_LeftEdge   = cl + 290;
        prev = CreateGadget(STRING_KIND, prev, &ng,
                            GTST_String, (ULONG)g_ws.mtu_str,
                            GTST_MaxChars, 5, GA_TabCycle, TRUE, TAG_END);

        /* Host + Domain share one row (compact fit, TNET-110 part 3) */
        ng.ng_TopEdge    = ct + (g_ws.ip_mode == 1 ? 5 : 3) * g_m.pitch;
        ng.ng_Width      = 130;
        ng.ng_Height     = g_m.fy + 8;
        ng.ng_Flags      = PLACETEXT_LEFT;
        ng.ng_GadgetID   = GID_P4_HOST_STR;
        ng.ng_GadgetText = (STRPTR)"Host:";
        ng.ng_LeftEdge   = cl + 70;
        prev = CreateGadget(STRING_KIND, prev, &ng,
                            GTST_String, (ULONG)g_ws.host_str,
                            GTST_MaxChars, sizeof(g_ws.host_str) - 1,
                            GA_TabCycle, TRUE, TAG_END);
        ng.ng_GadgetID   = GID_P4_DOMAIN_STR;
        ng.ng_GadgetText = (STRPTR)"Domain:";
        ng.ng_LeftEdge   = cl + 290;
        ng.ng_Width      = 200;
        prev = CreateGadget(STRING_KIND, prev, &ng,
                            GTST_String, (ULONG)g_ws.domain_str,
                            GTST_MaxChars, sizeof(g_ws.domain_str) - 1,
                            GA_TabCycle, TRUE, TAG_END);

        ng.ng_LeftEdge   = cl;
        ng.ng_TopEdge    = ct + (g_ws.ip_mode == 1
                                 ? (g_m.compact ? 6 : 7)
                                 : (g_m.compact ? 5 : 6)) * g_m.pitch;
        ng.ng_Width      = 26;
        ng.ng_Height     = g_m.fy + 6;
        ng.ng_GadgetText = (STRPTR)"Also write _Roadshow-style DEVS:NetInterfaces/ for other tools";
        ng.ng_GadgetID   = GID_P4_ROADSHOW_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.write_roadshow,
                            TAG_END);
        break;

    case WIZARD_PAGE_TEST: {
        int i;
        static const char *names[5] = { "Start stack", "DHCP lease",
                                        "Ping gateway", "DNS lookup",
                                        "HTTP HEAD" };
        NewList(&g_check_list);
        for (i = 0; i < 5; i++) {
            int st = (i == 0) ? g_ws.test_daemon_ok
                    : (i == 1) ? g_ws.test_dhcp_ok
                    : (i == 2) ? g_ws.test_ping_ok
                    : (i == 3) ? g_ws.test_dns_ok : g_ws.test_http_ok;
            snprintf(g_check_lines[i], sizeof(g_check_lines[i]),
                     "%-13s %-6s %s%s%s",
                     names[i],
                     (st == 1) ? "OK" : (st == 0) ? "FAILED" : "..",
                     g_ws.test_details[i],
                     (st != 1 && g_ws.test_advice[i][0]) ? " -> " : "",
                     (st != 1 && g_ws.test_advice[i][0]) ? g_ws.test_advice[i] : "");
            memset(&g_check_nodes[i], 0, sizeof(struct Node));
            g_check_nodes[i].ln_Name = g_check_lines[i];
            AddTail(&g_check_list, &g_check_nodes[i]);
        }

        /* checklist leaves room for the two right-side buttons */
        ng.ng_LeftEdge   = cl;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = cw - 150;
        ng.ng_Height     = g_m.pitch * 5 + 6;
        ng.ng_GadgetText = (STRPTR)"Checks:";
        ng.ng_GadgetID   = GID_P5_CHECKLIST;
        ng.ng_Flags      = PLACETEXT_ABOVE;
        prev = CreateGadget(LISTVIEW_KIND, prev, &ng,
                            GTLV_Labels, (ULONG)&g_check_list,
                            GTLV_ReadOnly, TRUE,
                            TAG_END);
        g_gad_listview = prev;

        ng.ng_LeftEdge   = cl + cw - 140;
        ng.ng_TopEdge    = ct;
        ng.ng_Width      = 140;
        ng.ng_Height     = g_m.btn_h;
        ng.ng_GadgetText = (STRPTR)"Run tests a_gain";
        ng.ng_GadgetID   = GID_P5_TEST_BTN;
        ng.ng_Flags      = PLACETEXT_IN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_',
                            TAG_END);

        ng.ng_TopEdge    = ct + g_m.btn_h + 6;
        ng.ng_Width      = 140;
        ng.ng_GadgetText = (STRPTR)"_Save log...";
        ng.ng_GadgetID   = GID_P5_SAVELOG_BTN;
        prev = CreateGadget(BUTTON_KIND, prev, &ng,
                            GT_Underscore, '_',
                            TAG_END);
    }

        ng.ng_LeftEdge   = cl;
        ng.ng_TopEdge    = ct + g_m.pitch * 5 + 10;
        ng.ng_Width      = 26;
        ng.ng_Height     = g_m.fy + 6;
        ng.ng_GadgetText = (STRPTR)"Start at _boot";
        ng.ng_GadgetID   = GID_P5_BOOT_CHK;
        ng.ng_Flags      = PLACETEXT_RIGHT;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.start_at_boot,
                            TAG_END);

        ng.ng_LeftEdge   = cl + 180;
        ng.ng_GadgetText = (STRPTR)"Open _Prefs after finish";
        ng.ng_GadgetID   = GID_P5_PREFS_CHK;
        prev = CreateGadget(CHECKBOX_KIND, prev, &ng,
                            GTCB_Checked, g_ws.open_prefs_after_finish,
                            TAG_END);
        break;
    }

    if (g_page_glist) {
        AddGList(g_win, g_page_glist, -1, -1, NULL);
        RefreshGList(g_page_glist, g_win, NULL, -1);
    }
    update_nav_buttons();
    render_frames();
    draw_page_content();
    update_screen_title();
    write_layout_geom();
}

static void apply_wizard_finish(void)
{
    sync_page_gadgets_to_state();

    /* TNET-110: explicit MTU overrides the driver default in the writer */
    if (g_ws.mtu_str[0] != '\0' &&
        g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
        long m = atol(g_ws.mtu_str);
        if (m >= 576 && m <= 1500) {
            g_ws.hw[g_ws.selected_hw_idx].mtu = (ULONG)m;
        }
    }

    /* 1. Migrate / disable other stacks */
    if (g_ws.replace_stacks) {
        tn_stack_apply_replacement(&g_ws);
        tn_stack_request_quit(&g_ws);
    }

    /* 2. Write WiFi credentials if wireless (saved networks with
     * priority= in Wireless.prefs, TNET-110 part 3) */
    if (g_ws.selected_hw_idx >= 0 && g_ws.selected_hw_idx < g_ws.hw_count) {
        if (g_ws.hw[g_ws.selected_hw_idx].is_wireless) {
            if (!tn_wifi_write_prefs_multi(&g_ws)) {
                const char *ssid = (g_ws.selected_wifi_idx >= 0 &&
                                    g_ws.selected_wifi_idx < g_ws.wifi_count)
                                   ? g_ws.wifi[g_ws.selected_wifi_idx].ssid : "DefaultAP";
                tn_wifi_write_prefs(ssid, g_ws.wifi_pass);
            }
            tn_wifi_start_manager(g_ws.hw[g_ws.selected_hw_idx].device_name,
                                  g_ws.hw[g_ws.selected_hw_idx].unit);
        }
    }

    /* 3. Write tolunnet configuration (also writes DEVS:Internet/ files
     * when the Advanced checkbox is set) */
    tn_write_tolunnet_config(&g_ws);

    /* 4. Write Roadshow interface if requested */
    if (g_ws.write_roadshow) {
        tn_write_roadshow_interface(&g_ws);
    }

    /* 5. Install boot block if requested */
    if (g_ws.start_at_boot) {
        tn_install_boot_block(TRUE);
    }

    /* 6. Optionally hand over to the Prefs editor (TNET-110 part 3) */
    if (g_ws.open_prefs_after_finish) {
        const char *prefs_path = NULL;
        BPTR l = Lock((CONST_STRPTR)"SYS:Prefs/TolunnetPrefs", ACCESS_READ);
        if (l) {
            UnLock(l);
            prefs_path = "SYS:Prefs/TolunnetPrefs";
        } else {
            l = Lock((CONST_STRPTR)"TolunnetPrefs", ACCESS_READ);
            if (l) {
                UnLock(l);
                prefs_path = "TolunnetPrefs";
            }
        }
        if (prefs_path) {
            SystemTags((CONST_STRPTR)prefs_path,
                       SYS_Asynch, TRUE,
                       SYS_Input, (BPTR)0,
                       SYS_Output, (BPTR)0,
                       TAG_END);
        }
    }

    /* Zero the memory containing passphrase immediately after writing */
    volatile char *vpass = (volatile char *)g_ws.wifi_pass;
    for (size_t i = 0; i < sizeof(g_ws.wifi_pass); i++) {
        vpass[i] = 0;
    }
    volatile char *vdisp = (volatile char *)s_pass_display_buf;
    for (size_t i = 0; i < sizeof(s_pass_display_buf); i++) {
        vdisp[i] = 0;
    }
}

/* 1 s status tick (timer.device, in the Wait mask — never Delay()) */
static BOOL start_status_tick(void)
{
    g_tick_port = CreateMsgPort();
    if (!g_tick_port) return FALSE;
    g_tick_io = (struct timerequest *)AllocVec(sizeof(struct timerequest),
                                               MEMF_CLEAR | MEMF_PUBLIC);
    if (!g_tick_io) return FALSE;
    g_tick_io->tr_node.io_Message.mn_ReplyPort = g_tick_port;
    g_tick_io->tr_node.io_Message.mn_Length = (UWORD)sizeof(struct timerequest);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK,
                   (struct IORequest *)g_tick_io, 0UL) != 0) {
        return FALSE;
    }
    g_tick_sig = 1UL << g_tick_port->mp_SigBit;
    g_last_secs = 0;

    g_tick_io->tr_node.io_Command = TR_ADDREQUEST;
    g_tick_io->tr_time.tv_secs = 1;
    g_tick_io->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)g_tick_io);
    return TRUE;
}

static void handle_status_tick(void)
{
    struct Message *m;
    while ((m = GetMsg(g_tick_port)) != NULL) {}
    g_last_secs++;

    /* keep the status line alive during long operations (elapsed seconds) */
    if (g_ws.wifi_status_msg[0]) {
        set_status("%s (%lus)", g_ws.wifi_status_msg, g_last_secs);
    }

    /* re-arm */
    g_tick_io->tr_node.io_Command = TR_ADDREQUEST;
    g_tick_io->tr_time.tv_secs = 1;
    g_tick_io->tr_time.tv_micro = 0;
    SendIO((struct IORequest *)g_tick_io);
}

static void stop_status_tick(void)
{
    if (g_tick_io) {
        if (!CheckIO((struct IORequest *)g_tick_io)) {
            AbortIO((struct IORequest *)g_tick_io);
        }
        WaitIO((struct IORequest *)g_tick_io);
        CloseDevice((struct IORequest *)g_tick_io);
        FreeVec(g_tick_io);
        g_tick_io = NULL;
    }
    if (g_tick_port) {
        DeleteMsgPort(g_tick_port);
        g_tick_port = NULL;
    }
    g_tick_sig = 0;
}

int main(int argc, char **argv)
{
    memset(&g_ws, 0, sizeof(g_ws));
    g_ws.replace_stacks = TRUE;
    g_ws.write_roadshow = TRUE;
    g_ws.start_at_boot  = TRUE;
    g_ws.ip_mode        = 0; /* DHCP default */
    g_ws.task_priority  = 5;
    g_ws.open_prefs_after_finish = FALSE;
    g_ws.test_daemon_ok = -1;
    g_ws.test_dhcp_ok   = -1;
    g_ws.test_ping_ok   = -1;
    g_ws.test_dns_ok    = -1;
    g_ws.test_http_ok   = -1;
    strncpy(g_ws.ip_str, "192.168.1.100", sizeof(g_ws.ip_str) - 1);
    strncpy(g_ws.nm_str, "255.255.255.0", sizeof(g_ws.nm_str) - 1);
    strncpy(g_ws.gw_str, "192.168.1.1", sizeof(g_ws.gw_str) - 1);
    strncpy(g_ws.dns1_str, "1.1.1.1", sizeof(g_ws.dns1_str) - 1);
    strncpy(g_ws.host_str, "amiga", sizeof(g_ws.host_str) - 1);

    /* Preserve existing log file setting if present */
    {
        BPTR cfg_fh = Open((CONST_STRPTR)"DEVS:tolunnet.config", MODE_OLDFILE);
        if (cfg_fh) {
            char cline[160];
            while (FGets(cfg_fh, (STRPTR)cline, (LONG)sizeof(cline))) {
                if (strncmp(cline, "LOG=", 4) == 0) {
                    size_t n = strlen(cline);
                    while (n > 4 && (cline[n - 1] == '\n' || cline[n - 1] == '\r')) {
                        cline[--n] = '\0';
                    }
                    strncpy(g_ws.log_file, cline + 4, sizeof(g_ws.log_file) - 1);
                    g_ws.log_file[sizeof(g_ws.log_file) - 1] = '\0';
                    break;
                }
            }
            Close(cfg_fh);
        }
    }

    struct Process *pr = (struct Process *)FindTask(NULL);
    APTR old_win_ptr = NULL;
    if (pr && pr->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        old_win_ptr = pr->pr_WindowPtr;
        pr->pr_WindowPtr = (APTR)-1;
    }

    wlog("startup: opening libraries");
    /* Initialize ARexx host port early so external scripts and bench can connect immediately */
    struct MsgPort *rexx_port = tn_setup_rexx_init();

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
    GfxBase       = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 36);
    GadToolsBase  = OpenLibrary((CONST_STRPTR)"gadtools.library", 36);
    DiskfontBase  = OpenLibrary((CONST_STRPTR)"diskfont.library", 37);
    IconBase      = OpenLibrary((CONST_STRPTR)"icon.library", 36);
    AslBase       = OpenLibrary((CONST_STRPTR)"asl.library", 36);

    wlog("libraries open, choosing font");
    setup_font(argc, argv);

    wlog("scanning installed stacks");
    tn_stack_detect_all(&g_ws);
    wlog("scanning network hardware");
    tn_hw_scan_all(&g_ws);

    struct Screen *scr = NULL;
    BOOL owns_screen = FALSE;

    wlog("opening screen");
    if (IntuitionBase && GfxBase && GadToolsBase) {
        scr = LockPubScreen(NULL);
        if (!scr) {
            scr = OpenScreenTags(NULL,
                                 SA_Depth, 2,
                                 SA_DisplayID, DEFAULT_MONITOR_ID | HIRES_KEY,
                                 SA_Title, (ULONG)"tolunnet Network Setup",
                                 SA_Type, CUSTOMSCREEN,
                                 TAG_END);
            if (scr) owns_screen = TRUE;
        }
    }

    if (scr) {
        g_dri = GetScreenDrawInfo(scr);
        g_vi = GetVisualInfo(scr, TAG_END);
        if (g_vi) {
            derive_metrics(scr);

            struct NewGadget ng;
            memset(&ng, 0, sizeof(ng));
            ng.ng_VisualInfo = g_vi;
            ng.ng_TextAttr   = &g_gui_font;

            struct Gadget *prev = CreateContext(&g_nav_glist);

            /* v2 button bar: Cancel left, Back + Next right */
            ng.ng_LeftEdge   = 8;
            ng.ng_TopEdge    = g_m.btn_t;
            ng.ng_Width      = 10 * g_m.fx + 8;
            ng.ng_Height     = g_m.btn_h;
            ng.ng_GadgetText = (STRPTR)"_Cancel";
            ng.ng_GadgetID   = GID_BTN_CANCEL;
            ng.ng_Flags      = PLACETEXT_IN;
            prev = CreateGadget(BUTTON_KIND, prev, &ng,
                                GT_Underscore, '_',
                                TAG_END);
            g_gad_cancel = prev;

            ng.ng_LeftEdge   = g_m.win_w - 8 - 2 * (10 * g_m.fx + 8) - 8;
            ng.ng_GadgetText = (STRPTR)"< _Back";
            ng.ng_GadgetID   = GID_BTN_BACK;
            prev = CreateGadget(BUTTON_KIND, prev, &ng,
                                GT_Underscore, '_',
                                GA_Disabled, TRUE,
                                TAG_END);
            g_gad_back = prev;

            ng.ng_LeftEdge   = g_m.win_w - 8 - (10 * g_m.fx + 8);
            ng.ng_GadgetText = (STRPTR)"_Next >";
            ng.ng_GadgetID   = GID_BTN_NEXT;
            prev = CreateGadget(BUTTON_KIND, prev, &ng,
                                GT_Underscore, '_',
                                TAG_END);
            g_gad_next = prev;

            /* status line under the pane */
            ng.ng_LeftEdge   = g_m.pane_l;
            ng.ng_TopEdge    = g_m.status_t;
            ng.ng_Width      = g_m.pane_w;
            ng.ng_Height     = g_m.fy + 6;
            ng.ng_GadgetText = (STRPTR)"";
            ng.ng_GadgetID   = GID_STATUS_TX;
            ng.ng_Flags      = PLACETEXT_IN;
            prev = CreateGadget(TEXT_KIND, prev, &ng,
                                GTTX_Text, (ULONG)g_status_text,
                                GTTX_Border, TRUE,
                                GTTX_CopyText, TRUE,
                                TAG_END);
            g_gad_status = prev;

            wlog("building gadgets");
            g_win = OpenWindowTags(NULL,
                                   WA_Left,         (scr->Width - g_m.win_w) / 2,
                                   WA_Top,          (scr->Height - g_m.win_h) / 2,
                                   WA_Width,        g_m.win_w,
                                   WA_Height,       g_m.win_h,
                                   WA_IDCMP,        IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                                                    IDCMP_GADGETUP | IDCMP_RAWKEY,
                                   WA_Flags,        WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                                                    WFLG_CLOSEGADGET | WFLG_SMART_REFRESH |
                                                    WFLG_ACTIVATE,
                                   WA_Title,        (ULONG)"tolunnet Network Setup",
                                   WA_ScreenTitle,  (ULONG)"tolunnet Network Setup 1.2 - 1 / 5",
                                   WA_Gadgets,      (ULONG)g_nav_glist,
                                   WA_PubScreen,    (ULONG)scr,
                                   TAG_END);

            if (!owns_screen) {
                UnlockPubScreen(NULL, scr);
            }

            if (g_win) {
                GT_RefreshWindow(g_win, NULL);
                rebuild_page_gadgets();
                start_status_tick();
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
            ULONG adv_sig = g_adv_win ? (1UL << g_adv_win->UserPort->mp_SigBit) : 0;
            ULONG sigs = Wait(win_sig | rexx_sig | g_tick_sig | adv_sig | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) {
            running = FALSE;
            break;
        }

        if (g_tick_sig && (sigs & g_tick_sig)) {
            handle_status_tick();
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
                    render_frames();
                    draw_page_content();
                    GT_EndRefresh(g_win, TRUE);
                    break;

                case IDCMP_RAWKEY:
                    if (im_code == 0x44) { /* RETURN = default action */
                        if (g_ws.current_page == WIZARD_PAGE_TEST &&
                            !all_tests_passed()) {
                            run_page_tests();   /* "Run tests again" default */
                            break;
                        }
                        advance_next_page();
                        if (g_ws.rexx_done) {
                            apply_wizard_finish();
                            running = FALSE;
                        }
                    } else if (im_code == 0x45) { /* ESC = Cancel */
                        running = FALSE;
                    }
                    break;

                case IDCMP_GADGETUP: {
                    struct Gadget *gad = (struct Gadget *)im_iaddr;
                    if (!gad) break;

                    switch (gad->GadgetID) {
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

                    case GID_P1_IMPORT_CHK:
                        g_ws.imported_settings = !g_ws.imported_settings;
                        break;

                    case GID_P2_LIST:
                        g_ws.selected_hw_idx = im_code;
                        rebuild_page_gadgets();
                        break;

                    case GID_P2_TEST_BTN: {
                        const char *res = "adapter ok";
                        if (g_ws.selected_hw_idx >= 0 &&
                            g_ws.selected_hw_idx < g_ws.hw_count &&
                            !g_ws.hw[g_ws.selected_hw_idx].is_operational) {
                            res = "adapter unusable (S2 error)";
                        } else if (g_ws.hw_count == 0) {
                            res = "no adapter selected";
                        }
                        set_status("Test adapter: %s", res);
                        break;
                    }

                    case GID_P2_SCAN_BTN:
                        set_status("Probing adapters...");
                        tn_hw_scan_all(&g_ws);
                        set_status("Found %d adapter(s)", g_ws.hw_count);
                        rebuild_page_gadgets();
                        break;

                    case GID_P3_RESCAN_BTN:
                        strncpy(g_ws.wifi_status_msg, "Scanning for access points",
                                sizeof(g_ws.wifi_status_msg) - 1);
                        g_ws.wifi_status_msg[sizeof(g_ws.wifi_status_msg) - 1] = '\0';
                        set_status("%s...", g_ws.wifi_status_msg);
                        draw_page_content();
                        tn_wifi_scan(&g_ws);
                        snprintf(g_ws.wifi_status_msg, sizeof(g_ws.wifi_status_msg),
                                 "Scan complete: %d network(s)", g_ws.wifi_count);
                        set_status("%s", g_ws.wifi_status_msg);
                        rebuild_page_gadgets();
                        break;

                    case GID_P3_SHOWPASS_CHK:
                        g_ws.wifi_show_pass = !g_ws.wifi_show_pass;
                        rebuild_page_gadgets();
                        break;

                    case GID_P3_NETLIST:
                        if ((int)im_code < g_ws.wifi_count) {
                            g_ws.selected_wifi_idx = im_code;
                            strncpy(g_ws.wifi_ssid_str,
                                    g_ws.wifi[im_code].ssid,
                                    sizeof(g_ws.wifi_ssid_str) - 1);
                            g_ws.wifi_ssid_str[sizeof(g_ws.wifi_ssid_str) - 1] = '\0';
                            rebuild_page_gadgets();
                        }
                        break;

                    case GID_P4_IPMODE_CYCLE:
                        g_ws.ip_mode = im_code;
                        rebuild_page_gadgets();
                        break;

                    case GID_P4_ADVANCED_BTN:
                        open_advanced_window();
                        break;

                    case GID_P4_ROADSHOW_CHK:
                        g_ws.write_roadshow = !g_ws.write_roadshow;
                        break;

                    case GID_P5_TEST_BTN:
                        run_page_tests();
                        break;

                    case GID_P5_SAVELOG_BTN:
                        save_test_log();
                        break;

                    case GID_P5_BOOT_CHK:
                        g_ws.start_at_boot = !g_ws.start_at_boot;
                        break;

                    case GID_P5_PREFS_CHK:
                        g_ws.open_prefs_after_finish = !g_ws.open_prefs_after_finish;
                        break;
                    }
                    break;
                } /* case IDCMP_GADGETUP */
                } /* switch (im_class) */
            } /* while (imsg) */
        } /* if (sigs & win_sig) */

        /* Sibling Advanced window (TNET-110 part 3): own UserPort in the
         * main Wait mask; values are committed to the wizard state only
         * on OK. Any close breaks out — the port dies with the window. */
        if (g_adv_win && adv_sig && (sigs & adv_sig)) {
            struct MsgPort *adv_port = g_adv_win->UserPort;
            struct IntuiMessage *amsg;
            BOOL adv_closed = FALSE;
            while (!adv_closed && (amsg = GT_GetIMsg(adv_port))) {
                ULONG am_class = amsg->Class;
                UWORD am_code  = amsg->Code;
                APTR  am_iaddr = amsg->IAddress;
                GT_ReplyIMsg(amsg);

                switch (am_class) {
                case IDCMP_CLOSEWINDOW:
                    close_advanced_window(FALSE);
                    adv_closed = TRUE;
                    break;

                case IDCMP_RAWKEY:
                    if (am_code == 0x44) {        /* RETURN = OK */
                        close_advanced_window(TRUE);
                        adv_closed = TRUE;
                    } else if (am_code == 0x45) { /* ESC = Cancel */
                        close_advanced_window(FALSE);
                        adv_closed = TRUE;
                    }
                    break;

                case IDCMP_GADGETUP: {
                    struct Gadget *agad = (struct Gadget *)am_iaddr;
                    if (!agad) break;
                    switch (agad->GadgetID) {
                    case GID_ADV_OK_BTN:
                        close_advanced_window(TRUE);
                        adv_closed = TRUE;
                        break;
                    case GID_ADV_CANCEL_BTN:
                        close_advanced_window(FALSE);
                        adv_closed = TRUE;
                        break;
                    case GID_ADV_ROADSHOW_CHK:
                        g_adv_write_roadshow = !g_adv_write_roadshow;
                        break;
                    case GID_ADV_DNS2FALLBACK_CHK:
                        g_adv_dhcp_fallback = !g_adv_dhcp_fallback;
                        break;
                    }
                    break;
                }
                }
            }
        }
    } /* while (running) */
    } /* else */

    if (g_ws.rexx_finish_msg) {
        ReplyMsg(g_ws.rexx_finish_msg);
        g_ws.rexx_finish_msg = NULL;
    }

    if (rexx_port) {
        tn_setup_rexx_cleanup(rexx_port);
    }

    stop_status_tick();

    close_advanced_window(FALSE);

    if (g_gad_listview && g_win) {
        GT_SetGadgetAttrs(g_gad_listview, g_win, NULL, GTLV_Labels, ~0, TAG_DONE);
        g_gad_listview = NULL;
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

    if (g_font != NULL) {
        CloseFont(g_font);
        g_font = NULL;
    }

    if (g_vi) {
        FreeVisualInfo(g_vi);
    }
    if (g_dri) {
        FreeScreenDrawInfo(scr, g_dri);
    }

    if (owns_screen && scr) {
        CloseScreen(scr);
    }

    if (AslBase)        CloseLibrary(AslBase);
    if (IconBase)       CloseLibrary(IconBase);
    if (DiskfontBase)  CloseLibrary(DiskfontBase);
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
