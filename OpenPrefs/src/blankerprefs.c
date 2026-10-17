/* Blanker: OpenBlanker's one setting as a page of OpenPrefs (Blanker/DESIGN.md, Settings): how many seconds
 * without a key or the mouse before the show. ENV:OpenPrefs/Blanker holds the number and a newline, as
 * OpenUp writes it ("300\n"); Save writes ENVARC: too; either sends Ctrl-F to the running OpenBlanker (its
 * port "OpenBlanker"), which reads the file again. A file is written only when its bytes change. "Show it
 * now" runs OpenBlanker NOW.
 *
 *   Blanker [SECONDS=n] [USE] [SAVE]          (from OpenPrefs: OpenPrefs BLANKER SECONDS=600 SAVE)
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "looktabs.h"

#define BL_ENV "ENV:OpenPrefs/Blanker"
#define BL_ENVARC "ENVARC:OpenPrefs/Blanker"
#define BL_TOOL "SYS:Tools/Commodities/OpenBlanker"
#define BL_DEFAULT 300

enum { G_SECS = 1, G_NOW, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };

static struct Window *win;
static struct Gadget *gad[G_COUNT];
static char status_text[100];
static LONG secs;

static LONG read_secs(const char *path)
{
    char buf[16];
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    LONG n = 0, got;
    if (!f) return 0;
    got = Read(f, buf, sizeof buf - 1);
    Close(f);
    if (got > 0) { buf[got] = 0; n = atol(buf); }
    return n > 0 ? n : 0;
}

/* the file's bytes, written only when they change */
static int write_secs(const char *path, LONG n)
{
    char want[16], have[16];
    BPTR f;
    LONG len = snprintf(want, sizeof want, "%ld\n", (long)n), got = 0;
    if ((f = Open((STRPTR)path, MODE_OLDFILE))) { got = Read(f, have, sizeof have); Close(f); }
    if (got == len && !memcmp(want, have, len)) return 1;
    if (!(f = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    got = Write(f, want, len);
    Close(f);
    return got == len;
}

static void tell(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)"OpenBlanker")) && p->mp_SigTask) Signal((struct Task *)p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
}

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

static LONG clamp(LONG n) { return n < 10 ? 10 : n > 86400 ? 86400 : n; }

static int put_in_place(int save)
{
    LONG n = secs;
    if (gad[G_SECS] && win) GT_GetGadgetAttrs(gad[G_SECS], win, NULL, GTIN_Number, (ULONG)&n, TAG_DONE);
    secs = clamp(n);
    if (save && !write_secs(BL_ENVARC, secs)) { status("Couldn't write ENVARC:OpenPrefs/Blanker."); return 0; }
    if (!write_secs(BL_ENV, secs)) { status("Couldn't write ENV:OpenPrefs/Blanker."); return 0; }
    tell();
    return 1;
}

static void show_now(void)
{
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)"Run >NIL: " BL_TOOL " NOW", SYS_Input, in, SYS_Output, out, TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
        status("OpenBlanker isn't installed (SYS:Tools/Commodities/OpenBlanker).");
        return;
    }
    Close(in); Close(out);
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
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W = 420, X, i;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    X = 12 + TextLength(&scr->RastPort, (STRPTR)"Show after", 10) + 8;
    snprintf(status_text, sizeof status_text, "%s", FindPort((STRPTR)"OpenBlanker") ? "OpenBlanker is running."
                                                     : "OpenBlanker isn't running: it starts at the next boot.");
    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#ifdef LT_ONE
    g = lt_make(g, &ng, 10, top, W - 20, lh, LT_BLANKER);
    top += lt_space(scr->Font, W - 20, lh) + 8;
#endif
    row = top;
    G(INTEGER_KIND, G_SECS, X, row, TextLength(&scr->RastPort, (STRPTR)"000000", 6) + 16, lh, "Show after", PLACETEXT_LEFT,
      GTIN_Number, secs, GTIN_MaxChars, 5);
    G(TEXT_KIND, 0, X + TextLength(&scr->RastPort, (STRPTR)"000000", 6) + 24, row, 200, lh, NULL, 0,
      GTTX_Text, (ULONG)"seconds with no key or mouse");
    row += lh + 8;
    G(BUTTON_KIND, G_NOW, X, row, TextLength(&scr->RastPort, (STRPTR)"Show it now", 11) + 24, lh, "Show it now", 0, TAG_DONE);
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
#undef G
    if (!g) { rc = RETURN_FAIL; goto out; }
#ifdef LT_ONE
    win = lt_open(scr, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, scr->BarHeight + 20, W, row - scr->WBorTop - fh - 1,
                  glist, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | INTEGERIDCMP, "OpenPrefs Blanker");
#else
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Blanker", WA_ScreenTitle, (ULONG)"OpenPrefs Blanker", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | INTEGERIDCMP, TAG_DONE);
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
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); lt_draw(win, -1); GT_EndRefresh(win, TRUE); }
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
            } else if (cls == IDCMP_GADGETUP) {
                int to;
                switch (gg->GadgetID) {
                case G_SECS: break;
                case G_NOW: show_now(); break;
                case G_SAVE: if (put_in_place(1)) quit = 1; break;
                case G_USE: if (put_in_place(0)) quit = 1; break;
                case G_CANCEL: quit = 1; break;
                default:
                    if ((to = lt_hit(gg, code)) >= 0) { if (LT_DIRTY) put_in_place(0); lt_go(win, to); quit = 1; }
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
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"SECONDS/K/N,USE/S,SAVE/S", args, NULL) : NULL;
    int rc = RETURN_OK;
    (void)argv;
    win = NULL;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Blanker"); return RETURN_FAIL; }
    if (!(secs = read_secs(BL_ENV)) && !(secs = read_secs(BL_ENVARC))) secs = BL_DEFAULT;
    if (args[0]) secs = clamp(*(LONG *)args[0]);
    if (args[1] || args[2]) {
        if ((args[2] && !write_secs(BL_ENVARC, secs)) || !write_secs(BL_ENV, secs)) {
            PutStr((STRPTR)"Blanker: the setting couldn't be written.\n");
            rc = RETURN_ERROR;
        } else tell();
    } else rc = gui();
    if (rd) FreeArgs(rd);
    return rc;
}
