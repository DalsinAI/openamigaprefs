/* OpenWindows: the commodity behind OpenPrefs Windows. No Intuition patches:
 * it watches the input stream and moves, sizes and activates windows with
 * the OS's own calls.
 *
 *   snapping      a window dropped near a screen edge or another window's
 *                 edge lines up with it; dropped with the pointer at the
 *                 screen's left or right edge, it fills that half (option)
 *   Amiga+Tab     brings the window at the back to the front and activates
 *                 it; with Shift, sends the front window to the back
 *   wheel         the mouse wheel goes to the window under the pointer
 *   places        a program's window opens where it was last left
 *   drawers       Workbench drawer windows open at least this big
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
#include <graphics/layers.h>
#include <graphics/clip.h>
#include <libraries/commodities.h>
#include <libraries/keymap.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/commodities.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char version[] = "$VER: OpenWindows 0.1 (6.10.2026) MIT, Copyright (c) 2026 Dalsin Limited";

#define PREFS_ENV "ENV:OpenPrefs/Windows"
#define PLACES_ENV "ENV:OpenPrefs/WindowPlaces"
#define PLACES_ENVARC "ENVARC:OpenPrefs/WindowPlaces"
#define IECLASS_NEWMOUSE_ 0x16                  /* NewMouse drivers' class; OS 3.2 sends RAWKEY 0x7A-0x7D */
#define MAX_PLACES 64
#define MAX_SEEN 96
#define MAX_NEVER 16

struct Library *CxBase, *LayersBase;
struct IntuitionBase *IntuitionBase;

/* ---- settings ------------------------------------------------------------ */

static struct {
    int snap, snap_dist, halves, switcher, wheel, places, drawer_w, drawer_h;
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
    cfg.snap = 1; cfg.snap_dist = 12; cfg.halves = 0; cfg.switcher = 1; cfg.wheel = 1; cfg.places = 1;
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
        else if (!strcmp(line, "places")) cfg.places = on_off(v);
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
            if (ie->ie_Code == IECODE_LBUTTON) {
                ev_down = 1; down_x = s ? s->MouseX : 0; down_y = s ? s->MouseY : 0;
                Signal(me, sig_button);
            } else if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
                ev_up = 1; up_x = s ? s->MouseX : 0; up_y = s ? s->MouseY : 0;
                Signal(me, sig_button);
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

static struct Window *drag_win;
static WORD drag_l, drag_t, drag_w, drag_h;

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
static struct { struct Window *w; WORD l, t, wd, ht; int idx; } seen[MAX_SEEN];
static int nseen;

/* The program behind a window: its task's name, or the command a Shell runs. */
static void program_of(struct Window *w, char *out, int size)
{
    struct Task *t = w->UserPort ? (struct Task *)w->UserPort->mp_SigTask : NULL;
    *out = 0;
    if (!t) { strncpy(out, "window", size - 1); return; }     /* no IDCMP (a console, say): known by its title */
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

static void make_key(struct Window *w, const char *prog, char *key, int size)
{
    /* the program and the window's title up to its first digit or bracket, so a
       title with a changing count or file name still matches */
    char title[40];
    const char *s = (const char *)w->Title;
    int n = 0;
    if (s) while (*s && n < 30 && !(*s >= '0' && *s <= '9') && *s != '(' && *s != '[' && *s != '"') title[n++] = *s++;
    while (n && title[n - 1] == ' ') n--;
    title[n] = 0;
    snprintf(key, size, "%s:%s", prog, title);
}

/* Once a second: new windows go to their places (or, Workbench drawers, at
   least the drawer size); every known window's place is noted. */
static void watch(void)
{
    struct { struct Window *w; WORD l, t, wd, ht, minw, minh, maxw, maxh, sw, sh; ULONG flags; char prog[32]; char key[64]; int wb; } now[MAX_SEEN];
    int n = 0, i, j;
    struct Screen *s;
    struct Window *w;
    ULONG lock;
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
            program_of(w, now[n].prog, sizeof now[n].prog);      /* while the window is sure to be there */
            make_key(w, now[n].prog, now[n].key, sizeof now[n].key);
            n++;
        }
    UnlockIBase(lock);
    places_clock++;
    for (i = 0; i < n; i++) {
        int known = -1;
        for (j = 0; j < nseen; j++) if (seen[j].w == now[i].w) { known = j; break; }
        if (known < 0 && places_clock > 1) {
            /* a new window (the first look only notes the windows already open) */
            if (now[i].wb && cfg.drawer_w && (now[i].flags & WFLG_SIZEGADGET) && now[i].prog[0] && !stricmp(now[i].prog, "Workbench")) {
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
            } else if (cfg.places && !now[i].wb && now[i].prog[0] && !never(now[i].prog)) {
                int p = place_index(now[i].key, 0);
                if (p >= 0) {
                    struct place *pl = &places[p];
                    WORD l = pl->l, t = pl->t, wd = now[i].wd, ht = now[i].ht;
                    if ((now[i].flags & WFLG_SIZEGADGET) && pl->w >= now[i].minw && pl->h >= now[i].minh &&
                        (!now[i].maxw || (UWORD)now[i].maxw == 0xffff || pl->w <= (UWORD)now[i].maxw) &&
                        (!now[i].maxh || (UWORD)now[i].maxh == 0xffff || pl->h <= (UWORD)now[i].maxh)) { wd = pl->w; ht = pl->h; }
                    if (wd > now[i].sw) wd = now[i].sw;
                    if (ht > now[i].sh) ht = now[i].sh;
                    if (l < 0) l = 0;
                    if (t < 0) t = 0;
                    if (l + wd > now[i].sw) l = now[i].sw - wd;
                    if (t + ht > now[i].sh) t = now[i].sh - ht;
                    if ((l != now[i].l || t != now[i].t || wd != now[i].wd || ht != now[i].ht) && is_window(now[i].w)) {
                        if (wd != now[i].wd || ht != now[i].ht) ChangeWindowBox(now[i].w, l, t, wd, ht);
                        else MoveWindow(now[i].w, l - now[i].l, t - now[i].t);
                        now[i].l = l; now[i].t = t; now[i].wd = wd; now[i].ht = ht;
                    }
                    pl->used = places_clock;
                }
            }
        } else if (known >= 0 && cfg.places && !now[i].wb && now[i].prog[0] && !never(now[i].prog) &&
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
}

/* ---- the commodity --------------------------------------------------------------------------- */

static struct NewBroker nb = {
    NB_VERSION, (STRPTR)"OpenWindows", (STRPTR)"OpenWindows 0.1", (STRPTR)"Snapping, Amiga+Tab, wheel and window places",
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
    LONG bsig = -1, wsig = -1;
    int quit = 0, timer = 0;
    (void)version;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    CxBase = OpenLibrary((STRPTR)"commodities.library", 39);
    if (!IntuitionBase || !LayersBase || !CxBase) goto out;
    me = FindTask(NULL);
    if ((bsig = AllocSignal(-1)) < 0 || (wsig = AllocSignal(-1)) < 0) goto out;
    sig_button = 1UL << bsig; sig_wheel = 1UL << wsig;
    if (!(port = CreateMsgPort())) goto out;
    nb.nb_Port = port;
    if (!(broker = CxBroker(&nb, NULL))) goto out;    /* already running: NBU_UNIQUE */
    port->mp_Node.ln_Name = (char *)"OpenWindows";    /* OpenPrefs Windows finds this task by it */
    port->mp_Node.ln_Pri = 0;
    AddPort(port);
    read_prefs();
    read_places();
    cust = CxCustom(custom, 0);
    if (!cust) goto out;
    AttachCxObj(broker, cust);
    hotkeys(port);
    ActivateCxObj(broker, 1);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    if (timer) { tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0; SendIO((struct IORequest *)tr); }

    while (!quit) {
        ULONG got = Wait((1UL << port->mp_SigBit) | sig_button | sig_wheel | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F |
                         (timer ? 1UL << tport->mp_SigBit : 0));
        CxMsg *m;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) { read_prefs(); hotkeys(port); }
        if (got & sig_wheel) wheel();
        if (got & sig_button) buttons();
        while ((m = (CxMsg *)GetMsg(port))) {
            ULONG type = CxMsgType(m), id = CxMsgID(m);
            ReplyMsg((struct Message *)m);
            if (type == CXM_IEVENT) {
                if (on && (id == EV_FWD || id == EV_BACK)) switcher(id == EV_BACK);
            } else if (type == CXM_COMMAND) {
                switch (id) {
                case CXCMD_DISABLE: ActivateCxObj(broker, 0); on = 0; break;
                case CXCMD_ENABLE: ActivateCxObj(broker, 1); on = 1; break;
                case CXCMD_KILL: case CXCMD_UNIQUE: quit = 1; break;
                }
            }
        }
        if (timer && CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            if (on) watch();
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
        }
    }
    if (places_dirty) { write_places(PLACES_ENV); write_places(PLACES_ENVARC); }
out:
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
    if (wsig >= 0) FreeSignal(wsig);
    if (bsig >= 0) FreeSignal(bsig);
    if (CxBase) CloseLibrary(CxBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
