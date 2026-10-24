/* OpenWindows: the commodity behind OpenPrefs Windows. It watches the input
 * stream and moves, sizes and activates windows with the OS's own calls. One
 * patch (0.7): OpenWindowTagList and OpenWindow, so a window whose place is
 * kept opens there instead of opening where it asked and jumping a second
 * later; stacked with OpenLook's patch of the same calls.
 *
 *   snapping      a window dropped near a screen edge or another window's
 *                 edge lines up with it; dropped with the pointer at the
 *                 screen's left or right edge, it fills that half (option)
 *   Amiga+Tab     brings the window at the back to the front and activates
 *                 it; with Shift, sends the front window to the back
 *   double-click  a double-click in a window's title bar (its drag bar)
 *                 brings it to the front, as ClickToFront does for a
 *                 double-click anywhere; one already in front stays
 *   wheel         the mouse wheel goes to the window under the pointer
 *   places        a program's window opens where it was last left (0.7: it
 *                 opens there, placed before Intuition draws it; 0.9.2: also
 *                 when the program gives its own corner in its tags, and
 *                 sized when it asks by its inner size; nothing is moved
 *                 after it opens)
 *   drawers       Workbench drawer windows open at least this big
 *   edges         a resizable window is resized by dragging any edge but
 *                 its title bar: the sides, the bottom and the bottom
 *                 corners, from a few pixels outside the frame (0.5);
 *                 0.6.1: a strip 4 pixels outside the frame, a 5 x 5 square
 *                 on each bottom corner, an outline while the button is held
 *                 and one size at the button-up
 *   drive.title   (0.6) a drive's window is titled with the drive's name only:
 *                 Workbench's "Work  70% full, 453MB free, 62.0MB in use"
 *                 loses its usage part. On by default
 *   drag.solid    (0.8, off unless asked for) a window is dragged by its title
 *                 bar with its contents, not as an outline: OpenWindows takes
 *                 the press and moves the window as the pointer moves
 *                 0.8.4: every grip inside the window, none outside (a near miss
 *                 no longer lands on the desktop): 3 pixels in from the sides
 *                 and the bottom, 8 x 8 at the bottom corners; a window with
 *                 a sizing gadget of its own counts as one that can be sized
 *   edges.pointer while one is, the pointer shows the edge held (0.8.4: and
 *                 while it is over one of the active window's grips). Off unless
 *                 asked for: a window's own pointer (WA_Pointer, a busy
 *                 pointer) can't be read back, so it would come back as the
 *                 default one
 *
 *   gestures      (0.9, the action filter: docs/design/action-filter.md) every
 *                 left press goes through one table (ENV:OpenPrefs/Gestures,
 *                 built in when there is none): where it was (title bar,
 *                 inside a window, a border gadget, an edge, the backdrop),
 *                 the qualifiers, and what the hand did (a click with no
 *                 move, a double-click, a press and a move past 4 pixels)
 *                 give the actions, decided once. A press OpenWindows does
 *                 not take goes on to Intuition at once, so a drag starts
 *                 on the first move; a click fronts the window at its
 *                 release, never at the press; the release ends a drag at
 *                 its own place. With OpenIntuition (which drags windows
 *                 with their contents itself) drag.solid leaves the drag to
 *                 Intuition; OpenWindows' own solid drag is for the others
 *
 * Settings: ENV:OpenPrefs/Windows (OpenPrefs Windows writes it, then sends
 * Ctrl-F to the task of the public port "OpenWindows" to read it again). Places: ENV:OpenPrefs/WindowPlaces,
 * kept in ENVARC: too. Exchange can disable, enable or quit it.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/classusr.h>
#include <intuition/pointerclass.h>
#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <graphics/clip.h>
#include <libraries/commodities.h>
#include <libraries/keymap.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/graphics.h>
#include <proto/commodities.h>
#include <proto/utility.h>
#include <utility/tagitem.h>
#include <exec/semaphores.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Common/cogplace.h"
#include "gestures.h"
#include "places.h"

static const char version[] __attribute__((used)) = "$VER: OpenWindows 0.9.2 (24.10.2026) MIT, Copyright (c) 2026 AmigaChrome Team";

#define PREFS_ENV "ENV:OpenPrefs/Windows"
#define PLACES_ENV "ENV:OpenPrefs/WindowPlaces"
#define PLACES_ENVARC "ENVARC:OpenPrefs/WindowPlaces"
#define IECLASS_NEWMOUSE_ 0x16                  /* NewMouse drivers' class; OS 3.2 sends RAWKEY 0x7A-0x7D */
#define MAX_SEEN 96
#define MAX_NEVER 16

struct Library *CxBase, *LayersBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

/* ---- settings ------------------------------------------------------------ */

static struct {
    int snap, snap_dist, halves, switcher, wheel, places, places_wb, drawer_w, drawer_h, dblfront, edges, edge_ptr, drive_title, solid;
    int taller;                                  /* 0.8.2: extra rows in every window's title bar (default 4) */
    char key[48];
    char never[MAX_NEVER][32];
    int nnever;
} cfg;
static char log_path[128];                      /* 0.9.2: ENV:OpenWindowsLog names a file: what the look saw */

static char *read_file(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf = NULL;
    if (fh) {
        LONG len;
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len >= 0 && (buf = AllocVec(len + 1, MEMF_ANY))) {
            if (Read(fh, buf, len) != len) { FreeVec(buf); buf = NULL; }
            else buf[len] = 0;
        }
        Close(fh);
    }
    return buf;
}

static int on_off(const char *s) { return !strncmp(s, "on", 2); }

static void read_prefs(void)
{
    char *text = read_file(PREFS_ENV), *p, *line;
    memset(&cfg, 0, sizeof cfg);
    cfg.snap = 1; cfg.snap_dist = 12; cfg.halves = 0; cfg.switcher = 1; cfg.wheel = 1; cfg.places = 1; cfg.dblfront = 1; cfg.edges = 1; cfg.edge_ptr = 1; cfg.drive_title = 1; cfg.taller = 4;
    strcpy(cfg.key, "lcommand tab");
    if (!text) return;
    for (p = text; *p; ) {
        char *v;
        line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (*line == ';' || !*line) continue;
        if (!(v = strchr(line, ' '))) continue;
        *v++ = 0;
        if (!strcmp(line, "snap")) { cfg.snap = on_off(v); if (strchr(v, ' ')) cfg.snap_dist = atoi(strchr(v, ' ') + 1); }
        else if (!strcmp(line, "snap.halves")) cfg.halves = on_off(v);
        else if (!strcmp(line, "switcher")) cfg.switcher = on_off(v);
        else if (!strcmp(line, "switcher.key")) { strncpy(cfg.key, v, sizeof cfg.key - 1); }
        else if (!strcmp(line, "wheel")) cfg.wheel = on_off(v);
        else if (!strcmp(line, "doubleclick.front")) cfg.dblfront = on_off(v);
        else if (!strcmp(line, "edges")) cfg.edges = on_off(v);
        else if (!strcmp(line, "edges.pointer")) cfg.edge_ptr = on_off(v);
        else if (!strcmp(line, "drive.title")) cfg.drive_title = on_off(v);
        else if (!strcmp(line, "drag.solid")) cfg.solid = on_off(v);
        else if (!strcmp(line, "title.taller")) cfg.taller = atoi(v);
        else if (!strcmp(line, "places")) cfg.places = on_off(v);
        else if (!strcmp(line, "places.drawers")) cfg.places_wb = on_off(v);
        else if (!strcmp(line, "never") && cfg.nnever < MAX_NEVER) strncpy(cfg.never[cfg.nnever++], v, 31);
        else if (!strcmp(line, "drawer")) { cfg.drawer_w = atoi(v); if (strchr(v, ' ')) cfg.drawer_h = atoi(strchr(v, ' ') + 1); }
    }
    if (cfg.snap_dist < 2) cfg.snap_dist = 2;
    if (cfg.snap_dist > 64) cfg.snap_dist = 64;
    if (cfg.taller < 0) cfg.taller = 0;
    if (cfg.taller > 16) cfg.taller = 16;
    FreeVec(text);
}

/* ---- the action filter's table (0.9; gestures.h, docs/design/action-filter.md) ---------- */

#define GESTURES_ENV "ENV:OpenPrefs/Gestures"
static struct gesture_table gtab;               /* the handler reads it; changed under Forbid */
static int intuition_drags;                     /* Intuition drags windows with their contents itself (OpenIntuition) */

/* The built-in table from the settings, or ENV:OpenPrefs/Gestures in its place
 * when there is one that reads (a line it can't read: the built-in one, and why). */
static void read_gestures(void)
{
    static struct gesture_table t;
    char *text = read_file(GESTURES_ENV);
    gestures_default(&t, cfg.dblfront, cfg.edges, cfg.snap);
    if (text) {
        int n = gestures_parse(&t, text);
        if (n < 0) {
            char msg[96];
            snprintf(msg, sizeof msg, "OpenWindows: " GESTURES_ENV " line %d can't be read; the built-in table is used\n", -n);
            PutStr((STRPTR)msg);
        }
        FreeVec(text);
    }
    Forbid();
    gtab = t;
    Permit();
}

static void read_settings(void)
{
    const char *id = (const char *)IntuitionBase->LibNode.lib_IdString;
    read_prefs();
    read_gestures();
    if (GetVar((STRPTR)"OpenWindowsLog", (STRPTR)log_path, sizeof log_path, 0) <= 0) log_path[0] = 0;
    intuition_drags = id && strstr(id, "OpenIntuition") != NULL;
}

/* ---- taller title bars (0.8.2) ---------------------------------------------------
 *
 * Intuition gives a window's title bar scr->WBorTop + the screen font's height + 1 rows,
 * reading WBorTop from the screen each time a window opens. So the Workbench screen's
 * WBorTop is raised by cfg.taller (4: 2 rows above the text, 2 below, once OpenLook
 * centres the title), and every window opened after that has a taller bar. The screen's
 * own bar was laid out when the screen opened and is not changed here. Put back on exit
 * if nothing else has changed it since. */
static struct Screen *tall_scr;
static WORD tall_was, tall_now;

static void taller_on(void)
{
    struct Screen *s = LockPubScreen((STRPTR)"Workbench");
    if (!s) return;
    Forbid();
    if (tall_scr == s && s->WBorTop == tall_now) s->WBorTop = tall_was;     /* the old extra off first */
    else if (tall_scr != s) { tall_scr = s; tall_was = s->WBorTop; }
    tall_now = tall_was + cfg.taller;
    s->WBorTop = tall_now;
    Permit();
    UnlockPubScreen(NULL, s);
}

static void taller_off(void)
{
    struct Screen *s;
    if (!tall_scr) return;
    s = LockPubScreen((STRPTR)"Workbench");
    if (s == tall_scr) { Forbid(); if (s->WBorTop == tall_now) s->WBorTop = tall_was; Permit(); }
    if (s) UnlockPubScreen(NULL, s);
    tall_scr = NULL;
}

/* ---- the input stream (input.device's task: no waiting, no DOS) ------------- */

static struct Task *me;
static struct cogplace cog;                      /* 0.8.1: the cog's request, public by name (cogplace.h) */
static ULONG sig_button, sig_wheel;
#define WHEEL_RING 16
static struct InputEvent wheel_ring[WHEEL_RING];
static volatile int wheel_in, wheel_out;
static volatile int pass_wheel;                 /* events we put back: let them through */
static volatile int on = 1;

/* The action filter (0.9). The press being watched, the handler's own: where
 * it was and on what, whether OpenWindows took it, where the pointer is now
 * and whether it has gone past the threshold. At the release the gesture is
 * decided and its actions go to the main task through the done ring. */
static struct {
    int on, target, take, moved;
    int menu;                                   /* 0.9.1: the menu button came while it was held */
    struct Window *w;
    struct Screen *s;
    struct Layer *top;                          /* 0.9.1: the screen's front layer at the press */
    WORD x0, y0, x, y;
    WORD l, t, wd, ht;                          /* the window's box at the press */
    ULONG secs, micro;
    unsigned qual;
} press;
/* the last click, for a double-click: a second click on the same thing, close in place and time */
static struct { struct Window *w; int target; WORD x, y; ULONG secs, micro; } last_click;
#define DONE_RING 8
static struct done {
    struct Window *w;
    struct Screen *s;
    struct Layer *top;                          /* 0.9.1: the screen's front layer at the press */
    UBYTE kind, target, actions, pad;
    WORD x0, y0, x, y, l, t, wd, ht;
} done_ring[DONE_RING];
static volatile int done_in, done_out;

/* Resizing from an edge (0.5): the handler sees a press on an edge, keeps it
 * from Intuition and tells the main task, which resizes the window with
 * ChangeWindowBox as the pointer moves, until the button comes up. */
#define EDGE_L 1
#define EDGE_R 2
#define EDGE_B 4
#define EDGE_MOVE 8                             /* 0.8: not an edge: the title bar, dragging the window solid */
/* 0.8.3 (the user, 10 October 2026: "mouse behaviour at window edges and corner is odd and inconsistent"): the
 * same reach outside and inside on every side, a corner square big enough to find, and the corner takes in
 * Intuition's own size gadget, so a press there resizes the same way (solid, from here) as a press on an edge. */
/* 0.8.4 (the Team, 11 October 2026: "a mis click outside it removes active status ... an invisible 3 pixel
 * boundary around the two sides and bottom where the mouse pointer changes, and lets us drag"): every grip is
 * inside the window, none outside it, and the pointer shows the grip while it is over one (the window active). */
#define ZONE_IN 3                               /* a side or the bottom: this many pixels inside the frame */
#define ZONE_CORNER 8                           /* a bottom corner: an 8 x 8 square inside it */
#define SIZE_GADGET_W 18                        /* Intuition's size gadget at the bottom right: its box is the corner's too */
static ULONG sig_grab;
static ULONG sig_hover;                         /* 0.8.4: the grip under the pointer changed */
static struct Window *volatile hover_win;       /* the active window whose grip the pointer is over, or NULL */
static volatile int hover_edges;
static struct Window *volatile grab_win;        /* the window being resized, set by the handler */
static volatile int grab_edges, grab_on;
static volatile WORD grab_x0, grab_y0;          /* the pointer at the press, screen relative */
/* 0.9: the pointer as the handler saw it with the drag's latest event, and at
 * the release: the main task follows these, not the screen's MouseX, which by
 * the time it runs may hold moves made after the release (W66: the drag went
 * on past the release by what the hand did after it) */
static volatile WORD grab_px, grab_py, grab_ex, grab_ey;
static volatile ULONG grab_gen;                 /* up by one for each press taken: a new press is never lost to the end of the last */
static volatile ULONG grab_end_gen;             /* the press whose release came (its place: grab_ex, grab_ey) */
static volatile ULONG grab_last;                /* the time stamp (seconds) of the drag's latest event, from the handler */

/* The window whose edge (x, y) is on, and which edges, or NULL. The screen's
 * layers are read front to back under their lock, so none goes while we look;
 * the input handler can't wait for it, so when another task holds it the
 * press is left to Intuition (AttemptSemaphore). The first window whose frame
 * holds the point is the one under the pointer, and only its own edge counts;
 * a point just outside every frame in front of a window belongs to that
 * window's edge. The frame is the window's own box, not its layer's: a
 * GimmeZeroZero window's inner layer and a requester's are inside it. The
 * title bar's height never resizes (it moves the window), nor does a window
 * that can't be resized, a backdrop or a borderless one. */
/* 0.8.4: a window the user may size: Intuition's size gadget, or a sizing gadget of its own (OpenBench's
 * drawers have one in place of Intuition's, so their frame stays thin). */
static int resizable(struct Window *w)
{
    struct Gadget *g;
    if (w->Flags & WFLG_SIZEGADGET) return 1;
    for (g = w->FirstGadget; g; g = g->NextGadget)
        if ((g->GadgetType & GTYP_SYSTYPEMASK) == GTYP_SIZING) return 1;
    return 0;
}

static struct Window *edge_at(struct Screen *s, WORD x, WORD y, int *edges)
{
    struct Layer *l;
    struct Window *found = NULL;
    if (!AttemptSemaphore(&s->LayerInfo.Lock))
        return NULL;
    for (l = s->LayerInfo.top_layer; l; l = l->back) {
        struct Window *w = (struct Window *)l->Window;
        WORD L, T, R, B;
        int in, e = 0;
        if (!w || l == s->BarLayer) {           /* the screen's title bar, a menu */
            if (x >= l->bounds.MinX && x <= l->bounds.MaxX && y >= l->bounds.MinY && y <= l->bounds.MaxY) break;
            continue;
        }
        L = w->LeftEdge; T = w->TopEdge; R = L + w->Width - 1; B = T + w->Height - 1;
        in = x >= L && x <= R && y >= T && y <= B;
        if ((w->Flags & (WFLG_BACKDROP | WFLG_BORDERLESS)) || !resizable(w)) {
            if (in) break;                      /* a window that keeps its size */
            continue;
        }
        if (in && y >= T + w->BorderTop) {
            /* the sides and the bottom: a strip inside the frame, below the title bar */
            if (x <= L + ZONE_IN - 1) e |= EDGE_L;
            if (x >= R - ZONE_IN + 1) e |= EDGE_R;
            if (y >= B - ZONE_IN + 1) e |= EDGE_B;
            /* a bottom corner: a square inside it holds both edges */
            if (y >= B - ZONE_CORNER + 1) {
                if (x <= L + ZONE_CORNER - 1) e |= EDGE_L | EDGE_B;
                if (x >= R - ZONE_CORNER + 1) e |= EDGE_R | EDGE_B;
            }
            /* Intuition's size gadget (bottom right, in the bottom border): ours, so both corners feel the same */
            if (x >= R - SIZE_GADGET_W && x <= R && y >= B - (w->BorderBottom > 2 ? w->BorderBottom : 2) - 1 && y <= B)
                e |= EDGE_R | EDGE_B;
        }
        if (e) { *edges = e; found = w; break; }
        if (in) break;                          /* inside it, off its edges: an ordinary press */
    }
    ReleaseSemaphore(&s->LayerInfo.Lock);
    return found;
}

/* 0.9: what a press at (x, y) is on, for the gesture table: the frontmost layer there (read as edge_at reads,
 * under the layers' lock, AttemptSemaphore: the input task never waits), its window, and which part of it:
 * the drag bar (the title bar's drag gadget and no other gadget), a gadget (0.9.1: the border's, close, depth,
 * zoom, size, or one of the program's own, or anything while a requester is up: gestures_target), inside it
 * (GT_BODY), a backdrop window, or the screen's bar. -1 when the layers are busy. */
static int target_at(struct Screen *s, WORD x, WORD y, struct Window **wp)
{
    struct Layer *l;
    int t = GT_BACKDROP;                                    /* nothing there: the screen itself */
    *wp = NULL;
    if (!AttemptSemaphore(&s->LayerInfo.Lock))
        return -1;
    for (l = s->LayerInfo.top_layer; l; l = l->back) {
        struct Window *w = (struct Window *)l->Window;
        if (x < l->bounds.MinX || x > l->bounds.MaxX || y < l->bounds.MinY || y > l->bounds.MaxY) continue;
        if (!w || l == s->BarLayer) { t = GT_SCREENBAR; break; }   /* the screen's title bar, a menu */
        *wp = w;
        if (w->Flags & WFLG_BACKDROP) { t = GT_BACKDROP; break; }
        {
            WORD wx = x - w->LeftEdge, wy = y - w->TopEdge;
            struct Gadget *g;
            struct gesture_hit h;
            memset(&h, 0, sizeof h);
            for (g = w->FirstGadget; g; g = g->NextGadget) {
                /* 0.9.1: a GimmeZeroZero window's own gadgets are placed in its inner part */
                int inner = (w->Flags & WFLG_GIMMEZEROZERO) && !(g->GadgetType & GTYP_GZZGADGET);
                WORD ox = inner ? w->BorderLeft : 0, oy = inner ? w->BorderTop : 0;
                WORD bw = inner ? w->GZZWidth : w->Width, bh = inner ? w->GZZHeight : w->Height;
                WORD gl = ox + g->LeftEdge + ((g->Flags & GFLG_RELRIGHT) ? bw - 1 : 0);
                WORD gt = oy + g->TopEdge + ((g->Flags & GFLG_RELBOTTOM) ? bh - 1 : 0);
                WORD gw = g->Width + ((g->Flags & GFLG_RELWIDTH) ? bw : 0);
                WORD gh = g->Height + ((g->Flags & GFLG_RELHEIGHT) ? bh : 0);
                if (wx < gl || wx >= gl + gw || wy < gt || wy >= gt + gh) continue;
                if ((g->GadgetType & GTYP_SYSTYPEMASK) == GTYP_WDRAGGING && (g->GadgetType & GTYP_SYSGADGET)) h.drag_bar = 1;
                else if (g->GadgetType & GTYP_SYSGADGET) h.border_gadget = 1;
                else if (!(g->Flags & GFLG_DISABLED)) h.own_gadget = 1;   /* 0.9.1: its press is the gadget's */
            }
            h.in_title = (w->Flags & WFLG_DRAGBAR) && wy >= 0 && wy < w->BorderTop;
            h.requester = w->FirstRequest != NULL;
            t = gestures_target(&h);
        }
        break;
    }
    ReleaseSemaphore(&s->LayerInfo.Lock);
    return t;
}

static int over_active(void)
{
    struct Window *w = IntuitionBase->ActiveWindow;
    struct Screen *s = w ? w->WScreen : NULL;
    WORD x, y;
    if (!w || !s || s != IntuitionBase->FirstScreen) return 1;      /* nothing better to offer */
    if (s->LayerInfo.top_layer != w->WLayer) return 0;             /* something may cover it: ask the layers */
    x = s->MouseX; y = s->MouseY;
    return x >= w->LeftEdge && y >= w->TopEdge && x < w->LeftEdge + w->Width && y < w->TopEdge + w->Height;
}

/* The action filter (0.9, docs/design/action-filter.md): a left press. What it is on and the table decide, once,
 * whether OpenWindows takes it (an edge to size, or a title bar to drag where Intuition's drag is an outline) or
 * it goes on to Intuition at once (Intuition's own drag then starts on the first move; a press is never held
 * back to see what it becomes). Answers 1 when it took it. */
static int press_begin(struct InputEvent *ie, struct Screen *s)
{
    struct Window *w = NULL;
    int target, edges = 0, take = 0;
    unsigned qual = gestures_qual(ie->ie_Qualifier), move;
    WORD x = s->MouseX, y = s->MouseY;                      /* this handler runs before Intuition: the place of the events before */
    if (ie->ie_Qualifier & IEQUALIFIER_RBUTTON) {           /* 0.9.1: a left press while the menu button is held is the menus' */
        press.on = 0;
        return 0;
    }
    target = gestures_actions(&gtab, GT_EDGE, GB_SELECT, GK_MOVE, qual, &take) && take ? -2 : -1;
    if (target == -2 && (w = edge_at(s, x, y, &edges))) target = GT_EDGE;
    else target = target_at(s, x, y, &w);
    press.on = 0;
    if (target < 0) return 0;                               /* the layers busy: Intuition has it, and no gesture */
    move = gestures_actions(&gtab, target, GB_SELECT, GK_MOVE, qual, &take);
    if (target == GT_TITLE && (move & GA_DRAG) && cfg.solid && !intuition_drags) take = 1;   /* OpenWindows' own solid drag */
    if (target != GT_EDGE && target != GT_TITLE) take = 0;  /* only an edge or a title bar is OpenWindows' to drag */
    press.on = 1; press.target = target; press.take = take; press.moved = 0; press.menu = 0;
    press.top = s->LayerInfo.top_layer;                     /* 0.9.1: what the press brings up comes in front of it */
    press.w = w; press.s = s; press.x0 = press.x = x; press.y0 = press.y = y; press.qual = qual;
    press.secs = ie->ie_TimeStamp.tv_secs; press.micro = ie->ie_TimeStamp.tv_micro;
    if (w) { press.l = w->LeftEdge; press.t = w->TopEdge; press.wd = w->Width; press.ht = w->Height; }
    if (!take || !w) { press.take = 0; return 0; }
    grab_win = w; grab_edges = target == GT_EDGE ? edges : EDGE_MOVE;
    grab_x0 = grab_px = grab_ex = x; grab_y0 = grab_py = grab_ey = y;
    grab_gen++;
    grab_on = 1; grab_last = ie->ie_TimeStamp.tv_secs;
    ie->ie_Code = IECODE_NOBUTTON;                          /* Intuition sees a move, not the press */
    Signal(me, sig_grab);
    return 1;
}

/* The pointer moved to (x, y) with the button held: past the threshold, the gesture is a move */
static void press_moved(WORD x, WORD y)
{
    press.x = x; press.y = y;
    if (!press.moved && gestures_moved(press.x0, press.y0, x, y)) press.moved = 1;
}

/* The release: the gesture is decided and its actions go to the main task. A click (no move past the threshold)
 * is a double-click when the last click was on the same thing, close in place, within Input prefs' time. The drag
 * itself is over already: Intuition ended its own at this event, OpenWindows' ends at this place. */
static void press_end(struct InputEvent *ie)
{
    int kind;
    unsigned a;
    if (!press.on) return;
    press.on = 0;
    kind = press.moved ? GK_MOVE : GK_CLICK;
    if (kind == GK_CLICK) {
        if (last_click.w == press.w && last_click.target == press.target && press.w &&
            last_click.x - press.x0 <= 4 && press.x0 - last_click.x <= 4 && last_click.y - press.y0 <= 4 && press.y0 - last_click.y <= 4 &&
            DoubleClick(last_click.secs, last_click.micro, press.secs, press.micro)) {
            kind = GK_DOUBLE;
            last_click.w = NULL;                            /* a third click starts again */
        } else {
            last_click.w = press.w; last_click.target = press.target; last_click.x = press.x0; last_click.y = press.y0;
            last_click.secs = press.secs; last_click.micro = press.micro;
        }
    } else last_click.w = NULL;
    a = gestures_actions(&gtab, press.target, GB_SELECT, kind, press.qual, NULL);
    /* 0.9.1: a window the press brought up (a popup, a requester, a new drawer) is in front of the screen's layers
     * by now: it stays in front. The main task looks again before it fronts (gestures_done). */
    a = gestures_release(a, press.menu, press.s && press.s->LayerInfo.top_layer != press.top);
    a &= ~(GA_DRAG | GA_RESIZE);                            /* done as the pointer moved */
    if (press.take) a &= ~GA_SNAP;                          /* OpenWindows' own drag snaps at its end (resize) */
    if (!a || !press.w || ((done_in + 1) & (DONE_RING - 1)) == done_out) return;
    {
        struct done *d = &done_ring[done_in];
        d->w = press.w; d->s = press.s; d->top = press.top;
        d->kind = (UBYTE)kind; d->target = (UBYTE)press.target; d->actions = (UBYTE)a;
        d->x0 = press.x0; d->y0 = press.y0; d->x = press.x; d->y = press.y;
        d->l = press.l; d->t = press.t; d->wd = press.wd; d->ht = press.ht;
        done_in = (done_in + 1) & (DONE_RING - 1);
    }
    (void)ie;
    Signal(me, sig_button);
}

/* The place an event puts the pointer at on screen s, when it is a placing (IECLASS_NEWPOINTERPOS, as
 * AmigaChrome's absolute pointer and tablets send) on that screen: 1 and (x, y) */
static int placed_at(struct InputEvent *ie, struct Screen *s, WORD *x, WORD *y)
{
    if (ie->ie_Class == IECLASS_NEWPOINTERPOS && ie->ie_SubClass == IESUBCLASS_PIXEL && ie->ie_EventAddress) {
        struct IEPointerPixel *pp = (struct IEPointerPixel *)ie->ie_EventAddress;
        if (pp->iepp_Screen != s) return 0;
        *x = pp->iepp_Position.X; *y = pp->iepp_Position.Y;
        return 1;
    }
    return 0;
}

static void custom(CxMsg *msg, CxObj *co)
{
    struct InputEvent *ie = (struct InputEvent *)CxMsgData(msg);
    (void)co;
    if (!on) return;
    for (; ie; ie = ie->ie_NextEvent) {
        struct Screen *s = IntuitionBase->FirstScreen;
        WORD px, py;
        if (ie->ie_Class == IECLASS_RAWMOUSE) {
            WORD mx = s ? s->MouseX + ie->ie_X : 0, my = s ? s->MouseY + ie->ie_Y : 0;   /* a relative mouse's move, near enough */
            if (ie->ie_Code == IECODE_RBUTTON && press.on) press.menu = 1;   /* 0.9.1: the menus' now; it goes on untouched */
            if (grab_on) {                         /* OpenWindows' own drag or edge: the button coming up ends it, here */
                grab_px = mx; grab_py = my;
                if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
                    grab_ex = mx; grab_ey = my;
                    grab_on = 0; grab_end_gen = grab_gen;
                    ie->ie_Code = IECODE_NOBUTTON;     /* the pointer's last move still goes on */
                    press_end(ie);
                } else if (press.on && (ie->ie_X || ie->ie_Y)) press_moved(mx, my);
                grab_last = ie->ie_TimeStamp.tv_secs;
                Signal(me, sig_grab);              /* the main task follows the place the handler saw */
                continue;
            }
            if (ie->ie_Code == IECODE_NOBUTTON && cfg.edges && cfg.edge_ptr && s && sig_hover) {
                /* 0.8.4: a move: the pointer shows the grip of the active window it is over */
                int e = 0;
                struct Window *hw = edge_at(s, s->MouseX, s->MouseY, &e);
                if (hw != IntuitionBase->ActiveWindow) { hw = NULL; e = 0; }
                if (hw != hover_win || e != hover_edges) { hover_win = hw; hover_edges = e; Signal(me, sig_hover); }
            }
            if (ie->ie_Code == IECODE_LBUTTON && s) {
                if (press_begin(ie, s)) continue;
            } else if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
                press_end(ie);
            } else if (press.on && (ie->ie_X || ie->ie_Y)) press_moved(mx, my);
        } else if ((ie->ie_Class == IECLASS_POINTERPOS || ie->ie_Class == IECLASS_NEWPOINTERPOS) && s) {
            /* a pointer put somewhere (not a mouse counting): the drag follows it too. 0.8.6: AmigaChrome's absolute
             * pointer comes through the open input.device as IECLASS_NEWPOINTERPOS (IESUBCLASS_PIXEL), one a move */
            if (!placed_at(ie, s, &px, &py)) { px = s->MouseX; py = s->MouseY; }
            if (press.on) press_moved(px, py);
            if (grab_on) {
                grab_px = px; grab_py = py;
                grab_last = ie->ie_TimeStamp.tv_secs;
                Signal(me, sig_grab);
            }
        } else if (cfg.wheel && (ie->ie_Class == IECLASS_RAWKEY || ie->ie_Class == IECLASS_NEWMOUSE_) &&
                   (ie->ie_Code & 0x7F) >= RAWKEY_WHEEL_UP && (ie->ie_Code & 0x7F) <= RAWKEY_WHEEL_RIGHT) {
            if (pass_wheel) { pass_wheel--; continue; }
            if (over_active()) continue;
            if (((wheel_in + 1) & (WHEEL_RING - 1)) != wheel_out) {
                wheel_ring[wheel_in] = *ie;
                wheel_ring[wheel_in].ie_NextEvent = NULL;
                wheel_in = (wheel_in + 1) & (WHEEL_RING - 1);
                Signal(me, sig_wheel);
            }
            ie->ie_Class = IECLASS_NULL;           /* it goes on after the window under the pointer is active */
        }
    }
}

/* ---- windows ---------------------------------------------------------------------------- */

/* The window whose layer is under (x, y) on screen s, or NULL. */
static struct Window *window_at(struct Screen *s, WORD x, WORD y)
{
    struct Layer *l;
    struct Window *w = NULL;
    LockLayerInfo(&s->LayerInfo);
    l = WhichLayer(&s->LayerInfo, x, y);
    if (l) w = (struct Window *)l->Window;
    UnlockLayerInfo(&s->LayerInfo);
    if (w && (w->Flags & WFLG_BACKDROP) && !(w->Flags & WFLG_WBENCHWINDOW)) w = NULL;
    return w;
}

static int is_window(struct Window *w)
{
    struct Screen *s;
    struct Window *x;
    ULONG lock = LockIBase(0);
    int found = 0;
    for (s = IntuitionBase->FirstScreen; s && !found; s = s->NextScreen)
        for (x = s->FirstWindow; x; x = x->NextWindow)
            if (x == w) { found = 1; break; }
    UnlockIBase(lock);
    return found;
}

static void wheel(void)
{
    while (wheel_out != wheel_in) {
        struct InputEvent ev = wheel_ring[wheel_out];
        struct Screen *s = IntuitionBase->FirstScreen;
        struct Window *w = s ? window_at(s, s->MouseX, s->MouseY) : NULL;
        wheel_out = (wheel_out + 1) & (WHEEL_RING - 1);
        if (w && w != IntuitionBase->ActiveWindow && !(w->Flags & WFLG_BACKDROP)) {
            ActivateWindow(w);
            Delay(1);                              /* Intuition makes it active on its next event */
        }
        pass_wheel++;
        AddIEvents(&ev);
    }
}

/* Snapping: the nearest of the screen's and the other windows' edges. */
static void best(WORD *delta, WORD edge, WORD target, int dist)
{
    WORD d = target - edge;
    if (d < 0 ? -d <= dist : d <= dist)
        if (*delta == 0x7fff || (d < 0 ? -d : d) < (*delta < 0 ? -*delta : *delta)) *delta = d;
}

static void snap(struct Window *w)
{
    struct Screen *s = w->WScreen;
    struct Window *o;
    WORD dx = 0x7fff, dy = 0x7fff, l = w->LeftEdge, t = w->TopEdge, r = l + w->Width, b = t + w->Height;
    WORD top = s->BarHeight + 1;
    int dist = cfg.snap_dist;
    best(&dx, l, 0, dist); best(&dx, r, s->Width, dist);
    best(&dy, t, top, dist); best(&dy, t, 0, dist); best(&dy, b, s->Height, dist);
    {
        ULONG lock = LockIBase(0);
        for (o = s->FirstWindow; o; o = o->NextWindow) {
            WORD ol = o->LeftEdge, ot = o->TopEdge, orr = ol + o->Width, ob = ot + o->Height;
            if (o == w || (o->Flags & WFLG_BACKDROP)) continue;
            if (t < ob + dist && b > ot - dist) {                     /* side by side */
                best(&dx, l, orr, dist); best(&dx, r, ol, dist); best(&dx, l, ol, dist); best(&dx, r, orr, dist);
            }
            if (l < orr + dist && r > ol - dist) {                    /* one above the other */
                best(&dy, t, ob, dist); best(&dy, b, ot, dist); best(&dy, t, ot, dist); best(&dy, b, ob, dist);
            }
        }
        UnlockIBase(lock);
    }
    if (dx == 0x7fff) dx = 0;
    if (dy == 0x7fff) dy = 0;
    if (dx || dy) MoveWindow(w, dx, dy);
}

static void halves(struct Window *w, WORD px)
{
    struct Screen *s = w->WScreen;
    WORD top = s->BarHeight + 1, half = s->Width / 2;
    if (!(w->Flags & WFLG_SIZEGADGET)) return;
    if (half < w->MinWidth || s->Height - top < w->MinHeight) return;
    ChangeWindowBox(w, px <= 1 ? 0 : s->Width - half, top, half, s->Height - top);
}

/* ---- the gestures' actions (0.9, the main task) ------------------------------------------------- */

/* 1 when no other window's layer in front of w overlaps it */
static int in_front(struct Window *w)
{
    struct Layer *l;
    int covered = 0;
    LockLayerInfo(&w->WScreen->LayerInfo);
    for (l = w->WLayer->front; l && !covered; l = l->front) {
        struct Window *o = (struct Window *)l->Window;
        if (o && (o->Flags & WFLG_BORDERLESS) && o->Height <= w->WScreen->BarHeight + 1) continue;  /* the title bar's tray */
        covered = l->bounds.MinX <= w->WLayer->bounds.MaxX && l->bounds.MaxX >= w->WLayer->bounds.MinX &&
                  l->bounds.MinY <= w->WLayer->bounds.MaxY && l->bounds.MaxY >= w->WLayer->bounds.MinY;
    }
    UnlockLayerInfo(&w->WScreen->LayerInfo);
    return !covered;
}

/* 0.9.1: 1 when w's program waits on another window of its own that is in front of w and overlaps it: a
 * requester (EasyRequest, ASL, its own dialog) whose loop isn't reading w's port, so w in front would hide
 * it and the program would look stuck (Dale, 18 October: "open files ... wont close"). A program that waits
 * on w's port too (OpenBench's drawers share one) or is running isn't held. */
static int own_requester_in_front(struct Window *w)
{
    struct MsgPort *p = w->UserPort;
    struct Task *t = p && (p->mp_Flags & PF_ACTION) == PA_SIGNAL ? (struct Task *)p->mp_SigTask : NULL;
    struct Layer *l;
    int waits_elsewhere, found = 0;
    if (!t) return 0;
    Forbid();
    waits_elsewhere = t->tc_State == TS_WAIT && !(t->tc_SigWait & (1UL << p->mp_SigBit));
    Permit();
    if (!waits_elsewhere) return 0;
    LockLayerInfo(&w->WScreen->LayerInfo);
    for (l = w->WLayer->front; l && !found; l = l->front) {
        struct Window *o = (struct Window *)l->Window;
        if (!o || o == w || !o->UserPort || o->UserPort->mp_SigTask != (APTR)t) continue;
        found = l->bounds.MinX <= w->WLayer->bounds.MaxX && l->bounds.MaxX >= w->WLayer->bounds.MinX &&
                l->bounds.MinY <= w->WLayer->bounds.MaxY && l->bounds.MaxY >= w->WLayer->bounds.MinY;
    }
    UnlockLayerInfo(&w->WScreen->LayerInfo);
    return found;
}

/* What the handler decided at each release, in order: the window to the front or the back, made
 * active, or lined up with an edge after a drag. Intuition has ended its drag before this runs (the
 * input task, at priority 20, takes the release through every handler first); a Intuition that
 * moves a dragged window at the next event (OS 3.2's) gets a frame for it before a snap. */
static void gestures_done(void)
{
    while (done_out != done_in) {
        struct done d = done_ring[done_out];
        struct Window *w = d.w;
        done_out = (done_out + 1) & (DONE_RING - 1);
        if (!on || !w || !is_window(w)) continue;
        if ((d.actions & GA_SNAP) && d.kind == GK_MOVE) {
            if (!intuition_drags) Delay(1);
            if (is_window(w) && w->Width == d.wd && w->Height == d.ht && (w->LeftEdge != d.l || w->TopEdge != d.t)) {
                if (cfg.halves && (d.x <= 1 || d.x >= w->WScreen->Width - 2)) halves(w, d.x);
                else snap(w);
            }
        }
        if (!is_window(w)) continue;
        if ((d.actions & GA_ACTIVATE) && w != IntuitionBase->ActiveWindow) ActivateWindow(w);
        /* 0.9.1: only when nothing came in front since the press: a popup, a requester or a drawer the click
         * opened after the release (its program had the release first) stays in front of it */
        if ((d.actions & GA_FRONT) && w->WScreen->LayerInfo.top_layer == d.top && !in_front(w) &&
            !own_requester_in_front(w))
            WindowToFront(w);
        if (d.actions & GA_BACK) WindowToBack(w);
    }
}

/* ---- resizing from an edge (0.5) ------------------------------------------------------------- */

/* The pointers shown while a window is resized: 16 x 16, the hot spot at the
 * middle (7, 7). '.' is clear, 'X' the outline (the pointer's second colour)
 * and 'o' the fill (its third). */
static const char *const shape_h[16] = {
    "................", "................", "................", "..XXX.....XXX...",
    ".XXoX.....XoXX..", "XXooX.....XooXX.", "XoooXXXXXXXoooXX", "oooooooooooooooX",
    "XoooXXXXXXXoooXX", "XXooX.....XooXX.", ".XXoX.....XoXX..", "..XXX.....XXX...",
    "................", "................", "................", "................",
};
static const char *const shape_v[16] = {
    ".....XXoXX......", "....XXoooXX.....", "...XXoooooXX....", "...XoooooooX....",
    "...XXXXoXXXX....", "......XoX.......", "......XoX.......", "......XoX.......",
    "......XoX.......", "......XoX.......", "...XXXXoXXXX....", "...XoooooooX....",
    "...XXoooooXX....", "....XXoooXX.....", ".....XXoXX......", "......XXX.......",
};
static const char *const shape_d1[16] = {                  /* the bottom right corner */
    "................", ".XXXXXXX........", ".XoooooX........", ".XooooXX........",
    ".XooooXX........", ".XoooooXX.......", ".XoXXoooXX......", ".XXXXXoooXXXXX..",
    ".....XXoooXXoX..", "......XXoooooX..", ".......XXooooX..", ".......XXooooX..",
    ".......XoooooX..", ".......XXXXXXX..", "................", "................",
};
static const char *const shape_d2[16] = {                  /* the bottom left corner */
    "................", ".......XXXXXXX..", ".......XoooooX..", ".......XXooooX..",
    ".......XXooooX..", "......XXoooooX..", ".....XXoooXXoX..", ".XXXXXoooXXXXX..",
    ".XoXXoooXX......", ".XoooooXX.......", ".XooooXX........", ".XooooXX........",
    ".XoooooX........", ".XXXXXXX........", "................", "................",
};
enum { PTR_H, PTR_V, PTR_D1, PTR_D2, PTR_COUNT };
static struct BitMap *ptr_bm[PTR_COUNT];
static Object *ptr_obj[PTR_COUNT];

static void make_pointer(int i, const char *const rows[16])
{
    struct BitMap *bm = AllocBitMap(16, 16, 2, BMF_CLEAR, NULL);
    int x, y;
    if (!bm) return;
    for (y = 0; y < 16; y++) {
        UWORD p0 = 0, p1 = 0;
        for (x = 0; x < 16; x++) {
            UWORD bit = (UWORD)(0x8000 >> x);
            if (rows[y][x] == 'X') p1 |= bit;                       /* the second colour: plane 1 only */
            else if (rows[y][x] == 'o') { p0 |= bit; p1 |= bit; }   /* the third: both planes */
        }
        *(UWORD *)(bm->Planes[0] + y * bm->BytesPerRow) = p0;
        *(UWORD *)(bm->Planes[1] + y * bm->BytesPerRow) = p1;
    }
    ptr_bm[i] = bm;
    ptr_obj[i] = NewObject(NULL, (STRPTR)"pointerclass", POINTERA_BitMap, (ULONG)bm, POINTERA_XOffset, -7, POINTERA_YOffset, -7,
                           POINTERA_WordWidth, 1, POINTERA_XResolution, POINTERXRESN_SCREENRES,
                           POINTERA_YResolution, POINTERYRESN_SCREENRESASPECT, TAG_DONE);
}

static void pointers_on(void)
{
    make_pointer(PTR_H, shape_h);
    make_pointer(PTR_V, shape_v);
    make_pointer(PTR_D1, shape_d1);
    make_pointer(PTR_D2, shape_d2);
}

static void pointers_off(void)
{
    int i;
    for (i = 0; i < PTR_COUNT; i++) {
        if (ptr_obj[i]) DisposeObject(ptr_obj[i]);
        if (ptr_bm[i]) FreeBitMap(ptr_bm[i]);
        ptr_obj[i] = NULL; ptr_bm[i] = NULL;
    }
}

static struct Window *rs_win;                   /* the window the main task is resizing */
static ULONG rs_gen, rs_gen_done;               /* its press (grab_gen), and the last press done with */
static int rs_edges, rs_ptr;
static WORD rs_l, rs_t, rs_w, rs_h, rs_x0, rs_y0;
static ULONG rs_start;                          /* when the press came (seconds), for the time limit */

/* The size outline (0.6.1): while the button is held only a rectangle is drawn
 * (two pixels, inverted, straight in the screen's bitmap, as Intuition's own
 * sizing does), and the window is sized once, when the button comes up. 0.6
 * called ChangeWindowBox for every move of the pointer: dozens of NEWSIZE and
 * REFRESHWINDOW a second to the program, each one a full redraw of the frame
 * in the look, all through the input task. A program could not keep up and
 * the whole input stream stopped behind it. */
static int ol_on;
static struct Screen *ol_scr;
static WORD ol_l, ol_t, ol_w, ol_h;

static int screen_alive(struct Screen *sc)
{
    struct Screen *x;
    ULONG lock = LockIBase(0);
    int found = 0;
    for (x = IntuitionBase->FirstScreen; x; x = x->NextScreen)
        if (x == sc) { found = 1; break; }
    UnlockIBase(lock);
    return found;
}

static void ol_xor(struct Screen *sc, WORD l, WORD t, WORD w, WORD h)
{
    struct RastPort *rp = &sc->RastPort;
    UBYTE dm = rp->DrawMode;
    WORD th = w < 6 || h < 6 ? 1 : 2;
    SetDrMd(rp, COMPLEMENT);
    RectFill(rp, l, t, l + w - 1, t + th - 1);                      /* top */
    RectFill(rp, l, t + h - th, l + w - 1, t + h - 1);              /* bottom */
    if (h > 2 * th) {
        RectFill(rp, l, t + th, l + th - 1, t + h - th - 1);        /* left */
        RectFill(rp, l + w - th, t + th, l + w - 1, t + h - th - 1); /* right */
    }
    SetDrMd(rp, dm);
}

static void ol_clear(void)
{
    if (ol_on && screen_alive(ol_scr)) ol_xor(ol_scr, ol_l, ol_t, ol_w, ol_h);
    ol_on = 0;
}

static void ol_show(struct Screen *sc, WORD l, WORD t, WORD w, WORD h)
{
    if (ol_on && ol_l == l && ol_t == t && ol_w == w && ol_h == h) return;
    ol_clear();
    ol_xor(sc, l, t, w, h);
    ol_scr = sc; ol_l = l; ol_t = t; ol_w = w; ol_h = h; ol_on = 1;
}

/* A window this drags (drag.solid) is drawn at the look's drag opacity while it moves, as Intuition's own solid drag,
 * on a screen layers.library composites (OpenLayers 47.101 and up: SetLayerLook, -258, LAYERLOOK_DRAGGED;
 * openamigagfx include/graphics/openlayers.h). Nothing on a classic screen, or with an older layers.library. */
static void drag_look(struct Window *w, ULONG on)
{
    struct Layer *ly[2];
    int i;
    if (LayersBase->lib_Version != 47 || LayersBase->lib_Revision < 101) return;
    ly[0] = w->WLayer;
    ly[1] = w->BorderRPort && w->BorderRPort->Layer != w->WLayer ? w->BorderRPort->Layer : NULL;    /* GZZ */
    for (i = 0; i < 2; i++)
        if (ly[i]) {
            /* the base through a1, A6 saved by hand (A6 is the frame pointer at -O0) */
            register struct Library *a1 __asm("a1") = LayersBase;
            register struct Layer *a0 __asm("a0") = ly[i];
            register ULONG d0 __asm("d0") = 2, d1 __asm("d1") = on;
            __asm__ volatile("move.l %%a6,-(%%sp)\n\tmove.l %%a1,%%a6\n\tjsr -258(%%a6)\n\tmove.l (%%sp)+,%%a6"
                             : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : : "cc", "memory");
        }
}

static void resize_end(void)
{
    ol_clear();
    if (rs_win && (rs_edges & EDGE_MOVE) && is_window(rs_win)) drag_look(rs_win, 0);
    if (rs_win && rs_ptr && is_window(rs_win)) SetWindowPointer(rs_win, WA_Pointer, (ULONG)NULL, TAG_DONE);
    rs_win = NULL;
    rs_ptr = 0;
}

/* The drag is over without a button-up (the release never came): the
 * outline goes and the window is left as it is. The handler always ends a
 * drag at the button-up; this is for the one that never arrives. */
#define DRAG_IDLE_SECS 8
#define DRAG_MAX_SECS 120
static void resize_watch(void)
{
    ULONG secs, micro;
    if (!rs_win || !grab_on) return;
    CurrentTime(&secs, &micro);
    if (secs - rs_start > DRAG_MAX_SECS || (secs - grab_last > DRAG_IDLE_SECS && grab_last <= secs)) {
        Forbid(); if (grab_gen == rs_gen) grab_on = 0; Permit();
        rs_gen_done = rs_gen;
        resize_end();
    }
}

/* The pointer has moved, or the button has come up. The box the window
 * would have: the edge or corner held follows the pointer, the others stay;
 * it keeps to the window's own limits (a MaxWidth of 0 or 65535 is the
 * screen's size) and to the screen. While the button is held the outline
 * shows it; at the button-up the window is sized, once. */
/* 0.8.4: the pointer over a grip of the active window shows which way it sizes. A window's own pointer can't
 * be read back, so leaving the grip gives the window the default one. */
static struct Window *ptr_win;
static int ptr_k = -1;

static void hover(void)
{
    struct Window *hw = hover_win;
    int e = hover_edges, k = -1;
    if (rs_win) return;                          /* a drag shows its own */
    if (hw && e && is_window(hw)) k = !(e & EDGE_B) ? PTR_H : (e & EDGE_R) ? PTR_D1 : (e & EDGE_L) ? PTR_D2 : PTR_V;
    if (k >= 0 && hw == ptr_win && k == ptr_k) return;
    if (ptr_win && (ptr_win != hw || k < 0) && is_window(ptr_win)) SetWindowPointer(ptr_win, TAG_DONE);
    ptr_win = NULL; ptr_k = -1;
    if (k >= 0 && ptr_obj[k]) {
        SetWindowPointer(hw, WA_Pointer, (ULONG)ptr_obj[k], TAG_DONE);
        ptr_win = hw; ptr_k = k;
    }
}

static void resize(void)
{
    for (;;) {
        struct Window *w, *gw;
        struct Screen *s;
        WORD dx, dy, l, t, wd, ht, maxw, maxh, px, py, ex, ey, gx0, gy0;
        ULONG gen, endgen;
        int gedges, ended;
        /* what the handler has, in one piece (it runs in input.device's task: Forbid keeps it out) */
        Forbid();
        gen = grab_gen; endgen = grab_end_gen; gw = grab_win; gedges = grab_edges; gx0 = grab_x0; gy0 = grab_y0;
        px = grab_px; py = grab_py; ex = grab_ex; ey = grab_ey;
        Permit();
        if (!rs_win) {                              /* a new press on an edge, or a title bar to drag */
            if (!gw || gen == rs_gen_done) return;
            w = gw;
            rs_gen = gen;
            if (!is_window(w)) { rs_gen_done = gen; Forbid(); if (grab_gen == gen) grab_on = 0; Permit(); return; }
            rs_win = w; rs_edges = gedges; rs_x0 = gx0; rs_y0 = gy0;
            rs_l = w->LeftEdge; rs_t = w->TopEdge; rs_w = w->Width; rs_h = w->Height;
            { ULONG micro; CurrentTime(&rs_start, &micro); }
            if (w != IntuitionBase->ActiveWindow) ActivateWindow(w);
            if (rs_edges & EDGE_MOVE) drag_look(w, 1);
            if (!(rs_edges & EDGE_MOVE)) {          /* a solid move keeps the window's own pointer */
                int k = !(rs_edges & EDGE_B) ? PTR_H : (rs_edges & EDGE_R) ? PTR_D1 : (rs_edges & EDGE_L) ? PTR_D2 : PTR_V;
                /* only when asked for: the window's own pointer can't be read, so
                 * the end of the drag gives it the default one */
                if (cfg.edge_ptr && ptr_obj[k]) { SetWindowPointer(w, WA_Pointer, (ULONG)ptr_obj[k], TAG_DONE); rs_ptr = 1; }
            }
        }
        w = rs_win;
        if (!is_window(w)) {
            ol_on = 0; rs_win = NULL; rs_ptr = 0; rs_gen_done = rs_gen;
            Forbid(); if (grab_gen == rs_gen) grab_on = 0; Permit();
            continue;
        }
        /* 0.9: the place the handler saw with the drag's last event, or at its release: never one made after it */
        ended = endgen == rs_gen || gen != rs_gen;
        if (endgen == rs_gen) { px = ex; py = ey; }
        s = w->WScreen;
        dx = px - rs_x0;
        dy = py - rs_y0;
        if (rs_edges & EDGE_MOVE) {                 /* 0.8: the window itself follows the pointer, contents and all */
            l = rs_l + dx; t = rs_t + dy;
            if (l < 0) l = 0;
            if (t < 0) t = 0;
            if (l + w->Width > s->Width) l = s->Width - w->Width;
            if (t + w->Height > s->Height) t = s->Height - w->Height;
            if (l != w->LeftEdge || t != w->TopEdge) MoveWindow(w, l - w->LeftEdge, t - w->TopEdge);
            if (ended) {
                rs_gen_done = rs_gen;
                resize_end();
                if (cfg.halves && (px <= 1 || px >= s->Width - 2)) halves(w, px);
                else if (cfg.snap) { Delay(1); if (is_window(w)) snap(w); }   /* after the last move has been done */
                continue;                           /* a press that came after the release: its drag now */
            }
            return;
        }
        l = rs_l; t = rs_t; wd = rs_w; ht = rs_h;
        maxw = w->MaxWidth == 0 || w->MaxWidth > (UWORD)s->Width ? s->Width : (WORD)w->MaxWidth;
        maxh = w->MaxHeight == 0 || w->MaxHeight > (UWORD)s->Height ? s->Height : (WORD)w->MaxHeight;
        if (rs_edges & EDGE_R) wd += dx;
        if (rs_edges & EDGE_L) wd -= dx;
        if (rs_edges & EDGE_B) ht += dy;
        if (wd < w->MinWidth) wd = w->MinWidth;
        if (wd > maxw) wd = maxw;
        if (ht < w->MinHeight) ht = w->MinHeight;
        if (ht > maxh) ht = maxh;
        if (rs_edges & EDGE_L) l = rs_l + rs_w - wd;          /* the right edge stays where it was */
        if (l < 0) { wd += l; l = 0; }
        if (l + wd > s->Width) wd = s->Width - l;
        if (t + ht > s->Height) ht = s->Height - t;
        if (wd < w->MinWidth || ht < w->MinHeight) { wd = w->Width; ht = w->Height; l = w->LeftEdge; }   /* nothing sensible: no change */
        if (ended) {
            rs_gen_done = rs_gen;
            ol_clear();                                          /* the outline goes first, then the one size */
            if (wd != w->Width || ht != w->Height || l != w->LeftEdge)
                ChangeWindowBox(w, l, t, wd, ht);
            resize_end();
            continue;
        }
        ol_show(s, l, t, wd, ht);
        return;
    }
}

/* A window Amiga+Tab offers: not a backdrop, not tiny, and not Workbench's
   own root window (a Workbench window with no close gadget). */
static int switchable(struct Window *w)
{
    if (!w || (w->Flags & WFLG_BACKDROP) || w->Width < 8 || w->Height < 8) return 0;
    if ((w->Flags & WFLG_WBENCHWINDOW) && !(w->Flags & WFLG_CLOSEGADGET)) return 0;
    return 1;
}

/* Amiga+Tab: the screen's windows from front to back, by their layers. */
static void switcher(int back)
{
    struct Screen *s = IntuitionBase->FirstScreen;
    struct Layer *l;
    struct Window *front = NULL, *second = NULL, *rear = NULL;
    if (!s) return;
    LockLayerInfo(&s->LayerInfo);
    for (l = s->LayerInfo.top_layer; l; l = l->back) {
        struct Window *w = (struct Window *)l->Window;
        if (!switchable(w)) continue;
        if (!front) front = w;
        else if (!second) second = w;
        rear = w;
    }
    UnlockLayerInfo(&s->LayerInfo);
    if (!front || front == rear) { if (front) ActivateWindow(front); return; }
    if (back) {                       /* the front one to the back; the one behind it comes up */
        WindowToBack(front);
        ActivateWindow(second);
    } else {                          /* the one at the back to the front */
        WindowToFront(rear);
        ActivateWindow(rear);
    }
}

/* ---- places --------------------------------------------------------------------------------- */

/* The kept places (places.h: keys, the placing rule, the tags). Read by the patch on other tasks: 0.9.2 reads
 * them under Forbid(), and this task changes them only under Forbid() (a few bytes copied, nothing that can
 * wait), so the patch never waits and never misses them. 0.7 to 0.9.1 read them with AttemptSemaphoreShared
 * and opened a window as asked whenever the look held them, which it did across its moves and file writes. */
static struct place places[PLACES_MAX];
static int nplaces, places_dirty;
static ULONG places_clock;                      /* each use: a place's "used" (the file's order: used longest ago first) */
static volatile ULONG looks;                    /* the once-a-second looks so far */
/* every window the look knows: its box at the last look, the key it is known by, fixed when it was first
 * seen (0.9.2: the title it opened with, when the patch saw it open; a title changed later keeps the key),
 * and the key its title gave at the last look (cur) */
static struct { struct Window *w; WORD l, t, wd, ht; char key[64], cur[64]; } seen[MAX_SEEN];
static int nseen;

static void place_log(const char *line)
{
    BPTR fh;
    if (!log_path[0] || !(fh = Open((STRPTR)log_path, MODE_READWRITE))) return;
    Seek(fh, 0, OFFSET_END);
    FPuts(fh, (STRPTR)line);
    Close(fh);
}

/* The program behind a task: its name, or the command a Shell runs. NULL: a
   window with no IDCMP (a console, say), known by its title as "window". */
static void program_of_task(struct Task *t, char *out, int size)
{
    *out = 0;
    if (!t) { strncpy(out, "window", size - 1); return; }
    Forbid();
    if (t->tc_Node.ln_Type == NT_PROCESS && ((struct Process *)t)->pr_CLI) {
        struct CommandLineInterface *cli = BADDR(((struct Process *)t)->pr_CLI);
        UBYTE *bs = cli ? BADDR(cli->cli_CommandName) : NULL;
        if (bs && *bs) {
            int n = *bs < size - 1 ? *bs : size - 1;
            const char *base;
            memcpy(out, bs + 1, n); out[n] = 0;
            base = strrchr(out, '/'); if (!base) base = strchr(out, ':');
            if (base) memmove(out, base + 1, strlen(base + 1) + 1);
        }
    }
    if (!*out && t->tc_Node.ln_Name) { strncpy(out, t->tc_Node.ln_Name, size - 1); out[size - 1] = 0; }
    Permit();
}

/* The program behind a window: the task its IDCMP port signals. */
static void program_of(struct Window *w, char *out, int size)
{
    program_of_task(w->UserPort ? (struct Task *)w->UserPort->mp_SigTask : NULL, out, size);
}

static int never(const char *prog)
{
    int i;
    for (i = 0; i < cfg.nnever; i++)
        if (!stricmp(cfg.never[i], prog)) return 1;
    return 0;
}

static void read_places(void)
{
    char *text = read_file(PLACES_ENV), *p;
    nplaces = 0;
    if (!text) text = read_file(PLACES_ENVARC);
    if (!text) return;
    for (p = text; *p && nplaces < PLACES_MAX; ) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (place_parse(line, &places[nplaces])) { places[nplaces].used = nplaces; nplaces++; }
    }
    places_clock = nplaces;                     /* a place used from now on is newer than every one read */
    FreeVec(text);
}

static void write_places(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    int order[PLACES_MAX], i;
    if (!fh) {
        BPTR d = CreateDir((STRPTR)(path[0] == 'E' && path[3] == ':' ? "ENV:OpenPrefs" : "ENVARC:OpenPrefs"));
        if (d) UnLock(d);
        if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return;
    }
    FPuts(fh, (STRPTR)"; OpenWindows: where each program's windows were last left\n");
    place_order(places, nplaces, order);        /* 0.9.2: the one used longest ago first */
    for (i = 0; i < nplaces; i++) {
        const struct place *pl = &places[order[i]];
        FPrintf(fh, (STRPTR)"\"%s\" %ld %ld %ld %ld\n", (LONG)pl->key, (LONG)pl->l, (LONG)pl->t, (LONG)pl->w, (LONG)pl->h);
    }
    Close(fh);
}

/* A window whose place is kept: a program's (not one never to be touched),
   or a Workbench drawer when "places.drawers" is on (Workbench's own windows
   otherwise keep to its Snapshot; 0.9.2: OpenBench's are Workbench's too) */
static int placeable(int wb, int drawer, const char *prog)
{
    if (!prog[0] || never(prog)) return 0;
    if (!wb) return 1;
    return drawer && cfg.places_wb;
}

/* ---- placed before they open (0.7; 0.9.2) ----------------------------------------------------- */

/* Up to 0.6 a new window was moved to its place at the next once-a-second
 * look, so it opened where it asked and then jumped. From 0.7 OpenWindowTagList
 * and OpenWindow are patched, as OpenLook patches them and stacked with it
 * whichever loads first: a window whose place is kept opens there.
 *
 * 0.9.2 (the user, 24 October 2026: "make sure it reads the saved position before it renders the window.
 * Right now it jumps from one location to another"): the program's own WA_Left and WA_Top came after ours,
 * and OpenIntuition reads a tag's last (the open ROM): every window that gave its corner in its tags (the
 * Shell, OpenBench's drawers, most programs) opened where it asked and the look moved it a second later.
 * Now the window gets a copy of the program's tags without its corner (and without its size when the size
 * is ours) behind ours, and a copy of its NewWindow and ExtNewWindow tags the same way: the program's own are
 * left as they are, and ours are the only ones, read first or last. A window asked for by its inner size
 * (the Shell's CON: window from guest #472, ReAction's) is sized too, by its whole kept size. The look moves
 * nothing any more: a window the patch didn't see open stays where it opened.
 *
 * The patch runs on the opening program's task. It never waits: the places are read under Forbid(). It
 * places nothing while the commodity is disabled, for OpenWindows' own windows, or once it has been told
 * to pass everything through (quitting while another program has patched on top of it). */

static APTR old_owtl, old_ow;           /* what the patch replaced */
/* 0.8.4: windows the cog placed: the look leaves them where they opened (it moved them on to their kept place
 * a second later, the window jumping). Written by the patch on the opening task, read by the look. */
#define COG_WINS 4
static struct Window *volatile cog_wins[COG_WINS];
static volatile UBYTE cog_next;
static volatile UWORD patch_in;         /* callers inside the patch now */
static volatile UBYTE patch_pass;       /* 1: place nothing */
/* 0.9.2: the windows the patch saw open, for the look: the title each opened with (the key it is known by,
 * whatever its program calls it later), whether it opened at its kept place, and the look it opened in.
 * Written by the patch on the opening task, read and cleared by the look, both under Forbid(). */
#define OPENED 16
static struct { struct Window *w; char title[40]; UBYTE placed; ULONG look; } opened[OPENED];
static UBYTE opened_next;

static struct TagItem *tag_of(struct TagItem *tags, struct TagItem *ext, ULONG tag)
{
    struct TagItem *ti = tags ? FindTagItem(tag, tags) : NULL;
    if (!ti && ext) ti = FindTagItem(tag, ext);
    return ti;
}

static ULONG tag_or(struct TagItem *tags, struct TagItem *ext, ULONG tag, ULONG dflt)
{
    struct TagItem *ti = tag_of(tags, ext, tag);
    return ti ? ti->ti_Data : dflt;
}

/* The width and height of the screen a window is about to open on; 0 0 when
   it can't be told (then the window opens as asked). */
static void opening_screen(struct NewWindow *nw, struct TagItem *tags, struct TagItem *ext, WORD *sw, WORD *sh)
{
    struct Screen *s = (struct Screen *)tag_or(tags, ext, WA_CustomScreen, 0), *locked = NULL;
    *sw = *sh = 0;
    if (!s) s = (struct Screen *)tag_or(tags, ext, WA_PubScreen, 0);
    if (!s && nw && (nw->Type & SCREENTYPE) == CUSTOMSCREEN) s = nw->Screen;
    if (!s) {
        STRPTR name = (STRPTR)tag_or(tags, ext, WA_PubScreenName, 0);
        locked = LockPubScreen(name);           /* NULL: the default public screen */
        s = locked;
    }
    if (s) { *sw = s->Width; *sh = s->Height; }
    if (locked) UnlockPubScreen(NULL, locked);
}

/* The kept place for a window this program is opening, keyed as the look keys
   it; 1 when there is one. A window opened without IDCMP is keyed later by the
   task whose port the program attaches (Workbench and many programs share one
   port that way), or as "window" when it never gets one: both are tried. */
static int place_for(const char *title, int has_idcmp, int wbflag, struct place *pl)
{
    char prog[32], key[64];
    int pass, found = 0;
    for (pass = 0; pass < 2 && !found; pass++) {
        int wb, drawer, i;
        if (pass == 1 && has_idcmp) break;
        program_of_task(pass == 0 ? FindTask(NULL) : NULL, prog, sizeof prog);
        wb = place_workbench(prog, wbflag);
        drawer = wb && title && strncmp(title, "Workbench", 9);
        if (!placeable(wb, drawer, prog)) continue;
        place_key(title, prog, key, sizeof key);
        Forbid();                               /* the look changes them only under Forbid() too */
        if ((i = place_index(places, &nplaces, key, 0)) >= 0) { *pl = places[i]; found = 1; }
        Permit();
    }
    return found;
}

/* The program's tags and NewWindow with the plan's corner (and size), in copies: the program's own stay as
 * they are. 1 when made; 0 (no memory, a list too long) and the window opens as asked. */
static int with_plan(struct NewWindow *nw, struct TagItem *tags, struct TagItem *ext, const struct place_plan *p,
                     struct ExtNewWindow *copy, struct NewWindow **pass_nw, struct TagItem **use,
                     struct TagItem **buf, struct TagItem **extbuf)
{
    int n = place_tags_count(tags), m = ext ? place_tags_count(ext) : 0;
    if (n < 0 || m < 0) return 0;
    if (!(*buf = AllocVec((n + 6) * sizeof(struct TagItem), MEMF_ANY))) return 0;
    if (place_tags(*buf, n + 6, p, tags, 1) < 0) goto fail;
    if (nw) {
        memcpy(copy, nw, (nw->Flags & WFLG_NW_EXTENDED) ? sizeof(struct ExtNewWindow) : sizeof(struct NewWindow));
        copy->LeftEdge = p->l; copy->TopEdge = p->t;
        if (p->size) { copy->Width = p->w; copy->Height = p->h; }
        if (ext) {
            if (!(*extbuf = AllocVec((m + 1) * sizeof(struct TagItem), MEMF_ANY))) goto fail;
            if (place_tags(*extbuf, m + 1, p, ext, 0) < 0) goto fail;
            copy->Extension = *extbuf;
        }
        *pass_nw = (struct NewWindow *)copy;
    }
    *use = *buf;
    return 1;
fail:
    if (*buf) FreeVec(*buf);
    if (*extbuf) FreeVec(*extbuf);
    *buf = *extbuf = NULL;
    return 0;
}

/* OpenWindowTagList(nw, tags) with the window's kept place, when it has one. */
static struct Window *__attribute__((used)) ow_open_window(struct NewWindow *nw, struct TagItem *tags, APTR fn)
{
    struct TagItem *use = tags, *ext = NULL, *buf = NULL, *extbuf = NULL;
    struct ExtNewWindow copy;
    struct NewWindow *pass_nw = nw;
    struct Window *win;
    const char *title = NULL;
    int cogged = 0, saw = 0, placed = 0;
    patch_in++;
    if (nw && (nw->Flags & WFLG_NW_EXTENDED)) ext = ((struct ExtNewWindow *)nw)->Extension;
    /* 0.8.1: a window the cog asked for: at the screen's top right, COGPLACE_GAP from its top and right */
    if (!patch_pass && FindTask(NULL) != me && cog.armed) {
        ULONG flags = 0, secs, micros;
        int take = 0;
        flags = tag_or(tags, ext, WA_Flags, nw ? nw->Flags : 0);
        if (tag_or(tags, ext, WA_DragBar, 0)) flags |= WFLG_DRAGBAR;
        if (tag_or(tags, ext, WA_Backdrop, 0)) flags |= WFLG_BACKDROP;
        CurrentTime(&secs, &micros);
        Forbid();
        if (cog.armed && FindTask(NULL) != cog.from && (flags & WFLG_DRAGBAR) && !(flags & WFLG_BACKDROP)) {
            if (secs <= cog.until) take = 1;
            cog.armed = 0;
        }
        Permit();
        if (take) {
            WORD sw, sh, wd;
            struct Screen *ps = (struct Screen *)tag_or(tags, ext, WA_CustomScreen, 0);
            opening_screen(nw, tags, ext, &sw, &sh);
            if (!ps) ps = IntuitionBase->FirstScreen;
            wd = (WORD)tag_or(tags, ext, WA_Width, nw ? (ULONG)(LONG)nw->Width : 0);
            if (tag_or(tags, ext, WA_InnerWidth, 0))
                wd = (WORD)tag_or(tags, ext, WA_InnerWidth, 0) + (ps ? ps->WBorLeft + ps->WBorRight : 8) +
                     ((flags & WFLG_SIZEGADGET) || tag_or(tags, ext, WA_SizeGadget, 0) ? 14 : 0);
            if (sw > 0 && wd > 0) {
                struct place_plan p;
                p.l = sw - wd - COGPLACE_GAP; p.t = COGPLACE_GAP; p.w = p.h = 0; p.size = 0;
                if (p.l < 0) p.l = 0;
                cogged = with_plan(nw, tags, ext, &p, &copy, &pass_nw, &use, &buf, &extbuf);
            }
        }
    }
    if (!cogged && !patch_pass && on && cfg.places && FindTask(NULL) != me) {
        ULONG flags, idcmp;
        struct place pl;
        int found = 0;
        flags = tag_or(tags, ext, WA_Flags, nw ? nw->Flags : 0);
        if (tag_or(tags, ext, WA_Backdrop, 0)) flags |= WFLG_BACKDROP;
        if (tag_or(tags, ext, WA_GimmeZeroZero, 0)) flags |= WFLG_GIMMEZEROZERO;
        if (tag_or(tags, ext, WA_SizeGadget, 0)) flags |= WFLG_SIZEGADGET;
        if (tag_or(tags, ext, WA_DragBar, 0)) flags |= WFLG_DRAGBAR;
        title = (const char *)tag_or(tags, ext, WA_Title, nw ? (ULONG)nw->Title : 0);
        idcmp = tag_or(tags, ext, WA_IDCMP, nw ? nw->IDCMPFlags : 0);
        /* the same windows the look keeps: not backdrops, not GZZ, a drag bar or a title */
        if (place_candidate((flags & (WFLG_BACKDROP | WFLG_GIMMEZEROZERO)) != 0, (flags & WFLG_DRAGBAR) != 0, title)) {
            saw = 1;
            found = place_for(title, idcmp != 0, (flags & WFLG_WBENCHWINDOW) != 0, &pl);
        }
        if (found) {
            struct place_ask a;
            struct place_plan p;
            WORD sw, sh;
            a.w = (WORD)tag_or(tags, ext, WA_Width, nw ? (ULONG)(LONG)nw->Width : 0);
            a.h = (WORD)tag_or(tags, ext, WA_Height, nw ? (ULONG)(LONG)nw->Height : 0);
            a.inner = tag_of(tags, ext, WA_InnerWidth) || tag_of(tags, ext, WA_InnerHeight);
            a.sizeable = (flags & WFLG_SIZEGADGET) != 0;
            a.minw = (WORD)tag_or(tags, ext, WA_MinWidth, nw ? (ULONG)(LONG)nw->MinWidth : 0);
            a.minh = (WORD)tag_or(tags, ext, WA_MinHeight, nw ? (ULONG)(LONG)nw->MinHeight : 0);
            a.maxw = (WORD)tag_or(tags, ext, WA_MaxWidth, nw ? (ULONG)nw->MaxWidth : 0);
            a.maxh = (WORD)tag_or(tags, ext, WA_MaxHeight, nw ? (ULONG)nw->MaxHeight : 0);
            opening_screen(nw, tags, ext, &sw, &sh);
            if (sw > 0 && sh > 0) {
                place_plan(&pl, &a, sw, sh, &p);
                placed = with_plan(nw, tags, ext, &p, &copy, &pass_nw, &use, &buf, &extbuf);
            }
        }
    }
    {
        /* the registers are set here, after every other call */
        register struct Window *res __asm("d0");
        register struct NewWindow *a0 __asm("a0") = pass_nw;
        register struct TagItem *a1 __asm("a1") = use;
        register APTR a6 __asm("a6") = IntuitionBase;
        __asm volatile ("jsr (%4)" : "=r"(res), "+r"(a0), "+r"(a1), "+r"(a6) : "a"(fn) : "d1", "memory", "cc");
        win = res;
    }
    if (win && (cogged || saw)) {
        int k = 0;
        Forbid();
        if (cogged) {
            cog_wins[cog_next] = win;
            cog_next = (UBYTE)((cog_next + 1) % COG_WINS);
        } else {
            opened[opened_next].w = win;
            if (title) while (title[k] && k < (int)sizeof opened[0].title - 1) { opened[opened_next].title[k] = title[k]; k++; }
            opened[opened_next].title[k] = 0;
            opened[opened_next].placed = (UBYTE)placed;
            opened[opened_next].look = looks;
            opened_next = (UBYTE)((opened_next + 1) % OPENED);
        }
        Permit();
    }
    if (buf) FreeVec(buf);
    if (extbuf) FreeVec(extbuf);
    patch_in--;
    return win;
}

__asm(
"_ow_owtl:\n"
"   movem.l d2-d7/a2-a6,-(sp)\n"
"   move.l _old_owtl,-(sp)\n move.l a1,-(sp)\n move.l a0,-(sp)\n"
"   jsr _ow_open_window\n"
"   lea 12(sp),sp\n"
"   movem.l (sp)+,d2-d7/a2-a6\n"
"   rts\n"
/* OpenWindow(nw a0): as OpenWindowTagList(nw, NULL), through what OpenWindowTagList was */
"_ow_ow:\n"
"   movem.l d2-d7/a2-a6,-(sp)\n"
"   move.l _old_owtl,-(sp)\n clr.l -(sp)\n move.l a0,-(sp)\n"
"   jsr _ow_open_window\n"
"   lea 12(sp),sp\n"
"   movem.l (sp)+,d2-d7/a2-a6\n"
"   rts\n");
void ow_owtl(void), ow_ow(void);

static void places_patch_on(void)
{
    patch_pass = 0;                         /* also after a switch-off that couldn't take the calls back */
    if (old_owtl) return;
    Forbid();
    old_owtl = SetFunction((struct Library *)IntuitionBase, -606, (APTR)ow_owtl);
    old_ow = SetFunction((struct Library *)IntuitionBase, -204, (APTR)ow_ow);
    Permit();
}

/* 1 when Intuition has its own calls back and nobody is still inside ours; 0
   when another program patched on top (ours must stay, passing through). */
static int places_patch_off(void)
{
    int ok = 1, i;
    if (!old_owtl) return 1;
    Forbid();
    if (SetFunction((struct Library *)IntuitionBase, -606, old_owtl) != (APTR)ow_owtl) {
        SetFunction((struct Library *)IntuitionBase, -606, (APTR)ow_owtl);
        ok = 0;
    } else if (SetFunction((struct Library *)IntuitionBase, -204, old_ow) != (APTR)ow_ow) {
        SetFunction((struct Library *)IntuitionBase, -204, (APTR)ow_ow);
        SetFunction((struct Library *)IntuitionBase, -606, (APTR)ow_owtl);
        ok = 0;
    }
    Permit();
    if (!ok) { patch_pass = 1; return 0; }
    old_owtl = old_ow = NULL;
    for (i = 0; i < 250 && patch_in; i++) Delay(1);      /* a window opening now finishes in our code */
    return patch_in == 0;
}

/* ---- drive windows: the name only (0.6) --------------------------------------------------------- */

/* Workbench titles a drive's window "Work  70% full, 453MB free, 62.0MB in use"
 * (workbench.library's format; the two spaces, the percentage and its
 * percent sign are the part to find). Cut in place, at the first of the two
 * spaces, in the string Workbench made: its memory is its own and stays so.
 * 1 when something was cut. */
static int cut_usage(char *t)
{
    char *p;
    for (p = t; *p; p++) {
        char *q = p + 2;
        if (p[0] != ' ' || p[1] != ' ' || *q < '0' || *q > '9') continue;
        while (*q >= '0' && *q <= '9') q++;
        if (*q == '%' && p > t) { *p = 0; return 1; }
    }
    return 0;
}

static void drive_titles(void)
{
    struct Window *fix[8], *w;
    struct Screen *s;
    int n = 0, i;
    ULONG lock = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)
        for (w = s->FirstWindow; w && n < 8; w = w->NextWindow)
            if ((w->Flags & WFLG_WBENCHWINDOW) && w->Title && cut_usage((char *)w->Title)) fix[n++] = w;
    UnlockIBase(lock);
    for (i = 0; i < n; i++)
        if (is_window(fix[i])) SetWindowTitles(fix[i], fix[i]->Title, (STRPTR)-1);      /* the same string: drawn again */
}

/* Once a second: every window's place is noted, and kept when it was moved or sized; a Workbench drawer
   is made at least the drawer size. 0.9.2: nothing is moved to its place here any more: the patch places a
   window before it opens, and a window it didn't see open (a system requester Intuition opens itself, one
   opened before OpenWindows started) stays where it opened. ENV:OpenWindowsLog, when set, names a file
   that gets a line for each new window and each place kept. */
static void watch(void)
{
    /* static: Run gives a commodity a 4 KB stack, and this is 12 KB */
    static struct { struct Window *w; WORD l, t, wd, ht, sw, sh; ULONG flags; char prog[32]; char key[64], cur[64]; int wb, drawer, keep; } now[MAX_SEEN];
    char line[200];
    int n = 0, i, j, k, busy;
    struct Screen *s;
    struct Window *w;
    ULONG lock;
    if (cfg.drive_title) drive_titles();
    if (!cfg.places && !cfg.drawer_w) return;
    busy = patch_in != 0;                       /* a window opening through the patch now: new ones wait a look */
    lock = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)
        for (w = s->FirstWindow; w && n < MAX_SEEN; w = w->NextWindow) {
            if (!place_candidate((w->Flags & (WFLG_BACKDROP | WFLG_GIMMEZEROZERO)) != 0, (w->Flags & WFLG_DRAGBAR) != 0, (const char *)w->Title))
                continue;
            now[n].w = w; now[n].l = w->LeftEdge; now[n].t = w->TopEdge; now[n].wd = w->Width; now[n].ht = w->Height;
            now[n].sw = s->Width; now[n].sh = s->Height; now[n].flags = w->Flags;
            {   /* the program's name while the window is sure to be there; the key from a copy
                   of it (GCC 16 can't tell now[n].prog from now[n].key: -Wrestrict) */
                char prog[sizeof now[0].prog];
                program_of(w, prog, sizeof prog);
                memcpy(now[n].prog, prog, sizeof prog);
                now[n].wb = place_workbench(prog, (w->Flags & WFLG_WBENCHWINDOW) != 0);
                /* a Workbench drawer, not its root window, takes a place when the switch says */
                now[n].drawer = now[n].wb && w->Title && strncmp((const char *)w->Title, "Workbench", 9);
                place_key((const char *)w->Title, prog, now[n].key, sizeof now[n].key);
                memcpy(now[n].cur, now[n].key, sizeof now[n].cur);
            }
            n++;
        }
    UnlockIBase(lock);
    looks++;
    for (i = 0; i < n; i++) {
        int known = -1;
        now[i].keep = 1;
        for (j = 0; j < nseen; j++) if (seen[j].w == now[i].w) { known = j; break; }
        if (known >= 0) {
            /* another window in the memory of one that closed since the last look: the patch noted the new one
             * opening; or, one it didn't see, its title and its box both changed at once */
            int moved = seen[known].l != now[i].l || seen[known].t != now[i].t || seen[known].wd != now[i].wd || seen[known].ht != now[i].ht;
            Forbid();
            for (k = 0; k < OPENED; k++) if (opened[k].w == now[i].w) known = -1;
            Permit();
            if (known >= 0 && moved && strcmp(seen[known].cur, now[i].cur)) known = -1;
        }
        if (known >= 0) {
            memcpy(now[i].key, seen[known].key, sizeof now[i].key);     /* the key it was first known by */
            if (cfg.places && placeable(now[i].wb, now[i].drawer, now[i].prog) &&
                (seen[known].l != now[i].l || seen[known].t != now[i].t || seen[known].wd != now[i].wd || seen[known].ht != now[i].ht)) {
                /* the user moved or sized it: remember that */
                int p;
                Forbid();
                p = place_index(places, &nplaces, now[i].key, 1);
                places[p].l = now[i].l; places[p].t = now[i].t; places[p].w = now[i].wd; places[p].h = now[i].ht;
                places[p].used = ++places_clock;
                Permit();
                places_dirty = 1;
                snprintf(line, sizeof line, "look %lu: kept \"%s\" %d %d %d %d\n", (unsigned long)looks, now[i].key,
                         now[i].l, now[i].t, now[i].wd, now[i].ht);
                place_log(line);
            }
        } else {
            /* a new window (the first look only notes the windows already open) */
            int rec = -1, cogged = 0, placed = 0, p = -1;
            char title[sizeof opened[0].title];
            if (busy && looks > 1) { now[i].keep = 0; continue; }   /* the patch may not have noted it yet */
            Forbid();
            for (k = 0; k < COG_WINS; k++)
                if (cog_wins[k] == now[i].w) { cog_wins[k] = NULL; cogged = 1; }
            for (k = 1; k <= OPENED && rec < 0; k++) {               /* the newest first: a window's memory used again */
                int x = (opened_next + OPENED - k) % OPENED;
                if (opened[x].w == now[i].w) rec = x;
            }
            if (rec >= 0) {
                memcpy(title, opened[rec].title, sizeof title);
                placed = opened[rec].placed;
                opened[rec].w = NULL;
            }
            Permit();
            if (rec >= 0) place_key(title, now[i].prog, now[i].key, sizeof now[i].key);   /* the title it opened with */
            if (looks > 1 && cfg.places && placeable(now[i].wb, now[i].drawer, now[i].prog) &&
                (p = place_index(places, &nplaces, now[i].key, 0)) >= 0) {
                Forbid();
                places[p].used = ++places_clock;
                Permit();
            }
            if (looks > 1 && log_path[0]) {
                int mine = cfg.places && placeable(now[i].wb, now[i].drawer, now[i].prog);
                snprintf(line, sizeof line, "look %lu: new \"%s\" at %d %d %d %d, %s%s\n", (unsigned long)looks, now[i].key,
                         now[i].l, now[i].t, now[i].wd, now[i].ht,
                         cogged ? "placed by the cog" : !mine ? (now[i].wb ? "a Workbench window: Workbench's" : "not one OpenWindows keeps") :
                         rec < 0 ? "not seen opening, left where it opened" :
                         placed ? "placed before it opened" : p >= 0 ? "opened as asked" : "no place kept",
                         p >= 0 && (places[p].l != now[i].l || places[p].t != now[i].t) ? " (not at its kept place)" : "");
                place_log(line);
            }
            if (looks > 1 && !cogged && p < 0 && now[i].wb && cfg.drawer_w && (now[i].flags & WFLG_SIZEGADGET) &&
                now[i].prog[0] && !stricmp(now[i].prog, "Workbench")) {
                /* a Workbench drawer with no place of its own: at least the drawer size */
                WORD wd = now[i].wd < cfg.drawer_w ? cfg.drawer_w : now[i].wd, ht = now[i].ht < cfg.drawer_h ? cfg.drawer_h : now[i].ht;
                WORD l = now[i].l, t = now[i].t;
                if (wd > now[i].sw) wd = now[i].sw;
                if (ht > now[i].sh) ht = now[i].sh;
                if (l + wd > now[i].sw) l = now[i].sw - wd;
                if (t + ht > now[i].sh) t = now[i].sh - ht;
                if (wd != now[i].wd || ht != now[i].ht) {
                    if (is_window(now[i].w)) ChangeWindowBox(now[i].w, l, t, wd, ht);
                    now[i].l = l; now[i].t = t; now[i].wd = wd; now[i].ht = ht;
                }
            }
        }
    }
    /* what the patch noted for windows that closed before a look saw them */
    Forbid();
    for (k = 0; k < OPENED; k++)
        if (opened[k].w && opened[k].look + 2 < looks) opened[k].w = NULL;
    Permit();
    nseen = 0;
    for (i = 0; i < n; i++) {
        if (!now[i].keep) continue;
        seen[nseen].w = now[i].w; seen[nseen].l = now[i].l; seen[nseen].t = now[i].t; seen[nseen].wd = now[i].wd; seen[nseen].ht = now[i].ht;
        memcpy(seen[nseen].key, now[i].key, sizeof seen[0].key);
        memcpy(seen[nseen].cur, now[i].cur, sizeof seen[0].cur);
        nseen++;
    }
    if (places_dirty && looks % 2 == 0) write_places(PLACES_ENV);
    if (places_dirty && looks % 10 == 0) { write_places(PLACES_ENV); write_places(PLACES_ENVARC); places_dirty = 0; }
}

/* ---- the commodity --------------------------------------------------------------------------- */

static struct NewBroker nb = {
    NB_VERSION, (STRPTR)"OpenWindows", (STRPTR)"OpenWindows 0.9.2", (STRPTR)"Snapping, edges, Amiga+Tab, wheel and window places",
    NBU_UNIQUE | NBU_NOTIFY, 0, 0, NULL, 0
};
static CxObj *broker, *hot_fwd, *hot_back;
#define EV_FWD 1
#define EV_BACK 2

static CxObj *hotkey(const char *desc, struct MsgPort *port, LONG id)
{
    CxObj *f = CxFilter((STRPTR)desc), *snd, *tr;
    if (!f) return NULL;
    snd = CxSender(port, id);
    tr = CxTranslate(NULL);
    AttachCxObj(f, snd);
    AttachCxObj(f, tr);
    if (CxObjError(f)) { DeleteCxObjAll(f); return NULL; }
    AttachCxObj(broker, f);
    return f;
}

static void hotkeys(struct MsgPort *port)
{
    char back[64];
    const char *last;
    if (hot_fwd) { DeleteCxObjAll(hot_fwd); hot_fwd = NULL; }
    if (hot_back) { DeleteCxObjAll(hot_back); hot_back = NULL; }
    if (!cfg.switcher) return;
    hot_fwd = hotkey(cfg.key, port, EV_FWD);
    last = strrchr(cfg.key, ' ');
    if (last) snprintf(back, sizeof back, "%.*s shift%s", (int)(last - cfg.key), cfg.key, last);
    else snprintf(back, sizeof back, "shift %s", cfg.key);
    hot_back = hotkey(back, port, EV_BACK);
}

int main(void)
{
    struct MsgPort *port = NULL, *tport = NULL;
    struct timerequest *tr = NULL;
    CxObj *cust;
    LONG bsig = -1, wsig = -1, gsig = -1;
    int quit = 0, timer = 0;
    (void)version;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    CxBase = OpenLibrary((STRPTR)"commodities.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    if (!IntuitionBase || !LayersBase || !CxBase || !GfxBase) goto out;
    me = FindTask(NULL);
    if ((bsig = AllocSignal(-1)) < 0 || (wsig = AllocSignal(-1)) < 0 || (gsig = AllocSignal(-1)) < 0) goto out;
    sig_button = 1UL << bsig; sig_wheel = 1UL << wsig; sig_grab = 1UL << gsig;
    { BYTE hsig = AllocSignal(-1); if (hsig >= 0) sig_hover = 1UL << hsig; }
    pointers_on();
    if (!(port = CreateMsgPort())) goto out;
    nb.nb_Port = port;
    if (!(broker = CxBroker(&nb, NULL))) goto out;    /* already running: NBU_UNIQUE */
    port->mp_Node.ln_Name = (char *)"OpenWindows";    /* OpenPrefs Windows finds this task by it */
    port->mp_Node.ln_Pri = 0;
    AddPort(port);
    read_settings();
    read_places();
    taller_on();                                       /* 0.8.2: taller title bars from here on */
    places_patch_on();                                 /* 0.8.1: always: the cog's placing goes through it too */
    cog.sem.ss_Link.ln_Name = (char *)COGPLACE_NAME;
    cog.sem.ss_Link.ln_Pri = 0;
    AddSemaphore(&cog.sem);
    cust = CxCustom(custom, 0);
    if (!cust) goto out;
    AttachCxObj(broker, cust);
    hotkeys(port);
    ActivateCxObj(broker, 1);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    if (timer) { tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0; SendIO((struct IORequest *)tr); }

    while (!quit) {
        ULONG got = Wait((1UL << port->mp_SigBit) | sig_button | sig_wheel | sig_grab | sig_hover | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F |
                         (timer ? 1UL << tport->mp_SigBit : 0));
        CxMsg *m;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) {
            read_settings(); hotkeys(port); taller_on();
            places_patch_on();                   /* 0.8.1: always on: the kept places follow cfg.places inside it */
        }
        if (got & sig_wheel) wheel();
        if (got & sig_grab) resize();
        if (got & sig_hover) hover();
        if (got & sig_button) gestures_done();
        while ((m = (CxMsg *)GetMsg(port))) {
            ULONG type = CxMsgType(m), id = CxMsgID(m);
            ReplyMsg((struct Message *)m);
            if (type == CXM_IEVENT) {
                if (on && (id == EV_FWD || id == EV_BACK)) switcher(id == EV_BACK);
            } else if (type == CXM_COMMAND) {
                switch (id) {
                case CXCMD_DISABLE: ActivateCxObj(broker, 0); on = 0; break;
                case CXCMD_ENABLE: ActivateCxObj(broker, 1); on = 1; break;
                case CXCMD_KILL: quit = 1; break;
                case CXCMD_UNIQUE:                    /* started again: read the settings again, stay */
                    read_settings(); hotkeys(port); taller_on();
                    places_patch_on();               /* 0.8.1: always on */
                    break;
                }
            }
        }
        if (quit && !places_patch_off()) {
            /* Another program patched OpenWindowTagList or OpenWindow after us: our
               code must stay. Everything passes through it; the rest of OpenWindows stops. */
            quit = 0;
            on = 0;
            ActivateCxObj(broker, 0);
            PutStr((STRPTR)"OpenWindows: another program has patched the same Intuition calls; "
                           "OpenWindows stays loaded, passing every window through\n");
        }
        if (timer && CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            resize_watch();
            if (on) watch();
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
        }
    }
    if (places_dirty) { write_places(PLACES_ENV); write_places(PLACES_ENVARC); }
out:
    taller_off();
    if (cog.sem.ss_Link.ln_Name) { Forbid(); cog.armed = 0; RemSemaphore(&cog.sem); Permit(); }
    if (broker) ActivateCxObj(broker, 0);           /* no new presses on an edge */
    grab_on = 0;
    resize_end();
    if (timer) { AbortIO((struct IORequest *)tr); WaitIO((struct IORequest *)tr); CloseDevice((struct IORequest *)tr); }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (broker) DeleteCxObjAll(broker);
    if (port) {
        struct Message *m;
        if (port->mp_Node.ln_Name) RemPort(port);
        while ((m = GetMsg(port))) ReplyMsg(m);
        DeleteMsgPort(port);
    }
    pointers_off();
    if (gsig >= 0) FreeSignal(gsig);
    if (wsig >= 0) FreeSignal(wsig);
    if (bsig >= 0) FreeSignal(bsig);
    if (CxBase) CloseLibrary(CxBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
