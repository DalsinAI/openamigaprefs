/* OpenPrefs Menus 0.1: the editor for OpenMenus, our own menus (MIT). A
 * GadTools prefs editor, as the OS's own: Save, Use, Test (put back after
 * 15 seconds unless kept) and Cancel. What it sets lives in
 * ENV:OpenMenus/Menus (ENVARC: when saved), in the format of om_prefs.h;
 * OpenMenus draws.
 *
 *   - how menus open: from the screen bar, at the pointer, or by where
 *     the pointer is; hold and release, or sticky
 *   - a short delay before a menu opens, for each kind
 *   - submenus centred and marked, a single or double border
 *   - a shadow (size and strength), a solid, see-through or image background
 *   - colours from the theme (OpenLook), the screen's own, or the user's
 *   - keyboard control, with its key
 *   - whether programs keep running while a menu is open
 *   - MagicMenu's settings, taken over on the first start, or on request
 *
 * It opens in the Simple view, theme first: whether OpenMenus is on, how menus
 * open and work, colours from the theme, a shadow. View > Advanced (Amiga-A)
 * shows every setting. The view is shared by every OpenPrefs editor, in
 * ENV:OpenAmiga/PrefsView ("simple" or "advanced"); it hides gadgets only and
 * never changes a setting.
 *
 *   Menus [FROM file] [MAGICMENU file] [USE] [SAVE] [ADVANCED]
 *
 * From the Shell, MAGICMENU takes over a MagicMenu settings file (by default
 * ENVARC:MagicMenu.prefs) and USE or SAVE puts it in place with no window:
 * OpenUp's part does this once when it installs OpenMenus.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/gfxmacros.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/asl.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "om_prefs.h"
#include "ogt_theme.h"

const char version[] __attribute__((used)) = "$VER: Menus 0.1 (6.10.2026) OpenPrefs, Dalsin Limited";

#define PREFS_ENV "ENV:OpenMenus/Menus"
#define PREFS_ENVARC "ENVARC:OpenMenus/Menus"
#define MM_ENV "ENV:MagicMenu.prefs"
#define MM_ENVARC "ENVARC:MagicMenu.prefs"
#define LOOK_ENV "ENV:OpenGadTools/Look"
#define THEME_DIR "SYS:Prefs/Presets/Themes"
#define TEST_SECONDS 15
#define VIEW_ENV "ENV:OpenAmiga/PrefsView"
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"
#define LOOK_TOOL "SYS:Prefs/Look"

struct Library *AslBase;

static om_prefs cur, orig;              /* orig: what is in ENV: now, which Test and Cancel put back */

/* ---- files -------------------------------------------------------------------------------- */

static char *read_file(const char *path, LONG *lenp)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG len;
    char *buf;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len < 0 || len > 256 * 1024 || !(buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) { Close(fh); return NULL; }
    if (Read(fh, buf, len) != len) { FreeVec(buf); Close(fh); return NULL; }
    buf[len] = 0;
    Close(fh);
    if (lenp) *lenp = len;
    return buf;
}

static int write_file(const char *path, const void *data, LONG len)
{
    char dir[96], *slash;
    BPTR fh, lock;
    strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    if ((slash = strrchr(dir, '/'))) {
        *slash = 0;
        if ((lock = Lock((STRPTR)dir, ACCESS_READ))) UnLock(lock);
        else if ((lock = CreateDir((STRPTR)dir))) UnLock(lock);
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    if (Write(fh, (APTR)data, len) != len) { Close(fh); return 0; }
    Close(fh);
    return 1;
}

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

static int tell_openmenus(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)"OpenMenus"))) Signal(p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
    return p != NULL;
}

static int magicmenu_running(void)
{
    struct MsgPort *p;
    Forbid();
    p = FindPort((STRPTR)"MagicMenu");
    Permit();
    return p != NULL;
}

static int write_prefs(const om_prefs *p, int save)
{
    static char text[3072];
    int n = om_write(p, text, sizeof text);
    if (n < 0) return 0;
    if (!write_file(PREFS_ENV, text, n)) return 0;
    if (save && !write_file(PREFS_ENVARC, text, n)) return 0;
    return 1;
}

/* MagicMenu's settings onto p; the file used is noted so the take-over happens once. */
static int take_over(om_prefs *p, const char *path, char *err, int errlen)
{
    char *text = read_file(path, NULL);
    int ok;
    if (!text) { snprintf(err, errlen, "%s can't be read", path); return 0; }
    ok = om_from_magicmenu(p, text, err, errlen);
    FreeVec(text);
    if (ok) { strncpy(p->imported, path, sizeof p->imported - 1); p->imported[sizeof p->imported - 1] = 0; }
    return ok;
}

static const char *magicmenu_file(void)
{
    return exists(MM_ENVARC) ? MM_ENVARC : exists(MM_ENV) ? MM_ENV : NULL;
}

/* ---- the view: simple or advanced, shared by every OpenPrefs editor ----------------------------- */

static int advanced;

static void read_view(void)
{
    char *t = read_file(VIEW_ENV, NULL);
    if (!t) t = read_file(VIEW_ENVARC, NULL);
    if (t) { advanced = !strncmp(t, "advanced", 8); FreeVec(t); }
}

static void write_view(void)
{
    const char *v = advanced ? "advanced\n" : "simple\n";
    write_file(VIEW_ENV, v, strlen(v));
    write_file(VIEW_ENVARC, v, strlen(v));
}

/* ---- the theme, for the preview ------------------------------------------------------------- */

static ogt_theme theme;
static int theme_loaded, theme_dark, lite_on;
static char theme_line[64] = "Theme: Open, light";

/* The theme and mode in Look prefs (first line "<theme> light|dark|auto"), and Lite. */
static void load_theme(void)
{
    char *look = read_file(LOOK_ENV, NULL), name[48] = "Open", mode[8] = "light", path[128], err[100];
    char *text;
    if (look) {
        sscanf(look, "%47s %7s", name, mode);
        lite_on = strstr(look, "\nlite on") != NULL;
        FreeVec(look);
    }
    theme_dark = !strcmp(mode, "dark");
    snprintf(theme_line, sizeof theme_line, "Theme: %s, %s%s", name, mode, lite_on ? ", Lite" : "");
    if (!strcmp(mode, "auto")) {
        struct DateStamp ds;
        int hour;
        DateStamp(&ds);
        hour = (int)(ds.ds_Minute / 60);
        theme_dark = hour >= 19 || hour < 7;
    }
    snprintf(path, sizeof path, THEME_DIR "/%s.theme", name);
    if ((text = read_file(path, NULL))) {
        if (ogt_theme_parse(&theme, text, err, sizeof err)) theme_loaded = 1;
        FreeVec(text);
    }
}

/* ---- the preview: a menu as OpenMenus would draw it -------------------------------------- */

static LONG pens[32];
static int npens;

static void free_pens(struct Screen *scr)
{
    for (int i = 0; i < npens; i++) ReleasePen(scr->ViewPort.ColorMap, pens[i]);
    npens = 0;
}

static LONG pen_for(struct Screen *scr, ogt_rgb c)
{
    LONG pen = ObtainBestPen(scr->ViewPort.ColorMap, (ULONG)c.r * 0x01010101UL, (ULONG)c.g * 0x01010101UL,
                             (ULONG)c.b * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    if (pen >= 0) { if (npens < 32) pens[npens++] = pen; else ReleasePen(scr->ViewPort.ColorMap, pen); }
    return pen;
}

/* The colour a menu part is drawn in, by the Colours choice. */
static ogt_rgb part_colour(const om_prefs *p, struct Screen *scr, int part)
{
    static const char *const keys[OM_C_COUNT] = { "menu", "menu.text", "accent", "accent.text", "frame.highlight", "menu.line", NULL };
    ogt_rgb c = { 0, 0, 0 };
    if (p->colours == OM_COL_OWN || (p->colours == OM_COL_THEME && (!theme_loaded || theme.passthrough))) {
        if (p->colours == OM_COL_OWN) { ogt_parse_colour(p->colour[part], &c); return c; }
    }
    if (p->colours == OM_COL_THEME && theme_loaded && !theme.passthrough && keys[part] &&
        ogt_theme_colour(&theme, theme_dark ? OGT_DARK : OGT_LIGHT, keys[part], &c))
        return c;
    {
        /* the screen's own: its DrawInfo pens */
        struct DrawInfo *dri = GetScreenDrawInfo(scr);
        static const int dpen[OM_C_COUNT] = { BARBLOCKPEN, BARDETAILPEN, BARDETAILPEN, BARBLOCKPEN, SHINEPEN, SHADOWPEN, SHADOWPEN };
        ULONG rgb[3] = { 0, 0, 0 };
        if (dri) {
            GetRGB32(scr->ViewPort.ColorMap, dri->dri_Pens[dpen[part]], 1, rgb);
            FreeScreenDrawInfo(scr, dri);
        }
        c.r = rgb[0] >> 24; c.g = rgb[1] >> 24; c.b = rgb[2] >> 24;
    }
    return c;
}

static void box(struct RastPort *rp, LONG pen, int x, int y, int w, int h)
{
    if (pen < 0 || w <= 0 || h <= 0) return;
    SetAPen(rp, (UBYTE)pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

static void draw_preview(struct Window *win, int x, int y, int w, int h, const om_prefs *p)
{
    struct RastPort *rp = win->RPort;
    struct Screen *scr = win->WScreen;
    static const char *const items[] = { "Open...", "Save", "Save as...", NULL, "Information", "Quit" };
    int fh = rp->TxHeight, ih = fh + 3, mw = w - 40, mx = x + 14, my = y + 8, mh = 6 * ih + 2, i;
    int shadow = p->shadow && !lite_on, s = shadow ? p->shadow_size : 0;
    LONG bg, tx, sel, seltx, light, dark, sh;
    free_pens(scr);
    box(rp, pen_for(scr, (ogt_rgb){ 150, 156, 166 }), x, y, w, h);            /* the "screen" behind */
    if (!p->enabled) {
        SetAPen(rp, 1); SetDrMd(rp, JAM1);
        Move(rp, x + 8, y + h / 2); Text(rp, (STRPTR)"The OS's own menus", 18);
        return;
    }
    bg = pen_for(scr, part_colour(p, scr, OM_C_BACKGROUND));
    tx = pen_for(scr, part_colour(p, scr, OM_C_TEXT));
    sel = pen_for(scr, part_colour(p, scr, OM_C_SELECTED));
    seltx = pen_for(scr, part_colour(p, scr, OM_C_SELECTED_TEXT));
    light = pen_for(scr, part_colour(p, scr, OM_C_LIGHT));
    dark = pen_for(scr, part_colour(p, scr, OM_C_DARK));
    sh = pen_for(scr, p->colours == OM_COL_OWN ? part_colour(p, scr, OM_C_SHADOW) : (ogt_rgb){ 60, 60, 66 });
    if (s) { box(rp, sh, mx + mw, my + s, s, mh); box(rp, sh, mx + s, my + mh, mw, s); }
    box(rp, dark, mx, my, mw, mh);
    box(rp, light, mx, my, mw - 1, 1); box(rp, light, mx, my, 1, mh - 1);
    if (p->border_double) { box(rp, dark, mx + 2, my + 2, mw - 4, mh - 4); box(rp, light, mx + 2, my + 2, mw - 5, 1); }
    box(rp, bg, mx + (p->border_double ? 3 : 1), my + (p->border_double ? 3 : 1), mw - (p->border_double ? 6 : 2), mh - (p->border_double ? 6 : 2));
    if (p->background == OM_BG_SEE || p->background == OM_BG_SEEIMAGE) {
        /* see-through: the screen's colour shows in a dotted pattern */
        static UWORD dots[2] = { 0x5555, 0xaaaa };
        SetAfPt(rp, dots, 1);
        box(rp, pen_for(scr, (ogt_rgb){ 150, 156, 166 }), mx + 3, my + 3, mw - 6, mh - 6);
        SetAfPt(rp, NULL, 0);
    }
    SetDrMd(rp, JAM1);
    for (i = 0; i < 6; i++) {
        int iy = my + 1 + i * ih;
        if (!items[i]) {
            box(rp, dark, mx + (p->separators_bold ? 2 : 8), iy + ih / 2, mw - (p->separators_bold ? 4 : 16), 1);
            continue;
        }
        if (i == 1) box(rp, sel, mx + 3, iy, mw - 6, ih);
        if ((i == 1 ? seltx : tx) >= 0) SetAPen(rp, (UBYTE)(i == 1 ? seltx : tx));
        Move(rp, mx + 8, iy + rp->TxBaseline + 1);
        Text(rp, (STRPTR)items[i], strlen(items[i]));
        if (i == 2 && p->sub_mark) {                       /* a submenu's mark */
            int ax = mx + mw - 12, ay = iy + ih / 2;
            for (int k = 0; k < 4; k++) { Move(rp, ax + k, ay - 3 + k); Draw(rp, ax + k, ay + 3 - k); }
        }
    }
}

/* ---- the window ---------------------------------------------------------------------------- */

enum {
    G_OPEN, G_PDUSE, G_PUUSE, G_PDDELAY, G_PUDELAY, G_LAST, G_CENTRE, G_MARK, G_RUNNING,
    G_KEYBOARD, G_KEY, G_RALT, G_TOP,
    G_COLOURS, G_WHICH, G_HEX, G_BORDER, G_SHADOW, G_SSIZE, G_SSTRENGTH, G_BG, G_IMAGE, G_CHOOSE, G_SEPS,
    G_ENABLED, G_TAKEOVER, G_FEEL, G_THEME, G_LOOK, G_STATUS, G_SAVE, G_USE, G_TEST, G_CANCEL, G_COUNT
};

static const char *open_labels[] = { "From the bar", "At the pointer", "Bar or pointer", NULL };
static const char *use_labels[] = { "Hold and release", "Sticky, on move", "Sticky, on click", NULL };
static const char *colours_labels[] = { "From the theme", "The screen's own", "My own", NULL };
static const char *which_labels[] = { "Background", "Text", "Selected", "Selected text", "Bright edges", "Dark edges", "Shadow", NULL };
static const char *border_labels[] = { "Single", "Double", NULL };
static const char *bg_labels[] = { "Solid", "See-through", "Image", "See-through image", NULL };
static const char *seps_labels[] = { "Lite", "Bold", NULL };

static struct Gadget *gad[G_COUNT];
static struct Window *win;
static int px, py, pw, ph, which;
static char status_text[120];

/* a gadget the view leaves out is NULL, and setting it does nothing */
#define SET(id, ...) (gad[id] ? GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE) : (void)0)

static void status(const char *s)
{
    if (s != status_text) { strncpy(status_text, s, sizeof status_text - 1); status_text[sizeof status_text - 1] = 0; }
    SET(G_STATUS, GTTX_Text, (ULONG)status_text);
}

static void show(const om_prefs *p)
{
    int own = p->colours == OM_COL_OWN, img = p->background == OM_BG_IMAGE || p->background == OM_BG_SEEIMAGE;
    SET(G_OPEN, GTCY_Active, p->open);
    SET(G_PDUSE, GTCY_Active, p->use[OM_PD], GA_Disabled, p->open == OM_OPEN_POPUP);
    SET(G_PUUSE, GTCY_Active, p->use[OM_PU], GA_Disabled, p->open == OM_OPEN_PULLDOWN);
    SET(G_PDDELAY, GTSL_Level, p->delay[OM_PD], GA_Disabled, p->open == OM_OPEN_POPUP);
    SET(G_PUDELAY, GTSL_Level, p->delay[OM_PU], GA_Disabled, p->open == OM_OPEN_PULLDOWN);
    SET(G_LAST, GTCB_Checked, p->popup_last, GA_Disabled, p->open == OM_OPEN_PULLDOWN);
    SET(G_CENTRE, GTCB_Checked, p->sub_centre);
    SET(G_MARK, GTCB_Checked, p->sub_mark);
    SET(G_RUNNING, GTCB_Checked, p->keep_running);
    SET(G_KEYBOARD, GTCB_Checked, p->keyboard);
    SET(G_KEY, GTST_String, (ULONG)p->key, GA_Disabled, !p->keyboard);
    SET(G_RALT, GTCB_Checked, p->keyboard_ralt, GA_Disabled, !p->keyboard);
    SET(G_TOP, GTCB_Checked, p->keyboard_top, GA_Disabled, !p->keyboard);
    SET(G_COLOURS, GTCY_Active, p->colours);
    SET(G_WHICH, GTCY_Active, which, GA_Disabled, !own);
    SET(G_HEX, GTST_String, (ULONG)p->colour[which], GA_Disabled, !own);
    SET(G_BORDER, GTCY_Active, p->border_double);
    SET(G_SHADOW, GTCB_Checked, p->shadow);
    SET(G_SSIZE, GTSL_Level, p->shadow_size, GA_Disabled, !p->shadow);
    SET(G_SSTRENGTH, GTSL_Level, p->shadow_strength, GA_Disabled, !p->shadow);
    SET(G_BG, GTCY_Active, p->background);
    SET(G_IMAGE, GTST_String, (ULONG)p->image, GA_Disabled, !img);
    SET(G_CHOOSE, GA_Disabled, !img || !AslBase);
    SET(G_SEPS, GTCY_Active, p->separators_bold);
    SET(G_ENABLED, GTCB_Checked, p->enabled);
    SET(G_TAKEOVER, GA_Disabled, magicmenu_file() == NULL);
    SET(G_FEEL, GTCY_Active, p->use[p->open == OM_OPEN_POPUP ? OM_PU : OM_PD]);
    SET(G_LOOK, GA_Disabled, !exists(LOOK_TOOL));
    draw_preview(win, px, py, pw, ph, p);
}

static void choose_image(void)
{
    struct FileRequester *fr;
    char dir[256], *part;
    strncpy(dir, cur.image, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    part = (char *)PathPart((STRPTR)dir);
    *part = 0;
    fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)"Choose a picture for the menus",
                             ASLFR_Window, (ULONG)win, ASLFR_InitialDrawer, (ULONG)(dir[0] ? dir : "SYS:Prefs/Patterns"),
                             ASLFR_InitialFile, (ULONG)FilePart((STRPTR)cur.image), TAG_DONE);
    if (!fr) return;
    if (AslRequest(fr, NULL)) {
        strncpy(cur.image, (char *)fr->fr_Drawer, sizeof cur.image - 1);
        cur.image[sizeof cur.image - 1] = 0;
        AddPart((STRPTR)cur.image, fr->fr_File, sizeof cur.image);
    }
    FreeAslRequest(fr);
}

static ULONG test_until;

static ULONG now_seconds(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400 + (ULONG)ds.ds_Minute * 60 + (ULONG)ds.ds_Tick / TICKS_PER_SECOND;
}

static int put_in_place(int save)
{
    if (!write_prefs(&cur, save)) {
        status(save ? "The settings couldn't be saved. Is the system disk write-protected or full?" : "The settings couldn't be written. Is ENV: full?");
        return 0;
    }
    tell_openmenus();
    return 1;
}

static void go_back(void)
{
    write_prefs(&orig, 0);
    tell_openmenus();
}

static void opening_hint(void)
{
    if (magicmenu_running()) status("MagicMenu is running too: quit it (Exchange) so OpenMenus can draw the menus.");
    else if (cur.imported[0] && !orig.imported[0]) {
        snprintf(status_text, sizeof status_text, "Took over MagicMenu's settings. Save keeps them.");
        status(status_text);
    }
    else if (lite_on && (cur.shadow || cur.background == OM_BG_SEE || cur.background == OM_BG_SEEIMAGE))
        status("Lite is on in Look: OpenMenus leaves out shadows and see-through.");
}

static int handle_key(UWORD code)
{
    switch (code | 0x20) {
    case 's': return G_SAVE;
    case 'u': return G_USE;
    case 't': return G_TEST;
    case 'c': return G_CANCEL;
    }
    return code == 27 ? G_CANCEL : -1;
}

/* The widest of some words in the screen's font. */
static int widest(struct Screen *scr, const char *const *words)
{
    int w = 0;
    for (; *words; words++) {
        int t = TextLength(&scr->RastPort, (STRPTR)*words, strlen(*words));
        if (t > w) w = t;
    }
    return w;
}

static int text_w(struct Screen *scr, const char *s) { return TextLength(&scr->RastPort, (STRPTR)s, strlen(s)); }

/* The gadgets for the view, laid out from the screen font's own widths;
 * the window's inner height (and *Wp its inner width), or 0. */
static int make_gadgets(struct Screen *scr, APTR vi, struct Gadget **glist, int *Wp)
{
    static const char *const left_words[] = { "Menus open", "From the bar", "At the pointer", "Bar delay", "Pointer delay", "Open on last",
                                              "Centre submenus", "Programs run on", "Keyboard", "R.Amiga+R.Alt", "Use OpenMenus", "They work",
                                              "Colours", "Shadow", NULL };
    static const char *const right_words[] = { "Colours", "Border", "Shadow", "Strength", "Background", "Picture", NULL };
    struct Gadget *g;
    struct NewGadget ng;
    int fh = scr->Font->ta_YSize, lh = fh + 6, bl = scr->WBorLeft, top = scr->WBorTop + fh + 1 + 6, row, row2, i;
    int gp = scr->Height < 320 ? 2 : 4, cb = 26, slv = 3 * scr->RastPort.TxWidth + 8;    /* a checkbox; a slider's number */
    /* the left column: labels, then gadgets as wide as the widest choice */
    int lx = bl + 8 + widest(scr, left_words) + 8;
    int cw = widest(scr, open_labels) > widest(scr, use_labels) ? widest(scr, open_labels) : widest(scr, use_labels);
    int lw, rx0, rx, rg, bw, sw, hexw, chw, W;
    if (widest(scr, colours_labels) > cw) cw = widest(scr, colours_labels);
    lw = cw + 30;                                                /* and the cycle's mark */
    if (lw < 2 * cb + 16 + text_w(scr, "Mark them")) lw = 2 * cb + 16 + text_w(scr, "Mark them");
    if (lw < 2 * cb + 16 + text_w(scr, "Pointer up")) lw = 2 * cb + 16 + text_w(scr, "Pointer up");
    /* the right column: labels, then gadgets */
    rx0 = lx + lw + 16;
    rx = rx0 + widest(scr, right_words) + 8;
    bw = widest(scr, border_labels) + 30;
    sw = widest(scr, seps_labels) + 30;
    hexw = text_w(scr, "#000000") + 16;
    chw = text_w(scr, "Choose...") + 16;
    rg = widest(scr, bg_labels) + 30;
    if (rg < widest(scr, which_labels) + 30 + 6 + hexw) rg = widest(scr, which_labels) + 30 + 6 + hexw;
    if (rg < bw + 8 + text_w(scr, "Lines") + 8 + sw) rg = bw + 8 + text_w(scr, "Lines") + 8 + sw;
    if (rg < 160) rg = 160;
    W = rx + rg + 10 - bl;
    *Wp = W;
    memset(gad, 0, sizeof gad);
    g = CreateContext(glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))

    /* the right column: the preview, in both views */
    px = rx0; py = top; pw = rx + rg - rx0; ph = 6 * (fh + 3) + (gp < 4 ? 8 : 22);
    row = row2 = top;

    if (!advanced) {
        /* Simple: the theme first, and the few things people change */
        int look = text_w(scr, "Look...") + 20;
        G(CHECKBOX_KIND, G_ENABLED, lx, row, cb, lh, "Use OpenMenus", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + gp;
        G(CYCLE_KIND, G_OPEN, lx, row, lw, lh, "Menus open", PLACETEXT_LEFT, GTCY_Labels, (ULONG)open_labels); row += lh + gp;
        G(CYCLE_KIND, G_FEEL, lx, row, lw, lh, "They work", PLACETEXT_LEFT, GTCY_Labels, (ULONG)use_labels); row += lh + gp;
        G(CYCLE_KIND, G_COLOURS, lx, row, lw, lh, "Colours", PLACETEXT_LEFT, GTCY_Labels, (ULONG)colours_labels); row += lh + gp;
        G(CHECKBOX_KIND, G_SHADOW, lx, row, cb, lh, "Shadow", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 2 * gp;
        G(TEXT_KIND, G_THEME, bl + 8, row, lx + lw - look - 6 - (bl + 8), lh, NULL, 0, GTTX_Text, (ULONG)theme_line);
        G(BUTTON_KIND, G_LOOK, lx + lw - look, row, look, lh, "Look...", 0, GA_Disabled, FALSE); row += lh + 2 * gp;
        row2 += ph + 2 * gp;
        G(BUTTON_KIND, G_TAKEOVER, rx0, row2, pw, lh, "Take over MagicMenu's...", 0, GA_Disabled, FALSE); row2 += lh + 2 * gp;
        if (row2 > row) row = row2;
    } else {
        /* the left column: how menus open, and the keys */
        G(CYCLE_KIND, G_OPEN, lx, row, lw, lh, "Menus open", PLACETEXT_LEFT, GTCY_Labels, (ULONG)open_labels); row += lh + gp;
        G(CYCLE_KIND, G_PDUSE, lx, row, lw, lh, "From the bar", PLACETEXT_LEFT, GTCY_Labels, (ULONG)use_labels); row += lh + gp;
        G(CYCLE_KIND, G_PUUSE, lx, row, lw, lh, "At the pointer", PLACETEXT_LEFT, GTCY_Labels, (ULONG)use_labels); row += lh + gp;
        G(SLIDER_KIND, G_PDDELAY, lx, row, lw - slv, lh, "Bar delay", PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, OM_DELAY_MAX,
          GTSL_LevelFormat, (ULONG)"%2ld", GTSL_MaxLevelLen, 2, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE); row += lh + gp;
        G(SLIDER_KIND, G_PUDELAY, lx, row, lw - slv, lh, "Pointer delay", PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, OM_DELAY_MAX,
          GTSL_LevelFormat, (ULONG)"%2ld", GTSL_MaxLevelLen, 2, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE); row += lh + gp;
        G(CHECKBOX_KIND, G_LAST, lx, row, cb, lh, "Open on last", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + gp / 2;
        G(CHECKBOX_KIND, G_CENTRE, lx, row, cb, lh, "Centre submenus", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        G(CHECKBOX_KIND, G_MARK, lx + lw - cb, row, cb, lh, "Mark them", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + gp / 2;
        G(CHECKBOX_KIND, G_RUNNING, lx, row, cb, lh, "Programs run on", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 2 * gp;
        G(CHECKBOX_KIND, G_KEYBOARD, lx, row, cb, lh, "Keyboard", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        G(STRING_KIND, G_KEY, lx + cb + 8, row, lw - cb - 8, lh, NULL, 0, GTST_MaxChars, 60); row += lh + gp / 2;
        G(CHECKBOX_KIND, G_RALT, lx, row, cb, lh, "R.Amiga+R.Alt", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        G(CHECKBOX_KIND, G_TOP, lx + lw - cb, row, cb, lh, "Pointer up", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 2 * gp;

        /* the right column: below the preview, the look */
        row2 += ph + 2 * gp;
        G(CYCLE_KIND, G_COLOURS, rx, row2, rg, lh, "Colours", PLACETEXT_LEFT, GTCY_Labels, (ULONG)colours_labels); row2 += lh + gp;
        G(CYCLE_KIND, G_WHICH, rx, row2, rg - hexw - 6, lh, NULL, 0, GTCY_Labels, (ULONG)which_labels);
        G(STRING_KIND, G_HEX, rx + rg - hexw, row2, hexw, lh, NULL, 0, GTST_MaxChars, 7); row2 += lh + gp;
        G(CYCLE_KIND, G_BORDER, rx, row2, bw, lh, "Border", PLACETEXT_LEFT, GTCY_Labels, (ULONG)border_labels);
        G(CYCLE_KIND, G_SEPS, rx + rg - sw, row2, sw, lh, "Lines", PLACETEXT_LEFT, GTCY_Labels, (ULONG)seps_labels); row2 += lh + gp;
        G(CHECKBOX_KIND, G_SHADOW, rx, row2, cb, lh, "Shadow", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        G(SLIDER_KIND, G_SSIZE, rx + cb + 8, row2, rg - cb - 8 - slv, lh, NULL, 0, GTSL_Min, 1, GTSL_Max, OM_SHADOW_SIZE_MAX,
          GTSL_LevelFormat, (ULONG)"%2ld", GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE); row2 += lh + gp;
        G(SLIDER_KIND, G_SSTRENGTH, rx + cb + 8, row2, rg - cb - 8 - slv, lh, "Strength", PLACETEXT_LEFT, GTSL_Min, 1, GTSL_Max, OM_SHADOW_STRENGTH_MAX,
          GTSL_LevelFormat, (ULONG)"%3ld", GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE); row2 += lh + gp;
        G(CYCLE_KIND, G_BG, rx, row2, rg, lh, "Background", PLACETEXT_LEFT, GTCY_Labels, (ULONG)bg_labels); row2 += lh + gp;
        G(STRING_KIND, G_IMAGE, rx, row2, rg - chw - 6, lh, "Picture", PLACETEXT_LEFT, GTST_MaxChars, 250);
        G(BUTTON_KIND, G_CHOOSE, rx + rg - chw, row2, chw, lh, "Choose...", 0, GA_Disabled, FALSE); row2 += lh + 2 * gp;

        /* at the foot of the left column, which is the shorter */
        G(CHECKBOX_KIND, G_ENABLED, lx, row, cb, lh, "Use OpenMenus", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + gp;
        G(BUTTON_KIND, G_TAKEOVER, bl + 8, row, lx + lw - (bl + 8), lh, "Take over MagicMenu's...", 0, GA_Disabled, FALSE); row += lh + 2 * gp;
        if (row2 > row) row = row2;
    }
    G(TEXT_KIND, G_STATUS, bl + 8, row, W - 16, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + gp + 2;
    {
        static const char *const names[4] = { "_Save", "_Use", "_Test", "_Cancel" };
        static const int ids[4] = { G_SAVE, G_USE, G_TEST, G_CANCEL };
        int bwid = (W - 16 - 3 * 10) / 4;
        for (i = 0; i < 4; i++) G(BUTTON_KIND, ids[i], bl + 8 + i * (bwid + 10), row, bwid, lh, names[i], 0, GT_Underscore, '_');
        row += lh + gp + 2;
    }
#undef G
    return g ? row - scr->WBorTop - fh - 1 : 0;
}

/* The menu strip: Project > Quit, View > Advanced (Amiga-A). */
enum { M_QUIT = 1, M_ADVANCED };
static struct NewMenu newmenus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"View", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Advanced", (STRPTR)"A", CHECKIT | MENUTOGGLE, 0, (APTR)M_ADVANCED },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL;
    struct Menu *menus = NULL;
    int quit = 0, rc = RETURN_OK, W = 640, inner, wx = 40, wy;
    struct MsgPort *tport = NULL;
    struct timerequest *tr = NULL;
    int timer = 0;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    wy = scr->BarHeight + 10;
    strcpy(status_text, "Choose how menus open and look. Test shows it for 15 seconds.");
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);

  reopen:
    /* a view change opens the window again where it was, with the other view's gadgets */
    newmenus[3].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    if (!(inner = make_gadgets(scr, vi, &glist, &W))) { rc = RETURN_FAIL; goto out; }
    if (!(menus = CreateMenus(newmenus, GTMN_FullMenu, TRUE, TAG_DONE)) || !LayoutMenus(menus, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) {
        rc = RETURN_FAIL; goto out;
    }
    {
        /* the whole window on the screen: higher up, over the screen bar if need be */
        int tall = inner + scr->WBorTop + scr->Font->ta_YSize + 1 + scr->WBorBottom;
        int wide = W + scr->WBorLeft + scr->WBorRight;
        if (wy + tall > scr->Height) wy = scr->Height - tall > 0 ? scr->Height - tall : 0;
        if (wx + wide > scr->Width) wx = scr->Width - wide > 0 ? scr->Width - wide : 0;
    }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Menus", WA_ScreenTitle, (ULONG)"OpenPrefs Menus 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, wx, WA_Top, wy, WA_InnerWidth, W, WA_InnerHeight, inner,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_MENUPICK | BUTTONIDCMP | CYCLEIDCMP |
                                   STRINGIDCMP | CHECKBOXIDCMP | SLIDERIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    SetMenuStrip(win, menus);
    GT_RefreshWindow(win, NULL);
    show(&cur);
    opening_hint();

    while (!quit) {
        struct IntuiMessage *m;
        ULONG tsig = timer ? 1UL << tport->mp_SigBit : 0;
        int switch_view = 0;
        if (timer && test_until) {
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
        }
        Wait((1UL << win->UserPort->mp_SigBit) | tsig);
        if (timer && test_until) {
            if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
            WaitIO((struct IORequest *)tr);
            if (now_seconds() >= test_until) {
                go_back();
                test_until = 0;
                status("The test is over: the menus are back as they were.");
            } else {
                snprintf(status_text, sizeof status_text, "Testing: back in %lu s unless you choose Use or Save.",
                         (unsigned long)(test_until - now_seconds()));
                status(status_text);
            }
        }
        while (!quit && !switch_view && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int id = -1, redraw = 1;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) id = G_CANCEL;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); draw_preview(win, px, py, pw, ph, &cur); }
            else if (cls == IDCMP_VANILLAKEY) id = handle_key(code);
            else if (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN || cls == IDCMP_MOUSEMOVE) id = gg->GadgetID;
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL) {
                    struct MenuItem *it = ItemAddress(menus, code);
                    if (!it) break;
                    if ((ULONG)GTMENUITEM_USERDATA(it) == M_QUIT) id = G_CANCEL;
                    else if ((ULONG)GTMENUITEM_USERDATA(it) == M_ADVANCED) {
                        advanced = (it->Flags & CHECKED) != 0;
                        write_view();
                        switch_view = 1;
                    }
                    code = it->NextSelect;
                }
            }
            if (id < 0) continue;
            switch (id) {
            case G_OPEN: cur.open = code; break;
            case G_PDUSE: cur.use[OM_PD] = code; break;
            case G_PUUSE: cur.use[OM_PU] = code; break;
            case G_FEEL: cur.use[OM_PD] = cur.use[OM_PU] = code; break;
            case G_PDDELAY: cur.delay[OM_PD] = (WORD)code; redraw = 0; break;
            case G_PUDELAY: cur.delay[OM_PU] = (WORD)code; redraw = 0; break;
            case G_LAST: cur.popup_last = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
            case G_CENTRE: cur.sub_centre = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
            case G_MARK: cur.sub_mark = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_RUNNING: cur.keep_running = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
            case G_KEYBOARD: cur.keyboard = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_KEY:
                strncpy(cur.key, (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof cur.key - 1);
                redraw = 0;
                break;
            case G_RALT: cur.keyboard_ralt = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
            case G_TOP: cur.keyboard_top = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
            case G_COLOURS:
                cur.colours = code;
                if (!advanced && code == OM_COL_OWN) status("Your own colours are set in View > Advanced.");
                break;
            case G_WHICH: which = code; break;
            case G_HEX: {
                ogt_rgb c;
                const char *s = (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer;
                if (strlen(s) == 7 && ogt_parse_colour(s, &c)) strcpy(cur.colour[which], s);
                else status("A colour is #rrggbb, for example #365fa3.");
                break;
            }
            case G_BORDER: cur.border_double = code; break;
            case G_SEPS: cur.separators_bold = code; break;
            case G_SHADOW: cur.shadow = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_SSIZE: cur.shadow_size = (WORD)code; break;
            case G_SSTRENGTH: cur.shadow_strength = (WORD)code; redraw = 0; break;
            case G_BG: cur.background = code; break;
            case G_IMAGE:
                strncpy(cur.image, (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof cur.image - 1);
                break;
            case G_CHOOSE: choose_image(); break;
            case G_LOOK: SystemTags((STRPTR)"Run >NIL: <NIL: SYS:Prefs/Look", TAG_DONE); redraw = 0; break;
            case G_ENABLED: cur.enabled = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_TAKEOVER: {
                const char *f = magicmenu_file();
                char err[100];
                if (f && take_over(&cur, f, err, sizeof err)) status("Took over MagicMenu's settings: Use, Test or Save to put them in place.");
                else { snprintf(status_text, sizeof status_text, "MagicMenu's settings couldn't be taken over: %s.", f ? err : "there are none"); status(status_text); }
                break;
            }
            case G_TEST:
                put_in_place(0);
                test_until = now_seconds() + TEST_SECONDS;
                status("Testing: back in 15 s unless you choose Use or Save.");
                redraw = 0;
                break;
            /* a failed write keeps the window open, with the reason on the status line */
            case G_USE: if (put_in_place(0)) { test_until = 0; quit = 1; } redraw = 0; break;
            case G_SAVE: if (put_in_place(1)) { test_until = 0; quit = 1; } redraw = 0; break;
            case G_CANCEL: if (test_until) go_back(); quit = 1; break;
            default: redraw = 0;
            }
            if (redraw && !quit) show(&cur);
        }
        if (switch_view && !quit) {
            wx = win->LeftEdge; wy = win->TopEdge;
            ClearMenuStrip(win);
            free_pens(win->WScreen);
            CloseWindow(win); win = NULL;
            FreeGadgets(glist); glist = NULL;
            FreeMenus(menus); menus = NULL;
            goto reopen;
        }
    }
out:
    if (timer) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (win) { ClearMenuStrip(win); free_pens(win->WScreen); CloseWindow(win); }
    FreeMenus(menus);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

int main(void)
{
    LONG args[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"FROM,MAGICMENU/K,USE/S,SAVE/S,ADVANCED/S", args, NULL);
    char *text, err[100];
    int rc = RETURN_OK;
    if (!rd) { PrintFault(IoErr(), (STRPTR)"Menus"); return RETURN_FAIL; }
    /* orig is always what ENV: holds now; FROM only fills the window */
    text = read_file(PREFS_ENV, NULL);
    om_parse(&orig, text);
    if (text) FreeVec(text);
    if (!args[0]) cur = orig;
    else {
        if (!(text = read_file((const char *)args[0], NULL))) {
            Printf((STRPTR)"Menus: %s can't be read.\n", args[0]);
            FreeArgs(rd);
            return RETURN_FAIL;
        }
        om_parse(&cur, text);
        FreeVec(text);
    }

    if (args[1]) {                               /* MAGICMENU <file>: take it over */
        if (!take_over(&cur, (const char *)args[1], err, sizeof err)) {
            /* nothing else is written: USE or SAVE was for these settings */
            Printf((STRPTR)"Menus: MagicMenu's settings couldn't be taken over: %s.\n", (LONG)err);
            FreeArgs(rd);
            return RETURN_WARN;
        }
    } else if (!cur.imported[0] && !exists(PREFS_ENVARC) && magicmenu_file()) {
        /* the first start: MagicMenu's settings are taken over once */
        if (!take_over(&cur, magicmenu_file(), err, sizeof err)) cur.imported[0] = 0;
    }
    if (args[2] || args[3]) {                    /* from the Shell: no window */
        if (!write_prefs(&cur, args[3] != 0)) {
            PutStr((STRPTR)"Menus: the settings couldn't be written.\n");
            rc = RETURN_FAIL;
        } else tell_openmenus();
        FreeArgs(rd);
        return rc;
    }
    read_view();
    if (args[4]) advanced = 1;
    FreeArgs(rd);
    AslBase = OpenLibrary((STRPTR)"asl.library", 39);
    load_theme();
    rc = gui();
    if (theme_loaded) ogt_theme_free(&theme);
    if (AslBase) CloseLibrary(AslBase);
    return rc;
}
