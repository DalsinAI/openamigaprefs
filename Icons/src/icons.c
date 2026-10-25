/* Icons: where icons' labels go and how the desktop and the drawers lay icons out, as one section of OpenPrefs (the
 * user, 24 October 2026, 15:37: "a page or tab in Preferences for this").
 *
 *   - Labels: close to the picture (the default: icon.library puts the label's text the gap's rows under the
 *     picture's lowest row that shows), or below the picture's whole box, as OS 3.2.3
 *   - Label gap: 0 to 8 rows, 2 by default (close only)
 *   - Cell size: Auto (the icons' own usual size) or a size set: the standard cell of the column grid, which every
 *     icon of that size sits on (a bigger or smaller one takes its own height, a wider one widens its column)
 *   - Snap dropped icons to the grid (on); the Clean Up order (name, kind, size or date); the desktop's columns
 *     down the left (the default) or the right; the desktop's labels in the colour the backdrop's .tone asks for
 *     (light on a dark picture, dark on a light one; the default) or the theme's
 *   - Icon text font: a button to the Fonts page, whose Workbench icon text font the labels are in
 *   - No icons on the desktop (0.2, Salvor's OS list, item 59; the user, 24 October 2026, 21:08: "offer a no-icons mode
 *     too... for workbench itself"): OpenBench's desktop shows no icons at all, its disks', the ones left out or
 *     AppIcons, and the backdrop, the bar, the dock and the meters stay; off, they come back where they were. OpenBench's
 *     Window > No icons is the same setting, and its Icons > Clean Up turns it off
 *
 * Two variables, so icon.library (in the ROM) reads one word: ENV:Sys/IconLabels ("close", "close 3", "below") and
 * ENV:Sys/IconLayout ("cell=auto snap=on order=name columns=left labelcolour=backdrop"), read by OpenBench and its
 * drawers, which follow a change at once (opengadtools' workbench/wb_grid.c reads and writes both). And (0.2) a third
 * beside them, ENV:Sys/DesktopIcons ("show" or "none"; Common/deskicons.h), which OpenBench alone reads. Save writes
 * ENV: and ENVARC: (SetVar), Use ENV:, Test ENV: until Cancel puts back what was there, Cancel changes nothing else.
 *
 * Its own page rather than a tab of Workbench's: OpenPrefs's sections are one list of pages, each with its own Save,
 * Use, Test and Cancel and no tabs inside them, and Workbench's page is already full (its paths, its devices, its
 * buffers); this page replaces the "Labels close" box that was there.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include <string.h>
#include <stdio.h>
#ifdef LT_ONE
#include "looktabs.h"                       /* a page of OpenPrefs (LT_OP): its frame and sections */
#define LT_REFRESH(w) lt_draw((w), -1)
#else
#define LT_REFRESH(w) ((void)0)
#define LT_DIRTY 1
#define lt_note_to(f) ((void)0)
#endif

#include "wb_grid.h"                        /* opengadtools' workbench/: the two variables' words */
#include "deskicons.h"                      /* Common/: ENV:Sys/DesktopIcons's words, shared with OpenBench */

static const char version[] __attribute__((used)) = "$VER: IconsPage 0.2 (25.10.2026) OpenPrefs, MIT, Copyright (c) 2026 AmigaChrome Team";

#define LABELS "Sys/IconLabels"
#define LAYOUT "Sys/IconLayout"

static wb_iconprefs ip;
static int noicons;                         /* 0.2: DESKICONS_NONE, no icons on the desktop */
/* what ENV: had when the page opened, put back by Cancel after a Test */
static char was_labels[48], was_layout[128], was_desk[16];
static int had_labels, had_layout, had_desk, tested;

/* A variable's text from ENV: (GetVar), else from ENVARC: (the file); 1 when there was one */
static int read_var(const char *name, char *buf, int size)
{
    char path[64];
    BPTR f;
    LONG n = GetVar((CONST_STRPTR)name, (STRPTR)buf, size, GVF_GLOBAL_ONLY);
    if (n >= 0) return 1;
    snprintf(path, sizeof path, "ENVARC:%s", name);
    buf[0] = 0;
    if (!(f = Open((CONST_STRPTR)path, MODE_OLDFILE))) return 0;
    n = Read(f, buf, size - 1);
    Close(f);
    buf[n > 0 ? n : 0] = 0;
    return n > 0;
}

/* One variable into ENV: (and ENVARC: when save), through SetVar as DOS writes variables; a few tries (in the lab a
 * plain write once failed, one Use soon after another, while OpenBench and its drawers were reading the variables) */
static int set_var(const char *name, const char *text, int save)
{
    int i;
    for (i = 0; i < 5; i++) {
        if (SetVar((CONST_STRPTR)name, (CONST_STRPTR)text, -1, GVF_GLOBAL_ONLY | (save ? GVF_SAVE_VAR : 0))) return 1;
        Delay(10);
    }
    return 0;
}

/* The settings into ENV: (and ENVARC: when save); 1 done */
static int put(int save, void (*say)(const char *))
{
    char a[48], b[128];
    wb_iconprefs_text(&ip, a, sizeof a, b, sizeof b);
    if (!set_var(LABELS, a, save) || !set_var(LAYOUT, b, save)) {
        say(save ? "Couldn't write ENV: and ENVARC:Sys/IconLabels or IconLayout." : "Couldn't write ENV:Sys/IconLabels or IconLayout.");
        return 0;
    }
    if (!set_var(DESKICONS_VAR, deskicons_text(noicons), save)) {
        say(save ? "Couldn't write ENV: and ENVARC:Sys/DesktopIcons." : "Couldn't write ENV:Sys/DesktopIcons.");
        return 0;
    }
    return 1;
}

/* After a Test, ENV: as it was when the page opened */
static void put_back(void)
{
    if (!tested) return;
    if (had_labels) set_var(LABELS, was_labels, 0);
    else DeleteVar((CONST_STRPTR)LABELS, GVF_GLOBAL_ONLY);
    if (had_layout) set_var(LAYOUT, was_layout, 0);
    else DeleteVar((CONST_STRPTR)LAYOUT, GVF_GLOBAL_ONLY);
    if (had_desk) set_var(DESKICONS_VAR, was_desk, 0);
    else DeleteVar((CONST_STRPTR)DESKICONS_VAR, GVF_GLOBAL_ONLY);
    tested = 0;
}

/* ---- the window ------------------------------------------------------------------------------------ */

enum { G_LABELS = 1, G_GAPLABEL, G_GAP, G_GAPTEXT, G_CELL, G_SNAP, G_ORDER, G_COLUMNS, G_TONE, G_NOICONS, G_FONTS, G_FONTTEXT,
       G_STATUS, G_SAVE, G_USE, G_TEST, G_CANCEL, G_COUNT };

static const char *const labels_names[] = { "Close to the picture", "Below the picture (OS 3.2.3)", NULL };
static const char *const cell_names[] = { "Auto (from the icons)", "32 pixels", "40 pixels", "48 pixels", "56 pixels",
                                          "64 pixels", "80 pixels", "96 pixels", "128 pixels", NULL };
static const int cell_px[] = { 0, 32, 40, 48, 56, 64, 80, 96, 128 };
#define NCELLS 9
static const char *const order_names[] = { "Name", "Kind", "Size", "Date", NULL };
static const char *const columns_names[] = { "Down the left", "Down the right", NULL };
static const char *const tone_names[] = { "From the backdrop", "The theme's", NULL };

static struct Window *win;
static struct Gadget *gad[G_COUNT];
static char status_text[96], gap_text[24];

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    status_text[sizeof status_text - 1] = 0;
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

static void gap_for(int g)
{
    snprintf(gap_text, sizeof gap_text, g == 1 ? "%d pixel" : "%d pixels", g);
    if (win && gad[G_GAPTEXT]) GT_SetGadgetAttrs(gad[G_GAPTEXT], win, NULL, GTTX_Text, (ULONG)gap_text, TAG_DONE);
}

static int cell_index(int px)
{
    int i;
    for (i = 0; i < NCELLS; i++)
        if (cell_px[i] == px) return i;
    return 0;
}

static void say_shell(const char *s) { Printf((STRPTR)"Icons: %s\n", (ULONG)s); }

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, (STRPTR)"Cancel", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_END, 0, 0, 0, 0, 0 }
};

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    struct Menu *menu = NULL;
    struct RastPort *rp;
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, lw, cw, i;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    rp = &scr->RastPort;

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    lw = TextLength(rp, (STRPTR)"Desktop columns", 15) + 16;           /* the labels' column */
    X = 12 + lw;
    cw = TextLength(rp, (STRPTR)"Below the picture (OS 3.2.3)", 28) + 48;  /* the widest cycle */
    W = X + cw + 12;
    i = 12 + TextLength(rp, (STRPTR)"The labels are in Font prefs' Workbench icon text font.", 56) + 12;
    if (W < i) W = i;
    snprintf(status_text, sizeof status_text, "%s", "Change what you like, then Test, Use or Save.");
    snprintf(gap_text, sizeof gap_text, ip.gap == 1 ? "%d pixel" : "%d pixels", ip.gap);

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#ifdef LT_ONE
    lt_make(NULL, NULL, 10, top, W - 24, lh, LT_ICONS);    /* the frame's sections (OpenPrefs) */
    top += lt_space(scr->Font, W - 24, lh);
#endif
    row = top;
    G(CYCLE_KIND, G_LABELS, X, row, cw, lh, "Labels", PLACETEXT_LEFT, GTCY_Labels, (ULONG)labels_names,
      GTCY_Active, ip.close ? 0 : 1);
    row += lh + 4;
    /* the slider's own label isn't drawn in OpenPrefs's window (IControl's Verify time-out neither): a text of its own */
    G(TEXT_KIND, G_GAPLABEL, 12, row, X - 12 - 8, lh, NULL, 0, GTTX_Text, (ULONG)"Label gap", GTTX_Justification, GTJ_RIGHT);
    G(SLIDER_KIND, G_GAP, X, row, 120, lh, NULL, 0, GTSL_Min, 0, GTSL_Max, WB_GAP_MAX,
      GTSL_Level, ip.gap, GA_RelVerify, TRUE, GA_Immediate, TRUE, GA_Disabled, !ip.close);
    G(TEXT_KIND, G_GAPTEXT, X + 128, row, W - 12 - (X + 128), lh, NULL, 0, GTTX_Text, (ULONG)gap_text);
    row += lh + 10;
    G(CYCLE_KIND, G_CELL, X, row, cw, lh, "Cell size", PLACETEXT_LEFT, GTCY_Labels, (ULONG)cell_names,
      GTCY_Active, cell_index(ip.cell));
    row += lh + 4;
    G(CHECKBOX_KIND, G_SNAP, X, row, 26, lh, "Snap dropped icons to the grid", PLACETEXT_RIGHT, GTCB_Checked, ip.snap,
      GTCB_Scaled, TRUE);
    row += lh + 4;
    G(CYCLE_KIND, G_ORDER, X, row, cw, lh, "Clean Up order", PLACETEXT_LEFT, GTCY_Labels, (ULONG)order_names,
      GTCY_Active, ip.order);
    row += lh + 4;
    G(CYCLE_KIND, G_COLUMNS, X, row, cw, lh, "Desktop columns", PLACETEXT_LEFT, GTCY_Labels, (ULONG)columns_names,
      GTCY_Active, ip.right ? 1 : 0);
    row += lh + 4;
    /* the desktop's labels light on a dark backdrop and dark on a light one, as its picture's .tone says (OpenUp's
     * backdrops), or in the theme's colour */
    G(CYCLE_KIND, G_TONE, X, row, cw, lh, "Desktop labels", PLACETEXT_LEFT, GTCY_Labels, (ULONG)tone_names,
      GTCY_Active, ip.tone ? 0 : 1);
    row += lh + 4;
    /* 0.2: no icons on the desktop at all (OpenBench's Window > No icons; Icons > Clean Up there brings them back) */
    G(CHECKBOX_KIND, G_NOICONS, X, row, 26, lh, "No icons on the desktop", PLACETEXT_RIGHT,
      GTCB_Checked, noicons == DESKICONS_NONE, GTCB_Scaled, TRUE);
    row += lh + 10;
    G(TEXT_KIND, G_FONTTEXT, 12, row, W - 24, lh, NULL, 0, GTTX_Text,
      (ULONG)"The labels are in Font prefs' Workbench icon text font.");
    row += lh + 2;
    G(BUTTON_KIND, G_FONTS, X, row, cw, lh, "Icon text font...", 0, GA_Disabled, FALSE);
    row += lh + 10;
    G(TEXT_KIND, G_STATUS, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE);
    row += lh + 6;
    {
        static const char *const names[4] = { "Save", "Use", "Test", "Cancel" };
        static const int ids[4] = { G_SAVE, G_USE, G_TEST, G_CANCEL };
        int bw = (W - 24 - 3 * 10) / 4;
        for (i = 0; i < 4; i++) G(BUTTON_KIND, ids[i], 12 + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
#define IDCMP_ALL (IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | CHECKBOXIDCMP | SLIDERIDCMP | \
                   CYCLEIDCMP | TEXTIDCMP)
#ifdef LT_ONE
    win = lt_open(scr, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, scr->BarHeight + 20, W, row - scr->WBorTop - fh - 1,
                  glist, IDCMP_ALL, "Icons 0.2");
#else
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Icons", WA_ScreenTitle, (ULONG)"Icons 0.2", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_IDCMP, IDCMP_ALL, TAG_DONE);
#endif
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    lt_note_to(status);

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { put_back(); quit = 1; }
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); LT_REFRESH(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: if (put(1, status)) quit = 1; break;
                    case 2: if (put(0, status)) quit = 1; break;
                    case 3: put_back(); quit = 1; break;
                    }
                    code = it->NextSelect;
                }
            }
            else if ((cls == IDCMP_MOUSEMOVE || cls == IDCMP_GADGETUP) && gg && gg->GadgetID == G_GAP) {
                ip.gap = (int)(WORD)code;
                gap_for(ip.gap);
            }
            else if (cls == IDCMP_GADGETUP && gg) {
                switch (gg->GadgetID) {
#ifdef LT_ONE
                default: {                          /* another section: the page's changes kept, as Use, when it has any */
                    int to = lt_hit(gg, code);
                    if (to >= 0) { if (LT_DIRTY) put(0, status); lt_go(win, to); quit = 1; }
                    break;
                }
#endif
                case G_LABELS:
                    ip.close = code == 0;
                    GT_SetGadgetAttrs(gad[G_GAP], win, NULL, GA_Disabled, !ip.close, TAG_DONE);
                    break;
                case G_CELL: ip.cell = code < NCELLS ? cell_px[code] : 0; break;
                case G_SNAP: ip.snap = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_ORDER: ip.order = code < 4 ? code : 0; break;
                case G_COLUMNS: ip.right = code == 1; break;
                case G_TONE: ip.tone = code == 0; break;
                case G_NOICONS: noicons = (gg->Flags & GFLG_SELECTED) ? DESKICONS_NONE : DESKICONS_SHOW; break;
                case G_FONTS:                       /* the Fonts page, this one's changes kept as Use */
#ifdef LT_ONE
                    if (LT_DIRTY) put(0, status);
                    lt_go(win, LT_FONTS);
                    quit = 1;
#else
                    status("The icon text font is set on OpenPrefs' Fonts page.");
#endif
                    break;
                case G_TEST:
                    if (!tested) {
                        had_labels = GetVar((CONST_STRPTR)LABELS, (STRPTR)was_labels, sizeof was_labels, GVF_GLOBAL_ONLY) >= 0;
                        had_layout = GetVar((CONST_STRPTR)LAYOUT, (STRPTR)was_layout, sizeof was_layout, GVF_GLOBAL_ONLY) >= 0;
                        had_desk = GetVar((CONST_STRPTR)DESKICONS_VAR, (STRPTR)was_desk, sizeof was_desk, GVF_GLOBAL_ONLY) >= 0;
                    }
                    if (put(0, status)) {
                        tested = 1;
                        status("Testing: the desktop and the drawers show it. Use or Save keeps it.");
                    }
                    break;
                case G_SAVE: if (put(1, status)) { tested = 0; quit = 1; } break;
                case G_USE: if (put(0, status)) { tested = 0; quit = 1; } break;
                case G_CANCEL: put_back(); quit = 1; break;
                }
            }
        }
    }
out:
#ifdef LT_ONE
    if (win) { ClearMenuStrip(win); lt_close(win, glist); win = NULL; }
#else
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
#endif
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

/* A word from a list, case aside: its index, or -1 */
static int word_of(const char *w, const char *const *list)
{
    int i;
    for (i = 0; w && list[i]; i++)
        if (!Stricmp((CONST_STRPTR)w, (CONST_STRPTR)list[i])) return i;
    return -1;
}

/*   Icons [LABELS close|below] [GAP n] [CELL auto|px] [SNAP on|off] [ORDER name|kind|size|date]
 *         [COLUMNS left|right] [LABELCOLOUR backdrop|theme] [DESKTOP show|none] [USE] [SAVE]
 * (OpenPrefs ICONS ...): the settings given over the ones in ENV:; with USE or SAVE written without a window.
 * DESKTOP (0.2): the desktop's icons shown (show, icons, on) or none at all (none, noicons, off). */
int main(int argc, char **argv)
{
    static const char *const lb[] = { "close", "below", "on", "off", NULL }, *const onoff[] = { "on", "off", "yes", "no", NULL };
    static const char *const ord[] = { "name", "kind", "size", "date", NULL }, *const side[] = { "left", "right", NULL };
    static const char *const tone[] = { "theme", "backdrop", NULL };
    LONG args[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"LABELS/K,GAP/K/N,CELL/K,SNAP/K,ORDER/K,COLUMNS/K,USE/S,SAVE/S,LABELCOLOUR/K,"
                                            "DESKTOP/K", args, NULL) : NULL;
    char a[48], b[128], d[16];
    int rc, ha, hb, hd, k;
    (void)argv;
    (void)version;
    win = NULL;                             /* built into OpenPrefs, the statics outlive a run */
    tested = 0;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Icons"); return RETURN_FAIL; }
    ha = read_var(LABELS, a, sizeof a);
    hb = read_var(LAYOUT, b, sizeof b);
    hd = read_var(DESKICONS_VAR, d, sizeof d);
    wb_iconprefs_parse(&ip, ha ? a : NULL, hb ? b : NULL);
    noicons = deskicons_parse(hd ? d : NULL, -1);
    if ((k = word_of((const char *)args[0], lb)) >= 0) ip.close = !(k & 1);
    if (args[1]) { k = (int)*(LONG *)args[1]; if (k >= 0 && k <= WB_GAP_MAX) ip.gap = k; }
    if (args[2]) {
        int px = 0;
        if (Stricmp((CONST_STRPTR)args[2], (CONST_STRPTR)"auto")) StrToLong((CONST_STRPTR)args[2], (LONG *)&px);
        if (px == 0 || (px >= WB_CELL_MIN && px <= WB_CELL_MAX)) ip.cell = px;
    }
    if ((k = word_of((const char *)args[3], onoff)) >= 0) ip.snap = !(k & 1);
    if ((k = word_of((const char *)args[4], ord)) >= 0) ip.order = k;
    if ((k = word_of((const char *)args[5], side)) >= 0) ip.right = k;
    if ((k = word_of((const char *)args[8], tone)) >= 0) ip.tone = k;
    if ((k = deskicons_word((const char *)args[9], -1)) >= 0) noicons = k;
    if (args[6] || args[7]) rc = put(args[7] != 0, say_shell) ? RETURN_OK : RETURN_ERROR;
    else rc = gui();
    if (rd) FreeArgs(rd);
    return rc;
}
