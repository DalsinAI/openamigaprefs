/* opframe.c: OpenPrefs's frame, the one window every page goes into (Common/looktabs.h with LT_OP).
 *
 * The sections stand down the left, one row each, the open one filled, in four groups: the look (Theme,
 * Pointer, Fonts, Colours, Backdrops, Workbench, File requesters), the desktop (Windows, Title bar, Menus,
 * Dock), sound and input (Sound, Input, Gamepads) and the rest (Date and time, Serial, File types, System,
 * Blanker). Each page stands lt_dx to the right of them, as it was laid out alone (opshift.h moves it), with
 * its own Save, Use, Test and Cancel along its foot. On a screen too narrow for the list beside the widest
 * page (under LT_LIST_MIN wide: 640 x 256 PAL), the sections are a cycle gadget above the page, where the
 * tabs were, and nothing moves.
 *
 * A click on another section keeps the page's changes as Use does, when it has any (lt_dirty: the page's own
 * gadgets or keys were used), and the next page's gadgets go into the same window, which keeps its place and
 * only grows (as OpenLook's tabs did). Save, Use and Cancel close the window, as each editor's did.
 *
 * Also here: the arguments OpenPrefs hands a page (lt_args), PUBSCREEN, and the folders a save needs (Dale,
 * 17 October 2026, 09:42): ENV: and its folders are made without asking (RAM:Env and the assign when ENV:
 * is missing); ENVARC:'s folders inside an existing Env-Archive too; when SYS:Prefs/Env-Archive (or
 * SYS:Prefs) is missing, a Save from the window asks once, "The preferences folders are missing on SYS:.
 * Create them?", and Create makes them (and ENVARC: when there is no assign) while Cancel saves as Use does
 * (ENV: only) and says so in the page's status line; from the Shell they are made without asking, and one
 * line says what was made.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <string.h>
#include <stdio.h>

#define LT_OP 1
#include "looktabs.h"

struct Window *lt_win;
int lt_next = -1;
int lt_win_w, lt_win_h;
int lt_dx;
struct RastPort *lt_rp;
int lt_dirty;
const char *lt_args;
const char *lt_pubname;
const char *lt_prog = "OpenPrefs";         /* the name it runs as, for the Shell's lines */

static const char *const lt_label[LT_N] = { "Theme", "Windows", "Title bar", "Menus", "Pointer", "Fonts", "Colours",
                                            "Backdrops", "Workbench", "File requesters", "Dock", "Sound", "Input",
                                            "Gamepads", "Date and time", "Serial", "File types", "System", "Blanker" };
/* the order down the list, and the row after which a group ends */
static const UBYTE lt_order[LT_N] = { LT_LOOK, LT_POINTER, LT_FONTS, LT_COLOURS, LT_BACKDROPS, LT_WORKBENCH, LT_ASL,
                                      LT_WINDOWS, LT_TITLEBAR, LT_MENUS, LT_DOCK,
                                      LT_SOUND, LT_INPUT, LT_GAMEPADS,
                                      LT_DATETIME, LT_SERIAL, LT_TYPES, LT_SYSTEM, LT_BLANKER };
static const UBYTE lt_group_end[LT_N] = { 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0 };
#define LT_GAP 4                           /* between the groups */
#define LT_LX 8                            /* the list's left edge in the window */
#define LT_LIST_MIN 760                    /* a screen at least this wide has the list beside the page */

static int lt_me = -1, lt_set, lt_list, lt_lw, lt_rowh, lt_ly, lt_lh, lt_cy, lt_ch, lt_cx, lt_cw, lt_fh;
static int lt_ry[LT_N];                    /* each section's row top, by section */
static struct TextAttr lt_ta;
static char lt_font[64];
static struct Gadget *lt_fg, *lt_cyc;      /* the frame's own gadgets */
static APTR lt_vi;
static char lt_note[160];
static int lt_refused, lt_refused_now, lt_made_tree;
static char lt_made[240];
static struct RDArgs *lt_myargs;

static int lt_pos(int sec) { int i; for (i = 0; i < LT_N; i++) if (lt_order[i] == sec) return i; return 0; }

/* The screen's font and the list's size, once: the list beside the page or the cycle gadget above it */
static void lt_setup(struct Screen *scr)
{
    struct RastPort rp;
    struct TextFont *tf;
    int i, w, y;
    if (lt_set || !scr) return;
    lt_set = 1;
    strncpy(lt_font, (const char *)scr->Font->ta_Name, sizeof lt_font - 1);
    lt_ta = *scr->Font;
    lt_ta.ta_Name = (STRPTR)lt_font;
    lt_fh = scr->Font->ta_YSize;
    InitRastPort(&rp);
    if ((tf = OpenFont(&lt_ta))) SetFont(&rp, tf);
    for (lt_lw = 0, i = 0; i < LT_N; i++)
        if ((w = TextLength(&rp, (STRPTR)lt_label[i], strlen(lt_label[i]))) > lt_lw) lt_lw = w;
    lt_cw = lt_lw + 40;                    /* the cycle gadget: its glyph and the longest name */
    lt_cx = 10 + TextLength(&rp, (STRPTR)"Section", 7) + 8;
    if (tf) CloseFont(tf);
    lt_lw += 2 * 10;
    lt_rowh = lt_fh + 5;
    lt_list = scr->Width >= LT_LIST_MIN;
    lt_dx = lt_list ? LT_LX + lt_lw + 12 - 10 : 0;   /* a page's left margin was 10: the page 12 right of the list */
    lt_ly = scr->WBorTop + lt_fh + 1 + 8;
    for (y = lt_ly, i = 0; i < LT_N; i++) {
        lt_ry[lt_order[i]] = y;
        y += lt_rowh + (lt_group_end[i] ? LT_GAP : 0);
    }
    lt_lh = y - lt_ly;
    lt_cy = scr->WBorTop + lt_fh + 1 + 4;
    lt_ch = lt_fh + 6;
}

static void lt_setup_default(void)
{
    struct Screen *s = NULL;
    if (lt_set) return;
    if (lt_pubname) s = LockPubScreen((CONST_STRPTR)lt_pubname);
    if (!s) s = LockPubScreen(NULL);
    if (s) { lt_setup(s); UnlockPubScreen(NULL, s); }
}

struct Gadget *lt_make(struct Gadget *g, struct NewGadget *ng, int x, int y, int w, int lh, int me)
{
    (void)ng; (void)x; (void)y; (void)w; (void)lh;
    lt_setup_default();
    lt_me = me;
    return g;
}

/* What a page leaves above its first gadget: the cycle gadget's row, or nothing beside the list */
int lt_space(struct TextAttr *ta, int w, int lh)
{
    (void)ta; (void)w; (void)lh;
    lt_setup_default();
    return lt_list ? 0 : lt_ch + 4;
}

int lt_place(int *left, int *top) { (void)left; (void)top; return 0; }

void lt_go(struct Window *win, int to) { (void)win; if (to >= 0 && to < LT_N) lt_next = to; }

int lt_hit(struct Gadget *g, UWORD code)
{
    int to = -1;
    if (!g) return -1;
    if (g->GadgetID == LT_CYCLE) to = code < LT_N ? lt_order[code] : -1;
    else if (g->GadgetID >= LT_ID && g->GadgetID < LT_ID + LT_N) to = g->GadgetID - LT_ID;
    return to == lt_me ? -1 : to;
}

void lt_note_to(void (*status)(const char *))
{
    if (lt_note[0] && status) status(lt_note);
    lt_note[0] = 0;
}

const char *lt_note_take(void)
{
    static char n[sizeof lt_note];
    if (!lt_note[0]) return NULL;
    strcpy(n, lt_note);
    lt_note[0] = 0;
    return n;
}

/* The list: each section's row, the open one filled, in a recessed box */
void lt_draw(struct Window *win, int page_bottom)
{
    struct RastPort *rp;
    struct DrawInfo *dri;
    struct TextFont *tf, *was;
    UBYTE apen, drmd;
    int i;
    (void)page_bottom;
    if (!win || !lt_list || !(dri = GetScreenDrawInfo(win->WScreen))) return;
    rp = win->RPort;
    apen = rp->FgPen; drmd = rp->DrawMode;
    was = rp->Font;
    if ((tf = OpenFont(&lt_ta))) SetFont(rp, tf);
    SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
    RectFill(rp, LT_LX, lt_ly, LT_LX + lt_lw - 1, lt_ly + lt_lh - 1);
    if (lt_vi) DrawBevelBox(rp, LT_LX - 2, lt_ly - 2, lt_lw + 4, lt_lh + 4, GTBB_Recessed, TRUE, GT_VisualInfo, (ULONG)lt_vi, TAG_DONE);
    SetDrMd(rp, JAM1);
    for (i = 0; i < LT_N; i++) {
        int y = lt_ry[i], open = i == lt_me;
        if (open) { SetAPen(rp, dri->dri_Pens[FILLPEN]); RectFill(rp, LT_LX, y, LT_LX + lt_lw - 1, y + lt_rowh - 1); }
        SetAPen(rp, dri->dri_Pens[open ? FILLTEXTPEN : TEXTPEN]);
        Move(rp, LT_LX + 10, y + (lt_rowh - rp->TxHeight) / 2 + rp->TxBaseline);
        Text(rp, (STRPTR)lt_label[i], strlen(lt_label[i]));
    }
    SetAPen(rp, apen); SetDrMd(rp, drmd);
    if (tf) { SetFont(rp, was); CloseFont(tf); }
    FreeScreenDrawInfo(win->WScreen, dri);
}

/* The frame's own gadgets: a click area for each row of the list, or the cycle gadget */
static int lt_frame_gadgets(struct Screen *scr)
{
    struct NewGadget ng;
    struct Gadget *g;
    int i;
    if (!(lt_vi = GetVisualInfoA(scr, NULL))) return 0;
    g = CreateContext(&lt_fg);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &lt_ta;
    ng.ng_VisualInfo = lt_vi;
    if (lt_list) {
        for (i = 0; i < LT_N && g; i++) {
            ng.ng_LeftEdge = LT_LX; ng.ng_TopEdge = lt_ry[i]; ng.ng_Width = lt_lw; ng.ng_Height = lt_rowh;
            ng.ng_GadgetID = LT_ID + i;
            if ((g = CreateGadget(GENERIC_KIND, g, &ng, TAG_DONE))) {
                g->Flags = GFLG_GADGHNONE;
                g->Activation = GACT_RELVERIFY;
                g->GadgetType |= GTYP_BOOLGADGET;
                g->GadgetRender = g->SelectRender = NULL;
            }
        }
    } else {
        static STRPTR labels[LT_N + 1];
        for (i = 0; i < LT_N; i++) labels[i] = (STRPTR)lt_label[lt_order[i]];
        labels[LT_N] = NULL;
        ng.ng_LeftEdge = lt_cx; ng.ng_TopEdge = lt_cy; ng.ng_Width = lt_cw; ng.ng_Height = lt_ch;
        ng.ng_GadgetText = (STRPTR)"Section"; ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = LT_CYCLE;
        g = lt_cyc = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)labels, GTCY_Active, lt_pos(lt_me), TAG_DONE);
    }
    return g != NULL;
}

static void lt_free_frame(void)
{
    if (lt_fg) { FreeGadgets(lt_fg); lt_fg = NULL; }
    lt_cyc = NULL;
    if (lt_vi) { FreeVisualInfo(lt_vi); lt_vi = NULL; }
}

struct Window *lt_open(struct Screen *scr, int left, int top, int W, int H, struct Gadget *glist, ULONG idcmp,
                       const char *screen_title)
{
    int w, h;
    lt_setup(scr);
    w = W + lt_dx;
    h = H;
    if (lt_list && h < lt_lh + 8 + (lt_ly - scr->WBorTop - lt_fh - 1)) h = lt_lh + 8 + (lt_ly - scr->WBorTop - lt_fh - 1);
    idcmp |= IDCMP_GADGETUP;
    lt_dirty = 0;
    if (lt_win) {
        struct IntuiMessage *m;
        if (w < lt_win_w) w = lt_win_w;
        if (h < lt_win_h) h = lt_win_h;
        if (w != lt_win_w || h != lt_win_h) {
            int ow = w + lt_win->BorderLeft + lt_win->BorderRight, oh = h + lt_win->BorderTop + lt_win->BorderBottom;
            int l = lt_win->LeftEdge, t = lt_win->TopEdge;
            if (l + ow > lt_win->WScreen->Width) l = lt_win->WScreen->Width - ow > 0 ? lt_win->WScreen->Width - ow : 0;
            if (t + oh > lt_win->WScreen->Height) t = lt_win->WScreen->Height - oh > 0 ? lt_win->WScreen->Height - oh : 0;
            ChangeWindowBox(lt_win, l, t, ow, oh);
            /* ChangeWindowBox is deferred until the next input comes along (its autodoc): the old page is
             * erased and the new one drawn once the window has its new size, all of it. Two seconds at most. */
            {
                int i;
                for (i = 0; i < 100 && (lt_win->Width != ow || lt_win->Height != oh); i++) Delay(1);
            }
            lt_win_w = w; lt_win_h = h;
        }
        while ((m = GT_GetIMsg(lt_win->UserPort))) GT_ReplyIMsg(m);
        ModifyIDCMP(lt_win, idcmp);
        SetWindowTitles(lt_win, (STRPTR)~0, (STRPTR)screen_title);
        EraseRect(lt_win->RPort, lt_win->BorderLeft, lt_win->BorderTop, lt_win->Width - lt_win->BorderRight - 1,
                  lt_win->Height - lt_win->BorderBottom - 1);
        if (lt_cyc) GT_SetGadgetAttrs(lt_cyc, lt_win, NULL, GTCY_Active, lt_pos(lt_me), TAG_DONE);
        RefreshGList(lt_fg, lt_win, NULL, -1);
        AddGList(lt_win, glist, ~0, -1, NULL);
        RefreshGList(glist, lt_win, NULL, -1);
    } else {
        int ow = w + scr->WBorLeft + scr->WBorRight + 2, oh = h + scr->WBorTop + lt_fh + 1 + scr->WBorBottom + 2;
        if (left + ow > scr->Width) left = scr->Width - ow > 0 ? scr->Width - ow : 0;
        if (top + oh > scr->Height) top = scr->Height - oh > 0 ? scr->Height - oh : 0;
        if (!lt_frame_gadgets(scr)) { lt_free_frame(); return NULL; }
        lt_win = OpenWindowTags(NULL, WA_Title, (ULONG)"OpenPrefs", WA_ScreenTitle, (ULONG)screen_title,
                                WA_PubScreen, (ULONG)scr, WA_Left, left, WA_Top, top, WA_InnerWidth, w, WA_InnerHeight, h,
                                WA_Gadgets, (ULONG)lt_fg, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                                WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_IDCMP, idcmp, TAG_DONE);
        if (!lt_win) {
            if (ow > scr->Width || oh > scr->Height)
                Printf((STRPTR)"%s: the window (%ld x %ld) is larger than the screen (%ld x %ld)\n", (LONG)lt_prog,
                       (LONG)ow, (LONG)oh, (LONG)scr->Width, (LONG)scr->Height);
            lt_free_frame();
            return NULL;
        }
        lt_win_w = w; lt_win_h = h;
        GT_RefreshWindow(lt_win, NULL);
        AddGList(lt_win, glist, ~0, -1, NULL);
        RefreshGList(glist, lt_win, NULL, -1);
    }
    lt_rp = lt_win->RPort;
    lt_draw(lt_win, -1);
    return lt_win;
}

void lt_detach(struct Window *win, struct Gadget *glist)
{
    struct IntuiMessage *m;
    if (!win) return;
    if (glist) RemoveGList(win, glist, -1);
    while ((m = GT_GetIMsg(win->UserPort))) GT_ReplyIMsg(m);
    lt_rp = NULL;
}

void lt_close(struct Window *win, struct Gadget *glist)
{
    if (!win) return;
    if (lt_next < 0 && lt_refused_now) {      /* a Save went to ENV: only: the page again, saying so */
        lt_next = lt_me;
        snprintf(lt_note, sizeof lt_note, "Not saved: used for now (ENV: only), as SYS: has no preferences folders.");
    }
    lt_refused_now = 0;
    if (lt_next >= 0) { lt_detach(win, glist); return; }
    lt_rp = NULL;
    lt_win = NULL;
    CloseWindow(win);
    lt_free_frame();
}

/* The end: the window, if a page left it open */
void lt_shutdown(void)
{
    if (lt_win) { CloseWindow(lt_win); lt_win = NULL; }
    lt_rp = NULL;
    lt_free_frame();
}

void lt_seen(struct IntuiMessage *m)
{
    switch (m->Class) {
    case IDCMP_GADGETUP:
    case IDCMP_GADGETDOWN: {
        struct Gadget *g = (struct Gadget *)m->IAddress;
        if (g && !LT_IS(g->GadgetID)) lt_dirty = 1;
        break;
    }
    case IDCMP_VANILLAKEY: lt_dirty = 1; break;
    case IDCMP_RAWKEY: if (!(m->Code & IECODE_UP_PREFIX)) lt_dirty = 1; break;
    case IDCMP_MOUSEBUTTONS: if (m->Code == SELECTDOWN && m->MouseX >= lt_dx + 4) lt_dirty = 1; break;
    case IDCMP_MENUPICK: if (m->Code != MENUNULL) lt_dirty = 1; break;
    }
}

/* ---- a page's arguments ---------------------------------------------------------------------- */

struct RDArgs *lt_readargs(CONST_STRPTR tmpl, LONG *array)
{
    struct RDArgs *my = AllocDosObject(DOS_RDARGS, NULL), *r;
    if (!my) return NULL;
    my->RDA_Source.CS_Buffer = (STRPTR)lt_args;
    my->RDA_Source.CS_Length = strlen(lt_args);
    my->RDA_Source.CS_CurChr = 0;
    my->RDA_Flags |= RDAF_NOPROMPT;
    if (!(r = ReadArgs(tmpl, array, my))) { FreeDosObject(DOS_RDARGS, my); return NULL; }
    lt_myargs = my;
    return r;
}

void lt_freeargs(struct RDArgs *rd)
{
    if (!rd) return;
    FreeArgs(rd);
    if (rd == lt_myargs) { FreeDosObject(DOS_RDARGS, rd); lt_myargs = NULL; }
}

/* ---- the folders a save needs ----------------------------------------------------------------- */

static int lt_prefix(const char *s, const char *p)
{
    for (; *p; s++, p++) {
        char a = *s, b = *p;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

/* a lock on name, or 0, with no "please insert" requester */
static BPTR lt_lock(const char *name)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR l;
    me->pr_WindowPtr = (APTR)-1;
    l = Lock((CONST_STRPTR)name, SHARED_LOCK);
    me->pr_WindowPtr = was;
    return l;
}

static int lt_there(const char *name) { BPTR l = lt_lock(name); if (l) UnLock(l); return l != 0; }

static void lt_record(const char *what)
{
    size_t n = strlen(lt_made);
    if (n + strlen(what) + 3 < sizeof lt_made) {
        if (n) strcat(lt_made, ", ");
        strcat(lt_made, what);
    }
}

static int lt_mkdir(const char *name, int record)
{
    BPTR l;
    if (lt_there(name)) return 1;
    if (!(l = CreateDir((CONST_STRPTR)name))) return 0;
    UnLock(l);
    if (record) lt_record(name);
    return 1;
}

/* The folders above path's last part, made where they are missing (ENV:Sys for ENV:Sys/x.prefs) */
static void lt_parents(const char *path, int record)
{
    char p[256];
    int i, n = strlen(path);
    if (n >= (int)sizeof p) return;
    strcpy(p, path);
    for (i = 0; i < n; i++)
        if (p[i] == '/') { p[i] = 0; lt_mkdir(p, record); p[i] = '/'; }
}

static int lt_assign(const char *name, const char *to)
{
    BPTR l = lt_lock(to);
    if (!l) return 0;
    if (!AssignLock((CONST_STRPTR)name, l)) { UnLock(l); return 0; }
    return 1;
}

/* ENVARC: there, or made on SYS: (asking first from a window). 0: refused or not made. */
static int lt_envarc(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (STRPTR)"OpenPrefs",
                             (STRPTR)"The preferences folders are missing on SYS:. Create them?", (STRPTR)"Create|Cancel" };
    if (lt_there("ENVARC:")) return 1;
    if (lt_there("SYS:Prefs/Env-Archive")) return lt_assign("ENVARC", "SYS:Prefs/Env-Archive");
    if (lt_rp && lt_win && !EasyRequestArgs(lt_win, &es, NULL, NULL)) return 0;
    lt_made[0] = 0;
    if (!lt_mkdir("SYS:Prefs", 1) || !lt_mkdir("SYS:Prefs/Env-Archive", 1)) return 0;
    if (!lt_there("ENVARC:")) {
        if (!lt_assign("ENVARC", "SYS:Prefs/Env-Archive")) return 0;
        lt_record("the ENVARC: assign");
    }
    if (!lt_rp) lt_made_tree = 1;
    return 1;
}

int lt_envpath(const char *path) { return lt_prefix(path, "ENVARC:") || lt_prefix(path, "ENV:"); }

int lt_ready(const char *path, int isdir);

/* A page's Open and Lock (opshift.h): ENV: and ENVARC: with no system requester; a write gets its folders first */
BPTR lt_open_file(CONST_STRPTR name, LONG mode)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR f;
    if (mode != MODE_OLDFILE && name && lt_ready((const char *)name, 0)) name = (CONST_STRPTR)"NIL:";
    if (!name || !lt_envpath((const char *)name)) return Open(name, mode);
    me->pr_WindowPtr = (APTR)-1;
    f = Open(name, mode);
    me->pr_WindowPtr = was;
    return f;
}

BPTR lt_lock_file(CONST_STRPTR name, LONG mode)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR l;
    if (!name || !lt_envpath((const char *)name)) return Lock(name, mode);
    me->pr_WindowPtr = (APTR)-1;
    l = Lock(name, mode);
    me->pr_WindowPtr = was;
    return l;
}

int lt_ready(const char *path, int isdir)
{
    (void)isdir;
    if (lt_prefix(path, "ENVARC:")) {
        if (lt_refused) { lt_refused_now = 1; return 1; }
        if (!lt_envarc()) {
            if (lt_rp && lt_win) { lt_refused = lt_refused_now = 1; return 1; }
            return 0;                       /* not made: the write fails as it did */
        }
        lt_parents(path, lt_made_tree);
        return 0;
    }
    if (lt_prefix(path, "ENV:")) {
        if (!lt_there("ENV:")) {
            lt_mkdir("RAM:Env", 0);
            lt_assign("ENV", "RAM:Env");
        }
        lt_parents(path, 0);
    }
    return 0;
}

/* From the Shell: one line saying what a save made on SYS: */
void lt_report(void)
{
    if (lt_made_tree && lt_made[0]) Printf((STRPTR)"%s: made %s\n", (LONG)lt_prog, (LONG)lt_made);
    lt_made_tree = 0;
    lt_made[0] = 0;
}
