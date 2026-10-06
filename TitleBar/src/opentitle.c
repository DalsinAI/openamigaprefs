/* OpenTitle 0.1: the Workbench screen's title bar as Dale drew it on
 * 6 October 2026: AmigaChrome's logo at the far left, Workbench's title with
 * the free chip and fast memory after it, and the day, date and time at the
 * right end, just left of OpenSpeaker. Optionally the display's border
 * around the screen is black.
 *
 * The free memory is Workbench's own: its title format (the WBTF chunk of
 * Sys/Workbench.prefs, as Prefs/Workbench writes it) becomes
 *   "Workbench %r  Chip %mcek KB  Fast %mfem MB"
 * in ENV:, so IPrefs shows it and it updates as Workbench does; spaces
 * before it leave room for the logo.
 *
 * The logo and the clock are two small borderless windows in the title bar,
 * as OpenSpeaker is. The clock is in OpenMenus' tray (ENV:OpenMenus/Tray/Clock,
 * "width order", order 10: left of the speaker, order 0), and goes where
 * OpenMenus' bar is when OpenMenus puts it at an edge. A click on either
 * leaves the active window active.
 *
 *   OpenTitle        (started at boot; Ctrl-C quits, Ctrl-F reads
 *                     ENV:OpenPrefs/TitleBar again and quits when nothing
 *                     is left to show)
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/videocontrol.h>
#include <graphics/layers.h>
#include <utility/date.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tb_prefs.h"
#include "om_prefs.h"
#include "logo.h"

const char version[] __attribute__((used)) = "$VER: OpenTitle 0.1 (6.10.2026) OpenPrefs, Dalsin Limited";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *LayersBase, *UtilityBase;

#define MENUS_ENV  "ENV:OpenMenus/Menus"
#define TRAY_DIR   "ENV:OpenMenus/Tray"
#define TRAY_MINE  "ENV:OpenMenus/Tray/Clock"
#define MY_ORDER   10                          /* left of OpenSpeaker (0) */
#define WB_ENV     "ENV:Sys/Workbench.prefs"
#define TITLE_MEM  "Workbench %r  Chip %mcek KB  Fast %mfem MB"
#define TITLE_PLAIN "Amiga Workbench %r"
#define LOGO_PAD   "    "                      /* room for the logo before Workbench's title */

static tb_prefs prefs;
static struct Screen *scr;
static struct DrawInfo *dri;
static struct Window *logo_w, *clock_w;
static WORD cx, cy, cw, ch;                    /* the clock window's place */
static int bar_edge = OM_BAR_TITLE;
static int border_set;                         /* we made the border black */
static struct Window *last_other;              /* the window that was active before a click on ours */
static char shown[40];

static LONG pens[64];
static ULONG pen_rgb[64];
static int npens;

/* ---- files --------------------------------------------------------------------------------- */

static char *read_file(const char *path, LONG *lenp)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf = NULL;
    LONG len = 0;
    if (fh) {
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len >= 0 && (buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) {
            if (Read(fh, buf, len) != len) { FreeVec(buf); buf = NULL; }
        }
        Close(fh);
    }
    if (lenp) *lenp = buf ? len : 0;
    return buf;
}

static void read_prefs(void)
{
    char *t = read_file(TB_ENV, NULL);
    if (!t) t = read_file(TB_ENVARC, NULL);
    tb_parse(&prefs, t);
    if (t) FreeVec(t);
}

static int read_bar_edge(void)
{
    om_prefs p;
    char *t;
    struct MsgPort *port;
    Forbid();
    port = FindPort((STRPTR)"OpenMenus");
    Permit();
    if (!port) return OM_BAR_TITLE;
    om_defaults(&p);
    if ((t = read_file(MENUS_ENV, NULL))) { om_parse(&p, t); FreeVec(t); }
    return p.enabled ? p.bar : OM_BAR_TITLE;
}

static void tell_openmenus(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)"OpenMenus")) && p->mp_SigTask) Signal((struct Task *)p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
}

static void tray(int width)
{
    BPTR fh, lock;
    char t[24];
    if (width > 0) {
        if ((lock = Lock((STRPTR)TRAY_DIR, ACCESS_READ))) UnLock(lock);
        else if ((lock = CreateDir((STRPTR)TRAY_DIR))) UnLock(lock);
        if ((fh = Open((STRPTR)TRAY_MINE, MODE_NEWFILE))) {
            LONG n = snprintf(t, sizeof t, "%d %d\n", width, MY_ORDER);
            Write(fh, t, n);
            Close(fh);
        }
    } else DeleteFile((STRPTR)TRAY_MINE);
    tell_openmenus();
}

/* the room the tray programs nearer the bar's end take, and how many they are */
static int tray_before(int *count)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR lock = Lock((STRPTR)TRAY_DIR, ACCESS_READ);
    int sum = 0;
    *count = 0;
    if (fib && lock && Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            char path[96], *t;
            int w = 0, order = 0;
            const char *name = (const char *)fib->fib_FileName;
            if (fib->fib_DirEntryType > 0 || !strcmp(name, "Clock")) continue;
            snprintf(path, sizeof path, TRAY_DIR "/%s", name);
            if (!(t = read_file(path, NULL))) continue;
            if (sscanf(t, "%d %d", &w, &order) < 1) w = 0;
            FreeVec(t);
            if (w <= 0 || w > 400) continue;
            if (order < MY_ORDER || (order == MY_ORDER && strcmp(name, "Clock") < 0)) { sum += w; (*count)++; }
        }
    }
    if (lock) UnLock(lock);
    if (fib) FreeDosObject(DOS_FIB, fib);
    return sum;
}

/* ---- Workbench's title format (the WBTF chunk of ENV:Sys/Workbench.prefs) ------------------ */

static ULONG get32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }
static void put32(UBYTE *p, ULONG v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

/* The title format Workbench.prefs has now (NULL when none) into fmt. */
static int wb_title(char *fmt, int size)
{
    LONG len, i;
    UBYTE *d = (UBYTE *)read_file(WB_ENV, &len);
    int found = 0;
    fmt[0] = 0;
    if (d && len >= 12 && !memcmp(d, "FORM", 4) && !memcmp(d + 8, "PREF", 4)) {
        for (i = 12; i + 8 <= len; ) {
            ULONG n = get32(d + i + 4);
            if (i + 8 + n > (ULONG)len) break;
            if (!memcmp(d + i, "WBTF", 4)) {
                int k = n < (ULONG)size - 1 ? n : size - 1;
                memcpy(fmt, d + i + 8, k);
                fmt[k] = 0;
                found = 1;
            }
            i += 8 + n + (n & 1);
        }
    }
    if (d) FreeVec(d);
    return found;
}

/* Writes ENV:Sys/Workbench.prefs with fmt as its title format (NULL: none), keeping its other chunks. */
static void wb_set_title(const char *fmt)
{
    LONG len, i, o = 12, flen = fmt ? strlen(fmt) + 1 : 0;
    UBYTE *d = (UBYTE *)read_file(WB_ENV, &len), *out;
    BPTR fh;
    if (!d || len < 12 || memcmp(d, "FORM", 4) || memcmp(d + 8, "PREF", 4)) {
        static const UBYTE prhd[14] = { 'P', 'R', 'H', 'D', 0, 0, 0, 6, 0, 0, 0, 0, 0, 0 };
        if (d) FreeVec(d);
        if (!fmt) return;
        len = 12 + 14;
        if (!(d = AllocVec(len, MEMF_CLEAR))) return;
        memcpy(d, "FORM", 4); memcpy(d + 8, "PREF", 4); memcpy(d + 12, prhd, 14);
    }
    if (!(out = AllocVec(len + flen + 16, MEMF_CLEAR))) { FreeVec(d); return; }
    memcpy(out, d, 12);
    for (i = 12; i + 8 <= len; ) {
        ULONG n = get32(d + i + 4), all = 8 + n + (n & 1);
        if (i + all > (ULONG)len) break;
        if (memcmp(d + i, "WBTF", 4)) { memcpy(out + o, d + i, all); o += all; }
        i += all;
    }
    if (fmt) {
        memcpy(out + o, "WBTF", 4);
        put32(out + o + 4, flen);
        memcpy(out + o + 8, fmt, flen);
        o += 8 + flen + (flen & 1);
    }
    put32(out + 4, o - 8);
    if ((fh = Open((STRPTR)WB_ENV, MODE_NEWFILE))) { Write(fh, out, o); Close(fh); }   /* IPrefs is notified and shows it */
    FreeVec(out);
    FreeVec(d);
}

static int ours(const char *fmt)
{
    const char *f = fmt;
    while (*f == ' ') f++;
    return !strcmp(f, TITLE_MEM) || !strcmp(f, TITLE_PLAIN);
}

static void apply_title(void)
{
    char now[160], want[160];
    int have = wb_title(now, sizeof now);
    if (!prefs.memory && !prefs.logo) {
        if (have && ours(now)) wb_set_title(NULL);       /* back to Workbench's own */
        return;
    }
    if (have && !ours(now) && now[0]) {
        /* someone's own title format: keep it, only make room for the logo */
        if (!prefs.logo || !strncmp(now, LOGO_PAD, strlen(LOGO_PAD))) return;
        snprintf(want, sizeof want, LOGO_PAD "%s", now);
    } else snprintf(want, sizeof want, "%s%s", prefs.logo ? LOGO_PAD : "", prefs.memory ? TITLE_MEM : TITLE_PLAIN);
    if (!have || strcmp(now, want)) wb_set_title(want);
}

/* ---- the border ------------------------------------------------------------------------------ */

static void apply_border(void)
{
    if (!scr || !scr->ViewPort.ColorMap) return;
    if (prefs.border_black == border_set) return;
    VideoControlTags(scr->ViewPort.ColorMap, prefs.border_black ? VTAG_BORDERBLANK_SET : VTAG_BORDERBLANK_CLR, 0, TAG_DONE);
    border_set = prefs.border_black;
    MakeScreen(scr);
    RethinkDisplay();
}

/* ---- the logo -------------------------------------------------------------------------------- */

static LONG pen_for(ULONG rgb)
{
    int i;
    for (i = 0; i < npens; i++) if (pen_rgb[i] == rgb) return pens[i];
    if (npens == 64) return dri->dri_Pens[BARDETAILPEN];
    pen_rgb[npens] = rgb;
    pens[npens] = ObtainBestPen(scr->ViewPort.ColorMap, (rgb >> 16) * 0x01010101UL, ((rgb >> 8) & 255) * 0x01010101UL,
                                (rgb & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    if (pens[npens] < 0) pens[npens] = dri->dri_Pens[BARDETAILPEN];
    return pens[npens++];
}

static void free_pens(void)
{
    int i;
    for (i = 0; i < npens; i++) if (pens[i] >= 0 && scr) ReleasePen(scr->ViewPort.ColorMap, pens[i]);
    npens = 0;
}

static void draw_logo(void)
{
    struct RastPort *rp;
    WORD s, x, y;
    if (!logo_w) return;
    rp = logo_w->RPort;
    SetAPen(rp, dri->dri_Pens[BARBLOCKPEN]);
    RectFill(rp, 0, 0, logo_w->Width - 1, logo_w->Height - 1);
    s = logo_w->Height - 2;                    /* the logo, scaled to the bar, a pixel in from the top */
    for (y = 0; y < s; y++)
        for (x = 0; x < s; x++) {
            long c = logo16[(y * 16 / s) * 16 + x * 16 / s];
            if (c < 0) continue;
            SetAPen(rp, pen_for((ULONG)c));
            WritePixel(rp, 2 + x, 1 + y);
        }
}

/* ---- the clock ------------------------------------------------------------------------------- */

static void clock_text(char *t, int size)
{
    static const char *const days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct DateStamp ds;
    struct ClockData cd;
    DateStamp(&ds);
    Amiga2Date(ds.ds_Days * 86400 + ds.ds_Minute * 60 + ds.ds_Tick / TICKS_PER_SECOND, &cd);
    if (prefs.date) snprintf(t, size, "%s %d %s  %02d:%02d", days[cd.wday % 7], cd.mday, months[(cd.month + 11) % 12], cd.hour, cd.min);
    else snprintf(t, size, "%02d:%02d", cd.hour, cd.min);
}

static WORD clock_width(void)
{
    struct RastPort rp;
    const char *widest = prefs.date ? "Wed 28 Sep  00:00" : "00:00";
    InitRastPort(&rp);
    SetFont(&rp, dri->dri_Font);
    return TextLength(&rp, (STRPTR)widest, strlen(widest)) + 10;
}

static void draw_clock(int force)
{
    struct RastPort *rp;
    char t[40];
    WORD h, w;
    if (!clock_w) return;
    clock_text(t, sizeof t);
    if (!force && !strcmp(t, shown)) return;
    strcpy(shown, t);
    rp = clock_w->RPort;
    SetAPen(rp, dri->dri_Pens[BARBLOCKPEN]);
    RectFill(rp, 0, 0, cw - 1, ch - 1);
    SetAPen(rp, dri->dri_Pens[BARDETAILPEN]);
    SetDrMd(rp, JAM1);
    h = ch - (bar_edge == OM_BAR_TITLE ? 1 : 0);
    w = TextLength(rp, (STRPTR)t, strlen(t));
    Move(rp, cw - 6 - w, (h - rp->TxHeight) / 2 + rp->TxBaseline);
    Text(rp, (STRPTR)t, strlen(t));
}

/* where the clock goes: left of the tray programs nearer the bar's end */
static void place(void)
{
    WORD bh = scr->BarHeight + 1;
    int n, off = tray_before(&n);
    cw = clock_width();
    switch (bar_edge) {
    case OM_BAR_BOTTOM: ch = bh; cx = scr->Width - cw - off; cy = scr->Height - bh; break;
    case OM_BAR_LEFT:   ch = bh; cx = 0; cy = scr->Height - bh * (n + 1); break;
    case OM_BAR_RIGHT:  ch = bh; cx = scr->Width - cw; cy = scr->Height - bh * (n + 1); break;
    case OM_BAR_TOP:    ch = bh; cx = scr->Width - 2 * scr->BarHeight - cw - off; cy = 0; break;
    default:            ch = scr->BarHeight; cx = scr->Width - 2 * scr->BarHeight - cw - off; cy = 0; break;
    }
}

/* ---- the windows ----------------------------------------------------------------------------- */

static struct Window *bar_window(WORD x, WORD y, WORD w, WORD h)
{
    return OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                          WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW, TAG_DONE);
}

static void close_all(void)
{
    if (clock_w) { CloseWindow(clock_w); clock_w = NULL; tray(0); }
    if (logo_w) { CloseWindow(logo_w); logo_w = NULL; }
    if (border_set) { prefs.border_black = 0; apply_border(); }
    free_pens();
    if (dri) { FreeScreenDrawInfo(scr, dri); dri = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static int open_all(void)
{
    if (!(scr = LockPubScreen((STRPTR)"Workbench"))) return 0;
    if (!(dri = GetScreenDrawInfo(scr))) { close_all(); return 0; }
    bar_edge = read_bar_edge();
    if (prefs.logo) {
        logo_w = bar_window(0, 0, scr->BarHeight + 2, scr->BarHeight);
        if (logo_w) draw_logo();
    }
    if (prefs.clock) {
        place();
        if ((clock_w = bar_window(cx, cy, cw, ch))) {
            SetFont(clock_w->RPort, dri->dri_Font);
            shown[0] = 0;
            draw_clock(1);
            tray(cw);
        }
    }
    apply_border();
    return 1;
}

/* in front again when a window (not a borderless one: menus, bars) has come over it */
static void keep_in_front(struct Window *w)
{
    struct Layer *l;
    int covered = 0;
    if (!w) return;
    LockLayerInfo(&scr->LayerInfo);
    for (l = w->WLayer->front; l && !covered; l = l->front) {
        struct Window *o = (struct Window *)l->Window;
        if (!o || (o->Flags & WFLG_BORDERLESS)) continue;
        covered = l->bounds.MinX <= w->LeftEdge + w->Width - 1 && l->bounds.MaxX >= w->LeftEdge &&
                  l->bounds.MinY <= w->TopEdge + w->Height - 1 && l->bounds.MaxY >= w->TopEdge;
    }
    UnlockLayerInfo(&scr->LayerInfo);
    if (covered) WindowToFront(w);
}

static void events(struct Window *w)
{
    struct IntuiMessage *m;
    if (!w) return;
    while ((m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
        ULONG cls = m->Class;
        ReplyMsg((struct Message *)m);
        if (cls == IDCMP_REFRESHWINDOW) {
            BeginRefresh(w);
            if (w == logo_w) draw_logo(); else draw_clock(1);
            EndRefresh(w, TRUE);
        }
        /* a click on ours: the window before stays the active one */
        else if (cls == IDCMP_ACTIVEWINDOW && last_other) ActivateWindow(last_other);
    }
}

static int is_window(struct Window *w)
{
    struct Window *o;
    if (!w || !scr) return 0;
    for (o = scr->FirstWindow; o; o = o->NextWindow) if (o == w) return 1;
    return 0;
}

int main(void)
{
    struct MsgPort *port;
    int tick = 0, quit = 0;
    (void)version;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 39);
    if (!IntuitionBase || !GfxBase || !LayersBase || !UtilityBase) goto out;
    Forbid();
    port = FindPort((STRPTR)TB_PORT);
    Permit();
    if (port) { PutStr((STRPTR)"OpenTitle is already running\n"); goto out; }
    if (!(port = CreateMsgPort())) goto out;
    port->mp_Node.ln_Name = (char *)TB_PORT;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);

    read_prefs();
    apply_title();
    if (!open_all()) quit = 1;
    while (!quit) {
        ULONG sig = SetSignal(0, SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
        struct Window *act;
        if (sig & SIGBREAKF_CTRL_C) break;
        if (sig & SIGBREAKF_CTRL_F) {
            close_all();
            read_prefs();
            apply_title();
            if (!prefs.logo && !prefs.memory && !prefs.clock && !prefs.border_black) break;
            if (!open_all()) break;
        }
        events(logo_w);
        events(clock_w);
        act = IntuitionBase->ActiveWindow;
        if (act && act != logo_w && act != clock_w) last_other = act;
        if (last_other && !is_window(last_other)) last_other = NULL;
        draw_clock(0);
        if (++tick % 4 == 0) {                 /* every 2 s: the tray and OpenMenus' bar may have moved */
            int edge = read_bar_edge();
            WORD ox = cx, oy = cy, ow = cw, oh = ch;
            if (edge != bar_edge && clock_w) { close_all(); if (!open_all()) break; }
            else if (clock_w) {
                place();
                if (cx != ox || cy != oy || cw != ow || ch != oh) { ChangeWindowBox(clock_w, cx, cy, cw, ch); shown[0] = 0; }
            }
            keep_in_front(logo_w);
            keep_in_front(clock_w);
        }
        Delay(TICKS_PER_SECOND / 2);
    }
    close_all();
    RemPort(port);
    DeleteMsgPort(port);
out:
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
