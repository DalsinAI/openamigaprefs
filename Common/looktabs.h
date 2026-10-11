/* looktabs.h: Look, Windows, Title bar and Menus as one editor with tabs (the user, 10 October 2026:
 * "combine windows and look into a single, tabbed interface under OpenLook ... title bar and menus
 * should live under here too"; then "the tab option is weird and should be closer to MUI").
 *
 * The tabs are drawn as MUI's Register class draws them: each tab a raised shape on a line, as wide as its
 * name; the open page's tab stands taller, with no line under it, joined to a raised frame round
 * the page, so the tab and its page read as one. A click on another tab keeps what was changed (as Use
 * does), opens that editor where this window is, and closes this one. Each editor is still its own
 * program, so each still opens alone from the cog, the menu or Workbench.
 *
 * Each tab is a GadTools GENERIC_KIND gadget (no imagery: the tab is drawn here) with the ID LT_ID + n,
 * so a click comes as IDCMP_GADGETUP like any other (the open page's own tab is left out of each
 * editor's cases: a click on it does nothing). The place passes in ENV:OpenPrefs/LookTab
 * ("left top"), read once and deleted by the editor that opens.
 *
 * An editor calls lt_make() while it makes its gadgets, lt_draw() after GT_RefreshWindow() and between
 * GT_BeginRefresh() and GT_EndRefresh(), lt_place() before it opens its window and lt_go() for a tab.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef LOOKTABS_H
#define LOOKTABS_H

#include <exec/types.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <graphics/rastport.h>
#include <proto/dos.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>

/* One program, one window (LT_ONE, the user, 10 October 2026: "openlook shouldn't open new windows for the
 * tabs", "the overall window should remain the same size"): the four editors are built into SYS:Prefs/OpenLook
 * (Look/src/openlookprefs.c), each page's gadgets swapped into the one window, which only ever grows. Without
 * LT_ONE each editor is its own program and a tab hands over to the next, as before. */
#ifdef LT_ONE
extern struct Window *lt_win;              /* the shared window, open across pages */
extern int lt_next;                        /* the tab clicked: the page to open next, or -1 */
extern int lt_win_w, lt_win_h;             /* the inner size the window has reached */
#endif

/* Built into OpenBench (IN_OPENBENCH, its desktop/ob_builtin.h; the Team, 11 October 2026: the Open preferences in
 * OpenBench natively): a tab starts the next editor through OpenBench when that one is built in too. */
#ifdef IN_OPENBENCH
int ob_builtin_open(const char *path);     /* OpenBench's: 1 when path names a built-in (started, or sent Ctrl-F) */
#endif

enum { LT_LOOK, LT_WINDOWS, LT_TITLEBAR, LT_MENUS, LT_N };
#define LT_ID 900
#define LT_IS(id) ((id) >= LT_ID && (id) < LT_ID + LT_N)
#define LT_PLACE "ENV:OpenPrefs/LookTab"
#define LT_PAD 12                          /* each side of a tab's name */
#define LT_LIFT 2                          /* how much taller the open page's tab stands */
#define BP_UNUSED_LT __attribute__((unused))

static const char *const lt_label[LT_N] = { "Look", "Windows", "Title bar", "Menus" };
static const char *const lt_path[LT_N] = { "SYS:Prefs/Look", "SYS:Prefs/Windows", "SYS:Prefs/TitleBar", "SYS:Prefs/Menus" };

/* where the row is: x and y of its top left, its width, a tab's height, which tab is this editor's,
 * and each tab's left edge and width */
static int lt_x, lt_y, lt_w, lt_h, lt_me = -1, lt_tx[LT_N], lt_tw[LT_N];
static struct TextAttr *lt_ta;             /* the font the tabs were measured in: drawn in it too */

/* The tabs' click areas, x..x+w at y, lh high, from the left, each as wide as its name (screen font)
 * plus its padding. Returns the last gadget (NULL when one could not be made). */
static struct Gadget *lt_make(struct Gadget *g, struct NewGadget *ng, int x, int y, int w, int lh, int me)
{
    struct TextAttr *ta = (struct TextAttr *)ng->ng_TextAttr;       /* NewGadget's is const */
    struct TextFont *tf = ta ? OpenFont(ta) : NULL;
    struct RastPort rp;
    int i, at = x;
    InitRastPort(&rp);
    if (tf) SetFont(&rp, tf);
    lt_ta = ta;
    lt_x = x; lt_y = y; lt_w = w; lt_h = lh; lt_me = me;
    for (i = 0; i < LT_N; i++) {
        lt_tw[i] = TextLength(&rp, (STRPTR)lt_label[i], strlen(lt_label[i])) + 2 * LT_PAD;
        lt_tx[i] = at;
        at += lt_tw[i] - 1;                                          /* neighbours share an edge, as MUI's do */
    }
    if (tf) CloseFont(tf);
    for (i = 0; i < LT_N && g; i++) {
        ng->ng_LeftEdge = lt_tx[i];
        ng->ng_TopEdge = y - LT_LIFT;
        ng->ng_Width = lt_tw[i];
        ng->ng_Height = lh + LT_LIFT;
        ng->ng_GadgetText = NULL;
        ng->ng_Flags = 0;
        ng->ng_GadgetID = LT_ID + i;
        if ((g = CreateGadget(GENERIC_KIND, g, ng, TAG_DONE))) {
            g->Flags = GFLG_GADGHNONE;
            g->Activation = GACT_RELVERIFY;
            g->GadgetType |= GTYP_BOOLGADGET;
            g->GadgetRender = g->SelectRender = NULL;
        }
    }
    return g;
}

/* The tabs and the page's frame down to page_bottom (the window's inner bottom less 4 when -1). */
static void lt_draw(struct Window *win, int page_bottom)
{
    struct RastPort *rp;
    struct DrawInfo *dri;
    UWORD shine, shadow, text, fill;
    int i, base, left, right, bottom;
    UBYTE apen, drmd;
    struct TextFont *tf, *was;
    if (!win || lt_me < 0 || !(dri = GetScreenDrawInfo(win->WScreen))) return;
    rp = win->RPort;
    /* the font the tabs were measured in (the screen's): a window's RastPort starts in Topaz, wider, and the
     * names ran into each other (the user, 10 October 2026: "tabs are awful") */
    was = rp->Font;
    tf = lt_ta ? OpenFont(lt_ta) : NULL;
    if (tf) SetFont(rp, tf);
    shine = dri->dri_Pens[SHINEPEN]; shadow = dri->dri_Pens[SHADOWPEN]; text = dri->dri_Pens[TEXTPEN];
    fill = dri->dri_Pens[BACKGROUNDPEN];
    apen = rp->FgPen; drmd = rp->DrawMode;
    base = lt_y + lt_h;                                               /* the line the tabs stand on */
    left = lt_x - 4;                                                  /* the frame stands clear of the page's gadgets */
    right = lt_x + lt_w + 3;
    bottom = page_bottom > base + 4 ? page_bottom : win->Height - win->BorderBottom - 4;
    if (right > win->Width - win->BorderRight - 4) right = win->Width - win->BorderRight - 4;
    SetDrMd(rp, JAM1);
    /* the closed tabs first, then the open one over its neighbours' shared edges */
    for (i = 0; i <= LT_N; i++) {
        int t = i < LT_N ? i : lt_me, open;
        int x0, x1, y0, ty;
        if (i < LT_N && i == lt_me) continue;
        open = t == lt_me;
        x0 = lt_tx[t]; x1 = x0 + lt_tw[t] - 1; y0 = open ? lt_y - LT_LIFT : lt_y;
        if (open) {                                                    /* clear behind it, to the line */
            SetAPen(rp, fill);
            RectFill(rp, x0 + 1, y0 + 1, x1 - 1, base);
        }
        SetAPen(rp, shine);                                            /* left and top, the corners cut */
        Move(rp, x0, base - (open ? 0 : 1)); Draw(rp, x0, y0 + 2); Draw(rp, x0 + 2, y0); Draw(rp, x1 - 2, y0);
        SetAPen(rp, shadow);                                           /* right */
        Move(rp, x1 - 1, y0 + 1); Draw(rp, x1, y0 + 2); Draw(rp, x1, base - (open ? 0 : 1));
        SetAPen(rp, text);
        ty = y0 + (lt_h + (open ? LT_LIFT : 0) - rp->TxHeight) / 2 + rp->TxBaseline + (open ? 1 : 0);
        Move(rp, x0 + LT_PAD, ty);
        Text(rp, (STRPTR)lt_label[t], strlen(lt_label[t]));
    }
    /* the line under the tabs, but not under the open one; then the page's frame */
    SetAPen(rp, shine);
    Move(rp, left, base); Draw(rp, lt_tx[lt_me], base);
    Move(rp, lt_tx[lt_me] + lt_tw[lt_me] - 1, base); Draw(rp, right, base);
    Move(rp, left, base); Draw(rp, left, bottom);
    SetAPen(rp, shadow);
    Move(rp, left + 1, bottom); Draw(rp, right, bottom); Draw(rp, right, base + 1);
    SetAPen(rp, apen); SetDrMd(rp, drmd);
    if (tf) { SetFont(rp, was); CloseFont(tf); }
    FreeScreenDrawInfo(win->WScreen, dri);
}

/* Where the editor before put its window, if it handed over to this one: 1 and left, top set. */
static int lt_place(int *left, int *top)
{
    BPTR f = Open((STRPTR)LT_PLACE, MODE_OLDFILE);
    char line[32];
    int l, t, ok = 0;
    if (!f) return 0;
    if (FGets(f, (STRPTR)line, sizeof line - 1) && sscanf(line, "%d %d", &l, &t) == 2 && l >= 0 && t >= 0) {
        *left = l; *top = t; ok = 1;
    }
    Close(f);
    DeleteFile((STRPTR)LT_PLACE);
    return ok;
}

/* Hands over to the editor of tab `to`, at this window's place. The caller then closes its window. */
static void lt_go(struct Window *win, int to)
{
    BPTR f, in, out;
    char cmd[64];
    if (to < 0 || to >= LT_N) return;
#ifdef LT_ONE
    lt_next = to;                          /* the one program opens that page in this window */
    return;
#endif
    if (win) {
        BPTR dir = CreateDir((STRPTR)"ENV:OpenPrefs");
        if (dir) UnLock(dir);
        if ((f = Open((STRPTR)LT_PLACE, MODE_NEWFILE))) {
            char line[32];
            snprintf(line, sizeof line, "%d %d\n", (int)win->LeftEdge, (int)win->TopEdge);
            Write(f, line, strlen(line));
            Close(f);
        }
    }
#ifdef IN_OPENBENCH
    if (ob_builtin_open(lt_path[to])) return;   /* built in too: a process of OpenBench's, at this window's place */
#endif
    snprintf(cmd, sizeof cmd, "\"%s\"", lt_path[to]);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, SYS_UserShell, TRUE,
                                  NP_StackSize, 16384, TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
    }
}

/* The editors' window: opened the first time (at left, top, W x H inside), or the one already open with this
 * page's gadgets swapped in: the window keeps its place and size, growing only if a page needs more. */
static BP_UNUSED_LT struct Window *lt_open(struct Screen *scr, int left, int top, int W, int H, struct Gadget *glist,
                                           ULONG idcmp, const char *screen_title)
{
#ifdef LT_ONE
    if (lt_win) {
        struct IntuiMessage *m;
        int w = W > lt_win_w ? W : lt_win_w, h = H > lt_win_h ? H : lt_win_h;
        if (w != lt_win_w || h != lt_win_h) {
            ChangeWindowBox(lt_win, lt_win->LeftEdge, lt_win->TopEdge, w + lt_win->BorderLeft + lt_win->BorderRight,
                            h + lt_win->BorderTop + lt_win->BorderBottom);
            lt_win_w = w; lt_win_h = h;
        }
        while ((m = GT_GetIMsg(lt_win->UserPort))) GT_ReplyIMsg(m);     /* the page before's last messages */
        ModifyIDCMP(lt_win, idcmp);
        SetWindowTitles(lt_win, (STRPTR)~0, (STRPTR)screen_title);
        EraseRect(lt_win->RPort, lt_win->BorderLeft, lt_win->BorderTop, lt_win->Width - lt_win->BorderRight - 1,
                  lt_win->Height - lt_win->BorderBottom - 1);
        AddGList(lt_win, glist, ~0, -1, NULL);
        RefreshGList(glist, lt_win, NULL, -1);
        return lt_win;
    }
    lt_win_w = W; lt_win_h = H;
    lt_win = OpenWindowTags(NULL, WA_Title, (ULONG)"OpenLook", WA_ScreenTitle, (ULONG)screen_title, WA_PubScreen, (ULONG)scr,
                            WA_Left, left, WA_Top, top, WA_InnerWidth, W, WA_InnerHeight, H,
                            WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                            WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_IDCMP, idcmp, TAG_DONE);
    return lt_win;
#else
    return OpenWindowTags(NULL, WA_Title, (ULONG)"OpenLook", WA_ScreenTitle, (ULONG)screen_title, WA_PubScreen, (ULONG)scr,
                          WA_Left, left, WA_Top, top, WA_InnerWidth, W, WA_InnerHeight, H,
                          WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                          WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_IDCMP, idcmp, TAG_DONE);
#endif
}

/* The page's gadgets out of the window, which stays open for the next page (the caller frees the gadgets). */
static BP_UNUSED_LT void lt_detach(struct Window *win, struct Gadget *glist)
{
    struct IntuiMessage *m;
    if (!win) return;
#ifndef LT_ONE
    CloseWindow(win);                      /* its own program: the page opens a fresh window */
    return;
#endif
    if (glist) RemoveGList(win, glist, -1);
    while ((m = GT_GetIMsg(win->UserPort))) GT_ReplyIMsg(m);
}

/* Done with the window: closed, unless another tab was clicked (LT_ONE), when it stays for that page. */
static BP_UNUSED_LT void lt_close(struct Window *win, struct Gadget *glist)
{
    if (!win) return;
#ifdef LT_ONE
    if (lt_next >= 0) { lt_detach(win, glist); return; }
    lt_win = NULL;
#endif
    CloseWindow(win);
}

#endif /* LOOKTABS_H */
