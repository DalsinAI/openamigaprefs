/* OpenScreenMode: the Workbench's screen mode, with the overscan under it, as one section of OpenPrefs
 * (Dale, 21 October 2026: "overscan should be under screenmode", folded into OpenPrefs). It takes over the
 * OS's ScreenMode and Overscan editors (OpenUp's RECONCILE puts those in SYS:Prefs/Classic).
 *
 *   OpenScreenMode [FROM file] [USE] [SAVE]
 *
 *   - the mode, from graphics.library's display database (the modes that can show the Workbench), with its
 *     sizes and colours shown under the list
 *   - the width and height, each the mode's own (Default) or a number, the colours, and autoscroll
 *   - Overscan, for a chipset mode only (an RTG board's modes have none): the text overscan's size and the
 *     standard overscan's rectangle, for the mode's monitor, or Reset for the display database's own
 *
 * Save writes ENV:Sys/screenmode.prefs and ENVARC:, and overscan.prefs beside them when the overscan was
 * changed; Use writes ENV: only (the open ROM's prefs task, or IPrefs, reads them at once); Cancel changes
 * nothing. Every chunk and byte this section doesn't edit is kept as the file had it (dp_prefs.c).
 *
 * Not yet compared with OS 3.2.3's Overscan editor in a lab: the overscan chunk's HStart..VStop and view
 * position for a monitor that has none yet are taken from the standard rectangle and the monitor's view
 * position, and the rectangles are in the chosen mode's own pixels.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef LT_ONE
#include "looktabs.h"                       /* a page of OpenPrefs (LT_OP): its frame and sections */
#define LT_REFRESH(w) lt_draw((w), -1)
#else
#define LT_REFRESH(w) ((void)0)
#define LT_DIRTY 1
#define LT_DX 0
#define lt_note_to(f) ((void)0)
#endif

#include "pf_iff.h"
#include "dp_prefs.h"

static const char version[] __attribute__((used)) = "$VER: OpenScreenMode 0.1 (21.10.2026) OpenPrefs, MIT, Copyright (c) 2026 AmigaChrome Team";

#define MODE_ENV "ENV:Sys/screenmode.prefs"
#define MODE_ENVARC "ENVARC:Sys/screenmode.prefs"
#define OSCAN_ENV "ENV:Sys/overscan.prefs"
#define OSCAN_ENVARC "ENVARC:Sys/overscan.prefs"
#define MAX_MODES 200

/* ---- the display database's modes ---------------------------------------------------------------- */

struct dmode {
    struct Node node;                       /* ln_Name: name */
    ULONG id;
    ULONG alt;                              /* the same mode by another ID (the default monitor's), or INVALID_ID */
    struct DimensionInfo dim;
    Point view;                             /* the monitor's view position */
    char name[DISPLAYNAMELEN + 12];
};
static struct dmode *modes;
static int nmodes;
static struct List mode_list;

static int width_of(const struct Rectangle *r) { return r->MaxX - r->MinX + 1; }
static int height_of(const struct Rectangle *r) { return r->MaxY - r->MinY + 1; }

/* Every mode that can be shown, the Workbench's kind first (DIPF_IS_WB; all of them when none has it),
 * sorted by name, into mode_list */
static void find_modes(void)
{
    ULONG id = INVALID_ID;
    int i, j, wb_only = 0, pass;
    nmodes = 0;
    mode_list.lh_Head = (struct Node *)&mode_list.lh_Tail;     /* NewList, which is amiga.lib's */
    mode_list.lh_Tail = NULL;
    mode_list.lh_TailPred = (struct Node *)&mode_list.lh_Head;
    if (!modes && !(modes = AllocVec(MAX_MODES * sizeof *modes, MEMF_ANY | MEMF_CLEAR))) return;
    for (pass = 0; pass < 2 && !nmodes; pass++) {
        wb_only = pass == 0;
        id = INVALID_ID;
        while ((id = NextDisplayInfo(id)) != (ULONG)INVALID_ID && nmodes < MAX_MODES) {
            DisplayInfoHandle h = FindDisplayInfo(id);
            struct DisplayInfo di;
            struct NameInfo ni;
            struct MonitorInfo mi;
            struct dmode *m = &modes[nmodes];
            if (!h || !GetDisplayInfoData(h, (UBYTE *)&di, sizeof di, DTAG_DISP, INVALID_ID) || di.NotAvailable) continue;
            if (wb_only && !(di.PropertyFlags & DIPF_IS_WB)) continue;
            if (!GetDisplayInfoData(h, (UBYTE *)&m->dim, sizeof m->dim, DTAG_DIMS, INVALID_ID)) continue;
            m->id = id;
            m->alt = INVALID_ID;
            m->view.x = m->view.y = 0;
            if (GetDisplayInfoData(h, (UBYTE *)&mi, sizeof mi, DTAG_MNTR, INVALID_ID)) m->view = mi.ViewPosition;
            if (GetDisplayInfoData(h, (UBYTE *)&ni, sizeof ni, DTAG_NAME, INVALID_ID) && ni.Name[0])
                strncpy(m->name, (const char *)ni.Name, DISPLAYNAMELEN);
            else
                snprintf(m->name, sizeof m->name, "Mode $%08lX", (unsigned long)id);
            m->name[sizeof m->name - 1] = 0;
            nmodes++;
        }
    }
    for (i = 1; i < nmodes; i++) {           /* by name */
        struct dmode t = modes[i];
        for (j = i; j > 0 && strcmp(modes[j - 1].name, t.name) > 0; j--) modes[j] = modes[j - 1];
        modes[j] = t;
    }
    /* One row a mode: the display database lists the default monitor's modes again under their own IDs and
     * the same names (each mode twice on stock 3.2.3, lab t3-openprefs-20261022); the real monitor's ID is
     * kept and the other remembered, so a file naming either finds the row */
    for (i = 0, j = 0; i < nmodes; i++) {
        if (j > 0 && !strcmp(modes[j - 1].name, modes[i].name)) {
            struct dmode *k = &modes[j - 1];
            if ((k->id & MONITOR_ID_MASK) == DEFAULT_MONITOR_ID && (modes[i].id & MONITOR_ID_MASK) != DEFAULT_MONITOR_ID) {
                k->alt = k->id;
                k->id = modes[i].id;
                k->dim = modes[i].dim;
                k->view = modes[i].view;
            } else if (k->alt == (ULONG)INVALID_ID) k->alt = modes[i].id;
            continue;
        }
        if (j != i) modes[j] = modes[i];
        j++;
    }
    nmodes = j;
    for (i = 0; i < nmodes; i++) {
        modes[i].node.ln_Name = modes[i].name;
        AddTail(&mode_list, &modes[i].node);
    }
}

static int mode_index(ULONG id)
{
    int i;
    for (i = 0; i < nmodes; i++) if (modes[i].id == id) return i;
    for (i = 0; i < nmodes; i++) if (id != (ULONG)INVALID_ID && modes[i].alt == id) return i;
    return -1;
}

/* ---- the files ------------------------------------------------------------------------------------ */

static pf_file mode_file, oscan_file;
static int had_mode_file;
static dp_mode mode;
static dp_oscan oscan;
static int oscan_touched;                   /* overscan.prefs is written only when it was changed */

static void load_files(const char *from)
{
    int r = pf_load(&mode_file, from ? from : MODE_ENV);
    if (r == 0 && !from) r = pf_load(&mode_file, MODE_ENVARC);
    had_mode_file = r == 1;
    if (r != 1) pf_new(&mode_file);
    dp_mode_get(&mode_file, &mode);
    r = pf_load(&oscan_file, OSCAN_ENV);
    if (r == 0) r = pf_load(&oscan_file, OSCAN_ENVARC);
    if (r != 1) pf_new(&oscan_file);
    oscan_touched = 0;
}

/* screenmode.prefs (and overscan.prefs when changed) into ENV:, and ENVARC: when save. 1 done. */
static int write_files(int save, void (*say)(const char *))
{
    if (!dp_mode_put(&mode_file, &mode)) { say("Out of memory."); return 0; }
    if (save && !pf_save(&mode_file, MODE_ENVARC)) { say("Couldn't write ENVARC:Sys/screenmode.prefs."); return 0; }
    if (!pf_save(&mode_file, MODE_ENV)) { say("Couldn't write ENV:Sys/screenmode.prefs."); return 0; }
    if (oscan_touched) {
        if (save && !pf_save(&oscan_file, OSCAN_ENVARC)) { say("Couldn't write ENVARC:Sys/overscan.prefs."); return 0; }
        if (!pf_save(&oscan_file, OSCAN_ENV)) { say("Couldn't write ENV:Sys/overscan.prefs."); return 0; }
    }
    return 1;
}

static void say_shell(const char *s) { Printf((STRPTR)"OpenScreenMode: %s\n", (ULONG)s); }

/* ---- the window ------------------------------------------------------------------------------------ */

enum { G_LIST = 1, G_INFO, G_WIDTH, G_WDEF, G_HEIGHT, G_HDEF, G_DEPTH, G_COLOURS, G_AUTOSCROLL,
       G_TW, G_TH, G_SL, G_ST, G_SW, G_SH, G_RESET, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };

static struct Window *win;
static struct Gadget *gad[G_COUNT];
static char status_text[80], info_text[96], colours_text[24];
static int sel;                             /* the mode's index in modes */

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

static void set(int id, ULONG tag, ULONG v)
{
    if (win && gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, tag, v, TAG_DONE);
}

static LONG number(int id)
{
    LONG v = 0;
    if (gad[id]) GT_GetGadgetAttrs(gad[id], win, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
    return v;
}

static void colours_for(int depth)
{
    if (depth <= 0) strcpy(colours_text, "");
    else if (depth <= 16) snprintf(colours_text, sizeof colours_text, "%lu colours", 1UL << depth);
    else snprintf(colours_text, sizeof colours_text, "%lu.%lu million", (1UL << depth) / 1000000UL, (1UL << depth) / 100000UL % 10);
}

static void info_for(const struct dmode *m)
{
    snprintf(info_text, sizeof info_text, "%d-%d x %d-%d, up to %lu colours%s", m->dim.MinRasterWidth, m->dim.MaxRasterWidth,
             m->dim.MinRasterHeight, m->dim.MaxRasterHeight, m->dim.MaxDepth <= 16 ? 1UL << m->dim.MaxDepth : 1UL << 24,
             dp_native(m->id) ? ", chipset" : ", graphics card");
}

/* The overscan of m's monitor: the file's, else the display database's own */
static void oscan_for(const struct dmode *m)
{
    if (!dp_oscan_get(&oscan_file, m->id, &oscan)) {
        oscan.id = m->id;
        oscan.textw = width_of(&m->dim.TxtOScan);
        oscan.texth = height_of(&m->dim.TxtOScan);
        oscan.std.minx = m->dim.StdOScan.MinX;
        oscan.std.miny = m->dim.StdOScan.MinY;
        oscan.std.maxx = m->dim.StdOScan.MaxX;
        oscan.std.maxy = m->dim.StdOScan.MaxY;
        oscan.hstart = oscan.std.minx;
        oscan.hstop = oscan.std.maxx;
        oscan.vstart = oscan.std.miny;
        oscan.vstop = oscan.std.maxy;
        oscan.viewx = m->view.x;
        oscan.viewy = m->view.y;
    }
}

/* The gadgets from mode, oscan and sel */
static void show(void)
{
    const struct dmode *m = sel >= 0 ? &modes[sel] : NULL;
    int native = m && dp_native(m->id), maxd = m ? m->dim.MaxDepth : 8, i;
    if (maxd < 1) maxd = 1;
    if (mode.depth > maxd) mode.depth = maxd;
    if (mode.depth < 1) mode.depth = 1;
    if (m) info_for(m); else strcpy(info_text, "No mode chosen.");
    colours_for(mode.depth);
    set(G_INFO, GTTX_Text, (ULONG)info_text);
    if (win && gad[G_WIDTH])
        GT_SetGadgetAttrs(gad[G_WIDTH], win, NULL, GTIN_Number,
                          mode.width == DP_SIZE_DEFAULT ? (m ? width_of(&m->dim.TxtOScan) : 640) : mode.width,
                          GA_Disabled, mode.width == DP_SIZE_DEFAULT, TAG_DONE);
    if (win && gad[G_HEIGHT])
        GT_SetGadgetAttrs(gad[G_HEIGHT], win, NULL, GTIN_Number,
                          mode.height == DP_SIZE_DEFAULT ? (m ? height_of(&m->dim.TxtOScan) : 256) : mode.height,
                          GA_Disabled, mode.height == DP_SIZE_DEFAULT, TAG_DONE);
    if (win && gad[G_DEPTH]) GT_SetGadgetAttrs(gad[G_DEPTH], win, NULL, GTSL_Max, maxd, GTSL_Level, mode.depth, TAG_DONE);
    set(G_COLOURS, GTTX_Text, (ULONG)colours_text);
    for (i = G_TW; i <= G_RESET; i++) set(i, GA_Disabled, !native);
    if (native) {
        set(G_TW, GTIN_Number, oscan.textw);
        set(G_TH, GTIN_Number, oscan.texth);
        set(G_SL, GTIN_Number, oscan.std.minx);
        set(G_ST, GTIN_Number, oscan.std.miny);
        set(G_SW, GTIN_Number, oscan.std.maxx - oscan.std.minx + 1);
        set(G_SH, GTIN_Number, oscan.std.maxy - oscan.std.miny + 1);
    }
}

/* The overscan gadgets into oscan and the file */
static void take_oscan(void)
{
    const struct dmode *m = sel >= 0 ? &modes[sel] : NULL;
    LONG w, h;
    if (!m || !dp_native(m->id)) return;
    oscan.textw = number(G_TW);
    oscan.texth = number(G_TH);
    oscan.std.minx = number(G_SL);
    oscan.std.miny = number(G_ST);
    w = number(G_SW);
    h = number(G_SH);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    oscan.std.maxx = oscan.std.minx + w - 1;
    oscan.std.maxy = oscan.std.miny + h - 1;
    if (!dp_oscan_put(&oscan_file, &oscan)) { status("Out of memory."); return; }
    oscan_touched = 1;
}

static void pick(int i)
{
    if (i < 0 || i >= nmodes) return;
    sel = i;
    mode.id = modes[i].id;
    oscan_for(&modes[i]);
    show();
}

/* What Use or Save does: the gadgets into mode, then the files. 1 done. */
static int put_in_place(int save)
{
    const struct dmode *m = sel >= 0 ? &modes[sel] : NULL;
    if (!m) { status("Pick a mode first."); return 0; }
    if (mode.width != DP_SIZE_DEFAULT) {
        LONG w = number(G_WIDTH);
        if (w < m->dim.MinRasterWidth) w = m->dim.MinRasterWidth;
        if (w > m->dim.MaxRasterWidth) w = m->dim.MaxRasterWidth;
        mode.width = w;
    }
    if (mode.height != DP_SIZE_DEFAULT) {
        LONG h = number(G_HEIGHT);
        if (h < m->dim.MinRasterHeight) h = m->dim.MinRasterHeight;
        if (h > m->dim.MaxRasterHeight) h = m->dim.MaxRasterHeight;
        mode.height = h;
    }
    mode.id = m->id;
    return write_files(save, status);
}

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
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, lvw, lvh, X2, X3, lw2, lw3, nw, cbw, i;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    rp = &scr->RastPort;

    find_modes();
    if (mode.id == DP_NOFILE_ID || mode_index(mode.id) < 0) {
        /* no mode chosen yet, or one this machine hasn't: the Workbench's own, as it is now */
        ULONG wb = GetVPModeID(&scr->ViewPort);
        if (mode_index(wb) >= 0) {
            mode.id = wb;
            if (!had_mode_file) mode.depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
        }
    }
    sel = mode_index(mode.id);
    if (sel < 0 && nmodes) sel = 0;
    if (sel >= 0) oscan_for(&modes[sel]);

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    for (lvw = 0, i = 0; i < nmodes; i++) {
        int w = TextLength(rp, (STRPTR)modes[i].name, strlen(modes[i].name));
        if (w > lvw) lvw = w;
    }
    lvw += 28;
    if (lvw < 200) lvw = 200;
    if (lvw > 300) lvw = 300;
    lvh = 5 * lh;
    nw = TextLength(rp, (STRPTR)"000000", 6) + 16;            /* a number */
    cbw = 26;                                                 /* a check box */
    lw2 = TextLength(rp, (STRPTR)"Autoscroll", 10) + 16;      /* the right column's longest label */
    X2 = 12 + lvw + 12 + lw2;                                 /* the right column's gadgets */
    lw3 = TextLength(rp, (STRPTR)"Standard overscan", 17) + 16;
    X3 = 12 + lw3;                                            /* the overscan rows' gadgets */
    W = X2 + nw + 8 + cbw + TextLength(rp, (STRPTR)"Default", 7) + 8 + 12;
    i = X3 + 4 * (nw + 8) + 12;
    if (W < i) W = i;
    snprintf(status_text, sizeof status_text, "%s", "Pick a mode, then Use or Save.");
    colours_for(mode.depth);

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#ifdef LT_ONE
    lt_make(NULL, NULL, 10, top, W - 24, lh, LT_SCREENMODE);  /* the frame's sections (OpenPrefs) */
    top += lt_space(scr->Font, W - 24, lh);
#endif
    row = top;
    G(LISTVIEW_KIND, G_LIST, 12, row, lvw, lvh, NULL, 0, GTLV_Labels, (ULONG)&mode_list, GTLV_ShowSelected, 0,
      GTLV_Selected, sel, GTLV_MakeVisible, sel >= 0 ? sel : 0);
    {
        const struct dmode *m = sel >= 0 ? &modes[sel] : NULL;
        int maxd = m && m->dim.MaxDepth > 0 ? m->dim.MaxDepth : 8;
        if (mode.depth > maxd) mode.depth = maxd;
        if (mode.depth < 1) mode.depth = 1;
        G(INTEGER_KIND, G_WIDTH, X2, row, nw, lh, "Width", PLACETEXT_LEFT, GTIN_MaxChars, 5,
          GTIN_Number, mode.width == DP_SIZE_DEFAULT ? (m ? width_of(&m->dim.TxtOScan) : 640) : mode.width,
          GA_Disabled, mode.width == DP_SIZE_DEFAULT);
        G(CHECKBOX_KIND, G_WDEF, X2 + nw + 8, row, cbw, lh, "Default", PLACETEXT_RIGHT, GTCB_Checked,
          mode.width == DP_SIZE_DEFAULT, GTCB_Scaled, TRUE);
        row += lh + 4;
        G(INTEGER_KIND, G_HEIGHT, X2, row, nw, lh, "Height", PLACETEXT_LEFT, GTIN_MaxChars, 5,
          GTIN_Number, mode.height == DP_SIZE_DEFAULT ? (m ? height_of(&m->dim.TxtOScan) : 256) : mode.height,
          GA_Disabled, mode.height == DP_SIZE_DEFAULT);
        G(CHECKBOX_KIND, G_HDEF, X2 + nw + 8, row, cbw, lh, "Default", PLACETEXT_RIGHT, GTCB_Checked,
          mode.height == DP_SIZE_DEFAULT, GTCB_Scaled, TRUE);
        row += lh + 4;
        G(SLIDER_KIND, G_DEPTH, X2, row, nw, lh, "Colours", PLACETEXT_LEFT, GTSL_Min, 1, GTSL_Max, maxd,
          GTSL_Level, mode.depth, GA_RelVerify, TRUE, GA_Immediate, TRUE);
        G(TEXT_KIND, G_COLOURS, X2 + nw + 8, row, W - 12 - (X2 + nw + 8), lh, NULL, 0, GTTX_Text, (ULONG)colours_text);
        row += lh + 4;
        G(CHECKBOX_KIND, G_AUTOSCROLL, X2, row, cbw, lh, "Autoscroll", PLACETEXT_LEFT, GTCB_Checked, mode.autoscroll,
          GTCB_Scaled, TRUE);
        if (m) info_for(m); else strcpy(info_text, "No mode chosen.");
    }
    row = top + lvh + 6;
    G(TEXT_KIND, G_INFO, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)info_text, GTTX_Border, TRUE);
    row += lh + 8;
    {
        int native = sel >= 0 && dp_native(modes[sel].id);
        G(INTEGER_KIND, G_TW, X3, row, nw, lh, "Text overscan", PLACETEXT_LEFT, GTIN_MaxChars, 5, GTIN_Number, oscan.textw,
          GA_Disabled, !native);
        G(INTEGER_KIND, G_TH, X3 + nw + 8, row, nw, lh, NULL, 0, GTIN_MaxChars, 5, GTIN_Number, oscan.texth, GA_Disabled, !native);
        G(BUTTON_KIND, G_RESET, X3 + 2 * (nw + 8), row, 2 * nw + 8, lh, "Reset", 0, GA_Disabled, !native);
        row += lh + 4;
        G(INTEGER_KIND, G_SL, X3, row, nw, lh, "Standard overscan", PLACETEXT_LEFT, GTIN_MaxChars, 6,
          GTIN_Number, oscan.std.minx, GA_Disabled, !native);
        G(INTEGER_KIND, G_ST, X3 + nw + 8, row, nw, lh, NULL, 0, GTIN_MaxChars, 6, GTIN_Number, oscan.std.miny, GA_Disabled, !native);
        G(INTEGER_KIND, G_SW, X3 + 2 * (nw + 8), row, nw, lh, NULL, 0, GTIN_MaxChars, 5,
          GTIN_Number, oscan.std.maxx - oscan.std.minx + 1, GA_Disabled, !native);
        G(INTEGER_KIND, G_SH, X3 + 3 * (nw + 8), row, nw, lh, NULL, 0, GTIN_MaxChars, 5,
          GTIN_Number, oscan.std.maxy - oscan.std.miny + 1, GA_Disabled, !native);
        row += lh + 10;
    }
    G(TEXT_KIND, G_STATUS, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE);
    row += lh + 6;
    {
        static const char *const names[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 24 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], 12 + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
#define IDCMP_ALL (IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | INTEGERIDCMP | CHECKBOXIDCMP | \
                   SLIDERIDCMP | LISTVIEWIDCMP | TEXTIDCMP)
#ifdef LT_ONE
    win = lt_open(scr, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, scr->BarHeight + 20, W, row - scr->WBorTop - fh - 1,
                  glist, IDCMP_ALL, "OpenScreenMode 0.1");
#else
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Screen mode", WA_ScreenTitle, (ULONG)"OpenScreenMode 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_IDCMP, IDCMP_ALL, TAG_DONE);
#endif
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    lt_note_to(status);
    if (!nmodes) status("graphics.library lists no modes.");

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) quit = 1;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); LT_REFRESH(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: if (put_in_place(1)) quit = 1; break;
                    case 2: if (put_in_place(0)) quit = 1; break;
                    case 3: quit = 1; break;
                    }
                    code = it->NextSelect;
                }
            }
            else if (cls == IDCMP_MOUSEMOVE && gg && gg->GadgetID == G_DEPTH) {
                mode.depth = (WORD)code;
                colours_for(mode.depth);
                set(G_COLOURS, GTTX_Text, (ULONG)colours_text);
            }
            else if (cls == IDCMP_GADGETUP) {
                switch (gg->GadgetID) {
                case G_LIST: pick(code); break;
                case G_WDEF:
                    mode.width = (gg->Flags & GFLG_SELECTED) ? DP_SIZE_DEFAULT : (int)number(G_WIDTH);
                    show();
                    break;
                case G_HDEF:
                    mode.height = (gg->Flags & GFLG_SELECTED) ? DP_SIZE_DEFAULT : (int)number(G_HEIGHT);
                    show();
                    break;
                case G_DEPTH:
                    mode.depth = (WORD)code;
                    colours_for(mode.depth);
                    set(G_COLOURS, GTTX_Text, (ULONG)colours_text);
                    break;
                case G_AUTOSCROLL: mode.autoscroll = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_TW: case G_TH: case G_SL: case G_ST: case G_SW: case G_SH: take_oscan(); break;
                case G_RESET:
                    if (sel >= 0) {
                        dp_oscan_remove(&oscan_file, modes[sel].id);
                        oscan_touched = 1;
                        oscan_for(&modes[sel]);
                        show();
                        status("This monitor's overscan is the display database's own again.");
                    }
                    break;
#ifdef LT_ONE
                default: {                          /* another section: the page's changes kept, as Use, when it has any */
                    int to = lt_hit(gg, code);
                    if (to >= 0) { if (LT_DIRTY) put_in_place(0); lt_go(win, to); quit = 1; }
                    break;
                }
#endif
                case G_SAVE: if (put_in_place(1)) quit = 1; break;
                case G_USE: if (put_in_place(0)) quit = 1; break;
                case G_CANCEL: quit = 1; break;
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

int main(int argc, char **argv)
{
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S", args, NULL) : NULL;
    int rc;
    (void)argv;
    (void)version;
    win = NULL;                             /* built into OpenPrefs, the statics outlive a run */
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"OpenScreenMode"); return RETURN_FAIL; }
    if (!pf_open_lib()) { if (rd) FreeArgs(rd); return RETURN_FAIL; }
    pf_init(&mode_file);
    pf_init(&oscan_file);
    load_files(args[0] ? (const char *)args[0] : NULL);
    if (args[1] || args[2]) rc = write_files(args[2] != 0, say_shell) ? RETURN_OK : RETURN_ERROR;
    else rc = gui();
    if (rd) FreeArgs(rd);
    pf_free(&mode_file);
    pf_free(&oscan_file);
    if (modes) { FreeVec(modes); modes = NULL; }
    pf_close_lib();
    return rc;
}
