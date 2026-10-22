/* OpenLook's Appearance tabs (OpenLook 1.1, 12 October 2026; P1 of the combined preferences design,
 * docs/combined-prefs/DESIGN.md): Pointer, Fonts, Colours, Backdrops, Workbench and File requesters.
 *
 * Each tab edits the OS's own file, ENV:Sys/pointer.prefs, font.prefs, palette.prefs, wbpattern.prefs,
 * workbench.prefs or asl.prefs, read and written with iffparse.library (pf_amiga.c) as a list of chunks
 * (pf_iff.c) whose fields pf_prefs.c knows, so every chunk and byte no tab knows is carried across. The six
 * share one frame: the tabs (Common/looktabs.h), the page, a status line, Save, Use, Test and Cancel, and the
 * Project and View menus, as the other pages of OpenLook have them.
 *
 *  - Save writes ENV: and ENVARC:, Use writes ENV:, Test writes ENV: and puts it back after 15 seconds unless
 *    Use or Save follows; Cancel or the close gadget puts back what a Test changed. A click on another tab
 *    keeps what was changed, as Use.
 *  - A file is written only when its bytes would change: changing a font leaves the other five files alone.
 *  - Intuition and Workbench pick the files up through IPrefs, as when the OS's editors save them; nothing
 *    here hooks Workbench. The OS's editors stay in SYS:Prefs/Classic.
 *  - Simple shows each tab's few main settings; View > Advanced (Amiga-A) every one, shared with every
 *    OpenPrefs editor through ENV:OpenAmiga/PrefsView.
 *
 * OpenLook 1.2 (12 October 2026): the Pointer tab's Advanced view draws a pointer, in fat bits, with its three
 * colours (shared by every pointer of the file, as the OS's Pointer editor has them) and its hot spot; the
 * Workbench tab shows the icon frame ("Icon borders", the OS editor's Border Size) and the hidden devices; and
 * every page fits a 640 x 256 screen in Topaz 8 (on a screen under 400 lines Advanced packs its rows closer).
 *
 * Built into OpenLook only (LT_ONE, Look/build-one.sh): the page runs in OpenLook's one window.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/text.h>
#include <graphics/view.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#define AslBase appearance_AslBase                 /* its own asl.library, under its own name: Menus has one too */
static struct Library *AslBase;            /* opened while a tab is open */
#include <proto/asl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pf_iff.h"
#include "pf_prefs.h"
#include "../../Common/looktabs.h"

#ifndef LT_ONE
#error "appearance.c is built into OpenLook only (Look/build-one.sh)"
#endif

int appearance_main(int tab);              /* LT_POINTER to LT_ASL: that tab; a RETURN_ code */

#define TEST_SECONDS 15
#define VIEW_ENV "ENV:OpenAmiga/PrefsView"           /* shared by every OpenPrefs editor: simple or advanced */
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"
#define POINTER_SETS "SYS:Prefs/Presets/Pointers"
#define BACKDROPS "SYS:Prefs/Presets/Backdrops"

enum { AP_POINTER, AP_FONTS, AP_COLOURS, AP_BACKDROPS, AP_WORKBENCH, AP_ASL, AP_N };

static const struct ap_page {
    const char *env, *envarc, *title, *hint;
} pages[AP_N] = {
    { "ENV:Sys/pointer.prefs", "ENVARC:Sys/pointer.prefs", "OpenLook: Pointer",
      "Choose a pointer set. Test shows it for 15 seconds." },
    { "ENV:Sys/font.prefs", "ENVARC:Sys/font.prefs", "OpenLook: Fonts",
      "New fonts are used when the Workbench screen opens again." },
    { "ENV:Sys/palette.prefs", "ENVARC:Sys/palette.prefs", "OpenLook: Colours",
      "Choose a colour, then its red, green and blue." },
    { "ENV:Sys/wbpattern.prefs", "ENVARC:Sys/wbpattern.prefs", "OpenLook: Backdrops",
      "A picture for the Workbench, its drawers or its screen." },
    { "ENV:Sys/workbench.prefs", "ENVARC:Sys/workbench.prefs", "OpenLook: Workbench",
      "How Workbench shows icons, drawers and its title bar." },
    { "ENV:Sys/asl.prefs", "ENVARC:Sys/asl.prefs", "OpenLook: File requesters",
      "How file requesters sort, and where they open." },
};

enum {
    G_NONE, G_STATUS, G_SAVE, G_USE, G_TEST, G_CANCEL,
    /* Pointer (G_PCOLR, G_PCOLG, G_PCOLB in that order) */
    G_PSETS, G_PUSESET, G_PONLY, G_PTYPES, G_PHOTX, G_PHOTY, G_PHOT, G_PCOLR, G_PCOLG, G_PCOLB, G_PCLEAR,
    /* Fonts */
    G_FSCREEN, G_FSCREENPICK, G_FSCREENSTD, G_FICON, G_FICONPICK, G_FICONSTD, G_FSYS, G_FSYSPICK, G_FLABELS, G_FTEXTPEN, G_FFIELDPEN,
    /* Colours */
    G_CPAL, G_CR, G_CG, G_CB, G_COS, G_CPENS, G_CPEN4, G_CPEN8,
    /* Backdrops */
    G_BFOR, G_BUSE, G_BPATH, G_BPICK, G_BPLACE, G_BREMAP, G_BDITHER, G_BMATCH,
    /* Workbench */
    G_WFRAME, G_WGAUGE, G_WTITLEBAR, G_WLABELLEN, G_WNEWICONS, G_WCOLICONS, G_WPRECISION, G_WLOCKPENS, G_WHIDDEN,
    G_WSTACK, G_WDRAWERS, G_WOLDFIRST, G_WBOUND, G_WDELAY, G_WCOPYBUF, G_WRESTART, G_WTITLE,
    /* File requesters */
    G_ASORT, G_ADRAWERS, G_AORDER, G_APLACE, G_ALEFT, G_ATOP, G_ASIZE, G_AWIDTH, G_AHEIGHT, G_AOVERRIDE,
    G_COUNT
};

/* ---- the frame's state ---------------------------------------------------------------------------- */

static int ap;                             /* the tab open: AP_ */
static pf_file cur, start, before;         /* the file as edited; as the tab found it; ENV: before a Test */
static int had_file;                       /* the file was there (ENV:, or ENVARC: when ENV: had none) */
static int damaged;                        /* there, but not a PREF file this can carry whole: nothing is written */
static int before_had, env_touched;        /* ENV: had the file before a Test; a Test wrote ENV: */
static ULONG test_until;                   /* seconds since 1978 when a Test goes back, or 0 */
static int advanced;
static int tight;                          /* a screen under 400 lines (PAL High Res): Advanced packs its rows closer */
static struct Window *win;
static struct Screen *scr;
static APTR vi;
static struct Gadget *gad[G_COUNT], *last;
static struct NewGadget ng;
static int fh, lh, L, W, X1, X2, W1, W2;   /* font height, line height, left edge, width, the columns */
static char status_text[160];

#define SET(id, ...) do { if (gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE); } while (0)
/* a number gadget, and the number it shows (page_typed() takes a number typed without Return) */
static LONG shown[G_COUNT];
#define SETINT(id, v, off) do { shown[id] = (LONG)(v); SET(id, GTIN_Number, (LONG)(v), GA_Disabled, (off)); } while (0)
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), last = gad[id] = CreateGadget(kind, last, &ng, __VA_ARGS__, TAG_DONE))

static void status(const char *s)
{
    if (s != status_text) strncpy(status_text, s, sizeof status_text - 1);
    status_text[sizeof status_text - 1] = 0;
    SET(G_STATUS, GTTX_Text, (ULONG)status_text);
}

static int tw(const char *s) { return TextLength(&scr->RastPort, (STRPTR)s, strlen(s)); }

/* The widest of some labels (NULL ends them), in the screen's font. */
static int widest(const char *const *labels)
{
    int w = 0, k;
    for (k = 0; labels[k]; k++) if (tw(labels[k]) > w) w = tw(labels[k]);
    return w;
}

/* The gadgets' left edges and widths: one column (right == NULL) or two, each after its widest label. */
static void columns(const char *const *left, const char *const *right)
{
    int mid = L + (W - 20) / 2;
    X1 = L + widest(left) + 10;
    if (!right) { W1 = L + W - 20 - X1; X2 = W2 = 0; return; }
    W1 = mid - 12 - X1;
    X2 = mid + widest(right) + 10;
    W2 = L + W - 20 - X2;
}

static ULONG now_seconds(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400 + (ULONG)ds.ds_Minute * 60 + (ULONG)ds.ds_Tick / TICKS_PER_SECOND;
}

static int read_view(void)
{
    char buf[16];
    BPTR f = Open((CONST_STRPTR)VIEW_ENV, MODE_OLDFILE);
    LONG n = 0;
    if (f) { n = Read(f, buf, sizeof buf - 1); Close(f); }
    return n >= 8 && !strncmp(buf, "advanced", 8);
}

static void write_text(const char *path, const char *text)
{
    BPTR f = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!f) {
        char dir[64];
        BPTR d;
        strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
        if (strrchr(dir, '/')) *strrchr(dir, '/') = 0;
        if ((d = CreateDir((CONST_STRPTR)dir))) UnLock(d);
        f = Open((CONST_STRPTR)path, MODE_NEWFILE);
    }
    if (f) { Write(f, (APTR)text, strlen(text)); Close(f); }
}

static void write_view(int a)
{
    write_text(VIEW_ENV, a ? "advanced\n" : "simple\n");
    write_text(VIEW_ENVARC, a ? "advanced\n" : "simple\n");
}

/* A list for a LISTVIEW: n names (each copied into its node). */
struct names {
    struct List list;
    struct Node node[48];
    char text[48][40];
    int n;
};

static void names_init(struct names *nm)
{
    nm->list.lh_Head = (struct Node *)&nm->list.lh_Tail;
    nm->list.lh_Tail = NULL;
    nm->list.lh_TailPred = (struct Node *)&nm->list.lh_Head;
    nm->n = 0;
}

static void names_add(struct names *nm, const char *s)
{
    if (nm->n >= 48) return;
    strncpy(nm->text[nm->n], s, sizeof nm->text[0] - 1);
    nm->text[nm->n][sizeof nm->text[0] - 1] = 0;
    nm->node[nm->n].ln_Name = nm->text[nm->n];
    AddTail(&nm->list, &nm->node[nm->n]);
    nm->n++;
}

/* The files in a drawer, sorted, without .info files: into nm. */
static void list_drawer(const char *dir, struct names *nm)
{
    BPTR lock = Lock((CONST_STRPTR)dir, ACCESS_READ);
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    static char found[48][40];
    int n = 0, i, j;
    names_init(nm);
    if (lock && fib && Examine(lock, fib))
        while (ExNext(lock, fib) && n < 48) {
            size_t l = strlen((char *)fib->fib_FileName);
            if (fib->fib_DirEntryType >= 0 || (l > 5 && !strcmp((char *)fib->fib_FileName + l - 5, ".info")) || l >= 40) continue;
            strcpy(found[n++], (char *)fib->fib_FileName);
        }
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (lock) UnLock(lock);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && strcmp(found[j - 1], found[j]) > 0; j--) {
            char t[40];
            strcpy(t, found[j]); strcpy(found[j], found[j - 1]); strcpy(found[j - 1], t);
        }
    for (i = 0; i < n; i++) names_add(nm, found[i]);
}

/* Pens for drawing a preview in a colour of our own; given back when the page is drawn again or closed. */
static LONG pens[16];
static int npens;

static void free_pens(void)
{
    int i;
    for (i = 0; i < npens; i++) ReleasePen(scr->ViewPort.ColorMap, pens[i]);
    npens = 0;
}

static LONG pen_for(int r, int g, int b)
{
    LONG p = ObtainBestPen(scr->ViewPort.ColorMap, (ULONG)r * 0x01010101UL, (ULONG)g * 0x01010101UL, (ULONG)b * 0x01010101UL,
                           OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    if (p >= 0 && npens < 16) pens[npens++] = p;
    else if (p >= 0) { ReleasePen(scr->ViewPort.ColorMap, p); p = -1; }
    return p;
}

/* The ASL font requester: 1 and f's name, size, style and flags set, or 0. */
static int ask_font(const char *title, pf_font *f, int fixed)
{
    struct FontRequester *fr;
    int ok = 0;
    if (!AslBase) { status("asl.library isn't there: no font requester."); return 0; }
    fr = (struct FontRequester *)AllocAslRequestTags(ASL_FontRequest, ASLFO_Window, (ULONG)win, ASLFO_SleepWindow, TRUE,
                                                     ASLFO_TitleText, (ULONG)title,
                                                     ASLFO_InitialName, (ULONG)(f->name[0] ? f->name : fixed ? "topaz.font" : PF_STANDARD_FONT),
                                                     ASLFO_InitialSize, f->ysize ? f->ysize : fixed ? 8 : PF_STANDARD_SIZE,
                                                     ASLFO_FixedWidthOnly, fixed,
                                                     ASLFO_MaxHeight, 64, TAG_DONE);
    if (!fr) return 0;
    if (AslRequest(fr, NULL) && fr->fo_Attr.ta_Name) {
        strncpy(f->name, (const char *)fr->fo_Attr.ta_Name, sizeof f->name - 1);
        f->name[sizeof f->name - 1] = 0;
        f->ysize = fr->fo_Attr.ta_YSize;
        f->style = fr->fo_Attr.ta_Style;
        f->flags = fr->fo_Attr.ta_Flags;
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

/* The ASL file requester, starting at path (or in dir): 1 and path set, or 0. */
static int ask_file(const char *title, const char *dir, char *path, int size)
{
    struct FileRequester *fr;
    char drawer[256], file[108];
    int ok = 0;
    if (!AslBase) { status("asl.library isn't there: no file requester."); return 0; }
    file[0] = 0;
    if (path[0]) {
        int n = (int)((const char *)PathPart((CONST_STRPTR)path) - path);
        if (n > (int)sizeof drawer - 1) n = sizeof drawer - 1;
        memcpy(drawer, path, (size_t)n); drawer[n] = 0;
        strncpy(file, (const char *)FilePart((CONST_STRPTR)path), sizeof file - 1); file[sizeof file - 1] = 0;
    } else {
        strncpy(drawer, dir, sizeof drawer - 1); drawer[sizeof drawer - 1] = 0;
    }
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
                                                     ASLFR_TitleText, (ULONG)title, ASLFR_InitialDrawer, (ULONG)drawer,
                                                     ASLFR_InitialFile, (ULONG)file, ASLFR_RejectIcons, TRUE, TAG_DONE);
    if (!fr) return 0;
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0]) {
        strncpy(path, (const char *)fr->fr_Drawer, (size_t)size - 1);
        path[size - 1] = 0;
        ok = AddPart((STRPTR)path, (CONST_STRPTR)fr->fr_File, (ULONG)size) != 0;
    }
    FreeAslRequest(fr);
    return ok;
}

static int screen_colours(void)            /* how many of the first eight colours the screen has */
{
    ULONG depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    return depth >= 3 ? 8 : 1 << depth;
}

/* ---- Pointer: pointer.prefs ---------------------------------------------------------------------------- */

static const char *const pointer_names[41] = {
    "Normal", "Busy", "Alias", "Cell", "Column resize", "Context menu", "Copy", "Cross", "Drag and drop", "East resize",
    "East-west resize", "Hand", "Help", "Link", "Menu", "No drop", "None", "North-east resize", "NE-SW resize",
    "North resize", "North-south resize", "North-west resize", "NW-SE resize", "Not allowed", "Progress", "Row resize",
    "Scroll all", "South-east resize", "South resize", "South-west resize", "Text", "Vertical text", "West resize",
    "Zoom in", "Zoom out", "Pen", "Rotate", "Rubber", "Select", "Smudge", "Type 40"
};
static struct names sets, types;
static int type_of[48];                    /* the pointer type each row of the types list shows */
static int sel_set = -1, sel_type;         /* the set chosen in the list; the type shown in Advanced */
static pf_file setfile;                    /* that set, read */
static int ppx, ppy, ppw, pph;             /* the preview's box (Simple) */
/* Drawing (Advanced, OpenLook 1.2): the pointer shown in fat bits, gs pixels a pixel, at gx, gy inside its box;
 * the four colours to draw with (0 clear) at swx, swy, sws each; the colour drawn with; a click places the hot
 * spot instead; the colour being painted while the button is held, or -1. */
static int gx, gy, gs, gbw, gbh, swx, swy, sws;
static int draw_colour = 2, hot_mode, painting = -1;
static LONG ptr_pen[4], grid_pen, mark_pen[2];

static void pointer_types(void)
{
    int t;
    pf_pntr p;
    names_init(&types);
    for (t = 0; t <= 40; t++)
        if (pf_pntr_get(&cur, t, &p)) { type_of[types.n] = t; names_add(&types, pointer_names[t]); }
}

static int type_row(int t)
{
    int i;
    for (i = 0; i < types.n; i++) if (type_of[i] == t) return i;
    return -1;
}

/* One pointer at x, y, twice its size when that fits in maxw x maxh, its name after it. */
static void draw_pointer(int which, int x, int y, int maxw, int maxh, LONG textpen)
{
    struct RastPort *rp = win->RPort;
    pf_pntr p;
    LONG pen[4];
    int s, i, px, py, w = 0;
    if (!pf_pntr_get(&cur, which, &p)) return;
    s = p.width * 2 <= maxw && p.height * 2 <= maxh ? 2 : 1;
    for (i = 1; i < 4; i++)
        pen[i] = i <= p.ncolours ? pen_for(p.rgb[3 * (i - 1)], p.rgb[3 * (i - 1) + 1], p.rgb[3 * (i - 1) + 2]) : -1;
    for (py = 0; py < p.height && (py + 1) * s <= maxh; py++)
        for (px = 0; px < p.width && (px + 1) * s <= maxw; px++) {
            int c = pf_pntr_pixel(&p, px, py);
            if (!c) continue;
            if (c > 3) c = 1 + c % 3;              /* a deeper pointer, in its first three colours */
            if (pen[c] < 0) continue;
            SetAPen(rp, (UBYTE)pen[c]);
            RectFill(rp, x + px * s, y + py * s, x + px * s + s - 1, y + py * s + s - 1);
            if (px * s + s > w) w = px * s + s;
        }
    SetAPen(rp, (UBYTE)textpen);
    SetDrMd(rp, JAM1);
    Move(rp, x + p.width * s + 6, y + rp->TxBaseline + 2);
    Text(rp, (STRPTR)pointer_names[which], strlen(pointer_names[which]));
}

/* The size of one pixel of pointer p in the grid: gs, smaller when p is bigger than 16 x 24. */
static int cell_of(const pf_pntr *p)
{
    int c = gs;
    if (p->width * c > 16 * gs) c = 16 * gs / p->width;
    if (p->height * c > 24 * gs) c = 24 * gs / p->height;
    return c < 1 ? 1 : c;
}

/* Pixel px, py of the pointer shown, in the grid: its colour, and the hot spot's mark on it. */
static void draw_cell(const pf_pntr *p, int px, int py)
{
    struct RastPort *rp = win->RPort;
    int c = pf_pntr_pixel(p, px, py), s = cell_of(p), x = gx + px * s, y = gy + py * s, inner = s > 2 ? s - 1 : s;
    if (c > 3) c = 1 + c % 3;
    SetAPen(rp, (UBYTE)(ptr_pen[c] >= 0 ? ptr_pen[c] : 0));
    RectFill(rp, x, y, x + inner - 1, y + inner - 1);
    if (px == -p->x && py == -p->y && s >= 3) {                /* the hot spot: a dark square with a light one in it */
        SetAPen(rp, (UBYTE)mark_pen[0]);
        Move(rp, x, y); Draw(rp, x + inner - 1, y); Draw(rp, x + inner - 1, y + inner - 1); Draw(rp, x, y + inner - 1); Draw(rp, x, y);
        if (inner >= 5) {
            SetAPen(rp, (UBYTE)mark_pen[1]);
            Move(rp, x + 1, y + 1); Draw(rp, x + inner - 2, y + 1); Draw(rp, x + inner - 2, y + inner - 2); Draw(rp, x + 1, y + inner - 2);
            Draw(rp, x + 1, y + 1);
        }
    }
}

/* The grid, the four colours to draw with, the one chosen standing out. */
static void draw_grid(void)
{
    struct RastPort *rp = win->RPort;
    pf_pntr p;
    int k, px, py, have = pf_pntr_get(&cur, sel_type, &p);
    SetAPen(rp, (UBYTE)(grid_pen >= 0 ? grid_pen : 0));
    RectFill(rp, gx, gy, gx + gbw - 5, gy + gbh - 5);
    if (have)
        for (py = 0; py < p.height; py++)
            for (px = 0; px < p.width; px++) draw_cell(&p, px, py);
    for (k = 0; k < 4; k++) {
        int x = swx + k * (sws + 4);
        SetAPen(rp, (UBYTE)(ptr_pen[k] >= 0 ? ptr_pen[k] : 0));
        RectFill(rp, x + 2, swy + 2, x + sws - 3, swy + sws - 3);
        DrawBevelBox(rp, x, swy, sws, sws, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, k == draw_colour, TAG_DONE);
        if (k == 0) {                                          /* clear: the grey with a line through it */
            SetAPen(rp, (UBYTE)mark_pen[0]);
            Move(rp, x + 3, swy + sws - 4); Draw(rp, x + sws - 4, swy + 3);
        }
    }
}

static void pointer_draw(void)
{
    struct RastPort *rp = win->RPort;
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    struct TextFont *tf = OpenFont(scr->Font), *was = rp->Font;    /* the names in the screen's font, as the tabs */
    LONG bg, text = dri ? dri->dri_Pens[TEXTPEN] : 1;
    if (tf) SetFont(rp, tf);
    free_pens();
    bg = pen_for(0xaa, 0xaa, 0xaa);                /* the OS's grey, which every pointer is drawn to stand on */
    if (advanced) {
        pf_pntr p;
        int k, have = pf_pntr_get(&cur, sel_type, &p);
        ptr_pen[0] = bg;
        for (k = 1; k < 4; k++)
            ptr_pen[k] = have && k <= p.ncolours ? pen_for(p.rgb[3 * (k - 1)], p.rgb[3 * (k - 1) + 1], p.rgb[3 * (k - 1) + 2]) : bg;
        grid_pen = pen_for(0x77, 0x77, 0x77);
        mark_pen[0] = dri ? dri->dri_Pens[SHADOWPEN] : 1;
        mark_pen[1] = dri ? dri->dri_Pens[SHINEPEN] : 2;
        DrawBevelBox(rp, gx - 2, gy - 2, gbw, gbh, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, TRUE, TAG_DONE);
        draw_grid();
        if (tf) { SetFont(rp, was); CloseFont(tf); }
        if (dri) FreeScreenDrawInfo(scr, dri);
        return;
    }
    SetAPen(rp, (UBYTE)(bg >= 0 ? bg : 0));
    RectFill(rp, ppx, ppy, ppx + ppw - 1, ppy + pph - 1);
    DrawBevelBox(rp, ppx - 2, ppy - 1, ppw + 4, pph + 2, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, TRUE, TAG_DONE);
    if (!types.n) {
        SetAPen(rp, (UBYTE)text);
        Move(rp, ppx + 6, ppy + rp->TxBaseline + 4);
        Text(rp, (STRPTR)"No pointers in the file yet.", 28);
    } else {
        draw_pointer(0, ppx + 6, ppy + 4, ppw / 2 - 12, pph - 8, text);
        draw_pointer(1, ppx + ppw / 2 + 6, ppy + 4, ppw / 2 - 12, pph - 8, text);
    }
    if (tf) { SetFont(rp, was); CloseFont(tf); }
    if (dri) FreeScreenDrawInfo(scr, dri);
}

static int pointer_make(int row)
{
    int y0 = row + fh + 4, end;
    list_drawer(POINTER_SETS, &sets);
    pointer_types();
    if (type_row(sel_type) < 0) sel_type = types.n ? type_of[0] : 0;
    if (!advanced) {
        int listh = 6 * (fh + 1) + 4, lw = 200;
        G(LISTVIEW_KIND, G_PSETS, L, y0, lw, listh, "Pointer sets", PLACETEXT_ABOVE, GTLV_Labels, (ULONG)&sets.list,
          GTLV_ShowSelected, NULL, GTLV_Selected, sel_set, GTLV_MakeVisible, sel_set < 0 ? 0 : sel_set);
        ppx = L + lw + 20; ppy = y0; ppw = W - 20 - lw - 20; pph = listh;
        return y0 + listh + 4;
    }
    {
        /* Advanced: the sets and the types; the pointer in fat bits; its colours and hot spot beside it. Six rows
         * beside the grid, which is 24 pixels tall: 5 a pixel on a 256-line screen, larger on a big one. */
        int step = lh + 2, lw, tlw, avail, dw, dx, bw, sl, sw, hx, iw, listh, k;
        gs = (6 * step) / 24 + 1;
        if (gs > 8) gs = 8;
        if (tight && gs > 5) gs = 5;
        gbw = 16 * gs + 4; gbh = 24 * gs + 4;
        if (gbh < 6 * step) gbh = 6 * step;
        sl = tw("Green") + 8;
        dw = sl + 60 + tw("255") + 8;
        hx = tw("Hot spot") + 8;
        iw = tw("00") + 12;                        /* the hot spot: 0 to 15 across, 0 to 23 down */
        if (dw < hx + 26 + 2 * (iw + 4)) dw = hx + 26 + 2 * (iw + 4);
        if (dw < 4 * (lh + 4)) dw = 4 * (lh + 4);
        avail = W - 20 - gbw - dw - 3 * 8;
        lw = avail / 2; tlw = avail - lw;
        listh = gbh - lh - 4;
        G(LISTVIEW_KIND, G_PSETS, L, y0, lw, listh, "Pointer sets", PLACETEXT_ABOVE, GTLV_Labels, (ULONG)&sets.list,
          GTLV_ShowSelected, NULL, GTLV_Selected, sel_set, GTLV_MakeVisible, sel_set < 0 ? 0 : sel_set);
        bw = (lw - 6) / 2;
        G(BUTTON_KIND, G_PUSESET, L, y0 + listh + 4, bw, lh, "The set", 0, GA_Disabled, sel_set < 0);
        G(BUTTON_KIND, G_PONLY, L + bw + 6, y0 + listh + 4, lw - bw - 6, lh, "This one", 0, GA_Disabled, sel_set < 0);
        G(LISTVIEW_KIND, G_PTYPES, L + lw + 8, y0, tlw, listh + lh + 4, "Pointer", PLACETEXT_ABOVE, GTLV_Labels, (ULONG)&types.list,
          GTLV_ShowSelected, NULL, GTLV_Selected, type_row(sel_type), GTLV_MakeVisible, type_row(sel_type) < 0 ? 0 : type_row(sel_type));
        gx = L + lw + 8 + tlw + 8 + 2; gy = y0 + 2;
        dx = L + W - 20 - dw;
        swx = dx; swy = y0; sws = lh;
        sw = dw - sl - tw("255") - 8;
        for (k = 0; k < 3; k++) {
            static const char *const rgb_label[3] = { "Red", "Green", "Blue" };
            G(SLIDER_KIND, G_PCOLR + k, dx + sl, y0 + (k + 1) * step, sw, lh, rgb_label[k], PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 255,
              GTSL_LevelFormat, (ULONG)"%3ld", GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, GA_Immediate, TRUE);
        }
        G(CHECKBOX_KIND, G_PHOT, dx + hx, y0 + 4 * step, 26, lh, "Hot spot", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        G(INTEGER_KIND, G_PHOTX, dx + hx + 30, y0 + 4 * step, iw, lh, NULL, 0, GTIN_MaxChars, 3);
        G(INTEGER_KIND, G_PHOTY, dx + hx + 34 + iw, y0 + 4 * step, iw, lh, NULL, 0, GTIN_MaxChars, 3);
        G(BUTTON_KIND, G_PCLEAR, dx, y0 + 5 * step, dw, lh, "Clear the pointer", 0, TAG_DONE);
        end = y0 + gbh;
        if (end < y0 + listh + lh + 4) end = y0 + listh + lh + 4;
        return end + 4;
    }
}

static void pointer_show(void)
{
    pf_pntr p;
    int have = pf_pntr_get(&cur, sel_type, &p), k, on = have && draw_colour >= 1 && draw_colour <= p.ncolours;
    SETINT(G_PHOTX, have ? -p.x : 0, !have);
    SETINT(G_PHOTY, have ? -p.y : 0, !have);
    SET(G_PUSESET, GA_Disabled, sel_set < 0);
    SET(G_PONLY, GA_Disabled, sel_set < 0);
    SET(G_PHOT, GTCB_Checked, hot_mode, GA_Disabled, !have);
    SET(G_PCLEAR, GA_Disabled, !have);
    for (k = 0; k < 3; k++) SET(G_PCOLR + k, GTSL_Level, on ? p.rgb[3 * (draw_colour - 1) + k] : 0, GA_Disabled, !on);
}

static void pointer_relist(void)
{
    SET(G_PTYPES, GTLV_Labels, ~0UL);
    pointer_types();
    if (type_row(sel_type) < 0) sel_type = types.n ? type_of[0] : 0;
    SET(G_PTYPES, GTLV_Labels, (ULONG)&types.list, GTLV_Selected, type_row(sel_type));
}

/* A click (down 1) or a move with the button held (down 0) at mx, my in the window: a colour to draw with, a
 * pixel drawn, or the hot spot placed. 1 when it was on the grid or the colours. */
static int pointer_mouse(int mx, int my, int down)
{
    pf_pntr p;
    int px, py, s, k;
    if (!advanced || damaged) return 0;
    if (down && my >= swy && my < swy + sws)
        for (k = 0; k < 4; k++)
            if (mx >= swx + k * (sws + 4) && mx < swx + k * (sws + 4) + sws) {
                draw_colour = k;
                pointer_show();
                draw_grid();
                return 1;
            }
    if (!pf_pntr_get(&cur, sel_type, &p)) return 0;
    s = cell_of(&p);
    if (mx < gx || my < gy) return 0;
    px = (mx - gx) / s; py = (my - gy) / s;
    if (px >= p.width || py >= p.height) return 0;
    if (hot_mode) {
        if (!down) return 1;
        pf_pntr_set_hotspot(&cur, sel_type, -px, -py);
        hot_mode = 0;
        status("The hot spot is the pixel the pointer clicks with.");
        pointer_show();
        draw_grid();
        return 1;
    }
    if (down) painting = draw_colour > p.ncolours ? 0 : draw_colour;
    if (painting < 0 || pf_pntr_pixel(&p, px, py) == painting) return 1;
    pf_pntr_set_pixel(&cur, sel_type, px, py, painting);
    pf_pntr_get(&cur, sel_type, &p);
    draw_cell(&p, px, py);
    return 1;
}

static void pointer_handle(int id, UWORD code, struct Gadget *gg)
{
    char path[160];
    LONG num = (id == G_PHOTX || id == G_PHOTY) && gg && gg->SpecialInfo ? ((struct StringInfo *)gg->SpecialInfo)->LongInt : 0;
    pf_pntr p;
    switch (id) {
    case G_PSETS:
        if (code >= sets.n) break;
        sel_set = code;
        snprintf(path, sizeof path, POINTER_SETS "/%s", sets.text[code]);
        if (pf_load(&setfile, path) != 1 || pf_find(&setfile, PF_PNTR, 0) < 0) { sel_set = -1; status("That set can't be read."); break; }
        if (!advanced) {
            if (pf_pntr_take_all(&cur, &setfile)) {
                pointer_relist();
                snprintf(status_text, sizeof status_text, "%s: Use, Test or Save to put it in place.", sets.text[code]);
            } else snprintf(status_text, sizeof status_text, "%s can't be taken (no memory?).", sets.text[code]);
        } else
            snprintf(status_text, sizeof status_text, "%s: The set takes all its pointers, This one the one shown.", sets.text[code]);
        status(status_text);
        break;
    case G_PUSESET:
        if (sel_set >= 0 && pf_pntr_take_all(&cur, &setfile)) { pointer_relist(); status("The whole set: Use, Test or Save to put it in place."); }
        break;
    case G_PONLY:
        if (sel_set >= 0 && pf_pntr_take(&cur, &setfile, sel_type)) {
            pointer_relist();
            snprintf(status_text, sizeof status_text, "The %s pointer from %s.", pointer_names[sel_type], sets.text[sel_set]);
        } else snprintf(status_text, sizeof status_text, "That set has no %s pointer.", pointer_names[sel_type]);
        status(status_text);
        break;
    case G_PTYPES:
        if (code < types.n) sel_type = type_of[code];
        break;
    case G_PHOTX: case G_PHOTY:
        if (pf_pntr_get(&cur, sel_type, &p)) {
            int x = id == G_PHOTX ? (int)num : -p.x, y = id == G_PHOTY ? (int)num : -p.y;
            if (x < 0) x = 0;
            if (x >= p.width) x = p.width - 1;
            if (y < 0) y = 0;
            if (y >= p.height) y = p.height - 1;
            pf_pntr_set_hotspot(&cur, sel_type, -x, -y);
        }
        break;
    case G_PHOT:
        hot_mode = gg && (gg->Flags & GFLG_SELECTED) != 0;
        status(hot_mode ? "Click the pixel the pointer clicks with." : "Click a colour, then the pixels to draw.");
        break;
    case G_PCOLR: case G_PCOLG: case G_PCOLB:
        if (pf_pntr_get(&cur, sel_type, &p) && draw_colour >= 1 && draw_colour <= p.ncolours) {
            uint8_t rgb[3];
            memcpy(rgb, p.rgb + 3 * (draw_colour - 1), 3);
            rgb[id - G_PCOLR] = (uint8_t)code;
            pf_pntr_set_colour(&cur, draw_colour, rgb);        /* every pointer's: they share their colours */
            pointer_draw();
        }
        break;
    case G_PCLEAR:
        if (pf_pntr_clear(&cur, sel_type)) {
            snprintf(status_text, sizeof status_text, "The %s pointer is clear: draw it, or Cancel.", pointer_names[sel_type]);
            status(status_text);
        }
        break;
    }
}

/* ---- Fonts: font.prefs --------------------------------------------------------------------------------- */

static const char *const label_labels[] = { "Plain", "Fields", "Shadow", "Outline", NULL };
static char font_text[3][80];

static int label_style(const pf_font *f)
{
    if (f->special == 1) return 2;
    if (f->special == 2) return 3;
    return f->drawmode == 1 ? 1 : 0;               /* JAM2: a field behind the label */
}

static void describe_font(int type, char *out)
{
    pf_font f;
    char n[128], *dot;
    if (!pf_font_get(&cur, type, &f) || !f.name[0]) { strcpy(out, "(the OS's own)"); return; }
    strcpy(n, f.name);
    if ((dot = strstr(n, ".font")) && !dot[5]) *dot = 0;
    snprintf(out, 80, pf_font_is_standard(&f) ? "%s %d (standard)" : "%s %d", n, f.ysize);
}

static int fonts_make(int row)
{
    static const char *const left[] = { "Screen font", "Icon font", "System font", "Icon labels", "Label colour", "Field colour", NULL };
    int bw = tw("Choose...") + 24, sw = tw("Standard") + 24, boxw, nc = screen_colours(), pw;
    columns(left, NULL);
    boxw = W1 - bw - sw - 12;
    if (boxw > 300) boxw = 300;
    pw = 24 * nc > W1 ? W1 : 24 * nc;
    describe_font(PF_SCREENFONT, font_text[PF_SCREENFONT]);
    describe_font(PF_WBFONT, font_text[PF_WBFONT]);
    describe_font(PF_SYSFONT, font_text[PF_SYSFONT]);
    G(TEXT_KIND, G_FSCREEN, X1, row, boxw, lh, "Screen font", PLACETEXT_LEFT, GTTX_Border, TRUE, GTTX_Text, (ULONG)font_text[PF_SCREENFONT]);
    G(BUTTON_KIND, G_FSCREENPICK, X1 + boxw + 6, row, bw, lh, "Choose...", 0, GA_Disabled, damaged);
    G(BUTTON_KIND, G_FSCREENSTD, X1 + boxw + bw + 12, row, sw, lh, "Standard", 0, GA_Disabled, damaged); row += lh + 4;
    G(TEXT_KIND, G_FICON, X1, row, boxw, lh, "Icon font", PLACETEXT_LEFT, GTTX_Border, TRUE, GTTX_Text, (ULONG)font_text[PF_WBFONT]);
    G(BUTTON_KIND, G_FICONPICK, X1 + boxw + 6, row, bw, lh, "Choose...", 0, GA_Disabled, damaged);
    G(BUTTON_KIND, G_FICONSTD, X1 + boxw + bw + 12, row, sw, lh, "Standard", 0, GA_Disabled, damaged); row += lh + 4;
    if (advanced) {
        G(TEXT_KIND, G_FSYS, X1, row, boxw, lh, "System font", PLACETEXT_LEFT, GTTX_Border, TRUE, GTTX_Text, (ULONG)font_text[PF_SYSFONT]);
        G(BUTTON_KIND, G_FSYSPICK, X1 + boxw + 6, row, bw, lh, "Choose...", 0, GA_Disabled, damaged); row += lh + 8;
        G(CYCLE_KIND, G_FLABELS, X1, row, 160, lh, "Icon labels", PLACETEXT_LEFT, GTCY_Labels, (ULONG)label_labels); row += lh + 4;
        G(PALETTE_KIND, G_FTEXTPEN, X1, row, pw, lh, "Label colour", PLACETEXT_LEFT, GTPA_NumColors, nc); row += lh + 4;
        G(PALETTE_KIND, G_FFIELDPEN, X1, row, pw, lh, "Field colour", PLACETEXT_LEFT, GTPA_NumColors, nc); row += lh + 4;
    }
    return row;
}

static void fonts_show(void)
{
    pf_font wb;
    int have = pf_font_get(&cur, PF_WBFONT, &wb), nc = screen_colours();
    describe_font(PF_SCREENFONT, font_text[PF_SCREENFONT]);
    describe_font(PF_WBFONT, font_text[PF_WBFONT]);
    describe_font(PF_SYSFONT, font_text[PF_SYSFONT]);
    SET(G_FSCREEN, GTTX_Text, (ULONG)font_text[PF_SCREENFONT]);
    SET(G_FICON, GTTX_Text, (ULONG)font_text[PF_WBFONT]);
    SET(G_FSYS, GTTX_Text, (ULONG)font_text[PF_SYSFONT]);
    SET(G_FLABELS, GTCY_Active, label_style(&wb), GA_Disabled, !have || damaged);
    SET(G_FTEXTPEN, GTPA_Color, wb.front < nc ? wb.front : 1, GA_Disabled, !have || damaged);
    SET(G_FFIELDPEN, GTPA_Color, wb.back < nc ? wb.back : 0, GA_Disabled, !have || damaged || label_style(&wb) != 1);
}

static void pick_font(int type, const char *title, int fixed)
{
    pf_font f;
    if (!pf_font_get(&cur, type, &f)) {            /* none in the file yet: the pens and mode the OS's own file has */
        f.front = 1; f.back = 0; f.drawmode = type == PF_WBFONT ? 0 : 1;
    }
    if (!ask_font(title, &f, fixed)) return;
    if (!pf_font_put(&cur, &f)) { status("No memory for that."); return; }
    status("Use, Test or Save: the font is used when the Workbench screen opens again.");
}

/* The Team's standard font for the screen or the icons (pf_font_standard), its pens and mode kept */
static void standard_font(int type)
{
    pf_font f;
    if (!pf_font_get(&cur, type, &f)) {
        f.front = 1; f.back = 0; f.drawmode = type == PF_WBFONT ? 0 : 1;
    }
    pf_font_standard(&f);
    if (!pf_font_put(&cur, &f)) { status("No memory for that."); return; }
    status("Use, Test or Save: the standard font is used when the Workbench screen opens again.");
}

static void fonts_handle(int id, UWORD code)
{
    pf_font wb;
    if (id == G_FSCREENPICK) pick_font(PF_SCREENFONT, "The screen's font", 0);
    else if (id == G_FSCREENSTD) standard_font(PF_SCREENFONT);
    else if (id == G_FICONSTD) standard_font(PF_WBFONT);
    else if (id == G_FICONPICK) pick_font(PF_WBFONT, "The icons' font", 0);
    else if (id == G_FSYSPICK) pick_font(PF_SYSFONT, "The system font (fixed width)", 1);
    else if ((id == G_FLABELS || id == G_FTEXTPEN || id == G_FFIELDPEN) && pf_font_get(&cur, PF_WBFONT, &wb)) {
        if (id == G_FLABELS) {
            wb.drawmode = code == 1 ? 1 : 0;
            wb.special = code == 2 ? 1 : code == 3 ? 2 : 0;
        } else if (id == G_FTEXTPEN) wb.front = code;
        else wb.back = code;
        pf_font_put(&cur, &wb);
    }
}

/* ---- Colours: palette.prefs ---------------------------------------------------------------------------- */

static const char *const pen_label[13] = {
    "Detail", "Block", "Text", "Bright edges", "Dark edges", "Fill", "Text on fill", "Background", "Important text",
    "Menu text", "Menu background", "Menu line", "Menu outline"
};
static pf_palette pal;
static int sel_colour, sel_pen = 2, ncolours;
static ULONG screen_rgb[8 * 3];            /* the screen's first colours as the tab opened */
static int colours_live;                   /* the screen's colours were changed here */
static int colours_ready;                  /* colours_open() done for this visit to the tab */
static struct names pen_names;

static void colours_open(void)
{
    int k;
    if (colours_ready) return;                 /* once a visit: a change of view keeps what was changed */
    colours_ready = 1;
    ncolours = screen_colours();
    GetRGB32(scr->ViewPort.ColorMap, 0, (ULONG)ncolours, screen_rgb);
    colours_live = 0;
    if (!pf_palette_get(&cur, &pal)) {         /* no file: the colours and pens the screen has now */
        struct DrawInfo *dri = GetScreenDrawInfo(scr);
        for (k = 0; k < ncolours; k++) {
            pal.col[k].index = (int16_t)k;
            pal.col[k].r = (uint16_t)(screen_rgb[3 * k] >> 16);
            pal.col[k].g = (uint16_t)(screen_rgb[3 * k + 1] >> 16);
            pal.col[k].b = (uint16_t)(screen_rgb[3 * k + 2] >> 16);
        }
        pal.col[ncolours].index = -1;
        if (dri && ncolours >= 8) {
            int n = dri->dri_NumPens < PF_PENS - 1 ? dri->dri_NumPens : PF_PENS - 1;
            for (k = 0; k < n; k++) pal.pens8[k] = dri->dri_Pens[k];
            pal.pens8[n] = PF_PEN_END;
        }
        if (dri) FreeScreenDrawInfo(scr, dri);
    }
    names_init(&pen_names);
    for (k = 0; k < 13; k++) names_add(&pen_names, pen_label[k]);
}

static void colour_now(int i, uint8_t rgb[3])
{
    if (pf_palette_colour(&pal, i, rgb)) return;
    rgb[0] = (uint8_t)(screen_rgb[3 * i] >> 24); rgb[1] = (uint8_t)(screen_rgb[3 * i + 1] >> 24); rgb[2] = (uint8_t)(screen_rgb[3 * i + 2] >> 24);
}

static void colour_live(int i)             /* colour i on the screen now, as the OS's Palette shows it */
{
    uint8_t rgb[3];
    if (i >= ncolours) return;
    colour_now(i, rgb);
    SetRGB32(&scr->ViewPort, (ULONG)i, rgb[0] * 0x01010101UL, rgb[1] * 0x01010101UL, rgb[2] * 0x01010101UL);
    colours_live = 1;
}

static void colours_restore(void)          /* the screen's colours as they were */
{
    int i;
    if (!colours_live) return;
    for (i = 0; i < ncolours; i++) SetRGB32(&scr->ViewPort, (ULONG)i, screen_rgb[3 * i], screen_rgb[3 * i + 1], screen_rgb[3 * i + 2]);
    colours_live = 0;
}

static int colours_make(int row)
{
    static const char *const left[] = { "Colour", "Red", "Green", "Blue", NULL };
    static const char *const right[] = { "Pen", "4 colours", "8 or more", NULL };
    int pw, sw, end2 = row;
    columns(left, advanced ? right : NULL);
    pw = 24 * ncolours > W1 ? W1 : 24 * ncolours;
    sw = W1 - tw("255") - 12;
    if (sw > 260) sw = 260;
    G(PALETTE_KIND, G_CPAL, X1, row, pw, lh, "Colour", PLACETEXT_LEFT, GTPA_NumColors, ncolours, GTPA_Color, sel_colour); row += lh + 6;
    G(SLIDER_KIND, G_CR, X1, row, sw, lh, "Red", PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 255, GTSL_LevelFormat, (ULONG)"%3ld",
      GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, GA_Immediate, TRUE); row += lh + 4;
    G(SLIDER_KIND, G_CG, X1, row, sw, lh, "Green", PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 255, GTSL_LevelFormat, (ULONG)"%3ld",
      GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, GA_Immediate, TRUE); row += lh + 4;
    G(SLIDER_KIND, G_CB, X1, row, sw, lh, "Blue", PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 255, GTSL_LevelFormat, (ULONG)"%3ld",
      GTSL_MaxLevelLen, 3, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, GA_Immediate, TRUE); row += lh + 6;
    if (advanced) {
        int listh = 5 * (fh + 1) + 4, p2 = 24 * ncolours > W2 ? W2 : 24 * ncolours;
        G(BUTTON_KIND, G_COS, X1, row, tw("The OS's colours") + 24, lh, "The OS's colours", 0, GA_Disabled, FALSE); row += lh + 4;
        G(LISTVIEW_KIND, G_CPENS, X2, end2, W2, listh, "Pen", PLACETEXT_LEFT, GTLV_Labels, (ULONG)&pen_names.list,
          GTLV_ShowSelected, NULL, GTLV_Selected, sel_pen, GTLV_MakeVisible, sel_pen);
        end2 += listh + 6;
        G(PALETTE_KIND, G_CPEN4, X2, end2, 24 * 4 > W2 ? W2 : 24 * 4, lh, "4 colours", PLACETEXT_LEFT, GTPA_NumColors, 4); end2 += lh + 4;
        G(PALETTE_KIND, G_CPEN8, X2, end2, p2, lh, "8 or more", PLACETEXT_LEFT, GTPA_NumColors, ncolours); end2 += lh + 4;
    }
    return row > end2 ? row : end2;
}

static void colours_show(void)
{
    uint8_t rgb[3];
    int p4 = pf_palette_pen(pal.pens4, sel_pen), p8 = pf_palette_pen(pal.pens8, sel_pen);
    colour_now(sel_colour, rgb);
    SET(G_CPAL, GTPA_Color, sel_colour);
    SET(G_CR, GTSL_Level, rgb[0], GA_Disabled, damaged);
    SET(G_CG, GTSL_Level, rgb[1], GA_Disabled, damaged);
    SET(G_CB, GTSL_Level, rgb[2], GA_Disabled, damaged);
    SET(G_CPEN4, GTPA_Color, p4 >= 0 && p4 < 4 ? p4 : 0, GA_Disabled, p4 < 0 || damaged);
    SET(G_CPEN8, GTPA_Color, p8 >= 0 && p8 < ncolours ? p8 : 0, GA_Disabled, p8 < 0 || damaged);
}

static void colours_handle(int id, UWORD code)
{
    uint8_t rgb[3];
    int k;
    switch (id) {
    case G_CPAL: sel_colour = code; break;
    case G_CR: case G_CG: case G_CB:
        colour_now(sel_colour, rgb);
        rgb[id == G_CR ? 0 : id == G_CG ? 1 : 2] = (uint8_t)code;
        if (!pf_palette_set_colour(&pal, sel_colour, rgb) || !pf_palette_put(&cur, &pal)) { status("No room for that colour."); break; }
        colour_live(sel_colour);
        break;
    case G_COS:
        pf_palette_defaults(&pal);
        pf_palette_put(&cur, &pal);
        for (k = 0; k < ncolours; k++) colour_live(k);
        status("The OS's own colours and pens: Use, Test or Save to keep them.");
        break;
    case G_CPENS: if (code < 13) sel_pen = code; break;
    case G_CPEN4: case G_CPEN8:
        if (!pf_palette_set_pen(id == G_CPEN4 ? pal.pens4 : pal.pens8, sel_pen, code)) status("This file has no such pen.");
        else {
            pf_palette_put(&cur, &pal);
            status("Pens are used when the Workbench screen opens again.");
        }
        break;
    }
}

/* ---- Backdrops: wbpattern.prefs ------------------------------------------------------------------------ */

static const char *const for_labels[] = { "The Workbench", "Drawers", "The screen", NULL };
static const char *const use_labels[] = { "A picture", "A pattern", "Nothing", NULL };
static const char *const place_labels[] = { "Tiled", "Centred", "Scaled", "Scaled smoothly", NULL };
static const char *const dither_labels[] = { "Default", "Fast", "Good", "Best", NULL };
static const char *const match_labels[] = { "Default", "As icons", "As pictures", "Exact", NULL };
static int bwhich;                         /* PF_WBP_ROOT (the Workbench), DRAWER or SCREEN */
static char bpath[256];

static int bflags(void)                    /* the flags of the backdrop shown; a new picture's when there is none */
{
    pf_ptrn p;
    return pf_ptrn_get(&cur, bwhich, &p) ? p.flags : 0x0a00;     /* dithered well, matched as a picture, tiled */
}

static int backdrops_make(int row)
{
    static const char *const left[] = { "For", "Use", "Picture", "Place", "Remap colours", "Dither", "Colour match", NULL };
    int bw = tw("Choose...") + 24, cw;
    columns(left, NULL);
    cw = W1 > 220 ? 220 : W1;
    G(CYCLE_KIND, G_BFOR, X1, row, cw, lh, "For", PLACETEXT_LEFT, GTCY_Labels, (ULONG)for_labels, GTCY_Active, bwhich); row += lh + 6;
    if (advanced) { G(CYCLE_KIND, G_BUSE, X1, row, cw, lh, "Use", PLACETEXT_LEFT, GTCY_Labels, (ULONG)use_labels); row += lh + 4; }
    G(STRING_KIND, G_BPATH, X1, row, W1 - bw - 6, lh, "Picture", PLACETEXT_LEFT, GTST_MaxChars, 255);
    G(BUTTON_KIND, G_BPICK, X1 + W1 - bw, row, bw, lh, "Choose...", 0, GA_Disabled, damaged); row += lh + 4;
    G(CYCLE_KIND, G_BPLACE, X1, row, cw, lh, "Place", PLACETEXT_LEFT, GTCY_Labels, (ULONG)place_labels); row += lh + 4;
    if (advanced) {
        G(CHECKBOX_KIND, G_BREMAP, X1, row, 26, lh, "Remap colours", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 4;
        G(CYCLE_KIND, G_BDITHER, X1, row, cw, lh, "Dither", PLACETEXT_LEFT, GTCY_Labels, (ULONG)dither_labels); row += lh + 4;
        G(CYCLE_KIND, G_BMATCH, X1, row, cw, lh, "Colour match", PLACETEXT_LEFT, GTCY_Labels, (ULONG)match_labels); row += lh + 4;
    }
    return row;
}

static void backdrops_show(void)
{
    pf_ptrn p;
    int have = pf_ptrn_get(&cur, bwhich, &p), use = !have ? 2 : p.picture ? 0 : 1, f = have ? p.flags : bflags();
    strcpy(bpath, have && p.picture ? p.name : "");
    SET(G_BFOR, GTCY_Active, bwhich);
    SET(G_BUSE, GTCY_Active, use, GA_Disabled, damaged);
    SET(G_BPATH, GTST_String, (ULONG)bpath, GA_Disabled, damaged);
    SET(G_BPLACE, GTCY_Active, (f & PF_WBPF_PLACEMENT_MASK) >> 12, GA_Disabled, use != 0 || damaged);
    SET(G_BREMAP, GTCB_Checked, !(f & PF_WBPF_NOREMAP), GA_Disabled, use == 2 || damaged);
    SET(G_BDITHER, GTCY_Active, (f & PF_WBPF_DITHER_MASK) >> 8, GA_Disabled, use == 2 || damaged);
    SET(G_BMATCH, GTCY_Active, (f & PF_WBPF_PRECISION_MASK) >> 10, GA_Disabled, use == 2 || damaged);
}

/* The pattern the file had for this backdrop when the tab opened, back in place: 1, or 0 when it had none. */
static int pattern_back(void)
{
    pf_ptrn p;
    int i, at = -1;
    if (!pf_ptrn_get(&start, bwhich, &p) || p.picture) return 0;
    for (i = 0; i < start.n; i++)
        if (start.ch[i].id == PF_PTRN && start.ch[i].len >= 18 && pf_get16(start.ch[i].data + 16) == bwhich) break;
    pf_ptrn_remove(&cur, bwhich);
    for (at = 0; at < cur.n; at++)             /* before the first backdrop after it, as WBPattern orders them */
        if (cur.ch[at].id == PF_PTRN && cur.ch[at].len >= 18 && pf_get16(cur.ch[at].data + 16) > bwhich) break;
    return pf_insert(&cur, at, PF_PTRN, start.ch[i].data, start.ch[i].len);
}

static void set_picture(const char *path)
{
    if (!path[0]) return;
    if (pf_ptrn_set_picture(&cur, bwhich, path, bflags())) status("Use, Test or Save to put the picture in place.");
    else status("No memory for that.");
}

static void backdrops_handle(int id, UWORD code, struct Gadget *gg)
{
    int f = bflags();
    pf_ptrn p;
    switch (id) {
    case G_BFOR: bwhich = code; break;
    case G_BUSE:
        if (code == 0) {
            if (pf_ptrn_get(&cur, bwhich, &p) && p.picture) break;
            if (bpath[0]) set_picture(bpath);
            else status("Type the picture's path, or Choose... one.");
        } else if (code == 1) {
            if (!pattern_back()) status("There is no pattern for this one: WBPattern in Prefs/Classic draws them.");
        } else pf_ptrn_remove(&cur, bwhich);
        break;
    case G_BPATH:
        strncpy(bpath, (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof bpath - 1);
        set_picture(bpath);
        break;
    case G_BPICK:
        if (ask_file("A picture for the backdrop", BACKDROPS, bpath, sizeof bpath)) set_picture(bpath);
        break;
    case G_BPLACE: case G_BREMAP: case G_BDITHER: case G_BMATCH:
        if (id == G_BPLACE) f = (f & ~PF_WBPF_PLACEMENT_MASK) | (code << 12);
        else if (id == G_BREMAP) f = (gg->Flags & GFLG_SELECTED) ? f & ~PF_WBPF_NOREMAP : f | PF_WBPF_NOREMAP;
        else if (id == G_BDITHER) f = (f & ~PF_WBPF_DITHER_MASK) | (code << 8);
        else f = (f & ~PF_WBPF_PRECISION_MASK) | (code << 10);
        if (!pf_ptrn_set_flags(&cur, bwhich, f)) status("Choose a picture first.");
        break;
    }
}

/* ---- Workbench: workbench.prefs ------------------------------------------------------------------------ */

static const char *const precision_labels[] = { "Exact", "Image", "Icon", "GUI", NULL };
static const LONG precision_values[4] = { -1, 0, 16, 32 };     /* PRECISION_EXACT, IMAGE, ICON, GUI */
static const char *const drawers_labels[] = { "Mixed", "First", "Last", NULL };     /* as the OS's Workbench editor words it */
/* the icon frame, as the OS's Workbench editor names its sizes ("Border Size"); "Its own" only when the file has
 * a frame none of them is, which choosing it puts back */
static const char *frame_labels[6] = { "None", "Small", "Medium", "Large", NULL, NULL };
static int16_t own_emboss[4];
static pf_wbnc wbn;
static char wbtitle[256];
/* the devices Workbench can hide: those mounted with a file system running (not the one SYS: is on), then any
 * the file hides that aren't mounted now; whether each is hidden */
static struct names devs;
static char dev_name[48][34];
static int dev_hidden[48], ndevs;

static void devices_label(void)
{
    int k;
    char line[40];
    names_init(&devs);
    for (k = 0; k < ndevs; k++) {
        snprintf(line, sizeof line, "%-8s %s", dev_name[k], dev_hidden[k] ? "hidden" : "shown");
        names_add(&devs, line);
    }
}

static void devices_add(const char *name, int hidden)
{
    int k;
    for (k = 0; k < ndevs; k++) {                             /* the same name in either case: there already */
        const char *a = dev_name[k], *b = name;
        while (*a && *b && ((*a ^ *b) & ~0x20) == 0) a++, b++;
        if (!*a && !*b) return;
    }
    if (ndevs >= 48 || strlen(name) >= sizeof dev_name[0]) return;
    strcpy(dev_name[ndevs], name);
    dev_hidden[ndevs++] = hidden;
}

/* The list, in the order the DOS list has the devices (the order the OS's Workbench editor shows them in). */
static void devices_read(void)
{
    struct { char name[34]; } found[40];
    struct DosList *dl;
    struct MsgPort *sys_port = NULL;
    BPTR sys = Lock((CONST_STRPTR)"SYS:", ACCESS_READ);
    char name[34];
    int n = 0, k;
    if (sys) { sys_port = ((struct FileLock *)BADDR(sys))->fl_Task; UnLock(sys); }
    dl = LockDosList(LDF_DEVICES | LDF_READ);
    while ((dl = NextDosEntry(dl, LDF_DEVICES)) && n < 40) {
        const UBYTE *b = (const UBYTE *)BADDR(dl->dol_Name);
        int len = b ? b[0] : 0;
        if (!dl->dol_Task || dl->dol_Task == sys_port || len < 1 || len > 30) continue;   /* no handler running, or SYS:'s */
        memcpy(found[n].name, b + 1, (size_t)len);
        found[n].name[len] = ':'; found[n].name[len + 1] = 0;
        n++;
    }
    UnLockDosList(LDF_DEVICES | LDF_READ);
    ndevs = 0;
    for (k = 0; k < n; k++)                                   /* outside the lock: asking a handler may take a while */
        if (IsFileSystem((CONST_STRPTR)found[k].name)) devices_add(found[k].name, pf_wbhd_is(&cur, found[k].name));
    for (k = 0; pf_wbhd_get(&cur, k, name, sizeof name); k++) devices_add(name, 1);
    devices_label();
}

static int devices_write(void)            /* the hidden ones into the file, in the list's order */
{
    const char *hide[48];
    int k, n = 0;
    for (k = 0; k < ndevs; k++) if (dev_hidden[k]) hide[n++] = dev_name[k];
    return pf_wbhd_set(&cur, hide, n);
}

static int workbench_make(int row)
{
    static const char *const simple[] = { "Icon borders", "Disk gauge", "Screen title bar", "Icon label length", NULL };
    static const char *const left[] = { "Icon borders", "Icon label length", "NewIcons", "Colour icons", "Icon colours",
                                        "Lock pens 4 to 7", "Hidden devices", NULL };
    static const char *const right[] = { "Disk gauge", "Screen title", "Title update ms", "Default stack", "Drawers in lists",
                                         "Oldest first", "Keep lists in view", "Copy buffer", "Type-ahead s", NULL };
    /* Advanced: icons and the hidden devices on the left, the screen and the drawers on the right; on a 256-line
     * screen one line between the rows, so it fits */
    int step = advanced ? (tight ? lh + 1 : lh + 2) : lh + 4, r2 = row, fw = widest(frame_labels) + 34, cw, iw, listh;
    columns(advanced ? left : simple, advanced ? right : NULL);
    if (fw > W1) fw = W1;
    G(CYCLE_KIND, G_WFRAME, X1, row, fw, lh, "Icon borders", PLACETEXT_LEFT, GTCY_Labels, (ULONG)frame_labels); row += step;
    if (!advanced) {
        G(CHECKBOX_KIND, G_WGAUGE, X1, row, 26, lh, "Disk gauge", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += step;
        G(CHECKBOX_KIND, G_WTITLEBAR, X1, row, 26, lh, "Screen title bar", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += step;
        G(INTEGER_KIND, G_WLABELLEN, X1, row, 60, lh, "Icon label length", PLACETEXT_LEFT, GTIN_MaxChars, 3); row += step;
        return row;
    }
    cw = W1 > 160 ? 160 : W1;
    iw = W2 > 100 ? 100 : W2;
    listh = 4 * (fh + 1) + 4;
    G(INTEGER_KIND, G_WLABELLEN, X1, row, 60, lh, "Icon label length", PLACETEXT_LEFT, GTIN_MaxChars, 3); row += step;
    G(CHECKBOX_KIND, G_WNEWICONS, X1, row, 26, lh, "NewIcons", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += step;
    G(CHECKBOX_KIND, G_WCOLICONS, X1, row, 26, lh, "Colour icons", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += step;
    G(CYCLE_KIND, G_WPRECISION, X1, row, cw, lh, "Icon colours", PLACETEXT_LEFT, GTCY_Labels, (ULONG)precision_labels); row += step;
    G(CHECKBOX_KIND, G_WLOCKPENS, X1, row, 26, lh, "Lock pens 4 to 7", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += step;
    G(LISTVIEW_KIND, G_WHIDDEN, X1, row, W1, listh, "Hidden devices", PLACETEXT_LEFT, GTLV_Labels, (ULONG)&devs.list);
    row += listh + 4;
    G(CHECKBOX_KIND, G_WGAUGE, X2, r2, 26, lh, "Disk gauge", PLACETEXT_LEFT, GTCB_Scaled, TRUE); r2 += step;
    G(CHECKBOX_KIND, G_WTITLEBAR, X2, r2, 26, lh, "Screen title", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    G(STRING_KIND, G_WTITLE, X2 + 32, r2, W2 - 32, lh, NULL, 0, GTST_MaxChars, 200); r2 += step;
    G(INTEGER_KIND, G_WDELAY, X2, r2, iw, lh, "Title update ms", PLACETEXT_LEFT, GTIN_MaxChars, 5); r2 += step;
    G(INTEGER_KIND, G_WSTACK, X2, r2, iw, lh, "Default stack", PLACETEXT_LEFT, GTIN_MaxChars, 8); r2 += step;
    G(CYCLE_KIND, G_WDRAWERS, X2, r2, W2, lh, "Drawers in lists", PLACETEXT_LEFT, GTCY_Labels, (ULONG)drawers_labels); r2 += step;
    G(CHECKBOX_KIND, G_WOLDFIRST, X2, r2, 26, lh, "Oldest first", PLACETEXT_LEFT, GTCB_Scaled, TRUE); r2 += step;
    G(CHECKBOX_KIND, G_WBOUND, X2, r2, 26, lh, "Keep lists in view", PLACETEXT_LEFT, GTCB_Scaled, TRUE); r2 += step;
    G(INTEGER_KIND, G_WCOPYBUF, X2, r2, iw, lh, "Copy buffer", PLACETEXT_LEFT, GTIN_MaxChars, 9); r2 += step;
    G(INTEGER_KIND, G_WRESTART, X2, r2, iw, lh, "Type-ahead s", PLACETEXT_LEFT, GTIN_MaxChars, 3); r2 += step;
    return row > r2 ? row : r2;
}

static void workbench_show(void)
{
    int k, prec = 2, frame = pf_wbnc_frame(&wbn);
    for (k = 0; k < 4; k++) if (precision_values[k] == wbn.precision) prec = k;
    SET(G_WFRAME, GTCY_Active, frame >= 0 ? frame : 4, GA_Disabled, damaged);
    SET(G_WGAUGE, GTCB_Checked, !wbn.nogauge, GA_Disabled, damaged);
    SET(G_WTITLEBAR, GTCB_Checked, !wbn.notitlebar, GA_Disabled, damaged);
    SETINT(G_WLABELLEN, wbn.maxname, damaged);
    SET(G_WNEWICONS, GTCB_Checked, wbn.newicons, GA_Disabled, damaged);
    SET(G_WCOLICONS, GTCB_Checked, wbn.coloricons, GA_Disabled, damaged);
    SET(G_WPRECISION, GTCY_Active, prec, GA_Disabled, damaged);
    SET(G_WLOCKPENS, GTCB_Checked, wbn.lockpens, GA_Disabled, damaged);
    SET(G_WHIDDEN, GA_Disabled, damaged);
    SETINT(G_WSTACK, wbn.stack, damaged);
    SET(G_WDRAWERS, GTCY_Active, (wbn.flags & PF_WBF_DRAWERPOSMASK) <= 2 ? (wbn.flags & PF_WBF_DRAWERPOSMASK) : 0, GA_Disabled, damaged);
    SET(G_WOLDFIRST, GTCB_Checked, (wbn.flags & PF_WBF_OLDDATESFIRST) != 0, GA_Disabled, damaged);
    SET(G_WBOUND, GTCB_Checked, (wbn.flags & PF_WBF_BOUNDTEXTVIEW) != 0, GA_Disabled, damaged);
    SETINT(G_WDELAY, wbn.titledelay, damaged);
    SETINT(G_WCOPYBUF, wbn.copybuf, damaged);
    SETINT(G_WRESTART, wbn.restart, damaged);
    SET(G_WTITLE, GTST_String, (ULONG)wbtitle, GA_Disabled, damaged);
}

static LONG clamp(LONG v, LONG lo, LONG hi) { return v < lo ? lo : v > hi ? hi : v; }

static void workbench_handle(int id, UWORD code, struct Gadget *gg)
{
    int sel = (gg->Flags & GFLG_SELECTED) != 0;
    LONG num = 0;
    if (id == G_WLABELLEN || id == G_WSTACK || id == G_WDELAY || id == G_WCOPYBUF || id == G_WRESTART)
        num = ((struct StringInfo *)gg->SpecialInfo)->LongInt;
    switch (id) {
    case G_WFRAME:
        if (code <= 3) pf_wbnc_set_frame(&wbn, code);
        else { memcpy(wbn.emboss, own_emboss, sizeof own_emboss); wbn.borderless = 0; }
        break;
    case G_WGAUGE: wbn.nogauge = !sel; break;
    case G_WTITLEBAR: wbn.notitlebar = !sel; break;
    case G_WLABELLEN: wbn.maxname = clamp(num, 4, 999); break;
    case G_WNEWICONS: wbn.newicons = sel; break;
    case G_WCOLICONS: wbn.coloricons = sel; break;
    case G_WPRECISION: if (code < 4) wbn.precision = precision_values[code]; break;
    case G_WLOCKPENS: wbn.lockpens = sel; break;
    case G_WHIDDEN:
        if (code >= ndevs) return;
        dev_hidden[code] = !dev_hidden[code];
        SET(G_WHIDDEN, GTLV_Labels, ~0UL);
        devices_label();
        SET(G_WHIDDEN, GTLV_Labels, (ULONG)&devs.list);
        if (!devices_write()) status("No memory for that.");
        else {
            snprintf(status_text, sizeof status_text, dev_hidden[code] ? "Workbench won't show %s." : "Workbench shows %s again.",
                     dev_name[code]);
            status(status_text);
        }
        return;
    case G_WSTACK: wbn.stack = (ULONG)clamp(num, 4096, 1048576); break;
    case G_WDRAWERS: wbn.flags = (wbn.flags & ~PF_WBF_DRAWERPOSMASK) | code; break;
    case G_WOLDFIRST: wbn.flags = sel ? wbn.flags | PF_WBF_OLDDATESFIRST : wbn.flags & ~PF_WBF_OLDDATESFIRST; break;
    case G_WBOUND: wbn.flags = sel ? wbn.flags | PF_WBF_BOUNDTEXTVIEW : wbn.flags & ~PF_WBF_BOUNDTEXTVIEW; break;
    case G_WDELAY: wbn.titledelay = (uint16_t)clamp(num, 0, 60000); break;
    case G_WCOPYBUF: wbn.copybuf = (ULONG)clamp(num, 0, 16 * 1024 * 1024); break;
    case G_WRESTART: wbn.restart = (ULONG)clamp(num, 0, 60); break;
    case G_WTITLE:
        strncpy(wbtitle, (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof wbtitle - 1);
        if (!pf_wbtf_put(&cur, wbtitle)) status("No memory for that.");
        else status(wbtitle[0] ? "The screen's title (OpenBench sets its own when it starts)." : "No title of its own: the OS's.");
        return;
    default: return;
    }
    if (!pf_wbnc_put(&cur, &wbn)) status("No memory for that.");
}

/* ---- File requesters: asl.prefs ------------------------------------------------------------------------ */

static const char *const sortby_labels[] = { "Name", "Date", "Size", NULL };
static const char *const sortdrawers_labels[] = { "First", "Mixed with files", "Last", NULL };
static const char *const order_labels[] = { "Ascending", "Descending", NULL };
static const char *const opens_labels[] = { "Where the program says", "Centred on its window", "Centred on the screen",
                                            "At its window's corner", "At the screen's corner", "Under the pointer", NULL };
static const char *const size_labels[] = { "As the program says", "A share of its window", NULL };
static pf_asl asl;

static int asl_make(int row)
{
    static const char *const left[] = { "Sort by", "Drawers", "Order", "Opens", NULL };
    static const char *const right[] = { "Offset across", "Offset down", "Size", "Width %", "Height %", "Always", NULL };
    int step = lh + 4, r2 = row, cw;
    columns(left, advanced ? right : NULL);
    cw = W1 > 240 ? 240 : W1;
    G(CYCLE_KIND, G_ASORT, X1, row, cw, lh, "Sort by", PLACETEXT_LEFT, GTCY_Labels, (ULONG)sortby_labels); row += step;
    G(CYCLE_KIND, G_ADRAWERS, X1, row, cw, lh, "Drawers", PLACETEXT_LEFT, GTCY_Labels, (ULONG)sortdrawers_labels); row += step;
    G(CYCLE_KIND, G_AORDER, X1, row, cw, lh, "Order", PLACETEXT_LEFT, GTCY_Labels, (ULONG)order_labels); row += step;
    G(CYCLE_KIND, G_APLACE, X1, row, cw, lh, "Opens", PLACETEXT_LEFT, GTCY_Labels, (ULONG)opens_labels); row += step;
    if (advanced) {
        int iw = W2 > 70 ? 70 : W2;
        G(INTEGER_KIND, G_ALEFT, X2, r2, iw, lh, "Offset across", PLACETEXT_LEFT, GTIN_MaxChars, 5); r2 += step;
        G(INTEGER_KIND, G_ATOP, X2, r2, iw, lh, "Offset down", PLACETEXT_LEFT, GTIN_MaxChars, 5); r2 += step;
        G(CYCLE_KIND, G_ASIZE, X2, r2, W2, lh, "Size", PLACETEXT_LEFT, GTCY_Labels, (ULONG)size_labels); r2 += step;
        G(INTEGER_KIND, G_AWIDTH, X2, r2, iw, lh, "Width %", PLACETEXT_LEFT, GTIN_MaxChars, 3); r2 += step;
        G(INTEGER_KIND, G_AHEIGHT, X2, r2, iw, lh, "Height %", PLACETEXT_LEFT, GTIN_MaxChars, 3); r2 += step;
        G(CHECKBOX_KIND, G_AOVERRIDE, X2, r2, 26, lh, "Always", PLACETEXT_LEFT, GTCB_Scaled, TRUE); r2 += step;
    }
    return row > r2 ? row : r2;
}

static void asl_show(void)
{
    int place = asl.sizepos & 0x0f, size = (asl.sizepos >> 4) & 3;
    SET(G_ASORT, GTCY_Active, asl.sortby <= 2 ? asl.sortby : 0, GA_Disabled, damaged);
    SET(G_ADRAWERS, GTCY_Active, asl.sortdrawers <= 2 ? asl.sortdrawers : 0, GA_Disabled, damaged);
    SET(G_AORDER, GTCY_Active, asl.sortorder <= 1 ? asl.sortorder : 0, GA_Disabled, damaged);
    SET(G_APLACE, GTCY_Active, place <= 5 ? place : 0, GA_Disabled, damaged);
    SETINT(G_ALEFT, asl.left, damaged || (place != 3 && place != 4));
    SETINT(G_ATOP, asl.top, damaged || (place != 3 && place != 4));
    SET(G_ASIZE, GTCY_Active, size <= 1 ? size : 0, GA_Disabled, damaged);
    SETINT(G_AWIDTH, asl.width, damaged || size != 1);
    SETINT(G_AHEIGHT, asl.height, damaged || size != 1);
    SET(G_AOVERRIDE, GTCB_Checked, (asl.sizepos & 0x40) != 0, GA_Disabled, damaged);
}

static void asl_handle(int id, UWORD code, struct Gadget *gg)
{
    LONG num = 0;
    if (id == G_ALEFT || id == G_ATOP || id == G_AWIDTH || id == G_AHEIGHT) num = ((struct StringInfo *)gg->SpecialInfo)->LongInt;
    switch (id) {
    case G_ASORT: asl.sortby = code; break;
    case G_ADRAWERS: asl.sortdrawers = code; break;
    case G_AORDER: asl.sortorder = code; break;
    case G_APLACE: asl.sizepos = (asl.sizepos & ~0x0f) | code; break;
    case G_ALEFT: asl.left = (int)clamp(num, -4000, 4000); break;
    case G_ATOP: asl.top = (int)clamp(num, -4000, 4000); break;
    case G_ASIZE: asl.sizepos = (asl.sizepos & ~0x30) | (code << 4); break;
    case G_AWIDTH: asl.width = (int)clamp(num, 10, 100); break;
    case G_AHEIGHT: asl.height = (int)clamp(num, 10, 100); break;
    case G_AOVERRIDE: asl.sizepos = (gg->Flags & GFLG_SELECTED) ? asl.sizepos | 0x40 : asl.sizepos & ~0x40; break;
    default: return;
    }
    if (!pf_asl_put(&cur, &asl)) status("No memory for that.");
}

/* ---- the frame: one page at a time ------------------------------------------------------------------- */

static void page_open(void)                /* the page's own state from the file just read */
{
    switch (ap) {
    case AP_POINTER: sel_set = -1; hot_mode = 0; pf_init(&setfile); break;
    case AP_COLOURS: break;                    /* colours_open() needs the screen: in gui_once */
    case AP_WORKBENCH:
        pf_wbnc_get(&cur, &wbn);
        pf_wbtf_get(&cur, wbtitle, sizeof wbtitle);
        memcpy(own_emboss, wbn.emboss, sizeof own_emboss);   /* "Its own" in the list only for a frame the sizes don't name */
        frame_labels[4] = pf_wbnc_frame(&wbn) < 0 ? "Its own" : NULL;
        devices_read();
        break;
    case AP_ASL: pf_asl_get(&cur, &asl); break;
    }
}

static int page_make(int row)
{
    switch (ap) {
    case AP_POINTER: return pointer_make(row);
    case AP_FONTS: return fonts_make(row);
    case AP_COLOURS: return colours_make(row);
    case AP_BACKDROPS: return backdrops_make(row);
    case AP_WORKBENCH: return workbench_make(row);
    default: return asl_make(row);
    }
}

static void page_show(void)
{
    switch (ap) {
    case AP_POINTER: pointer_show(); pointer_draw(); break;
    case AP_FONTS: fonts_show(); break;
    case AP_COLOURS: colours_show(); break;
    case AP_BACKDROPS: backdrops_show(); break;
    case AP_WORKBENCH: workbench_show(); break;
    default: asl_show(); break;
    }
}

static void page_handle(int id, UWORD code, struct Gadget *gg)
{
    if (damaged) { status("This file can't be read whole, so nothing in it is changed."); return; }
    switch (ap) {
    case AP_POINTER: pointer_handle(id, code, gg); break;
    case AP_FONTS: fonts_handle(id, code); break;
    case AP_COLOURS: colours_handle(id, code); break;
    case AP_BACKDROPS: backdrops_handle(id, code, gg); break;
    case AP_WORKBENCH: workbench_handle(id, code, gg); break;
    default: asl_handle(id, code, gg); break;
    }
}

/* What is typed in a string or number gadget counts even without Return (Use or Save straight after). */
static void page_typed(void)
{
    static const int ids[] = { G_PHOTX, G_PHOTY, G_BPATH, G_WLABELLEN, G_WSTACK, G_WDELAY, G_WCOPYBUF, G_WRESTART, G_WTITLE,
                               G_ALEFT, G_ATOP, G_AWIDTH, G_AHEIGHT };
    unsigned k;
    char keep[160];
    if (damaged) return;
    strcpy(keep, status_text);
    for (k = 0; k < sizeof ids / sizeof ids[0]; k++)
        if (gad[ids[k]] && !(gad[ids[k]]->Flags & GFLG_DISABLED)) {
            struct StringInfo *si = (struct StringInfo *)gad[ids[k]]->SpecialInfo;
            const char *s = (const char *)si->Buffer;
            if (ids[k] == G_BPATH) {                 /* a path typed: the same as one chosen */
                pf_ptrn p;
                if (!s[0] || (pf_ptrn_get(&cur, bwhich, &p) && p.picture && !strcmp(p.name, s))) continue;
            } else if (ids[k] == G_WTITLE) {
                if (!strcmp(s, wbtitle)) continue;
            } else {
                LONG typed = strtol(s, NULL, 10);
                if (typed == shown[ids[k]]) continue;  /* nothing typed, or taken already */
                si->LongInt = typed;
            }
            page_handle(ids[k], 0, gad[ids[k]]);
        }
    status(keep);
}

static void page_close(int kept)           /* the page is left: kept (Use, Save, another tab) or not */
{
    if (ap == AP_COLOURS && !kept) colours_restore();
    if (ap == AP_POINTER) { free_pens(); pf_free(&setfile); }
}

/* Use (save 0) or Save (1): ENV:, and ENVARC: for Save, each only when its bytes would change. 1 written. */
static int keep(int save)
{
    const struct ap_page *p = &pages[ap];
    int ok = 1, dirty;
    page_typed();
    test_until = 0;
    if (damaged) return 0;
    dirty = !pf_same(&cur, &start);
    if ((dirty || env_touched) && pf_differs(&cur, p->env)) ok = pf_save(&cur, p->env);
    if (save && (dirty || had_file) && pf_differs(&cur, p->envarc)) ok = pf_save(&cur, p->envarc) && ok;
    env_touched = 0;
    return ok;
}

static void test(void)
{
    const char *env = pages[ap].env;
    page_typed();
    if (damaged) return;
    if (!test_until) before_had = pf_load(&before, env) == 1;      /* ENV: as it was before the first Test */
    if (pf_differs(&cur, env)) { pf_save(&cur, env); env_touched = 1; }
    test_until = now_seconds() + TEST_SECONDS;
    status("Testing: back in 15 s unless you choose Use or Save.");
}

static void put_back(void)                 /* what a Test changed, as it was */
{
    const char *env = pages[ap].env;
    if (env_touched) {
        if (before_had) pf_save(&before, env);
        else DeleteFile((CONST_STRPTR)env);
    }
    if (ap == AP_COLOURS) colours_restore();
    env_touched = 0;
    test_until = 0;
}

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_TITLE, (STRPTR)"View", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Advanced", (STRPTR)"A", CHECKIT | MENUTOGGLE, 0, (APTR)4 },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* RETURN_OK or RETURN_FAIL, or 99 when the view changed (open again) */
static int gui_once(void)
{
    struct Gadget *glist = NULL;
    struct Menu *menu = NULL;
    struct MsgPort *tport = NULL;
    struct timerequest *tr = NULL;
    int top, row, inner, foot, quit = 0, kept = 0, rc = RETURN_OK, timer = 0, wl = 40, wt, i;
    win = NULL;
    if (!(scr = LockPubScreen(NULL))) return RETURN_FAIL;
    if (!(vi = GetVisualInfoA(scr, NULL))) { UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    L = 10;
    W = 620;
    tight = scr->Height < 400;
    painting = -1;
    if (ap == AP_COLOURS) colours_open();
    top = scr->WBorTop + fh + 1 + 6;
    last = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    last = lt_make(last, &ng, L, top, W - 20, lh, LT_POINTER + ap);
    row = top + lt_space(scr->Font, W - 20, lh) + 12;    /* below the tabs, one row or two (OpenPrefs: its sections) */
    row = page_make(row) + 6;
    /* the status line and the buttons at the window's foot, when another page made it taller */
    inner = scr->WBorTop + fh + 1;
    foot = (lh + 6) + (lh + 8);
    if (lt_win && inner + lt_win_h > row + foot) row = inner + lt_win_h - foot;
    G(TEXT_KIND, G_STATUS, L, row, W - 20, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + 6;
    {
        static const char *const names[4] = { "Save", "Use", "Test", "Cancel" };
        static const int ids[4] = { G_SAVE, G_USE, G_TEST, G_CANCEL };
        int bw = (W - 20 - 3 * 10) / 4;
        for (i = 0; i < 4; i++) G(BUTTON_KIND, ids[i], L + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, damaged && i < 3);
        row += lh + 8;
    }
    if (!last) { rc = RETURN_FAIL; goto out; }
    wt = scr->BarHeight + (scr->Height < 400 ? 1 : 10);
    lt_place(&wl, &wt);
    menus[6].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    win = lt_open(scr, wl, wt, W, row - inner, glist,
                  IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | CYCLEIDCMP | LISTVIEWIDCMP | STRINGIDCMP |
                  INTEGERIDCMP | CHECKBOXIDCMP | SLIDERIDCMP | PALETTEIDCMP | IDCMP_MOUSEBUTTONS, pages[ap].title);
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    lt_draw(win, gad[G_STATUS] ? gad[G_STATUS]->TopEdge - 6 : -1);
    page_show();
    lt_note_to(status);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);

    while (!quit) {
        struct IntuiMessage *m;
        ULONG tsig = timer ? 1UL << tport->mp_SigBit : 0;
        if (timer && test_until) {
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
        }
        Wait((1UL << win->UserPort->mp_SigBit) | tsig);
        if (timer && test_until) {
            if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
            WaitIO((struct IORequest *)tr);
            SetSignal(0, tsig);                /* an aborted request's reply leaves it set: Wait() would return at once */
            if (now_seconds() >= test_until) {
                put_back();
                status("The test is over: back as it was.");
                page_show();
            } else {
                snprintf(status_text, sizeof status_text, "Testing: back in %lu s unless you choose Use or Save.",
                         (unsigned long)(test_until - now_seconds()));
                status(status_text);
            }
        }
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            WORD mx = m->MouseX - LT_DX, my = m->MouseY;   /* the page's own place (OpenPrefs: right of the list) */
            int id;
            GT_ReplyIMsg(m);
            /* drawing the pointer (Advanced): the button down on the grid or a colour, moves while it is held */
            if (cls == IDCMP_MOUSEBUTTONS && ap == AP_POINTER) {
                if (code == SELECTDOWN && pointer_mouse(mx, my, 1) && painting >= 0) ReportMouse(TRUE, win);
                else if (code == SELECTUP && painting >= 0) { painting = -1; ReportMouse(FALSE, win); }
                continue;
            }
            if (cls == IDCMP_MOUSEMOVE && painting >= 0) { pointer_mouse(mx, my, 0); continue; }
            if (cls == IDCMP_CLOSEWINDOW) { if (test_until) put_back(); quit = 1; }
            else if (cls == IDCMP_MENUPICK) {
                UWORD mc = code;
                while (mc != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, mc);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: if (!damaged) { keep(1); kept = 1; } quit = 1; break;
                    case 2: if (!damaged) { keep(0); kept = 1; } quit = 1; break;
                    case 3: if (test_until) put_back(); quit = 1; break;
                    case 4: page_typed(); advanced = (it->Flags & CHECKED) != 0; write_view(advanced); rc = 99; quit = 1; kept = 1; break;
                    }
                    mc = it->NextSelect;
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(win);
                lt_draw(win, gad[G_STATUS] ? gad[G_STATUS]->TopEdge - 6 : -1);
                GT_EndRefresh(win, TRUE);
                if (ap == AP_POINTER) pointer_draw();
            }
            else if ((cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN || cls == IDCMP_MOUSEMOVE) && gg) {
                /* the sliders that show their colour as they move: the Colours tab's, the pointer's */
                int slider = gg == gad[G_CR] || gg == gad[G_CG] || gg == gad[G_CB] || gg == gad[G_PCOLR] || gg == gad[G_PCOLG] ||
                             gg == gad[G_PCOLB];
                if (cls == IDCMP_MOUSEMOVE && !slider) continue;
                id = gg->GadgetID;
                if (cls == IDCMP_GADGETDOWN && !slider) continue;
                if (id == G_SAVE || id == G_USE) { keep(id == G_SAVE); kept = 1; quit = 1; }
                else if (id == G_TEST) test();
                else if (id == G_CANCEL) { if (test_until) put_back(); quit = 1; }
                else if (LT_IS(id)) {
                    if (lt_hit(gg, code) >= 0) {     /* another tab: keep, as Use, and hand over (OpenPrefs: when changed) */
                        if (!damaged && LT_DIRTY) keep(0);
                        kept = 1;
                        lt_go(win, lt_hit(gg, code));
                        quit = 1;
                    }
                } else {
                    page_handle(id, code, gg);
                    if (cls != IDCMP_MOUSEMOVE) page_show();
                }
            }
        }
    }
out:
    if (timer) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (win && painting >= 0) ReportMouse(FALSE, win);       /* the window may go on to another page */
    painting = -1;
    if (rc != 99) page_close(kept);
    else if (ap == AP_POINTER) free_pens();
    if (win) { ClearMenuStrip(win); lt_close(win, glist); win = NULL; }
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

int appearance_main(int tab)
{
    const struct ap_page *p;
    int r;
    if (tab < LT_POINTER || tab > LT_ASL) return RETURN_FAIL;
    ap = tab - LT_POINTER;
    p = &pages[ap];
    test_until = 0; env_touched = 0; damaged = 0; had_file = 0; before_had = 0; colours_ready = 0;
    pf_init(&cur); pf_init(&start); pf_init(&before);
    if (!pf_open_lib()) return RETURN_FAIL;
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 38);
    r = pf_load(&cur, p->env);
    if (r == 0) r = pf_load(&cur, p->envarc);           /* nothing in ENV: what was last saved */
    had_file = r == 1;
    if (r < 0) damaged = 1;
    if (r != 1) pf_new(&cur);
    pf_copy(&start, &cur);
    page_open();
    advanced = read_view();
    strcpy(status_text, damaged ? "This file can't be read whole, so nothing in it is changed." : p->hint);
    if (ap == AP_POINTER && advanced && !damaged) strcpy(status_text, "Choose a pointer, click a colour, then its pixels.");
    while ((r = gui_once()) == 99) ;
    pf_free(&cur); pf_free(&start); pf_free(&before);
    if (AslBase) CloseLibrary(AslBase);
    AslBase = NULL;
    pf_close_lib();
    return r;
}
