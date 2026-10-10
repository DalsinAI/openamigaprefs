/* OpenDock 0.3: a dock on the Workbench screen (MIT). One row of icons
 * along an edge of the screen: a click starts the program, or brings it to
 * the front when it is running already; a small mark shows which are
 * running. Icons dropped on the dock are added to it; the menu (the right
 * mouse button while the dock is active) removes one, opens the Dock prefs,
 * or quits.
 *
 * Its settings are ENV:OpenDock/Dock (od_dock.h); the Dock prefs editor
 * changes them and tells OpenDock with Ctrl-F, which reads them again.
 * Ctrl-C (or Quit) ends it. A second OpenDock tells the first to read its
 * settings again and ends.
 *
 * It looks like the Mac's dock: one rounded shelf, tinted to the look and
 * see-through by the opacity setting, the icons evenly spaced on it. On a
 * graphics card the tint is mixed with what is behind, pixel by pixel; on
 * the native chipset it is drawn with the nearest pens. The shelf is made
 * once when the dock opens and kept, so a redraw is one blit and the icons,
 * and a real 68040 is never slowed. No magnification.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/imageclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxmacros.h>
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <workbench/icon.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/gadtools.h>
#include <proto/icon.h>
#include <proto/wb.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>

#include <string.h>
#include <stdio.h>

#include "od_dock.h"
#include "wbclose.h"

const char version[] __attribute__((used)) = "$VER: OpenDock 0.5 (10.10.2026) OpenPrefs, Dalsin Limited";

#define PREFS_ENV "ENV:OpenDock/Dock"
#define PREFS_ENVARC "ENVARC:OpenDock/Dock"
#define DOCK_PREFS "SYS:Prefs/Dock"
#define PORT_NAME "OpenDock"

struct Library *IconBase, *WorkbenchBase, *CyberGfxBase;

static od_dock dock;
static struct DiskObject *icons[OD_MAX];
static struct Task *running[OD_MAX];               /* compared, never followed */
static struct Window *win;
static struct Screen *scr;
static int cellw, cellh, along, standing, pressed = -1, removing;
static int wb_down;                                /* Workbench is shutting down or shut: 1 the dock is closed, 2 to open it again */

/* An icon at an item size under 100%, both looks ([0] as it is, [1] pressed). */
typedef struct { int w, h; ULONG *argb[2]; UWORD *pen[2]; } small_icon;
static small_icon small[OD_MAX];
static void free_small(int i);

/* ---- files -------------------------------------------------------------------------------- */

static char *read_file(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG len;
    char *buf;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len < 0 || len > 256 * 1024 || !(buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) { Close(fh); return NULL; }
    if (Read(fh, buf, len) != len) { FreeVec(buf); Close(fh); return NULL; }
    Close(fh);
    return buf;
}

static int write_file(const char *path, const char *data, LONG len)
{
    char dir[64], *slash;
    BPTR fh, lock;
    strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    if ((slash = strrchr(dir, '/'))) {
        *slash = 0;
        if ((lock = Lock((STRPTR)dir, ACCESS_READ))) UnLock(lock);
        else if ((lock = CreateDir((STRPTR)dir))) UnLock(lock);
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    if (Write(fh, (APTR)data, len) != len) { Close(fh); return 0; }
    Close(fh);
    return 1;
}

/* Is the program there? No "Please insert" requester while we look. */
static int exists(const char *path)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    BPTR l;
    me->pr_WindowPtr = (APTR)-1;
    l = Lock((STRPTR)path, ACCESS_READ);
    me->pr_WindowPtr = old;
    if (l) UnLock(l);
    return l != 0;
}

static void load(void)
{
    char *text = read_file(PREFS_ENV);
    if (!text) text = read_file(PREFS_ENVARC);
    if (text) { od_parse(&dock, text); FreeVec(text); }
    else { od_defaults(&dock); od_starter(&dock); od_drop_missing(&dock, exists); }   /* as the editor does */
}

/* A change made on the dock itself (an icon dropped, one removed) is kept, as AmiDock does. */
static void keep(void)
{
    static char text[32768];
    int n = od_write(&dock, text, sizeof text);
    if (n > 0 && write_file(PREFS_ENV, text, n)) write_file(PREFS_ENVARC, text, n);
}

/* ---- the icons ---------------------------------------------------------------------------- */

/* The program's name in a command: its first word, without quotes. */
static void program_of(const od_button *b, char *out, int size)
{
    const char *s = b->command;
    int n = 0;
    if (b->kind == OD_WB) { strncpy(out, s, size - 1); out[size - 1] = 0; return; }
    while (*s == ' ') s++;
    if (*s == '"') { s++; while (*s && *s != '"' && n < size - 1) out[n++] = *s++; }
    else while (*s && *s != ' ' && n < size - 1) out[n++] = *s++;
    out[n] = 0;
}

static int icons_ok;                                /* the icons are loaded for these buttons: a copy of the background alone needn't load them again */

static void free_icons(void)
{
    icons_ok = 0;
    for (int i = 0; i < OD_MAX; i++) {
        free_small(i);
        if (icons[i]) { FreeDiskObject(icons[i]); icons[i] = NULL; }
    }
}

/* An icon laid out for the dock's screen: OS 3.5 colour icons (as 3.2's own
 * are) and PNG icons are only drawn right once mapped to the screen's pens. */
static struct DiskObject *screen_icon(const char *name, int or_default)
{
    struct DiskObject *d = GetIconTags((STRPTR)name, ICONGETA_Screen, (ULONG)scr, ICONGETA_RemapIcon, TRUE,
                                       ICONGETA_GenerateImageMasks, TRUE, ICONGETA_FailIfUnavailable, TRUE, TAG_DONE);
    /* none: the default icon for a program (a Shell command has no file to go by) */
    if (!d && or_default)
        d = GetIconTags(NULL, ICONGETA_Screen, (ULONG)scr, ICONGETA_RemapIcon, TRUE, ICONGETA_GenerateImageMasks, TRUE,
                        ICONGETA_GetDefaultType, WBTOOL, TAG_DONE);
    return d;
}

static void load_icons(void)
{
    char prog[256];
    free_icons();
    for (int i = 0; i < dock.n; i++) {
        od_button *b = &dock.b[i];
        if (b->kind == OD_SEPARATOR) continue;
        if (b->icon[0]) icons[i] = screen_icon(b->icon, 0);
        /* the program's own icon (a drawer's too), or the default one for its kind */
        if (!icons[i]) { program_of(b, prog, sizeof prog); icons[i] = screen_icon(prog, 1); }
    }
}

/* ---- icons at 25, 50 and 75 per cent --------------------------------------------------------- */

/* Each icon is drawn once at its own size into a bitmap, over two backgrounds,
 * and averaged down to the item size. On a graphics card (more than 256
 * colours) it is drawn over black and over white: where the two differ, the
 * icon is see-through, by how much they differ, so soft edges stay soft. On
 * the native chipset it is drawn over two pens: where they differ, the icon
 * is see-through, and each small pixel takes the pen in the middle of its
 * square. Done once when the icons load, so drawing the dock stays one blit
 * per icon on a card and a few lines per icon on the chipset. */

#define NO_PEN 0xffff

static const struct TagItem plain_draw[] = {
    { ICONDRAWA_Frameless, TRUE }, { ICONDRAWA_Borderless, TRUE }, { ICONDRAWA_EraseBackground, FALSE }, { TAG_DONE, 0 }
};

static int truecolour(void)
{
    struct BitMap *bm = scr->RastPort.BitMap;
    if (!CyberGfxBase || !GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX)) return 0;
    return GetCyberMapAttr(bm, CYBRMATTR_DEPTH) > 8;
}

static void free_small(int i)
{
    for (int k = 0; k < 2; k++) {
        if (small[i].argb[k]) FreeVec(small[i].argb[k]);
        if (small[i].pen[k]) FreeVec(small[i].pen[k]);
    }
    memset(&small[i], 0, sizeof small[i]);
}

/* The icon at full size in rp over a background: ARGB colour c, or pen p when c is ~0. */
static void full_size(struct RastPort *rp, struct DiskObject *d, struct Rectangle *r, int iw, int ih, ULONG state, ULONG c, int p)
{
    if (c != ~0UL) FillPixelArray(rp, 0, 0, iw, ih, c);
    else { SetAPen(rp, p); RectFill(rp, 0, 0, iw - 1, ih - 1); }
    DrawIconStateA(rp, d, NULL, -r->MinX, -r->MinY, state, (struct TagItem *)plain_draw);
}

static void make_small(int i)
{
    struct Rectangle r;
    struct BitMap *bm;
    struct RastPort trp;
    ULONG *black = NULL, *white = NULL;
    int iw, ih, w, h, depth, card = truecolour();
    free_small(i);
    if (!icons[i] || dock.scale >= 100 || !GetIconRectangleA(&scr->RastPort, icons[i], NULL, &r, NULL)) return;
    iw = r.MaxX - r.MinX + 1; ih = r.MaxY - r.MinY + 1;
    w = od_scaled(iw, dock.scale); h = od_scaled(ih, dock.scale);
    depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    if (!(bm = AllocBitMap(iw, ih, depth, BMF_CLEAR, scr->RastPort.BitMap))) return;
    InitRastPort(&trp);
    trp.BitMap = bm;
    if (card) {
        black = AllocVec(iw * ih * 4, MEMF_ANY);
        white = AllocVec(iw * ih * 4, MEMF_ANY);
    }
    for (int k = 0; k < 2; k++) {
        ULONG state = k ? IDS_SELECTED : IDS_NORMAL;
        if (card && black && white) {
            ULONG *out = AllocVec(w * h * 4, MEMF_ANY);
            if (!out) break;
            full_size(&trp, icons[i], &r, iw, ih, state, 0x000000, 0);
            WaitBlit();
            ReadPixelArray(black, 0, 0, iw * 4, &trp, 0, 0, iw, ih, RECTFMT_ARGB);
            full_size(&trp, icons[i], &r, iw, ih, state, 0xffffff, 0);
            WaitBlit();
            ReadPixelArray(white, 0, 0, iw * 4, &trp, 0, 0, iw, ih, RECTFMT_ARGB);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    /* the square of full-size pixels behind this one: summed with their cover */
                    int x0 = x * iw / w, x1 = (x + 1) * iw / w, y0 = y * ih / h, y1 = (y + 1) * ih / h;
                    ULONG sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
                    if (x1 <= x0) x1 = x0 + 1;
                    if (y1 <= y0) y1 = y0 + 1;
                    for (int v = y0; v < y1; v++)
                        for (int u = x0; u < x1; u++) {
                            ULONG b = black[v * iw + u], wt = white[v * iw + u];
                            int diff = (int)(((wt >> 16) & 255) - ((b >> 16) & 255)) + (int)(((wt >> 8) & 255) - ((b >> 8) & 255)) +
                                       (int)((wt & 255) - (b & 255));
                            int a = 255 - diff / 3;
                            if (a < 0) a = 0;
                            if (a > 255) a = 255;
                            /* over black, a pixel is its colour times its cover already */
                            sa += a; sr += (b >> 16) & 255; sg += (b >> 8) & 255; sb += b & 255; n++;
                        }
                    {
                        ULONG a = sa / n, cr = 0, cg = 0, cb = 0;
                        if (sa) {
                            cr = sr * 255 / sa; cg = sg * 255 / sa; cb = sb * 255 / sa;
                            if (cr > 255) cr = 255;
                            if (cg > 255) cg = 255;
                            if (cb > 255) cb = 255;
                        }
                        out[y * w + x] = a << 24 | cr << 16 | cg << 8 | cb;
                    }
                }
            small[i].argb[k] = out;
        } else if (!card) {
            UWORD *out = AllocVec(w * h * 2, MEMF_ANY);
            if (!out) break;
            /* two pens it can't be drawn in alike: the screen's background and shadow */
            for (int y = 0; y < h; y++) {
                int v = (y * ih + ih / 2) / h;
                for (int x = 0; x < w; x++) out[y * w + x] = (x * iw + iw / 2) / w | (ULONG)v << 8;
            }
            {
                static UBYTE a[256 * 256];          /* one full-size icon's pens, over pen 0 */
                int big = iw <= 256 && ih <= 256;
                if (big) {
                    full_size(&trp, icons[i], &r, iw, ih, state, ~0UL, 0);
                    WaitBlit();
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            int u = out[y * w + x] & 255, v = out[y * w + x] >> 8;
                            a[v * 256 + u] = ReadPixel(&trp, u, v);
                        }
                    full_size(&trp, icons[i], &r, iw, ih, state, ~0UL, 1);
                    WaitBlit();
                    for (int y = 0; y < h; y++)
                        for (int x = 0; x < w; x++) {
                            int u = out[y * w + x] & 255, v = out[y * w + x] >> 8;
                            LONG p = ReadPixel(&trp, u, v);
                            out[y * w + x] = p == a[v * 256 + u] ? (UWORD)p : NO_PEN;
                        }
                } else for (int k2 = 0; k2 < w * h; k2++) out[k2] = NO_PEN;
            }
            small[i].pen[k] = out;
        }
    }
    small[i].w = w; small[i].h = h;
    if (black) FreeVec(black);
    if (white) FreeVec(white);
    WaitBlit();
    FreeBitMap(bm);
}

/* The small icon into rp at x, y. On a card it is mixed with what is there already. */
static void draw_small(struct RastPort *rp, int i, int x, int y, int k)
{
    small_icon *s = &small[i];
    if (s->argb[k]) {
        ULONG *under = AllocVec(s->w * s->h * 4, MEMF_ANY);
        if (!under) return;
        ReadPixelArray(under, 0, 0, s->w * 4, rp, x, y, s->w, s->h, RECTFMT_ARGB);
        for (int n = 0; n < s->w * s->h; n++) {
            ULONG c = s->argb[k][n], u = under[n], a = c >> 24;
            if (a == 255) under[n] = c;
            else if (a) {
                ULONG r = (((c >> 16) & 255) * a + ((u >> 16) & 255) * (255 - a)) / 255;
                ULONG g = (((c >> 8) & 255) * a + ((u >> 8) & 255) * (255 - a)) / 255;
                ULONG b = ((c & 255) * a + (u & 255) * (255 - a)) / 255;
                under[n] = 0xff000000 | r << 16 | g << 8 | b;
            }
        }
        WritePixelArray(under, 0, 0, s->w * 4, rp, x, y, s->w, s->h, RECTFMT_ARGB);
        FreeVec(under);
    } else if (s->pen[k]) {
        /* runs of one pen, as lines */
        for (int v = 0; v < s->h; v++) {
            const UWORD *row = s->pen[k] + v * s->w;
            for (int u = 0; u < s->w; ) {
                int e = u;
                if (row[u] == NO_PEN) { u++; continue; }
                while (e + 1 < s->w && row[e + 1] == row[u]) e++;
                SetAPen(rp, row[u]);
                RectFill(rp, x + u, y + v, x + e, y + v);
                u = e + 1;
            }
        }
    }
}

/* ---- running programs ------------------------------------------------------------------------ */

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

/* The task running a button's program: a process of its name (Workbench
 * names it so), or a Shell running it as its command. NULL when none. */
static struct Task *find_running(const od_button *b)
{
    char prog[256];
    const char *name;
    struct Task *t;
    LONG i, max;
    if (b->kind == OD_SEPARATOR || b->kind == OD_AREXX) return NULL;
    program_of(b, prog, sizeof prog);
    name = (const char *)FilePart((STRPTR)prog);
    if (!*name) return NULL;
    Forbid();
    t = FindTask((STRPTR)name);
    max = t ? 0 : MaxCli();
    for (i = 1; i <= max && !t; i++) {
        struct Process *p = FindCliProc(i);
        struct CommandLineInterface *cli = p ? (struct CommandLineInterface *)BADDR(p->pr_CLI) : NULL;
        if (cli && cli->cli_Module && cli->cli_CommandName) {
            UBYTE *bs = (UBYTE *)BADDR(cli->cli_CommandName);
            char cn[108];
            int n = bs[0] < sizeof cn - 1 ? bs[0] : sizeof cn - 1;
            memcpy(cn, bs + 1, n); cn[n] = 0;
            if (same_name((const char *)FilePart((STRPTR)cn), name)) t = (struct Task *)p;
        }
    }
    Permit();
    return t;
}

/* Brings the task's frontmost window and its screen to the front. */
static int to_front(struct Task *t)
{
    struct Window *w = NULL;
    ULONG lock = LockIBase(0);
    for (struct Screen *s = IntuitionBase->FirstScreen; s && !w; s = s->NextScreen)
        for (struct Window *x = s->FirstWindow; x; x = x->NextWindow)
            if (x->UserPort && x->UserPort->mp_SigTask == t) { w = x; break; }
    UnlockIBase(lock);
    if (!w) return 0;
    ScreenToFront(w->WScreen);
    WindowToFront(w);
    ActivateWindow(w);
    return 1;
}

static void start(const od_button *b)
{
    char cmd[400];
    if (b->kind == OD_WB) {
        if (WorkbenchBase && OpenWorkbenchObjectA((STRPTR)b->command, NULL)) return;
        DisplayBeep(scr);
        return;
    }
    if (b->kind == OD_AREXX) snprintf(cmd, sizeof cmd, "SYS:Rexxc/RX \"%s\"", b->command);
    else snprintf(cmd, sizeof cmd, "%s", b->command);
    {
        BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), old = 0, dir = b->dir[0] ? Lock((STRPTR)b->dir, ACCESS_READ) : 0;
        if (dir) old = CurrentDir(dir);
        if (!in || SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, 0, SYS_Asynch, TRUE,
                              NP_StackSize, b->stack > 4096 ? b->stack : 4096, TAG_DONE) == -1) {
            if (in) Close(in);
            DisplayBeep(scr);
        }
        if (dir) { CurrentDir(old); UnLock(dir); }
    }
}

/* ---- the window -------------------------------------------------------------------------------
 *
 * Like the Mac's: one shelf along the edge, a rounded rectangle tinted to
 * the look (dark on a dark look, light on a light one) through which the
 * desktop shows, with even room around and between the icons, a dot under
 * each running program and a thin line before a group. Above it is room for
 * an icon to hop when its program starts. What is behind the dock is copied
 * from the screen as the dock opens; the shelf is made from it once (on a
 * graphics card the tint is mixed into it pixel by pixel, the corners
 * smoothed) and kept, so a redraw, and each step of a hop, is one blit and
 * the icons. Everything is drawn into a bitmap of the window's size and
 * copied in at once, so nothing flickers. */

#define HOP_ROOM 10                                /* the hop's height, outside the shelf */
#define MAX_RADIUS 48

static struct BitMap *behind, *back, *shelfbm;    /* the screen behind the dock; the drawing; the shelf, made once */
static struct RastPort brp;
static int room, bx, by;                           /* hop room; where the first cell starts */
static int big, pad, sepw, radius, gap;            /* an icon's square; the room around; a separator's; the corners'; off the edge */
static int endpad;                                 /* the room at the shelf's ends (0.4: a third of an icon) */
static int apad, dot_at, dot_big;                  /* the room across the shelf; the dot's centre from its edge; a 5-pixel dot */
static int behind_w, behind_h;                     /* the size behind was copied at */
static struct Window *bubble;
static int bubble_for = -1;
static void bubble_off(void);

static int item_size(int i) { return dock.b[i].kind == OD_SEPARATOR ? sepw : (standing ? cellh : cellw); }

/* The button under a point in the window, or -1. */
static int hit(int x, int y)
{
    int pos = standing ? by : bx, at = standing ? y : x, across = standing ? x - bx : y - by;
    if (across < 0 || across >= (standing ? cellw : cellh)) return -1;
    for (int i = 0; i < dock.n; i++) {
        int s = item_size(i);
        if (win && pos + s > (standing ? win->Height : win->Width) - endpad) break;   /* past the edge: not drawn */
        if (at >= pos && at < pos + s) return dock.b[i].kind == OD_SEPARATOR ? -1 : i;
        pos += s;
    }
    return -1;
}

static void layout(int *w, int *h)
{
    struct Rectangle r;
    int label = dock.labels ? scr->RastPort.TxHeight + 2 : 0, thick, most = 0, margin, rows, trim;
    big = od_cell(dock.size);
    standing = dock.place == OD_LEFT || dock.place == OD_RIGHT;
    /* a cell fits the largest icon */
    for (int i = 0; i < dock.n; i++)
        if (icons[i] && GetIconRectangleA(&scr->RastPort, icons[i], NULL, &r, NULL)) {
            int iw = r.MaxX - r.MinX + 1 + 8, ih = r.MaxY - r.MinY + 1 + 8, a = standing ? iw : ih;
            if (iw > big) big = iw;
            if (ih > big) big = ih;
            if (a > most) most = a;                /* the largest across the shelf, with its 8 */
        }
    /* 25, 50 or 75 per cent: the cells shrink with the icons; the names stay readable */
    big = od_scaled(big, dock.scale);
    if (big < 12) big = 12;
    /* the Mac's proportions: a tenth of an icon around and between them, a
     * quarter of the shelf's height for its corners, a third for a group's gap */
    pad = big / 10 < 4 ? 4 : big / 10;
    /* 0.4 (the look of 8 October 2026): more room at the shelf's ends, a third
     * of an icon from the shelf's end to the first icon's edge (each cell
     * already holds four pixels round its icon) */
    endpad = big / 3 - 4 < pad ? pad : big / 3 - 4;
    sepw = big / 3 < 8 ? 8 : big / 3;
    gap = pad / 2;                                 /* the shelf floats a little off the screen's edge */
    cellw = big;
    cellh = big + label;
    /* across the shelf a little tighter than along it, as long as the running
     * dot keeps a clear row on both sides between the edge and what is nearest
     * it (the largest icon, or the names under the icons) */
    most = most ? od_scaled(most - 8, dock.scale) : big - 16;
    margin = dock.labels && dock.place == OD_BOTTOM ? 2 : (big - most) / 2;
    apad = pad - (big / 20 < 2 ? 2 : big / 20);
    if (apad < 6 - margin) apad = 6 - margin;
    if (apad < 2) apad = 2;
    rows = apad - 1 + margin;                      /* between the shelf's edge line and the nearest icon */
    /* 0.5 (10 October 2026): ten pixels less between the shelf's edge and the icons, above and below
     * (at the ends of a standing shelf, left and right), kept so the dot still has room */
    trim = rows - 5 < 10 ? (rows - 5 > 0 ? rows - 5 : 0) : 10;
    apad -= trim;
    rows -= trim;
    dot_big = big >= 40 && rows >= 7;
    dot_at = 1 + (rows - 1) / 2;
    thick = (standing ? cellw : cellh) + 2 * apad;
    radius = thick / 4 > MAX_RADIUS ? MAX_RADIUS : thick / 4;
    room = dock.hop ? HOP_ROOM : 0;
    along = 2 * endpad;
    for (int i = 0; i < dock.n; i++) along += item_size(i);
    if (along < 2 * endpad + cellw) along = 2 * endpad + cellw;
    /* the hop room is on the side away from the edge, the gap on the edge's side */
    bx = (standing ? apad : endpad) + (dock.place == OD_RIGHT ? room : 0) + (dock.place == OD_LEFT ? gap : 0);
    by = (standing ? endpad : apad) + (dock.place == OD_BOTTOM ? room : 0) + (dock.place == OD_TOP ? gap : 0);
    if (standing) { *w = thick + room + gap; *h = along; }
    else { *w = along; *h = thick + room + gap; }
    if (*w > scr->Width) *w = scr->Width;
    if (*h > scr->Height) *h = scr->Height;
}

static void place_of(int w, int h, int *x, int *y)
{
    switch (dock.place) {
    case OD_TOP: *x = (scr->Width - w) / 2; *y = scr->BarHeight + 1; break;
    case OD_LEFT: *x = 0; *y = (scr->Height - h) / 2; break;
    case OD_RIGHT: *x = scr->Width - w; *y = (scr->Height - h) / 2; break;
    default: *x = (scr->Width - w) / 2; *y = scr->Height - h;
    }
}

/* The shelf: the window less the hop room and the gap at the screen's edge. */
static void shelf(int W, int H, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = (dock.place == OD_RIGHT ? room : 0) + (dock.place == OD_LEFT ? gap : 0);
    *y0 = (dock.place == OD_BOTTOM ? room : 0) + (dock.place == OD_TOP ? gap : 0);
    *x1 = W - 1 - (dock.place == OD_LEFT ? room : 0) - (dock.place == OD_RIGHT ? gap : 0);
    *y1 = H - 1 - (dock.place == OD_TOP ? room : 0) - (dock.place == OD_BOTTOM ? gap : 0);
}

/* ---- the shelf's colours ------------------------------------------------------------------- */

#define LOOK_ENV "ENV:OpenGadTools/Look"

typedef struct { int r, g, b; } colour;
static colour tint, rim, sep_c, dot_c, bg_c;
static LONG tint_pen = -1, rim_pen = -1, sep_pen = -1, dot_pen = -1;
static int card;                                   /* a graphics card's true colour: the tint is mixed in */

static int luma(colour c) { return (c.r * 77 + c.g * 150 + c.b * 29) >> 8; }

static colour mix(colour a, colour b, int t)       /* t of 256 parts b */
{
    colour c;
    c.r = a.r + (b.r - a.r) * t / 256; c.g = a.g + (b.g - a.g) * t / 256; c.b = a.b + (b.b - a.b) * t / 256;
    return c;
}

static colour pen_colour(int pen)
{
    ULONG v[3] = { 0, 0, 0 };
    colour c;
    GetRGB32(scr->ViewPort.ColorMap, pen, 1, v);
    c.r = v[0] >> 24; c.g = v[1] >> 24; c.b = v[2] >> 24;
    return c;
}

/* The look's mode in Look prefs (its first line, "<theme> light|dark|auto"): 0 light, 1 dark, -1 none. */
static int look_mode(void)
{
    char *t = read_file(LOOK_ENV), name[48], mode[8];
    int m = -1;
    if (!t) return -1;
    if (sscanf(t, "%47s %7s", name, mode) == 2) {
        if (!strcmp(mode, "dark")) m = 1;
        else if (!strcmp(mode, "auto")) {
            struct DateStamp ds;
            int hour;
            DateStamp(&ds);
            hour = (int)(ds.ds_Minute / 60);
            m = hour >= 19 || hour < 7;
        } else m = 0;
    }
    FreeVec(t);
    return m;
}

static LONG best_pen(colour c)
{
    return ObtainBestPen(scr->ViewPort.ColorMap, (ULONG)c.r * 0x01010101UL, (ULONG)c.g * 0x01010101UL, (ULONG)c.b * 0x01010101UL,
                         OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}

static void free_pens(void)
{
    LONG *p[4] = { &tint_pen, &rim_pen, &sep_pen, &dot_pen };
    for (int i = 0; i < 4; i++) {
        if (*p[i] >= 0) ReleasePen(scr->ViewPort.ColorMap, *p[i]);
        *p[i] = -1;
    }
}

/* The tint follows the look: dark for a dark look, light for a light one,
 * from the screen's background where it fits; a neutral grey with no look. */
static void choose_colours(void)
{
    static const colour white = { 255, 255, 255 }, black = { 0, 0, 0 };
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    colour text = black;
    int mode = look_mode(), dark;
    bg_c = (colour){ 150, 150, 154 };
    if (dri) {
        bg_c = pen_colour(dri->dri_Pens[BACKGROUNDPEN]);
        text = pen_colour(dri->dri_Pens[TEXTPEN]);
        FreeScreenDrawInfo(scr, dri);
    }
    if (mode == 1) tint = luma(bg_c) < 110 ? mix(bg_c, black, 64) : (colour){ 36, 36, 40 };
    else if (mode == 0) tint = luma(bg_c) >= 150 ? mix(bg_c, white, 160) : (colour){ 236, 236, 240 };
    else tint = (colour){ 150, 150, 154 };
    dark = luma(tint) < 128;
    /* a slightly lighter edge; a group's line a step from the tint; the dot in the text's colour where it shows */
    rim = mix(tint, white, dark ? 72 : 150);
    sep_c = dark ? mix(tint, white, 48) : mix(tint, black, 64);
    dot_c = (luma(text) > luma(tint) ? luma(text) - luma(tint) : luma(tint) - luma(text)) >= 100 ? text :
            dark ? (colour){ 236, 236, 236 } : (colour){ 28, 28, 30 };
    free_pens();
    card = truecolour();
    dot_pen = best_pen(dot_c);
    if (!card) {
        /* the native chipset or 256 colours: the nearest pens, kept apart */
        tint_pen = best_pen(tint);
        rim_pen = best_pen(rim);
        if (rim_pen == tint_pen) { ReleasePen(scr->ViewPort.ColorMap, rim_pen); rim_pen = best_pen(mix(tint, dark ? white : black, 128)); }
        sep_pen = best_pen(sep_c);
        if (sep_pen == tint_pen) { ReleasePen(scr->ViewPort.ColorMap, sep_pen); sep_pen = best_pen(mix(tint, dark ? white : black, 160)); }
    }
}

/* ---- the shelf ----------------------------------------------------------------------------- */

/* A corner, as radius x radius cover: how much of each pixel is inside the
 * rounded edge, and how much of it is the 1-pixel rim, 0 to 255 (16 samples
 * a pixel). Index [j * radius + i], i and j counted from the corner. */
static UBYTE inside[MAX_RADIUS * MAX_RADIUS], rimcov[MAX_RADIUS * MAX_RADIUS];

static void make_corner(void)
{
    int R = radius, R8 = R * 8, in2 = R8 * R8, rim2 = (R8 - 8) * (R8 - 8);
    for (int j = 0; j < R; j++)
        for (int i = 0; i < R; i++) {
            int a = 0, b = 0;
            for (int v = 0; v < 4; v++)
                for (int u = 0; u < 4; u++) {
                    /* the sample's distance from the corner circle's centre, in eighths of a pixel */
                    int dx = R8 - (8 * i + 2 * u + 1), dy = R8 - (8 * j + 2 * v + 1), d2 = dx * dx + dy * dy;
                    if (d2 <= in2) { a++; if (d2 > rim2) b++; }
                }
            inside[j * R + i] = a * 255 / 16;
            rimcov[j * R + i] = b * 255 / 16;
        }
}

/* The cover of a pixel at (x, y) of a w x h shelf: in, and on the rim. */
static void cover(int x, int y, int w, int h, int *in, int *on_rim)
{
    int i = x < radius ? x : x >= w - radius ? w - 1 - x : -1;
    int j = y < radius ? y : y >= h - radius ? h - 1 - y : -1;
    if (i >= 0 && j >= 0) { *in = inside[j * radius + i]; *on_rim = rimcov[j * radius + i]; return; }
    *in = 255;
    *on_rim = x == 0 || y == 0 || x == w - 1 || y == h - 1 ? 255 : 0;
}

static int opacity(void) { return dock.background == OD_BG_SOLID ? 100 : dock.opacity; }

/* Calls fn for each separator with where its line goes. */
static void each_separator(int W, int H, void (*fn)(void *, int, int, int, int), void *ctx)
{
    int x0, y0, x1, y1, pos = standing ? by : bx;
    /* inset from the shelf's top and bottom as the Mac's is */
    int inset;
    shelf(W, H, &x0, &y0, &x1, &y1);
    inset = apad + (standing ? cellw : cellh) / 8;
    if (inset < 3) inset = 3;
    for (int i = 0; i < dock.n; i++) {
        int s = item_size(i);
        if (pos + s > (standing ? H : W) - endpad) break;
        if (dock.b[i].kind == OD_SEPARATOR) {
            if (standing) fn(ctx, x0 + inset, pos + s / 2, x1 - inset, pos + s / 2);
            else fn(ctx, pos + s / 2, y0 + inset, pos + s / 2, y1 - inset);
        }
        pos += s;
    }
}

/* ---- on a graphics card: the tint mixed in -------------------------------------------------- */

typedef struct { ULONG *px; int W; } argb_buf;

static ULONG blend(ULONG p, colour c, int a)       /* a of 255 parts c over p */
{
    int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
    r += (c.r - r) * a / 255; g += (c.g - g) * a / 255; b += (c.b - b) * a / 255;
    return 0xff000000UL | (ULONG)r << 16 | (ULONG)g << 8 | (ULONG)b;
}

/* Frosted glass: what is behind, blurred (a box of 2 radius+1 pixels, across then down). */
static void frost(ULONG *px, int W, int x0, int y0, int x1, int y1, int r)
{
    int n = (x1 - x0 + 1) > (y1 - y0 + 1) ? x1 - x0 + 1 : y1 - y0 + 1;
    ULONG *line = AllocVec(n * 4, MEMF_ANY);
    if (!line) return;
    for (int pass = 0; pass < 2; pass++) {
        int len = pass ? y1 - y0 + 1 : x1 - x0 + 1, lines = pass ? x1 - x0 + 1 : y1 - y0 + 1;
        for (int k = 0; k < lines; k++) {
            ULONG *first = pass ? px + y0 * W + x0 + k : px + (y0 + k) * W + x0;
            int step = pass ? W : 1, sr = 0, sg = 0, sb = 0, cnt = 2 * r + 1;
            for (int t = 0; t < len; t++) line[t] = first[t * step];
            /* a running sum, the ends repeated */
            for (int t = -r; t <= r; t++) {
                ULONG p = line[t < 0 ? 0 : t >= len ? len - 1 : t];
                sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255;
            }
            for (int t = 0; t < len; t++) {
                ULONG in, out;
                first[t * step] = 0xff000000UL | (ULONG)(sr / cnt) << 16 | (ULONG)(sg / cnt) << 8 | (ULONG)(sb / cnt);
                in = line[t + r + 1 < len ? t + r + 1 : len - 1];
                out = line[t - r < 0 ? 0 : t - r];
                sr += (int)((in >> 16) & 255) - (int)((out >> 16) & 255);
                sg += (int)((in >> 8) & 255) - (int)((out >> 8) & 255);
                sb += (int)(in & 255) - (int)(out & 255);
            }
        }
    }
    FreeVec(line);
}

static int dark_tint(void) { return luma(tint) < 128; }

/* A group's line: a step from the shelf as it shows, lighter on a dark tint, darker on a light one. */
static void argb_line(void *ctx, int xa, int ya, int xb, int yb)
{
    static const colour white = { 255, 255, 255 }, black = { 0, 0, 0 };
    argb_buf *b = ctx;
    int dark = dark_tint();
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) b->px[y * b->W + x] = blend(b->px[y * b->W + x], dark ? white : black, dark ? 56 : 48);
}

/* The shelf into rp (W x H at 0, 0) on a graphics card. 0 when there's no memory for it. */
static int make_shelf_card(struct RastPort *rp, int W, int H)
{
    ULONG *px = AllocVec(W * H * 4, MEMF_ANY);
    static const colour white = { 255, 255, 255 };
    int x0, y0, x1, y1, a = opacity() * 255 / 100, ra = dark_tint() ? 72 : 150, sw, sh;
    argb_buf b;
    if (!px) return 0;
    shelf(W, H, &x0, &y0, &x1, &y1);
    sw = x1 - x0 + 1; sh = y1 - y0 + 1;
    if (behind) {
        struct RastPort brp2;
        InitRastPort(&brp2);
        brp2.BitMap = behind;
        ReadPixelArray(px, 0, 0, W * 4, &brp2, 0, 0, W, H, RECTFMT_ARGB);
        if (dock.background == OD_BG_GLASS && a < 255) frost(px, W, x0, y0, x1, y1, big / 24 < 2 ? 2 : big / 24);   /* 0.4: a lighter frost */
    } else {
        /* nothing seen through: the screen's background */
        ULONG c = 0xff000000UL | (ULONG)bg_c.r << 16 | (ULONG)bg_c.g << 8 | (ULONG)bg_c.b;
        for (int n = 0; n < W * H; n++) px[n] = c;
        a = 255;
    }
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++) {
            int in, on_rim;
            ULONG *p = &px[(y0 + y) * W + x0 + x];
            cover(x, y, sw, sh, &in, &on_rim);
            if (!in) continue;
            if (a) *p = blend(*p, tint, a * in / 255);
            /* the edge: a little lighter than the shelf as it shows */
            if (dock.border && on_rim) *p = blend(*p, white, ra * on_rim / 255);
        }
    b.px = px; b.W = W;
    each_separator(W, H, argb_line, &b);
    WritePixelArray(px, 0, 0, W * 4, rp, 0, 0, W, H, RECTFMT_ARGB);
    FreeVec(px);
    return 1;
}

/* ---- on the native chipset (or 256 colours): the nearest pens ------------------------------- */

static void pen_line(void *ctx, int xa, int ya, int xb, int yb)
{
    struct RastPort *rp = ctx;
    RectFill(rp, xa, ya, xb, yb);
}

static void make_shelf_pens(struct RastPort *rp, int W, int H)
{
    static UWORD quarter[2] = { 0x8888, 0x2222 }, half[2] = { 0x5555, 0xaaaa }, three[2] = { 0x7777, 0xdddd };
    int x0, y0, x1, y1, sh, op = opacity(), R = radius;
    UWORD *pattern = NULL;
    int fill;
    shelf(W, H, &x0, &y0, &x1, &y1);
    sh = y1 - y0 + 1;
    SetDrMd(rp, JAM1);
    if (behind) BltBitMapRastPort(behind, 0, 0, rp, 0, 0, W, H, 0xc0);
    else {
        /* nothing seen through: the screen's background */
        struct DrawInfo *dri = GetScreenDrawInfo(scr);
        SetAPen(rp, dri ? dri->dri_Pens[BACKGROUNDPEN] : 0);
        RectFill(rp, 0, 0, W - 1, H - 1);
        if (dri) FreeScreenDrawInfo(scr, dri);
    }
    /* solid at 100 (or with nothing to see through); dithered for glass, by how solid; see-through: the edge alone */
    fill = op >= 100 || !behind || (dock.background == OD_BG_GLASS && op > 0);
    if (behind && op < 100) pattern = op <= 35 ? quarter : op < 70 ? half : three;   /* the default 35 is a quarter */
    if (fill && tint_pen >= 0) {
        SetAPen(rp, tint_pen);
        if (pattern) SetAfPt(rp, pattern, 1);
        for (int y = 0; y < sh; y++) {
            int j = y < R ? y : y >= sh - R ? sh - 1 - y : -1, l = 0;
            if (j < 0) {
                /* the straight part, at once */
                RectFill(rp, x0, y0 + y, x1, y0 + sh - R - 1);
                y = sh - R - 1;
                continue;
            }
            while (l < R && inside[j * R + l] < 128) l++;
            if (x0 + l <= x1 - l) RectFill(rp, x0 + l, y0 + y, x1 - l, y0 + y);
        }
        SetAfPt(rp, NULL, 0);
    }
    if (dock.border && rim_pen >= 0) {
        /* the edge: the straight lines, and the corners' outermost pixels inside */
        SetAPen(rp, rim_pen);
        RectFill(rp, x0 + R, y0, x1 - R, y0); RectFill(rp, x0 + R, y1, x1 - R, y1);
        RectFill(rp, x0, y0 + R, x0, y1 - R); RectFill(rp, x1, y0 + R, x1, y1 - R);
        for (int j = 0; j < R; j++)
            for (int i = 0; i < R; i++) {
                int in = inside[j * R + i] >= 128;
                int out_l = i == 0 || inside[j * R + i - 1] < 128, out_u = j == 0 || inside[(j - 1) * R + i] < 128;
                if (!in || !(out_l || out_u)) continue;
                WritePixel(rp, x0 + i, y0 + j); WritePixel(rp, x1 - i, y0 + j);
                WritePixel(rp, x0 + i, y1 - j); WritePixel(rp, x1 - i, y1 - j);
            }
    }
    if (sep_pen >= 0) { SetAPen(rp, sep_pen); each_separator(W, H, pen_line, rp); }
}

/* The shelf, made once into shelfbm (or straight into rp when there's no memory for it). */
static void make_shelf(struct RastPort *rp, int W, int H)
{
    make_corner();
    if (!(card && make_shelf_card(rp, W, H))) make_shelf_pens(rp, W, H);
}

/* The Mac's dot: a small round one in the dot's colour, smooth on a card. */
static void dot(struct RastPort *rp, int cx, int cy)
{
    int r4 = dot_big ? 10 : 6;                     /* 2.5 or 1.5 pixels, in quarters */
    if (card) {
        ULONG px[7 * 7];
        ReadPixelArray(px, 0, 0, 7 * 4, rp, cx - 3, cy - 3, 7, 7, RECTFMT_ARGB);
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 7; x++) {
                int n = 0;
                for (int v = 0; v < 4; v++)
                    for (int u = 0; u < 4; u++) {
                        int dx = 8 * x + 2 * u + 1 - 28, dy = 8 * y + 2 * v + 1 - 28;   /* from the dot's centre, in eighths */
                        if (dx * dx + dy * dy <= 4 * r4 * r4) n++;
                    }
                if (n) px[y * 7 + x] = blend(px[y * 7 + x], dot_c, n * 255 / 16);
            }
        WritePixelArray(px, 0, 0, 7 * 4, rp, cx - 3, cy - 3, 7, 7, RECTFMT_ARGB);
    } else {
        SetAPen(rp, dot_pen >= 0 ? dot_pen : 1);
        if (r4 > 6) { RectFill(rp, cx - 1, cy - 2, cx + 1, cy + 2); RectFill(rp, cx - 2, cy - 1, cx + 2, cy + 1); }
        else RectFill(rp, cx - 1, cy - 1, cx + 1, cy + 1);
    }
}

/* Everything, into the drawing, with button lift (if any) raised by lift pixels. */
static void render(int lifted, int lift)
{
    struct RastPort *rp = &brp;
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    UWORD *pens = dri ? dri->dri_Pens : NULL;
    int W = win->Width, H = win->Height, pos = standing ? by : bx, x0, y0, x1, y1;
    int shadow = pens ? pens[SHADOWPEN] : 1, text = dot_pen >= 0 ? dot_pen : pens ? pens[TEXTPEN] : 1;
    int edge = dot_at;                             /* the dot's centre, from the shelf's edge */
    shelf(W, H, &x0, &y0, &x1, &y1);
    SetDrMd(rp, JAM1);
    /* the shelf, as made when the dock opened: one blit */
    if (shelfbm) BltBitMapRastPort(shelfbm, 0, 0, rp, 0, 0, W, H, 0xc0);
    else make_shelf(rp, W, H);
    for (int i = 0; i < dock.n; i++) {
        int s = item_size(i), x = standing ? bx : pos, y = standing ? pos : by;
        od_button *b = &dock.b[i];
        /* the drawing has no layer to clip it: a dock longer than the screen stops at its edge */
        if (pos + s > (standing ? H : W) - endpad) break;
        if (b->kind != OD_SEPARATOR) {
            struct Rectangle r = { 0, 0, 0, 0 };
            int iw = 0, ih = 0, dx = 0, dy = 0;
            if (i == lifted) {
                /* away from the edge */
                if (dock.place == OD_BOTTOM) dy = -lift; else if (dock.place == OD_TOP) dy = lift;
                else if (dock.place == OD_LEFT) dx = lift; else dx = -lift;
            }
            if (small[i].w) { iw = small[i].w; ih = small[i].h; }
            else if (icons[i] && GetIconRectangleA(rp, icons[i], NULL, &r, NULL)) { iw = r.MaxX - r.MinX + 1; ih = r.MaxY - r.MinY + 1; }
            {
                int ix = x + (cellw - iw) / 2 + dx, iy = y + (big - ih) / 2 + dy;
                static struct TagItem plain[] = {
                    { ICONDRAWA_Frameless, TRUE }, { ICONDRAWA_Borderless, TRUE }, { ICONDRAWA_EraseBackground, FALSE }, { TAG_DONE, 0 }
                };
                if (small[i].w)
                    draw_small(rp, i, ix, iy, i == pressed);
                else if (icons[i])
                    DrawIconStateA(rp, icons[i], NULL, ix - r.MinX, iy - r.MinY, i == pressed ? IDS_SELECTED : IDS_NORMAL, plain);
                else {
                    /* no icon at all: the first letter in a box */
                    SetAPen(rp, shadow);
                    Move(rp, x + 8, y + 8); Draw(rp, x + big - 9, y + 8); Draw(rp, x + big - 9, y + big - 9);
                    Draw(rp, x + 8, y + big - 9); Draw(rp, x + 8, y + 8);
                    SetAPen(rp, text); SetDrMd(rp, JAM1);
                    Move(rp, x + big / 2 - rp->TxWidth / 2, y + big / 2 + rp->TxBaseline / 2);
                    Text(rp, (STRPTR)b->label, b->label[0] ? 1 : 0);
                }
                if (dock.labels) {
                    struct TextExtent te;
                    int n = TextFit(rp, (STRPTR)b->label, strlen(b->label), &te, NULL, 1, cellw - 4, rp->TxHeight);
                    SetAPen(rp, text); SetDrMd(rp, JAM1);
                    Move(rp, x + (cellw - te.te_Width) / 2, y + big + rp->TxBaseline);
                    Text(rp, (STRPTR)b->label, n);
                }
                if (dock.running && running[i]) {
                    /* the Mac's dot, between the icon and the screen's edge */
                    switch (dock.place) {
                    case OD_TOP: dot(rp, x + cellw / 2, y0 + edge); break;
                    case OD_LEFT: dot(rp, x0 + edge, y + big / 2); break;
                    case OD_RIGHT: dot(rp, x1 - edge, y + big / 2); break;
                    default: dot(rp, x + cellw / 2, y1 - edge);
                    }
                }
            }
        }
        pos += s;
    }
    if (dri) FreeScreenDrawInfo(scr, dri);
}

static void show(int lifted, int lift)
{
    if (!win) return;
    if (back) {
        render(lifted, lift);
        BltBitMapRastPort(back, 0, 0, win->RPort, 0, 0, win->Width, win->Height, 0xc0);
    } else {
        /* no memory for a drawing: straight into the window */
        brp = *win->RPort;
        render(lifted, lift);
    }
}

static void draw(void) { show(-1, 0); }

/* A started program's icon hops, as on the Mac: the shelf is the one made already. */
static void hop(int i)
{
    /* two bounces and a small one, two video frames a step: about a second */
    static const int lifts[] = { 3, 6, 8, 9, 8, 6, 3, 0, 3, 6, 8, 9, 8, 6, 3, 0, 2, 3, 2, 0 };
    if (!dock.hop || !back) return;
    bubble_off();
    for (unsigned k = 0; k < sizeof lifts / sizeof lifts[0]; k++) { show(i, lifts[k]); Delay(2); }
    show(-1, 0);
}

/* ---- the name above the icon under the pointer -------------------------------------------- */

static void bubble_off(void)
{
    if (bubble) { CloseWindow(bubble); bubble = NULL; }
    bubble_for = -1;
}

static void bubble_on(int i)
{
    struct RastPort *rp = &scr->RastPort;
    const char *name = dock.b[i].label;
    int n = strlen(name), bw = TextLength(rp, (STRPTR)name, n) + 14, bh = rp->TxHeight + 6, pos = standing ? by : bx, x, y;
    if (bw > scr->Width) {
        /* a name wider than the screen is cut to fit */
        struct TextExtent te;
        n = TextFit(rp, (STRPTR)name, n, &te, NULL, 1, scr->Width - 14, rp->TxHeight);
        bw = te.te_Width + 14;
    }
    for (int k = 0; k < i; k++) pos += item_size(k);
    if (standing) {
        y = win->TopEdge + pos + (cellh - bh) / 2;
        x = dock.place == OD_LEFT ? win->LeftEdge + win->Width + 2 : win->LeftEdge - bw - 2;
    } else {
        x = win->LeftEdge + pos + (cellw - bw) / 2;
        y = dock.place == OD_BOTTOM ? win->TopEdge + room - bh - 2 : win->TopEdge + win->Height + 2;
    }
    if (x < 0) x = 0;
    if (x + bw > scr->Width) x = scr->Width - bw;
    if (y < 0) y = 0;
    bubble_off();
    if ((bubble = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, bw, WA_Height, bh,
                                 WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, TAG_DONE))) {
        struct RastPort *b = bubble->RPort;
        struct DrawInfo *dri = GetScreenDrawInfo(scr);
        /* the screen's font (Font prefs' Screen text), as the labels under the icons: a window's own is Topaz */
        SetFont(b, scr->RastPort.Font);
        if (card) {
            /* the shelf's tint, and its rim as the frame */
            ULONG *px = AllocVec(bw * bh * 4, MEMF_ANY);
            if (px) {
                ULONG fill = 0xff000000UL | (ULONG)tint.r << 16 | (ULONG)tint.g << 8 | (ULONG)tint.b;
                ULONG edge = 0xff000000UL | (ULONG)sep_c.r << 16 | (ULONG)sep_c.g << 8 | (ULONG)sep_c.b;
                for (int yy = 0; yy < bh; yy++)
                    for (int xx = 0; xx < bw; xx++)
                        px[yy * bw + xx] = xx == 0 || yy == 0 || xx == bw - 1 || yy == bh - 1 ? edge : fill;
                WritePixelArray(px, 0, 0, bw * 4, b, 0, 0, bw, bh, RECTFMT_ARGB);
                FreeVec(px);
            }
        } else if (tint_pen >= 0) {
            SetAPen(b, tint_pen); RectFill(b, 0, 0, bw - 1, bh - 1);
            SetAPen(b, sep_pen >= 0 ? sep_pen : (dri ? dri->dri_Pens[SHADOWPEN] : 1));
            Move(b, 0, 0); Draw(b, bw - 1, 0); Draw(b, bw - 1, bh - 1); Draw(b, 0, bh - 1); Draw(b, 0, 0);
        } else {
            SetAPen(b, dri ? dri->dri_Pens[SHINEPEN] : 2); RectFill(b, 1, 1, bw - 2, bh - 2);
            SetAPen(b, dri ? dri->dri_Pens[SHADOWPEN] : 1);
            Move(b, 1, 0); Draw(b, bw - 2, 0); Move(b, bw - 1, 1); Draw(b, bw - 1, bh - 2);
            Move(b, bw - 2, bh - 1); Draw(b, 1, bh - 1); Move(b, 0, bh - 2); Draw(b, 0, 1);
        }
        SetAPen(b, dot_pen >= 0 ? dot_pen : dri ? dri->dri_Pens[TEXTPEN] : 1); SetDrMd(b, JAM1);
        Move(b, 7, 3 + b->TxBaseline); Text(b, (STRPTR)name, n);
        if (dri) FreeScreenDrawInfo(scr, dri);
        bubble_for = i;
    }
}

/* Called ten times a second: the name follows the pointer. */
static void check_hover(void)
{
    int i = -1;
    if (dock.hover && win && IntuitionBase->FirstScreen == scr) {
        int mx = scr->MouseX - win->LeftEdge, my = scr->MouseY - win->TopEdge;
        if (mx >= 0 && my >= 0 && mx < win->Width && my < win->Height) {
            /* only where the dock is seen, not under a window over it */
            struct Layer *l;
            LockLayerInfo(&scr->LayerInfo);
            l = WhichLayer(&scr->LayerInfo, scr->MouseX, scr->MouseY);
            UnlockLayerInfo(&scr->LayerInfo);
            if (l == win->WLayer) i = hit(mx, my);
        }
    }
    if (i != bubble_for) { if (i >= 0) bubble_on(i); else bubble_off(); }
}

/* ---- seeing through, kept up to date ------------------------------------------------------- */

/* The windows over the dock's place, as one number: where each is, how big,
 * in their order. It changes when a window is moved, sized, opened or closed
 * there, or brought in front of another. */
static ULONG behind_sig(void)
{
    ULONG sig = 0, lock = LockIBase(0);
    for (struct Window *w = scr->FirstWindow; w; w = w->NextWindow) {
        /* the dock sits just above the backdrop: only backdrop windows are behind it */
        if (w == win || w == bubble || !(w->Flags & WFLG_BACKDROP)) continue;
        if (w->LeftEdge >= win->LeftEdge + win->Width || w->LeftEdge + w->Width <= win->LeftEdge ||
            w->TopEdge >= win->TopEdge + win->Height || w->TopEdge + w->Height <= win->TopEdge) continue;
        sig = sig * 31 + (ULONG)w;
        sig = sig * 31 + (((ULONG)(UWORD)w->LeftEdge << 16) | (UWORD)w->TopEdge);
        sig = sig * 31 + (((ULONG)(UWORD)w->Width << 16) | (UWORD)w->Height);
    }
    UnlockIBase(lock);
    return sig;
}

static ULONG seen_sig, moving_sig;
static int still;
static int stale_wait;                             /* ticks to wait after a copy made for staleness */

/* True when the screen beside the dock (a line along its screen side) no longer matches the copy of what
 * is behind it: the picture on the desktop was drawn or changed after the dock opened, or a window that
 * was over the dock's place when it opened has gone. Only on a graphics card, and only where the desktop
 * itself shows beside the dock. */
static int behind_stale(void)
{
    int n, along_x = !standing, diff = 0, count = 0;
    int lx, ly, W, H;
    ULONG *screen_px, *copy_px;
    struct RastPort brp2;
    if (!win || !behind || !card || !CyberGfxBase) return 0;
    W = win->Width; H = win->Height;
    n = along_x ? W : H;
    /* the line beside the dock, on the side the screen's edge isn't */
    if (along_x) { lx = win->LeftEdge; ly = dock.place == OD_TOP ? win->TopEdge + H : win->TopEdge - 1; }
    else { ly = win->TopEdge; lx = dock.place == OD_LEFT ? win->LeftEdge + W : win->LeftEdge - 1; }
    if (lx < 0 || ly < 0 || lx >= scr->Width || ly >= scr->Height) return 0;
    if (along_x ? lx + n > scr->Width : ly + n > scr->Height) return 0;
    {
        int i, ok = 1;
        LockLayerInfo(&scr->LayerInfo);
        for (i = 0; i < n && ok; i += 16) {
            struct Layer *l = WhichLayer(&scr->LayerInfo, lx + (along_x ? i : 0), ly + (along_x ? 0 : i));
            if (!l || !l->Window || !(((struct Window *)l->Window)->Flags & WFLG_BACKDROP)) ok = 0;   /* a window is over the desktop there */
        }
        UnlockLayerInfo(&scr->LayerInfo);
        if (!ok) return 0;
    }
    screen_px = AllocVec(n * 4, MEMF_ANY);
    copy_px = AllocVec(n * 4, MEMF_ANY);
    if (screen_px && copy_px) {
        InitRastPort(&brp2);
        brp2.BitMap = behind;
        ReadPixelArray(screen_px, 0, 0, along_x ? n * 4 : 4, &scr->RastPort, lx, ly, along_x ? n : 1, along_x ? 1 : n, RECTFMT_ARGB);
        {
            /* the copy's own line on that side */
            int cx = along_x ? 0 : (dock.place == OD_LEFT ? W - 1 : 0), cy = along_x ? (dock.place == OD_TOP ? H - 1 : 0) : 0;
            ReadPixelArray(copy_px, 0, 0, along_x ? n * 4 : 4, &brp2, cx, cy, along_x ? n : 1, along_x ? 1 : n, RECTFMT_ARGB);
        }
        for (int i = 0; i < n; i += 4) {
            ULONG a = screen_px[i], b = copy_px[i];
            int dr = (int)((a >> 16) & 255) - (int)((b >> 16) & 255), dg = (int)((a >> 8) & 255) - (int)((b >> 8) & 255),
                db = (int)(a & 255) - (int)(b & 255);
            diff += (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);
            count++;
        }
        diff = count ? diff / count / 3 : 0;
    }
    if (screen_px) FreeVec(screen_px);
    if (copy_px) FreeVec(copy_px);
    return diff > 14;
}

/* Called ten times a second. True once a window has been dropped behind the
 * dock (what is there changed, then stayed put for 0.3 seconds, so a window
 * dragged solidly is copied once, where it lands): time to copy afresh. */
static int behind_changed(void)
{
    ULONG sig;
    if (!win || !behind) return 0;
    sig = behind_sig();
    if (sig == seen_sig) { still = 0; return 0; }
    if (sig != moving_sig) { moving_sig = sig; still = 0; return 0; }
    return ++still >= 3;
}

/* ---- opening and closing -------------------------------------------------------------------- */

static struct MsgPort *appport;
static struct AppWindow *aw;

static void free_bitmaps(void)
{
    WaitBlit();                                    /* the blitter may still be drawing in them */
    if (behind) { FreeBitMap(behind); behind = NULL; }
    if (back) { FreeBitMap(back); back = NULL; }
    if (shelfbm) { FreeBitMap(shelfbm); shelfbm = NULL; }
}

/* True when a window other than a backdrop one is over the box: the screen
 * there shows that window, not what is behind the dock. */
static int covered(int x, int y, int w, int h)
{
    int over = 0;
    ULONG lock = LockIBase(0);
    for (struct Window *v = scr->FirstWindow; v && !over; v = v->NextWindow) {
        if (v == win || v == bubble || (v->Flags & WFLG_BACKDROP)) continue;
        over = !(v->LeftEdge >= x + w || v->LeftEdge + v->Width <= x || v->TopEdge >= y + h || v->TopEdge + v->Height <= y);
    }
    UnlockIBase(lock);
    return over;
}

static int open_dock(struct Menu *menus)
{
    int w, h, x, y, depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    /* Loaded again only when the buttons or settings changed: icon.library gave wrong colours back for icons
     * freed and fetched again while the same screen was up (0.5) */
    if (!icons_ok) {
        load_icons();
        for (int i = 0; i < dock.n; i++) make_small(i);
        icons_ok = 1;
    }
    choose_colours();
    layout(&w, &h);
    place_of(w, h, &x, &y);
    /* what is behind the dock, before it opens; and the bitmap it is drawn in.
     * A window over the dock's place would be copied too, so then the last
     * copy is kept when it is the same size, or the shelf is drawn solid. */
    {
        struct BitMap *kept = NULL;
        int over = covered(x, y, w, h);
        if (over && behind && behind_w == w && behind_h == h) { kept = behind; behind = NULL; }
        free_bitmaps();
        behind = kept;
        /* A copy even with a window over the place (0.5): the screen as it shows, the best there is; once
         * the window is gone behind_stale() sees the dock no longer matches its neighbours and copies afresh.
         * Without one the shelf's corners and the room round it were a grey slab. */
        if (!behind) {
            if ((behind = AllocBitMap(w, h, depth, 0, scr->RastPort.BitMap)))
                BltBitMap(scr->RastPort.BitMap, x, y, behind, 0, 0, w, h, 0xc0, 0xff, NULL);
        }
        behind_w = w; behind_h = h;
    }
    back = AllocBitMap(w, h, depth, 0, scr->RastPort.BitMap);
    InitRastPort(&brp);
    brp.BitMap = back;
    SetFont(&brp, scr->RastPort.Font);
    /* the shelf, made once for this size, this copy of what is behind and these settings */
    if (back && (shelfbm = AllocBitMap(w, h, depth, 0, scr->RastPort.BitMap))) {
        struct RastPort srp;
        InitRastPort(&srp);
        srp.BitMap = shelfbm;
        make_shelf(&srp, w, h);
    }
    win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                         WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_ScreenTitle, (ULONG)"OpenDock 0.5",
                         WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_MENUPICK | IDCMP_REFRESHWINDOW | IDCMP_INACTIVEWINDOW |
                                   IDCMP_ACTIVEWINDOW, TAG_DONE);
    if (!win) { free_bitmaps(); return 0; }
    /* one above the backdrop: every other window, Workbench's drawers too, goes over it */
    WindowToBack(win);
    if (menus) SetMenuStrip(win, menus);
    if (!back && behind) {
        /* without a drawing the copy can't be used: the dock is solid */
        WaitBlit();
        FreeBitMap(behind);
        behind = NULL;
    }
    draw();
    seen_sig = moving_sig = behind_sig();
    still = 0;
    if (WorkbenchBase && appport) aw = AddAppWindowA(0, 0, win, appport, NULL);
    return 1;
}

static void close_dock(void)
{
    bubble_off();
    if (scr) free_pens();
    if (aw) { RemoveAppWindow(aw); aw = NULL; }
    if (!win) return;
    ClearMenuStrip(win);
    CloseWindow(win);
    win = NULL;
    free_bitmaps();
}

/* Settings or buttons changed: the dock opens again, so what is behind it is copied afresh. */
static int relayout(struct Menu *menus)
{
    close_dock();
    Delay(5);                                      /* Workbench redraws what the dock covered */
    return open_dock(menus);
}

static void check_running(void)
{
    int changed = 0;
    for (int i = 0; i < dock.n; i++) {
        struct Task *t = dock.running ? find_running(&dock.b[i]) : NULL;
        if ((t != NULL) != (running[i] != NULL)) changed = 1;
        running[i] = t;
    }
    if (changed && win) draw();
}

/* An icon dropped on the dock: a Workbench button for it at the end. */
static void dropped(struct AppMessage *am)
{
    for (LONG i = 0; i < am->am_NumArgs; i++) {
        char path[256];
        od_button *b;
        if (dock.n >= OD_MAX) { DisplayBeep(scr); break; }
        if (!NameFromLock(am->am_ArgList[i].wa_Lock, (STRPTR)path, sizeof path)) continue;
        if (am->am_ArgList[i].wa_Name && am->am_ArgList[i].wa_Name[0]) AddPart((STRPTR)path, am->am_ArgList[i].wa_Name, sizeof path);
        b = &dock.b[dock.n++];
        memset(b, 0, sizeof *b);
        b->kind = OD_WB;
        b->stack = 4096;
        strlcpy(b->command, path, sizeof b->command);
        strlcpy(b->label, (const char *)FilePart((STRPTR)path), sizeof b->label);   /* a label is short: cut to fit */
        if (!b->label[0]) strlcpy(b->label, path, sizeof b->label);
    }
    keep();
}

static void remove_button(int i)
{
    memmove(&dock.b[i], &dock.b[i + 1], (dock.n - i - 1) * sizeof dock.b[0]);
    dock.n--;
    /* a separator left at an end, or beside another, goes too */
    while (dock.n && dock.b[dock.n - 1].kind == OD_SEPARATOR) dock.n--;
    while (dock.n && dock.b[0].kind == OD_SEPARATOR) { memmove(&dock.b[0], &dock.b[1], (dock.n - 1) * sizeof dock.b[0]); dock.n--; }
    keep();
}

enum { M_PREFS = 1, M_REMOVE, M_QUIT };
static struct NewMenu newmenus[] = {
    { NM_TITLE, (STRPTR)"OpenDock", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Dock settings...", (STRPTR)"P", 0, 0, (APTR)M_PREFS },
    { NM_ITEM, (STRPTR)"Remove a button", (STRPTR)"R", 0, 0, (APTR)M_REMOVE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

int main(void)
{
    struct MsgPort *port, *tport = NULL;
    struct timerequest *tr = NULL;
    struct Menu *menus = NULL;
    APTR vi = NULL;
    int quit = 0, timer = 0, ticks = 0, rc = RETURN_OK;
    ULONG sigs;

    Forbid();
    if ((port = FindPort((STRPTR)PORT_NAME))) {
        /* one dock: the first reads its settings again */
        Signal(port->mp_SigTask, SIGBREAKF_CTRL_F);
        Permit();
        return RETURN_OK;
    }
    Permit();
    if (!(IconBase = OpenLibrary((STRPTR)"icon.library", 44))) { PutStr((STRPTR)"OpenDock needs icon.library 44 (AmigaOS 3.5 or later).\n"); return RETURN_FAIL; }
    WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 44);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 41);   /* small icons on a graphics card */
    wbc_start();
    if (!(port = CreateMsgPort())) { rc = RETURN_FAIL; goto out; }
    port->mp_Node.ln_Name = (char *)PORT_NAME;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);

    load();
    if (!(scr = LockPubScreen((STRPTR)"Workbench"))) { rc = RETURN_FAIL; goto out; }
    if ((vi = GetVisualInfoA(scr, NULL)) && (menus = CreateMenus(newmenus, TAG_DONE)))
        LayoutMenus(menus, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    if (WorkbenchBase) appport = CreateMsgPort();
    if (!open_dock(menus)) { rc = RETURN_FAIL; goto out; }
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    check_running();
    /* ten times a second for the name under the pointer; every two seconds for running programs */
    if (timer) { tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 100000; SendIO((struct IORequest *)tr); }

    while (!quit && (win || wb_down)) {
        ULONG got;
        struct IntuiMessage *m;
        sigs = (appport ? 1UL << appport->mp_SigBit : 0) | (timer ? 1UL << tport->mp_SigBit : 0) | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F |
               wbc_sigmask() | (win ? 1UL << win->UserPort->mp_SigBit : 0);
        got = Wait(sigs);
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        /* Workbench is shutting down (Font prefs' Use, a screen mode): no window, no lock on its screen until it is back */
        switch (wbc_take()) {
        case WBC_CLOSE:
            close_dock();
            free_icons();
            if (menus) { FreeMenus(menus); menus = NULL; }
            if (vi) { FreeVisualInfo(vi); vi = NULL; }
            if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
            wb_down = 1;
            wbc_closed();
            break;
        case WBC_OPEN: wb_down = 2; break;
        }
        if (wb_down == 2 && (scr = LockPubScreen((STRPTR)"Workbench"))) {
            if ((vi = GetVisualInfoA(scr, NULL)) && (menus = CreateMenus(newmenus, TAG_DONE)))
                LayoutMenus(menus, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
            if (open_dock(menus)) { wb_down = 0; check_running(); }
            else {                                 /* the screen isn't ready yet: try again */
                close_dock();
                if (menus) { FreeMenus(menus); menus = NULL; }
                if (vi) { FreeVisualInfo(vi); vi = NULL; }
                UnlockPubScreen(NULL, scr); scr = NULL;
            }
        }
        if (wb_down) continue;
        if (got & SIGBREAKF_CTRL_F) { load(); free_icons(); relayout(menus); check_running(); }
        if (timer && CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            check_hover();
            if (behind_changed()) relayout(menus);
            else if (ticks % 10 == 9 && stale_wait <= 0 && behind_stale()) { stale_wait = 30; relayout(menus); }
            if (stale_wait > 0) stale_wait--;
            if (++ticks >= 20) { ticks = 0; check_running(); }
            tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 100000;
            SendIO((struct IORequest *)tr);
        }
        if (appport) {
            struct AppMessage *am;
            int any = 0;
            while ((am = (struct AppMessage *)GetMsg(appport))) { dropped(am); ReplyMsg((struct Message *)am); any = 1; }
            if (any) { free_icons(); relayout(menus); check_running(); }
        }
        while (win && (m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            WORD mx = m->MouseX, my = m->MouseY;
            ReplyMsg((struct Message *)m);
            if (cls == IDCMP_REFRESHWINDOW) { BeginRefresh(win); draw(); EndRefresh(win, TRUE); }
            else if (cls == IDCMP_ACTIVEWINDOW) WindowToBack(win);   /* a click never brings it over a window */
            else if (cls == IDCMP_INACTIVEWINDOW && pressed >= 0) { pressed = -1; draw(); }
            else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) { pressed = hit(mx, my); if (pressed >= 0) draw(); }
            else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTUP) {
                int i = hit(mx, my), was = pressed;
                pressed = -1;
                if (was >= 0) draw();
                if (i >= 0 && i == was) {
                    if (removing) { removing = 0; remove_button(i); free_icons(); relayout(menus); check_running(); }
                    else if (!(running[i] && to_front(running[i]))) { start(&dock.b[i]); hop(i); }
                }
            } else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL) {
                    struct MenuItem *it = ItemAddress(menus, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case M_PREFS: SystemTags((STRPTR)"Run >NIL: " DOCK_PREFS, TAG_DONE); break;
                    case M_REMOVE: removing = 1; break;
                    case M_QUIT: quit = 1; break;
                    }
                    code = it->NextSelect;
                }
            }
        }
    }
out:
    wbc_stop();
    if (timer) { if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr); WaitIO((struct IORequest *)tr); CloseDevice((struct IORequest *)tr); }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    close_dock();
    if (appport) { struct Message *m; while ((m = GetMsg(appport))) ReplyMsg(m); DeleteMsgPort(appport); }
    if (menus) FreeMenus(menus);
    if (vi) FreeVisualInfo(vi);
    if (scr) UnlockPubScreen(NULL, scr);
    free_icons();
    if (port) { RemPort(port); DeleteMsgPort(port); }
    if (WorkbenchBase) CloseLibrary(WorkbenchBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    CloseLibrary(IconBase);
    return rc;
}
