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
 *                 opens there, placed before Intuition draws it)
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
 *   edges.pointer while one is, the pointer shows the edge held. Off unless
 *                 asked for: a window's own pointer (WA_Pointer, a busy
 *                 pointer) can't be read back, so it would come back as the
 *                 default one
 *
 * Settings: ENV:OpenPrefs/Windows (OpenPrefs Windows writes it, then sends
 * Ctrl-F to the task of the public port "OpenWindows" to read it again). Places: ENV:OpenPrefs/WindowPlaces,
 * kept in ENVARC: too. Exchange can disable, enable or quit it.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
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

static const char version[] __attribute__((used)) = "$VER: OpenWindows 0.7 (10.10.2026) MIT, Copyright (c) 2026 Dalsin Limited";

#define PREFS_ENV "ENV:OpenPrefs/Windows"
#define PLACES_ENV "ENV:OpenPrefs/WindowPlaces"
#define PLACES_ENVARC "ENVARC:OpenPrefs/WindowPlaces"
#define IECLASS_NEWMOUSE_ 0x16                  /* NewMouse drivers' class; OS 3.2 sends RAWKEY 0x7A-0x7D */
#define MAX_PLACES 64
#define MAX_SEEN 96
#define MAX_NEVER 16

struct Library *CxBase, *LayersBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

/* ---- settings ------------------------------------------------------------ */

static struct {
    int snap, snap_dist, halves, switcher, wheel, places, places_wb, drawer_w, drawer_h, dblfront, edges, edge_ptr, drive_title;
    char key[48];
    char never[MAX_NEVER][32];
    int nnever;
} cfg;

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
    cfg.snap = 1; cfg.snap_dist = 12; cfg.halves = 0; cfg.switcher = 1; cfg.wheel = 1; cfg.places = 1; cfg.dblfront = 1; cfg.edges = 1; cfg.edge_ptr = 0; cfg.drive_title = 1;
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
        else if (!strcmp(line, "places")) cfg.places = on_off(v);
        else if (!strcmp(line, "places.drawers")) cfg.places_wb = on_off(v);
        else if (!strcmp(line, "never") && cfg.nnever < MAX_NEVER) strncpy(cfg.never[cfg.nnever++], v, 31);
        else if (!strcmp(line, "drawer")) { cfg.drawer_w = atoi(v); if (strchr(v, ' ')) cfg.drawer_h = atoi(strchr(v, ' ') + 1); }
    }
    if (cfg.snap_dist < 2) cfg.snap_dist = 2;
    if (cfg.snap_dist > 64) cfg.snap_dist = 64;
    FreeVec(text);
}

/* ---- the input stream (input.device's task: no waiting, no DOS) ------------- */

static struct Task *me;
static ULONG sig_button, sig_wheel;
static volatile int ev_down, ev_up;             /* left button down / up seen */
static volatile WORD down_x, down_y, up_x, up_y; /* the pointer, screen relative, at that moment */
#define WHEEL_RING 16
static struct InputEvent wheel_ring[WHEEL_RING];
static volatile int wheel_in, wheel_out;
static volatile int pass_wheel;                 /* events we put back: let them through */
static volatile int on = 1;
/* left button presses for the double-click: when and where, from input.device's task */
#define CLICK_RING 8
static struct click { ULONG secs, micro; WORD x, y; struct Screen *s; } click_ring[CLICK_RING];
static volatile int click_in, click_out;

/* Resizing from an edge (0.5): the handler sees a press on an edge, keeps it
 * from Intuition and tells the main task, which resizes the window with
 * ChangeWindowBox as the pointer moves, until the button comes up. */
#define EDGE_L 1
#define EDGE_R 2
#define EDGE_B 4
#define ZONE_OUT 4                              /* a side: this many pixels outside the frame grab it (0.6.1; nothing is drawn) */
#define ZONE_IN 1                               /* and the frame's own line (a one pixel edge) */
#define ZONE_CORNER 2                           /* a bottom corner: a square 5 x 5 centred on it, two pixels each way */
static ULONG sig_grab;
static struct Window *volatile grab_win;        /* the window being resized, set by the handler */
static volatile int grab_edges, grab_on, grab_end;
static volatile WORD grab_x0, grab_y0;          /* the pointer at the press, screen relative */
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
        if ((w->Flags & (WFLG_BACKDROP | WFLG_BORDERLESS)) || !(w->Flags & WFLG_SIZEGADGET)) {
            if (in) break;                      /* a window that keeps its size */
            continue;
        }
        if (x >= L - ZONE_OUT && x <= R + ZONE_OUT && y >= T + w->BorderTop && y <= B + ZONE_CORNER + ZONE_OUT) {
            /* the sides: a strip outside the frame and its own line, below the title bar; the bottom: the same along it */
            if (x >= L - ZONE_OUT && x <= L + ZONE_IN - 1 && y <= B) e |= EDGE_L;
            if (x <= R + ZONE_OUT && x >= R - ZONE_IN + 1 && y <= B) e |= EDGE_R;
            if (y >= B - ZONE_IN + 1 && y <= B + ZONE_OUT && x >= L && x <= R) e |= EDGE_B;
            /* a bottom corner: a square centred on it holds both edges (it reaches a little outside and inside) */
            if (y >= B - ZONE_CORNER && y <= B + ZONE_CORNER) {
                if (x >= L - ZONE_CORNER && x <= L + ZONE_CORNER) e |= EDGE_L | EDGE_B;
                if (x >= R - ZONE_CORNER && x <= R + ZONE_CORNER) e |= EDGE_R | EDGE_B;
            }
        }
        if (e) { *edges = e; found = w; break; }
        if (in) break;                          /* inside it, off its edges: an ordinary press */
    }
    ReleaseSemaphore(&s->LayerInfo.Lock);
    return found;
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

static void custom(CxMsg *msg, CxObj *co)
{
    struct InputEvent *ie = (struct InputEvent *)CxMsgData(msg);
    (void)co;
    if (!on) return;
    for (; ie; ie = ie->ie_NextEvent) {
        if (ie->ie_Class == IECLASS_RAWMOUSE) {
            struct Screen *s = IntuitionBase->FirstScreen;
            if (grab_on) {                         /* resizing from an edge: the button coming up ends it */
                if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
                    grab_on = 0; grab_end = 1;
                    ie->ie_Code = IECODE_NOBUTTON;     /* the pointer's last move still goes on */
                }
                grab_last = ie->ie_TimeStamp.tv_secs;
                Signal(me, sig_grab);              /* the main task reads where the pointer is */
                continue;
            }
            if (ie->ie_Code == IECODE_LBUTTON && cfg.edges && s && !(ie->ie_Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT |
                IEQUALIFIER_CONTROL | IEQUALIFIER_LALT | IEQUALIFIER_RALT | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))) {
                int edges = 0;
                struct Window *gw = edge_at(s, s->MouseX, s->MouseY, &edges);
                if (gw) {
                    grab_win = gw; grab_edges = edges; grab_x0 = s->MouseX; grab_y0 = s->MouseY;
                    grab_end = 0; grab_on = 1; grab_last = ie->ie_TimeStamp.tv_secs;
                    ie->ie_Code = IECODE_NOBUTTON;     /* Intuition sees a move, not the press */
                    Signal(me, sig_grab);
                    continue;
                }
            }
            if (ie->ie_Code == IECODE_LBUTTON) {
                ev_down = 1; down_x = s ? s->MouseX : 0; down_y = s ? s->MouseY : 0;
                if (cfg.dblfront && s && ((click_in + 1) & (CLICK_RING - 1)) != click_out) {
                    struct click *c = &click_ring[click_in];
                    c->secs = ie->ie_TimeStamp.tv_secs; c->micro = ie->ie_TimeStamp.tv_micro;
                    c->x = s->MouseX; c->y = s->MouseY; c->s = s;
                    click_in = (click_in + 1) & (CLICK_RING - 1);
                }
                Signal(me, sig_button);
            } else if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
                ev_up = 1; up_x = s ? s->MouseX : 0; up_y = s ? s->MouseY : 0;
                Signal(me, sig_button);
            }
        } else if (grab_on && ie->ie_Class == IECLASS_POINTERPOS) {
            grab_last = ie->ie_TimeStamp.tv_secs;     /* a pointer put somewhere (not a mouse): it moves the outline too */
            Signal(me, sig_grab);
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

static struct Window *drag_win;
static WORD drag_l, drag_t, drag_w, drag_h;

/* ---- double-click in a title bar --------------------------------------------------------------- */

/* A gadget's box in the window, with its GFLG_REL* flags worked out. */
static int in_gadget(struct Window *w, struct Gadget *g, WORD x, WORD y)
{
    WORD l = g->LeftEdge + ((g->Flags & GFLG_RELRIGHT) ? w->Width - 1 : 0);
    WORD t = g->TopEdge + ((g->Flags & GFLG_RELBOTTOM) ? w->Height - 1 : 0);
    WORD wd = g->Width + ((g->Flags & GFLG_RELWIDTH) ? w->Width : 0);
    WORD ht = g->Height + ((g->Flags & GFLG_RELHEIGHT) ? w->Height : 0);
    return x >= l && x < l + wd && y >= t && y < t + ht;
}

/* 1 when (x, y), screen relative, is on w's drag bar: in its title bar, on the
 * drag gadget, and on no other gadget (close, depth, zoom, a program's own) */
static int on_drag_bar(struct Window *w, WORD x, WORD y)
{
    struct Gadget *g;
    int drag = 0, other = 0;
    ULONG lock;
    x -= w->LeftEdge; y -= w->TopEdge;
    if (!(w->Flags & WFLG_DRAGBAR) || x < 0 || x >= w->Width || y < 0 || y >= w->BorderTop) return 0;
    lock = LockIBase(0);
    for (g = w->FirstGadget; g; g = g->NextGadget) {
        if (!in_gadget(w, g, x, y)) continue;
        if ((g->GadgetType & GTYP_SYSTYPEMASK) == GTYP_WDRAGGING && (g->GadgetType & GTYP_SYSGADGET)) drag = 1;
        else other = 1;
    }
    UnlockIBase(lock);
    return drag && !other;
}

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

static struct click last_click;
static struct Window *last_click_win;

/* The presses the handler saw: two on the same window's drag bar, close in place and within
 * the double-click time of Input prefs, bring it to the front. The presses go on to Intuition
 * as they are (a drag, the window made active); this only acts after them. */
static void clicks(void)
{
    while (click_out != click_in) {
        struct click c = click_ring[click_out];
        struct Window *w;
        click_out = (click_out + 1) & (CLICK_RING - 1);
        if (!on || !cfg.dblfront) { last_click_win = NULL; continue; }
        w = c.s == IntuitionBase->FirstScreen ? window_at(c.s, c.x, c.y) : NULL;
        if (w && !is_window(w)) w = NULL;
        if (w && ((w->Flags & WFLG_BACKDROP) || !on_drag_bar(w, c.x, c.y))) w = NULL;
        if (w && w == last_click_win && c.s == last_click.s &&
            c.x - last_click.x <= 4 && last_click.x - c.x <= 4 && c.y - last_click.y <= 4 && last_click.y - c.y <= 4 &&
            DoubleClick(last_click.secs, last_click.micro, c.secs, c.micro)) {
            if (!in_front(w)) WindowToFront(w);
            last_click_win = NULL;                 /* a third click starts again */
            continue;
        }
        last_click = c;
        last_click_win = w;
    }
}

static void buttons(void)
{
    if (ev_down) {
        ev_down = 0;
        Delay(2);                                  /* the click has made its window active */
        drag_win = IntuitionBase->ActiveWindow;
        if (drag_win) { drag_l = drag_win->LeftEdge; drag_t = drag_win->TopEdge; drag_w = drag_win->Width; drag_h = drag_win->Height; }
    }
    if (ev_up) {
        struct Window *w = drag_win;
        ev_up = 0;
        drag_win = NULL;
        if (!w || !cfg.snap) return;
        Delay(3);                                  /* the drag has ended and the window is in place */
        if (!is_window(w) || w != IntuitionBase->ActiveWindow) return;
        if (w->Width != drag_w || w->Height != drag_h) return;                 /* a resize, not a move */
        if (w->LeftEdge == drag_l && w->TopEdge == drag_t) return;            /* not moved */
        if (cfg.halves && (up_x <= 1 || up_x >= w->WScreen->Width - 2)) halves(w, up_x);
        else snap(w);
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

static void resize_end(void)
{
    ol_clear();
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
        grab_on = 0; grab_end = 0; grab_win = NULL;
        resize_end();
    }
}

/* The pointer has moved, or the button has come up. The box the window
 * would have: the edge or corner held follows the pointer, the others stay;
 * it keeps to the window's own limits (a MaxWidth of 0 or 65535 is the
 * screen's size) and to the screen. While the button is held the outline
 * shows it; at the button-up the window is sized, once. */
static void resize(void)
{
    struct Window *w;
    struct Screen *s;
    WORD dx, dy, l, t, wd, ht, maxw, maxh;
    if (!rs_win && grab_win) {                  /* a new press on an edge */
        w = grab_win;
        if (!is_window(w)) { grab_win = NULL; grab_on = 0; grab_end = 0; return; }
        rs_win = w; rs_edges = grab_edges; rs_x0 = grab_x0; rs_y0 = grab_y0;
        rs_l = w->LeftEdge; rs_t = w->TopEdge; rs_w = w->Width; rs_h = w->Height;
        { ULONG micro; CurrentTime(&rs_start, &micro); }
        if (w != IntuitionBase->ActiveWindow) ActivateWindow(w);
        {
            int k = !(rs_edges & EDGE_B) ? PTR_H : (rs_edges & EDGE_R) ? PTR_D1 : (rs_edges & EDGE_L) ? PTR_D2 : PTR_V;
            /* only when asked for: the window's own pointer can't be read, so
             * the end of the drag gives it the default one */
            if (cfg.edge_ptr && ptr_obj[k]) { SetWindowPointer(w, WA_Pointer, (ULONG)ptr_obj[k], TAG_DONE); rs_ptr = 1; }
        }
    }
    if (!(w = rs_win)) return;
    if (!is_window(w)) { ol_on = 0; rs_win = NULL; rs_ptr = 0; grab_win = NULL; grab_on = 0; grab_end = 0; return; }
    s = w->WScreen;
    dx = s->MouseX - rs_x0;
    dy = s->MouseY - rs_y0;
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
    if (grab_end) {
        grab_end = 0;
        grab_win = NULL;
        ol_clear();                                          /* the outline goes first, then the one size */
        if (wd != w->Width || ht != w->Height || l != w->LeftEdge)
            ChangeWindowBox(w, l, t, wd, ht);
        resize_end();
        return;
    }
    ol_show(s, l, t, wd, ht);
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

struct place { char key[64]; WORD l, t, w, h; ULONG used; };
static struct place places[MAX_PLACES];
static int nplaces, places_dirty;
static ULONG places_clock;
static struct SignalSemaphore places_sem;   /* the places, between this task and the patch's callers */
static struct { struct Window *w; WORD l, t, wd, ht; int idx; } seen[MAX_SEEN];
static int nseen;

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
    for (p = text; *p && nplaces < MAX_PLACES; ) {
        char *line = p, *q;
        struct place *pl = &places[nplaces];
        int l, t, w, h;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (*line != '"' || !(q = strchr(line + 1, '"'))) continue;
        *q = 0;
        {   /* four numbers (no sscanf: libnix's pulls in FPU code a 68020 without an FPU lacks) */
            char *e = q + 1;
            l = strtol(e, &e, 10); t = strtol(e, &e, 10); w = strtol(e, &e, 10); h = strtol(e, &e, 10);
            if (w <= 0 || h <= 0) continue;
        }
        strncpy(pl->key, line + 1, sizeof pl->key - 1);
        pl->l = l; pl->t = t; pl->w = w; pl->h = h; pl->used = nplaces;
        nplaces++;
    }
    FreeVec(text);
}

static void write_places(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    int i;
    if (!fh) {
        BPTR d = CreateDir((STRPTR)(path[0] == 'E' && path[3] == ':' ? "ENV:OpenPrefs" : "ENVARC:OpenPrefs"));
        if (d) UnLock(d);
        if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return;
    }
    FPuts(fh, (STRPTR)"; OpenWindows: where each program's windows were last left\n");
    for (i = 0; i < nplaces; i++)
        FPrintf(fh, (STRPTR)"\"%s\" %ld %ld %ld %ld\n", (LONG)places[i].key, (LONG)places[i].l, (LONG)places[i].t, (LONG)places[i].w, (LONG)places[i].h);
    Close(fh);
}

static int place_index(const char *key, int make)
{
    int i, oldest = 0;
    for (i = 0; i < nplaces; i++) if (!strcmp(places[i].key, key)) return i;
    if (!make) return -1;
    if (nplaces < MAX_PLACES) i = nplaces++;
    else { for (i = 1; i < nplaces; i++) if (places[i].used < places[oldest].used) oldest = i; i = oldest; }
    memset(&places[i], 0, sizeof places[i]);
    strncpy(places[i].key, key, sizeof places[i].key - 1);
    return i;
}

static void make_key_title(const char *s, const char *prog, char *key, int size)
{
    /* the program and the window's title up to its first digit or bracket, so a
       title with a changing count or file name still matches */
    char title[40];
    int n = 0;
    if (s) while (*s && n < 30 && !(*s >= '0' && *s <= '9') && *s != '(' && *s != '[' && *s != '"') title[n++] = *s++;
    while (n && title[n - 1] == ' ') n--;
    title[n] = 0;
    snprintf(key, size, "%s:%s", prog, title);
}

static void make_key(struct Window *w, const char *prog, char *key, int size)
{
    make_key_title((const char *)w->Title, prog, key, size);
}

/* Where a window with a kept place goes: the place's corner, and its size when
   the window can be sized and the size is within the window's limits, kept on
   the screen. The patch (before a window opens) and the once-a-second look
   (after) both ask here, so they always agree and a placed window never moves
   again. wd and ht come in as the window's own size (0 when not known). */
static void fit_place(const struct place *pl, int sizeable, WORD minw, WORD minh, WORD maxw, WORD maxh,
                      WORD sw, WORD sh, WORD *l, WORD *t, WORD *wd, WORD *ht)
{
    *l = pl->l; *t = pl->t;
    if (sizeable && pl->w >= minw && pl->h >= minh &&
        (!maxw || (UWORD)maxw == 0xffff || pl->w <= (UWORD)maxw) &&
        (!maxh || (UWORD)maxh == 0xffff || pl->h <= (UWORD)maxh)) { *wd = pl->w; *ht = pl->h; }
    if (sw > 0 && *wd > sw) *wd = sw;
    if (sh > 0 && *ht > sh) *ht = sh;
    if (*l < 0) *l = 0;
    if (*t < 0) *t = 0;
    if (sw > 0 && *wd > 0 && *l + *wd > sw) *l = sw - *wd;
    if (sh > 0 && *ht > 0 && *t + *ht > sh) *t = sh - *ht;
}

/* A window whose place is kept: a program's (not one never to be touched),
   or a Workbench drawer when "places.drawers" is on (Workbench's own windows
   otherwise keep to its Snapshot) */
static int placeable(const int *wb, int drawer, const char *prog)
{
    if (!prog[0] || never(prog)) return 0;
    if (!*wb) return 1;
    return drawer && cfg.places_wb;
}

/* ---- placed before they open (0.7) ---------------------------------------------------------- */

/* Up to 0.6 a new window was moved to its place at the next once-a-second
 * look, so it opened where it asked and then jumped. From 0.7 OpenWindowTagList
 * and OpenWindow are patched, as OpenLook patches them and stacked with it
 * whichever loads first: a window whose place is kept opens there, with
 * WA_Left and WA_Top (and WA_Width and WA_Height when it can be sized) put in
 * front of the program's own tags, and the same corner and size in a copy of
 * its NewWindow. The once-a-second look stays for what the patch leaves (it
 * asks fit_place too, so a placed window doesn't move again).
 *
 * The patch runs on the opening program's task. It never waits: when this
 * task holds the places, the window opens as asked and the look places it.
 * It places nothing while the commodity is disabled, for OpenWindows' own
 * windows, or once it has been told to pass everything through (quitting
 * while another program has patched on top of it). */

static APTR old_owtl, old_ow;           /* what the patch replaced */
static volatile UWORD patch_in;         /* callers inside the patch now */
static volatile UBYTE patch_pass;       /* 1: place nothing */

static ULONG tag_or(struct TagItem *tags, struct TagItem *ext, ULONG tag, ULONG dflt)
{
    struct TagItem *ti = tags ? FindTagItem(tag, tags) : NULL;
    if (!ti && ext) ti = FindTagItem(tag, ext);
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
static int place_for(const char *title, int has_idcmp, struct place *pl)
{
    char prog[32], key[64];
    int pass, found = 0;
    for (pass = 0; pass < 2 && !found; pass++) {
        int wb, drawer;
        if (pass == 1 && has_idcmp) break;
        program_of_task(pass == 0 ? FindTask(NULL) : NULL, prog, sizeof prog);
        wb = !stricmp(prog, "Workbench");
        drawer = wb && title && strncmp(title, "Workbench", 9);
        if (!placeable(&wb, drawer, prog)) continue;
        make_key_title(title, prog, key, sizeof key);
        if (!AttemptSemaphoreShared(&places_sem)) return 0;     /* being changed: the look places it */
        {
            int i = place_index(key, 0);
            if (i >= 0) { *pl = places[i]; found = 1; }
        }
        ReleaseSemaphore(&places_sem);
    }
    return found;
}

/* OpenWindowTagList(nw, tags) with the window's kept place, when it has one. */
static struct Window *__attribute__((used)) ow_open_window(struct NewWindow *nw, struct TagItem *tags, APTR fn)
{
    struct TagItem more[5], *use = tags, *ext = NULL;
    struct ExtNewWindow copy;
    struct NewWindow *pass_nw = nw;
    struct Window *win;
    patch_in++;
    if (!patch_pass && on && cfg.places && FindTask(NULL) != me) {
        ULONG flags, idcmp;
        const char *title;
        struct place pl;
        int found = 0, sizeable, inner;
        memset(&pl, 0, sizeof pl);
        if (nw && (nw->Flags & WFLG_NW_EXTENDED)) ext = ((struct ExtNewWindow *)nw)->Extension;
        flags = tag_or(tags, ext, WA_Flags, nw ? nw->Flags : 0);
        if (tag_or(tags, ext, WA_Backdrop, 0)) flags |= WFLG_BACKDROP;
        if (tag_or(tags, ext, WA_GimmeZeroZero, 0)) flags |= WFLG_GIMMEZEROZERO;
        if (tag_or(tags, ext, WA_SizeGadget, 0)) flags |= WFLG_SIZEGADGET;
        if (tag_or(tags, ext, WA_DragBar, 0)) flags |= WFLG_DRAGBAR;
        title = (const char *)tag_or(tags, ext, WA_Title, nw ? (ULONG)nw->Title : 0);
        idcmp = tag_or(tags, ext, WA_IDCMP, nw ? nw->IDCMPFlags : 0);
        /* the same windows the look places: not backdrops, not GZZ, a drag bar or a title */
        if (!(flags & (WFLG_BACKDROP | WFLG_GIMMEZEROZERO)) && ((flags & WFLG_DRAGBAR) || title))
            found = place_for(title, idcmp != 0, &pl);
        if (found) {
            WORD sw, sh, l, t;
            WORD wd = (WORD)tag_or(tags, ext, WA_Width, nw ? (ULONG)(LONG)nw->Width : 0);
            WORD ht = (WORD)tag_or(tags, ext, WA_Height, nw ? (ULONG)(LONG)nw->Height : 0);
            WORD minw = (WORD)tag_or(tags, ext, WA_MinWidth, nw ? (ULONG)(LONG)nw->MinWidth : 0);
            WORD minh = (WORD)tag_or(tags, ext, WA_MinHeight, nw ? (ULONG)(LONG)nw->MinHeight : 0);
            WORD maxw = (WORD)tag_or(tags, ext, WA_MaxWidth, nw ? (ULONG)nw->MaxWidth : 0);
            WORD maxh = (WORD)tag_or(tags, ext, WA_MaxHeight, nw ? (ULONG)nw->MaxHeight : 0);
            int n = 0;
            /* a program that asks by its inner size is only moved: a whole size would fight it */
            inner = tag_or(tags, ext, WA_InnerWidth, 0) || tag_or(tags, ext, WA_InnerHeight, 0);
            sizeable = (flags & WFLG_SIZEGADGET) && !inner;
            opening_screen(nw, tags, ext, &sw, &sh);
            if (sw > 0 && sh > 0) {
                fit_place(&pl, sizeable, minw, minh, maxw, maxh, sw, sh, &l, &t, &wd, &ht);
                more[n].ti_Tag = WA_Left; more[n++].ti_Data = (ULONG)(LONG)l;
                more[n].ti_Tag = WA_Top; more[n++].ti_Data = (ULONG)(LONG)t;
                if (sizeable && wd > 0 && ht > 0) {
                    more[n].ti_Tag = WA_Width; more[n++].ti_Data = (ULONG)(LONG)wd;
                    more[n].ti_Tag = WA_Height; more[n++].ti_Data = (ULONG)(LONG)ht;
                }
                more[n].ti_Tag = use ? TAG_MORE : TAG_DONE; more[n].ti_Data = (ULONG)use;
                use = more;
                if (nw) {
                    /* the same corner and size in a copy of the program's NewWindow; its own stays as it gave it */
                    memcpy(&copy, nw, (nw->Flags & WFLG_NW_EXTENDED) ? sizeof(struct ExtNewWindow) : sizeof(struct NewWindow));
                    copy.LeftEdge = l; copy.TopEdge = t;
                    if (sizeable && wd > 0 && ht > 0) { copy.Width = wd; copy.Height = ht; }
                    pass_nw = (struct NewWindow *)&copy;
                }
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

/* Once a second: new windows go to their places (or, Workbench drawers, at
   least the drawer size); every known window's place is noted. */
static void watch(void)
{
    /* static: Run gives a commodity a 4 KB stack, and this is 12 KB */
    static struct { struct Window *w; WORD l, t, wd, ht, minw, minh, maxw, maxh, sw, sh; ULONG flags; char prog[32]; char key[64]; int wb, drawer; } now[MAX_SEEN];
    int n = 0, i, j;
    struct Screen *s;
    struct Window *w;
    ULONG lock;
    if (cfg.drive_title) drive_titles();
    if (!cfg.places && !cfg.drawer_w) return;
    lock = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen)
        for (w = s->FirstWindow; w && n < MAX_SEEN; w = w->NextWindow) {
            if (w->Flags & (WFLG_BACKDROP | WFLG_GIMMEZEROZERO)) continue;
            if (!(w->Flags & WFLG_DRAGBAR) && !w->Title) continue;
            now[n].w = w; now[n].l = w->LeftEdge; now[n].t = w->TopEdge; now[n].wd = w->Width; now[n].ht = w->Height;
            now[n].minw = w->MinWidth; now[n].minh = w->MinHeight; now[n].maxw = w->MaxWidth; now[n].maxh = w->MaxHeight;
            now[n].sw = s->Width; now[n].sh = s->Height; now[n].flags = w->Flags;
            now[n].wb = (w->Flags & WFLG_WBENCHWINDOW) != 0;
            /* a Workbench drawer, not its root window, takes a place when the switch says */
            now[n].drawer = now[n].wb && w->Title && strncmp((const char *)w->Title, "Workbench", 9);
            {   /* the program's name while the window is sure to be there; the key from a copy
                   of it (GCC 16 can't tell now[n].prog from now[n].key: -Wrestrict) */
                char prog[sizeof now[0].prog];
                program_of(w, prog, sizeof prog);
                memcpy(now[n].prog, prog, sizeof prog);
                make_key(w, prog, now[n].key, sizeof now[n].key);
            }
            n++;
        }
    UnlockIBase(lock);
    /* the patch reads the places from other tasks; it never waits for this */
    ObtainSemaphore(&places_sem);
    places_clock++;
    for (i = 0; i < n; i++) {
        int known = -1;
        for (j = 0; j < nseen; j++) if (seen[j].w == now[i].w) { known = j; break; }
        if (known < 0 && places_clock > 1) {
            /* a new window (the first look only notes the windows already open) */
            int placed = 0;
            if (cfg.places && placeable(&now[i].wb, now[i].drawer, now[i].prog) && place_index(now[i].key, 0) >= 0) placed = 1;
            if (!placed && now[i].wb && cfg.drawer_w && (now[i].flags & WFLG_SIZEGADGET) && now[i].prog[0] && !stricmp(now[i].prog, "Workbench")) {
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
            } else if (placed) {
                int p = place_index(now[i].key, 0);
                if (p >= 0) {
                    struct place *pl = &places[p];
                    WORD l, t, wd = now[i].wd, ht = now[i].ht;
                    fit_place(pl, (now[i].flags & WFLG_SIZEGADGET) != 0, now[i].minw, now[i].minh, now[i].maxw, now[i].maxh,
                              now[i].sw, now[i].sh, &l, &t, &wd, &ht);
                    if ((l != now[i].l || t != now[i].t || wd != now[i].wd || ht != now[i].ht) && is_window(now[i].w)) {
                        if (wd != now[i].wd || ht != now[i].ht) ChangeWindowBox(now[i].w, l, t, wd, ht);
                        else MoveWindow(now[i].w, l - now[i].l, t - now[i].t);
                        now[i].l = l; now[i].t = t; now[i].wd = wd; now[i].ht = ht;
                    }
                    pl->used = places_clock;
                }
            }
        } else if (known >= 0 && cfg.places && placeable(&now[i].wb, now[i].drawer, now[i].prog) &&
                   (seen[known].l != now[i].l || seen[known].t != now[i].t || seen[known].wd != now[i].wd || seen[known].ht != now[i].ht)) {
            /* the user moved or sized it: remember that */
            int p = place_index(now[i].key, 1);
            places[p].l = now[i].l; places[p].t = now[i].t; places[p].w = now[i].wd; places[p].h = now[i].ht;
            places[p].used = places_clock;
            places_dirty = 1;
        }
    }
    nseen = 0;
    for (i = 0; i < n; i++) { seen[nseen].w = now[i].w; seen[nseen].l = now[i].l; seen[nseen].t = now[i].t; seen[nseen].wd = now[i].wd; seen[nseen].ht = now[i].ht; nseen++; }
    if (places_dirty && places_clock % 2 == 0) write_places(PLACES_ENV);
    if (places_dirty && places_clock % 10 == 0) { write_places(PLACES_ENV); write_places(PLACES_ENVARC); places_dirty = 0; }
    ReleaseSemaphore(&places_sem);
}

/* ---- the commodity --------------------------------------------------------------------------- */

static struct NewBroker nb = {
    NB_VERSION, (STRPTR)"OpenWindows", (STRPTR)"OpenWindows 0.7", (STRPTR)"Snapping, edges, Amiga+Tab, wheel and window places",
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
    pointers_on();
    if (!(port = CreateMsgPort())) goto out;
    nb.nb_Port = port;
    if (!(broker = CxBroker(&nb, NULL))) goto out;    /* already running: NBU_UNIQUE */
    port->mp_Node.ln_Name = (char *)"OpenWindows";    /* OpenPrefs Windows finds this task by it */
    port->mp_Node.ln_Pri = 0;
    AddPort(port);
    InitSemaphore(&places_sem);
    read_prefs();
    read_places();
    if (cfg.places) places_patch_on();
    cust = CxCustom(custom, 0);
    if (!cust) goto out;
    AttachCxObj(broker, cust);
    hotkeys(port);
    ActivateCxObj(broker, 1);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    if (timer) { tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0; SendIO((struct IORequest *)tr); }

    while (!quit) {
        ULONG got = Wait((1UL << port->mp_SigBit) | sig_button | sig_wheel | sig_grab | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F |
                         (timer ? 1UL << tport->mp_SigBit : 0));
        CxMsg *m;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) {
            read_prefs(); hotkeys(port);
            if (cfg.places) places_patch_on();
            else (void)places_patch_off();       /* patched over: it stays, passing through */
        }
        if (got & sig_wheel) wheel();
        if (got & sig_grab) resize();
        if (got & sig_button) { clicks(); buttons(); }
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
                    read_prefs(); hotkeys(port);
                    if (cfg.places) places_patch_on();
                    else (void)places_patch_off();
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
