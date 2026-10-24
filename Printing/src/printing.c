/* Printing: the paper settings printer.device and OpenPrint's driver go by, as one section of OpenPrefs (the user,
 * 24 October 2026: the legacy printing retired in favour of OpenPrint, and the paper settings on an OpenPrefs
 * "Printing" page). The OS's Printer and PrinterGfx editors are archived by openamigaup's OpenPrinter part.
 *
 *   - Paper size (printer.prefs, pt_PaperSize): A4 first, then the other sizes the file can hold. OpenPrint's driver
 *     makes A3, A4, A5, US Letter and US Legal pages; any other size it prints on A4, and the status line says so.
 *   - Lines a page (pt_PaperLength), and the left and right margins in columns (pt_LeftMargin, pt_RightMargin).
 *   - Pictures (printergfx.prefs, pg_Aspect): horizontal, across the paper (OpenPrint turns the page to landscape),
 *     or vertical, turned onto an upright page.
 *   - Density, 1 to 7 (pg_PrintDensity): OpenPrint's driver prints pictures at 72 to 1200 dots to the inch.
 *   - Which driver and port printer.prefs names: shown, not changed here.
 *
 *   Printing [PAPER A4|LETTER|LEGAL|A3|A5|...] [LINES n] [LEFT n] [RIGHT n] [PICTURES HORIZONTAL|VERTICAL]
 *            [DENSITY n] [USE] [SAVE]
 * (OpenPrefs PRINTING ...): the settings given over the ones in ENV:; with USE or SAVE written without a window.
 *
 * Save writes ENV:Sys/printer.prefs and printergfx.prefs and ENVARC:'s, Use ENV:'s only, Cancel nothing. A file is
 * written only where its bytes change, and one that isn't there is made only when a setting in it changes: with
 * OpenPrint's driver in DEVS:Printers as openamigaup's OpenPrinter part makes it (OpenPrint's driver on
 * oapspool.device), else as printer.device takes no file (the generic driver on the parallel port). Every chunk
 * and byte this section doesn't edit is kept (pr_prefs.c): the driver, the port, PUNT and PDEV, the pitch, the
 * spacing, the quality and the shading. A file that isn't one this can keep whole is never written. printer.device
 * reads the files when a program opens it. Unit 0's files only. Nothing here prints: there is no test page.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

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

#include "pf_iff.h"
#include "pr_prefs.h"

static const char version[] __attribute__((used)) = "$VER: PrintingPage 0.1 (24.10.2026) OpenPrefs, MIT, Copyright (c) 2026 AmigaChrome Team";

#define TXT_ENV "ENV:Sys/printer.prefs"
#define TXT_ENVARC "ENVARC:Sys/printer.prefs"
#define GFX_ENV "ENV:Sys/printergfx.prefs"
#define GFX_ENVARC "ENVARC:Sys/printergfx.prefs"
#define OPENPRINT_DRIVER "DEVS:Printers/OpenPrint"

static pf_file txt, gfx, txt_was, gfx_was;   /* the files, and as they were read (or made) */
static int had_txt, had_gfx;                 /* read from a file */
static int bad_txt, bad_gfx;                 /* a file this can't keep whole: never written */
static pr_prefs pr;

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

/* One file from ENV:, else ENVARC:; with none, made new. *had: read; *bad: there, but not a file this keeps whole */
static void load_one(pf_file *f, const char *env, const char *envarc, int *had, int *bad, int gfxfile, int openprint)
{
    int r = pf_load(f, env);
    if (r == 0) r = pf_load(f, envarc);
    *had = r == 1;
    *bad = r < 0;
    if (r != 1 && !(gfxfile ? pr_new_gfx(f, openprint) : pr_new_txt(f, openprint))) pf_new(f);
}

static void load_files(void)
{
    int openprint = exists(OPENPRINT_DRIVER);
    load_one(&txt, TXT_ENV, TXT_ENVARC, &had_txt, &bad_txt, 0, openprint);
    load_one(&gfx, GFX_ENV, GFX_ENVARC, &had_gfx, &bad_gfx, 1, openprint);
    pf_copy(&txt_was, &txt);
    pf_copy(&gfx_was, &gfx);
    pr_get(&txt, &gfx, &pr);
}

/* One file into ENV: (and ENVARC: when save), where its bytes differ; none made when there was none and nothing in
 * it changed. 1 done. */
static int put_one(const pf_file *f, const pf_file *was, int had, int bad, const char *env, const char *envarc, int save,
                   void (*say)(const char *))
{
    static char line[96];
    if (!had && pf_same(f, was)) return 1;     /* none there (or one this can't keep), and nothing in it changed */
    if (bad) {
        snprintf(line, sizeof line, "%s can't be kept whole: not written.", env + 8);
        say(line);
        return 0;
    }
    if (save && pf_differs(f, envarc) && !pf_save(f, envarc)) {
        snprintf(line, sizeof line, "Couldn't write %s.", envarc);
        say(line);
        return 0;
    }
    if (pf_differs(f, env) && !pf_save(f, env)) {
        snprintf(line, sizeof line, "Couldn't write %s.", env);
        say(line);
        return 0;
    }
    return 1;
}

static int write_files(int save, void (*say)(const char *))
{
    if (!pr_put(&txt, &gfx, &pr)) { say("Out of memory."); return 0; }
    return put_one(&txt, &txt_was, had_txt, bad_txt, TXT_ENV, TXT_ENVARC, save, say) &&
           put_one(&gfx, &gfx_was, had_gfx, bad_gfx, GFX_ENV, GFX_ENVARC, save, say);
}

static void say_shell(const char *s) { Printf((STRPTR)"Printing: %s\n", (ULONG)s); }

/* ---- the window ------------------------------------------------------------------------------------ */

enum { G_ROUTE = 1, G_PAPER, G_LINES, G_LEFT, G_RIGHT, G_COLUMNS, G_ASPECT, G_DENSLABEL, G_DENSITY, G_DENSTEXT, G_STATUS,
       G_SAVE, G_USE, G_CANCEL, G_COUNT };

static const char *paper_labels[PR_PAPERS + 1];
static const char *const aspect_labels[] = { "Horizontal (landscape page)", "Vertical (upright page)", NULL };

static struct Window *win;
static struct Gadget *gad[G_COUNT];
static char status_text[96], dens_text[40], route_text[96];

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    status_text[sizeof status_text - 1] = 0;
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

static void density_for(int d)
{
    snprintf(dens_text, sizeof dens_text, "%d: %d dots to the inch", d, pr_density_dpi(d));
    if (win && gad[G_DENSTEXT]) GT_SetGadgetAttrs(gad[G_DENSTEXT], win, NULL, GTTX_Text, (ULONG)dens_text, TAG_DONE);
}

/* The status line's first words: a paper size OpenPrint prints on A4, or what to do */
static void paper_note(void)
{
    if (!pr_paper_openprint(pr.paper)) status("OpenPrint's driver prints this paper size on A4.");
    else status("Change what you like, then Use or Save.");
}

static int paper_index(int paper)
{
    int i;
    for (i = 0; i < PR_PAPERS; i++)
        if (pr_paper_order[i] == paper) return i;
    return 0;
}

static LONG number_of(int id)
{
    LONG v = 0;
    GT_GetGadgetAttrs(gad[id], win, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
    return v;
}

/* The three numbers from their gadgets, put in range, and shown as they are now */
static void read_numbers(void)
{
    pr.lines = (int)number_of(G_LINES);
    pr.left = (int)number_of(G_LEFT);
    pr.right = (int)number_of(G_RIGHT);
    pr_fix(&pr);
    GT_SetGadgetAttrs(gad[G_LINES], win, NULL, GTIN_Number, pr.lines, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_LEFT], win, NULL, GTIN_Number, pr.left, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_RIGHT], win, NULL, GTIN_Number, pr.right, TAG_DONE);
}

/* What Use or Save does: the numbers from their gadgets, then the files. 1 done. */
static int put_in_place(int save)
{
    read_numbers();
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
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, lw, cw, nw, tw, i;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    rp = &scr->RastPort;
    for (i = 0; i < PR_PAPERS; i++) paper_labels[i] = pr_paper_name(pr_paper_order[i]);
    paper_labels[PR_PAPERS] = NULL;
    pr_route(&txt, route_text, sizeof route_text);

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    lw = TextLength(rp, (STRPTR)"Lines a page", 12) + 16;               /* the labels' column */
    X = 12 + lw;
    cw = TextLength(rp, (STRPTR)"Horizontal (landscape page)", 27) + 48; /* the widest cycle */
    nw = TextLength(rp, (STRPTR)"0000", 4) + 16;                         /* a number */
    tw = TextLength(rp, (STRPTR)"to", 2) + 16;
    W = X + cw + 12;
    i = X + TextLength(rp, (STRPTR)route_text, strlen(route_text)) + 24;
    if (W < i) W = i;
    i = 12 + TextLength(rp, (STRPTR)"OpenPrint's driver prints this paper size on A4.", 48) + 24;
    if (W < i) W = i;
    i = X + 128 + TextLength(rp, (STRPTR)"7: 1200 dots to the inch", 24) + 12;
    if (W < i) W = i;
    density_for(pr.density);
    if (bad_txt || bad_gfx) snprintf(status_text, sizeof status_text, "%s isn't a prefs file this can keep whole.",
                                     bad_txt ? "printer.prefs" : "printergfx.prefs");
    else if (!pr_paper_openprint(pr.paper)) snprintf(status_text, sizeof status_text, "%s", "OpenPrint's driver prints this paper size on A4.");
    else snprintf(status_text, sizeof status_text, "%s", "Change what you like, then Use or Save.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#ifdef LT_ONE
    lt_make(NULL, NULL, 10, top, W - 24, lh, LT_PRINTING);   /* the frame's sections (OpenPrefs) */
    top += lt_space(scr->Font, W - 24, lh);
#endif
    row = top;
    G(TEXT_KIND, G_ROUTE, X, row, W - 12 - X, lh, "Printer", PLACETEXT_LEFT, GTTX_Text, (ULONG)route_text, GTTX_Border, TRUE);
    row += lh + 10;
    G(CYCLE_KIND, G_PAPER, X, row, cw, lh, "Paper size", PLACETEXT_LEFT, GTCY_Labels, (ULONG)paper_labels,
      GTCY_Active, paper_index(pr.paper));
    row += lh + 4;
    G(INTEGER_KIND, G_LINES, X, row, nw, lh, "Lines a page", PLACETEXT_LEFT, GTIN_Number, pr.lines, GTIN_MaxChars, 3);
    row += lh + 4;
    G(INTEGER_KIND, G_LEFT, X, row, nw, lh, "Margins", PLACETEXT_LEFT, GTIN_Number, pr.left, GTIN_MaxChars, 3);
    G(INTEGER_KIND, G_RIGHT, X + nw + tw, row, nw, lh, "to", PLACETEXT_LEFT, GTIN_Number, pr.right, GTIN_MaxChars, 3);
    G(TEXT_KIND, G_COLUMNS, X + 2 * nw + tw + 8, row, W - 12 - (X + 2 * nw + tw + 8), lh, NULL, 0,
      GTTX_Text, (ULONG)"columns");
    row += lh + 10;
    G(CYCLE_KIND, G_ASPECT, X, row, cw, lh, "Pictures", PLACETEXT_LEFT, GTCY_Labels, (ULONG)aspect_labels,
      GTCY_Active, pr.aspect == PR_VERTICAL ? 1 : 0);
    row += lh + 4;
    /* a slider's own label isn't drawn in OpenPrefs's window (Icons' Label gap): a text of its own */
    G(TEXT_KIND, G_DENSLABEL, 12, row, X - 12 - 8, lh, NULL, 0, GTTX_Text, (ULONG)"Density", GTTX_Justification, GTJ_RIGHT);
    G(SLIDER_KIND, G_DENSITY, X, row, 120, lh, NULL, 0, GTSL_Min, 1, GTSL_Max, PR_DENSITY_MAX, GTSL_Level, pr.density,
      GA_RelVerify, TRUE, GA_Immediate, TRUE);
    G(TEXT_KIND, G_DENSTEXT, X + 128, row, W - 12 - (X + 128), lh, NULL, 0, GTTX_Text, (ULONG)dens_text);
    row += lh + 10;
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
#define IDCMP_ALL (IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | CYCLEIDCMP | INTEGERIDCMP | \
                   SLIDERIDCMP | TEXTIDCMP)
#ifdef LT_ONE
    win = lt_open(scr, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, scr->BarHeight + 20, W, row - scr->WBorTop - fh - 1,
                  glist, IDCMP_ALL, "Printing 0.1");
#else
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Printing", WA_ScreenTitle, (ULONG)"Printing 0.1", WA_PubScreen, (ULONG)scr,
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
            else if ((cls == IDCMP_MOUSEMOVE || cls == IDCMP_GADGETUP) && gg && gg->GadgetID == G_DENSITY) {
                pr.density = (int)(WORD)code;
                density_for(pr.density);
            }
            else if (cls == IDCMP_GADGETUP && gg) {
                switch (gg->GadgetID) {
#ifdef LT_ONE
                default: {                          /* another section: the page's changes kept, as Use, when it has any */
                    int to = lt_hit(gg, code);
                    if (to >= 0) { if (LT_DIRTY) put_in_place(0); lt_go(win, to); quit = 1; }
                    break;
                }
#endif
                case G_PAPER: pr.paper = pr_paper_order[code < PR_PAPERS ? code : 0]; paper_note(); break;
                case G_LINES: case G_LEFT: case G_RIGHT: read_numbers(); break;
                case G_ASPECT: pr.aspect = code == 1 ? PR_VERTICAL : PR_HORIZONTAL; break;
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
    LONG args[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"PAPER/K,LINES/K/N,LEFT/K/N,RIGHT/K/N,PICTURES/K,DENSITY/K/N,USE/S,SAVE/S",
                                            args, NULL) : NULL;
    int rc, k;
    (void)argv;
    (void)version;
    win = NULL;                             /* built into OpenPrefs, the statics outlive a run */
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Printing"); return RETURN_FAIL; }
    if (!pf_open_lib()) { if (rd) FreeArgs(rd); return RETURN_FAIL; }
    pf_init(&txt);
    pf_init(&gfx);
    pf_init(&txt_was);
    pf_init(&gfx_was);
    load_files();
    if (args[0]) {
        if ((k = pr_paper_named((const char *)args[0])) >= 0) pr.paper = k;
        else say_shell("PAPER: A4, LETTER, LEGAL, A3, A5, A0 to A8, NARROW, WIDE or CUSTOM.");
    }
    if (args[1]) pr.lines = (int)*(LONG *)args[1];
    if (args[2]) pr.left = (int)*(LONG *)args[2];
    if (args[3]) pr.right = (int)*(LONG *)args[3];
    if (args[4]) {
        if ((k = pr_aspect_named((const char *)args[4])) >= 0) pr.aspect = k;
        else say_shell("PICTURES: HORIZONTAL or VERTICAL.");
    }
    if (args[5]) pr.density = (int)*(LONG *)args[5];
    pr_fix(&pr);
    if (args[6] || args[7]) rc = write_files(args[7] != 0, say_shell) ? RETURN_OK : RETURN_ERROR;
    else rc = gui();
    if (rd) FreeArgs(rd);
    pf_free(&txt);
    pf_free(&gfx);
    pf_free(&txt_was);
    pf_free(&gfx_was);
    pf_close_lib();
    return rc;
}
