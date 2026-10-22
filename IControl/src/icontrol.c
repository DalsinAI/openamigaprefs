/* OpenIControl: how Intuition handles screens, menus, requesters and text gadgets, as one section of OpenPrefs
 * (Dale, 21 October 2026: IControl folded into OpenPrefs). It takes over the OS's IControl editor (OpenUp's
 * RECONCILE puts that in SYS:Prefs/Classic).
 *
 *   OpenIControl [FROM file] [USE] [SAVE]
 *
 *   - the verify time-out, the keys held to drag a screen with the mouse, and the Amiga-key shortcuts
 *     (Workbench to front, front screen to back, a requester's OK and Cancel)
 *   - avoid flicker, preserve colours, mode promotion, menu snap, the text gadget filter, and windows dragged
 *     off the screen
 *
 * Save writes ENV:Sys/icontrol.prefs and ENVARC:; Use writes ENV: only; Cancel changes nothing. Every chunk
 * and byte this section doesn't edit is kept as the file had it (ic_prefs.c), OS 3.2's longer chunk too.
 * On the open ROM, intuition's prefs task reads the file but doesn't act on it yet; on OS 3.2.3, IPrefs does.
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
#define LT_DX 0
#define lt_note_to(f) ((void)0)
#endif

#include "pf_iff.h"
#include "ic_prefs.h"

static const char version[] __attribute__((used)) = "$VER: OpenIControl 0.1 (21.10.2026) OpenPrefs, MIT, Copyright (c) 2026 AmigaChrome Team";

#define IC_ENV "ENV:Sys/icontrol.prefs"
#define IC_ENVARC "ENVARC:Sys/icontrol.prefs"

static pf_file file;
static ic_prefs ic;

static void load_file(const char *from)
{
    int r = pf_load(&file, from ? from : IC_ENV);
    if (r == 0 && !from) r = pf_load(&file, IC_ENVARC);
    if (r != 1) pf_new(&file);
    ic_get(&file, &ic);
}

static int write_file(int save, void (*say)(const char *))
{
    if (!ic_put(&file, &ic)) { say("Out of memory."); return 0; }
    if (save && !pf_save(&file, IC_ENVARC)) { say("Couldn't write ENVARC:Sys/icontrol.prefs."); return 0; }
    if (!pf_save(&file, IC_ENV)) { say("Couldn't write ENV:Sys/icontrol.prefs."); return 0; }
    return 1;
}

static void say_shell(const char *s) { Printf((STRPTR)"OpenIControl: %s\n", (ULONG)s); }

/* ---- the window ------------------------------------------------------------------------------------ */

enum { G_TIMEOUT = 1, G_TIMETEXT, G_SHIFT, G_CTRL, G_ALT, G_AMIGA, G_KFRONT, G_KBACK, G_KOK, G_KCANCEL,
       G_FLICKER, G_COLOURS, G_PROMOTE, G_SNAP, G_FILTER, G_OFFSCREEN, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };

static const struct { int id; int qual; const char *label; } drags[4] = {
    { G_SHIFT, IC_Q_LSHIFT, "Shift" }, { G_CTRL, IC_Q_CONTROL, "Ctrl" }, { G_ALT, IC_Q_LALT, "Alt" },
    { G_AMIGA, IC_Q_LCOMMAND, "Amiga" },
};
static const struct { int id; ULONG flag; const char *label; } switches[6] = {
    { G_FLICKER, IC_COERCE_LACE, "Avoid flicker" }, { G_COLOURS, IC_COERCE_COLORS, "Preserve colours" },
    { G_PROMOTE, IC_MODEPROMOTE, "Mode promotion" }, { G_SNAP, IC_MENUSNAP, "Menu snap" },
    { G_FILTER, IC_STRGAD_FILTER, "Text gadget filter" }, { G_OFFSCREEN, IC_OFFSCRNWIN, "Windows off screen" },
};
static const struct { int id; const char *label; } keys[4] = {
    { G_KFRONT, "Workbench to front" }, { G_KBACK, "Screen to back" }, { G_KOK, "Requester OK" }, { G_KCANCEL, "Requester Cancel" },
};

static struct Window *win;
static struct Gadget *gad[G_COUNT];
static char status_text[80], time_text[16];

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

/* The time-out's slider is in tenths of a second, the file's in ticks (5 a tenth) */
static void time_for(int tenths)
{
    snprintf(time_text, sizeof time_text, "%d.%d seconds", tenths / 10, tenths % 10);
    if (win && gad[G_TIMETEXT]) GT_SetGadgetAttrs(gad[G_TIMETEXT], win, NULL, GTTX_Text, (ULONG)time_text, TAG_DONE);
}

/* What Use or Save does: the key gadgets into ic (the rest is kept as it changes), then the file. 1 done. */
static int put_in_place(int save)
{
    int i;
    for (i = 0; i < 4; i++) {
        STRPTR s = NULL;
        GT_GetGadgetAttrs(gad[keys[i].id], win, NULL, GTST_String, (ULONG)&s, TAG_DONE);
        if (s && s[0]) ic.keys[i] = (char)((s[0] >= 'a' && s[0] <= 'z') ? s[0] - 32 : s[0]);
    }
    return write_file(save, status);
}

/* "Screen drag" left of its four boxes, which have no label of their own on that side; x and the row as
 * the page laid them out (opshift.h moves the drawing beside OpenPrefs's list) */
static void drag_label(int x, int lh)
{
    struct RastPort *wrp = win->RPort;
    static const char drag[] = "Screen drag";
    SetAPen(wrp, 1);
    SetDrMd(wrp, JAM1);
    Move(wrp, x - 8 - TextLength(wrp, (STRPTR)drag, sizeof drag - 1),
         gad[G_SHIFT]->TopEdge + (lh - wrp->TxHeight) / 2 + wrp->TxBaseline);
    Text(wrp, (STRPTR)drag, sizeof drag - 1);
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
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, X2, lw, lw2, cbw = 26, sw, kw, tenths, i;
    char key[4][2];
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    rp = &scr->RastPort;

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    lw = TextLength(rp, (STRPTR)"Workbench to front", 18) + 16;   /* the left column's labels */
    X = 12 + lw;
    kw = TextLength(rp, (STRPTR)"MM", 2) + 16;                     /* a key */
    lw2 = TextLength(rp, (STRPTR)"Requester Cancel", 16) + 16;
    X2 = X + kw + 24 + lw2;                                        /* the right column's gadgets */
    sw = 4 * (cbw + 8 + TextLength(rp, (STRPTR)"Amiga", 5) + 12);  /* the screen drag's four boxes */
    W = X + sw + 12;
    if (W < X2 + kw + 12) W = X2 + kw + 12;
    i = X + cbw + 8 + TextLength(rp, (STRPTR)"Windows off screen", 18) + 24 + cbw + 8 +
        TextLength(rp, (STRPTR)"Text gadget filter", 18) + 12;
    if (W < i) W = i;
    tenths = (ic.timeout + 2) / 5;
    if (tenths < 1) tenths = 1;
    if (tenths > 50) tenths = 50;
    snprintf(status_text, sizeof status_text, "%s", "Change what you like, then Use or Save.");
    snprintf(time_text, sizeof time_text, "%d.%d seconds", tenths / 10, tenths % 10);

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#ifdef LT_ONE
    lt_make(NULL, NULL, 10, top, W - 24, lh, LT_ICONTROL);   /* the frame's sections (OpenPrefs) */
    top += lt_space(scr->Font, W - 24, lh);
#endif
    row = top;
    G(SLIDER_KIND, G_TIMEOUT, X, row, 120, lh, "Verify time-out", PLACETEXT_LEFT, GTSL_Min, 1, GTSL_Max, 50,
      GTSL_Level, tenths, GA_RelVerify, TRUE, GA_Immediate, TRUE);
    G(TEXT_KIND, G_TIMETEXT, X + 128, row, W - 12 - (X + 128), lh, NULL, 0, GTTX_Text, (ULONG)time_text);
    row += lh + 6;
    for (i = 0; i < 4; i++) {
        int x = X + i * (sw / 4);
        G(CHECKBOX_KIND, drags[i].id, x, row, cbw, lh, drags[i].label, PLACETEXT_RIGHT,
          GTCB_Checked, (ic.metadrag & drags[i].qual) != 0, GTCB_Scaled, TRUE);
    }
    row += lh + 6;
    for (i = 0; i < 4; i++) {
        key[i][0] = ic.keys[i] >= ' ' && ic.keys[i] < 127 ? ic.keys[i] : 0;
        key[i][1] = 0;
    }
    G(STRING_KIND, G_KFRONT, X, row, kw, lh, keys[0].label, PLACETEXT_LEFT, GTST_String, (ULONG)key[0], GTST_MaxChars, 1);
    G(STRING_KIND, G_KOK, X2, row, kw, lh, keys[2].label, PLACETEXT_LEFT, GTST_String, (ULONG)key[2], GTST_MaxChars, 1);
    row += lh + 4;
    G(STRING_KIND, G_KBACK, X, row, kw, lh, keys[1].label, PLACETEXT_LEFT, GTST_String, (ULONG)key[1], GTST_MaxChars, 1);
    G(STRING_KIND, G_KCANCEL, X2, row, kw, lh, keys[3].label, PLACETEXT_LEFT, GTST_String, (ULONG)key[3], GTST_MaxChars, 1);
    row += lh + 8;
    for (i = 0; i < 6; i++) {
        int x = i & 1 ? X + (W - 12 - X) / 2 : X;
        G(CHECKBOX_KIND, switches[i].id, x, row, cbw, lh, switches[i].label, PLACETEXT_RIGHT,
          GTCB_Checked, (ic.flags & switches[i].flag) != 0, GTCB_Scaled, TRUE);
        if (i & 1) row += lh + 4;
    }
    row += 6;
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
#define IDCMP_ALL (IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | CHECKBOXIDCMP | SLIDERIDCMP | \
                   STRINGIDCMP | TEXTIDCMP)
#ifdef LT_ONE
    win = lt_open(scr, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, scr->BarHeight + 20, W, row - scr->WBorTop - fh - 1,
                  glist, IDCMP_ALL, "OpenIControl 0.1");
#else
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"IControl", WA_ScreenTitle, (ULONG)"OpenIControl 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_IDCMP, IDCMP_ALL, TAG_DONE);
#endif
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    drag_label(X, lh);
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
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); LT_REFRESH(win); drag_label(X, lh); GT_EndRefresh(win, TRUE); }
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
            else if ((cls == IDCMP_MOUSEMOVE || cls == IDCMP_GADGETUP) && gg && gg->GadgetID == G_TIMEOUT) {
                ic.timeout = (WORD)code * 5;
                time_for((WORD)code);
            }
            else if (cls == IDCMP_GADGETUP) {
                int id = gg->GadgetID, on = (gg->Flags & GFLG_SELECTED) != 0;
                for (i = 0; i < 4; i++)
                    if (drags[i].id == id) ic.metadrag = on ? (ic.metadrag | drags[i].qual) : (ic.metadrag & ~drags[i].qual);
                for (i = 0; i < 6; i++)
                    if (switches[i].id == id) ic.flags = on ? (ic.flags | switches[i].flag) : (ic.flags & ~switches[i].flag);
                switch (id) {
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
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"OpenIControl"); return RETURN_FAIL; }
    if (!pf_open_lib()) { if (rd) FreeArgs(rd); return RETURN_FAIL; }
    pf_init(&file);
    load_file(args[0] ? (const char *)args[0] : NULL);
    if (args[1] || args[2]) rc = write_file(args[2] != 0, say_shell) ? RETURN_OK : RETURN_ERROR;
    else rc = gui();
    if (rd) FreeArgs(rd);
    pf_free(&file);
    pf_close_lib();
    return rc;
}
