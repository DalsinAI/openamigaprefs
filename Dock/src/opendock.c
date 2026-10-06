/* OpenDock 0.1: a dock on the Workbench screen (MIT). One row of icons
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
 * No magnification, no see-through: it draws with the screen's own pens and
 * the icons as they are, so a real 68040 on its native chipset or a graphics
 * card is never slowed.
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

#include <string.h>
#include <stdio.h>

#include "od_dock.h"

const char version[] __attribute__((used)) = "$VER: OpenDock 0.1 (6.10.2026) OpenPrefs, Dalsin Limited";

#define PREFS_ENV "ENV:OpenDock/Dock"
#define PREFS_ENVARC "ENVARC:OpenDock/Dock"
#define DOCK_PREFS "SYS:Prefs/Dock"
#define PORT_NAME "OpenDock"
#define SEP_W 8                                    /* a separator's width (or height, standing) */

struct Library *IconBase, *WorkbenchBase;

static od_dock dock;
static struct DiskObject *icons[OD_MAX];
static struct Task *running[OD_MAX];               /* compared, never followed */
static struct Window *win;
static struct Screen *scr;
static int cellw, cellh, along, standing, pressed = -1, removing;

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

static void load(void)
{
    char *text = read_file(PREFS_ENV);
    if (!text) text = read_file(PREFS_ENVARC);
    if (text) { od_parse(&dock, text); FreeVec(text); }
    else { od_defaults(&dock); od_starter(&dock); }
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

static void free_icons(void)
{
    for (int i = 0; i < OD_MAX; i++) if (icons[i]) { FreeDiskObject(icons[i]); icons[i] = NULL; }
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
 * Like the Mac's: a shelf along the edge, see-through (frosted by default),
 * with room above it for an icon to hop when its program starts. What is
 * behind the dock is copied from the screen as the dock opens, so seeing
 * through costs one blit and never reads the screen again; everything is
 * drawn into a bitmap of the window's size and copied in at once, so
 * nothing flickers. */

#define HOP_ROOM 10                                /* the hop's height, outside the shelf */

static struct BitMap *behind, *back;               /* the screen behind the dock; the drawing */
static struct RastPort brp;
static int room, bx, by;                           /* hop room; where the first cell starts */
static struct Window *bubble;
static int bubble_for = -1;
static void bubble_off(void);

static int item_size(int i) { return dock.b[i].kind == OD_SEPARATOR ? SEP_W : (standing ? cellh : cellw); }

/* The button under a point in the window, or -1. */
static int hit(int x, int y)
{
    int pos = standing ? by : bx, at = standing ? y : x, across = standing ? x - bx : y - by;
    if (across < 0 || across >= (standing ? cellw : cellh)) return -1;
    for (int i = 0; i < dock.n; i++) {
        int s = item_size(i);
        if (win && pos + s > (standing ? win->Height : win->Width) - 4) break;   /* past the edge: not drawn */
        if (at >= pos && at < pos + s) return dock.b[i].kind == OD_SEPARATOR ? -1 : i;
        pos += s;
    }
    return -1;
}

static void layout(int *w, int *h)
{
    struct Rectangle r;
    int big = od_cell(dock.size), label = dock.labels ? scr->RastPort.TxHeight + 2 : 0;
    standing = dock.place == OD_LEFT || dock.place == OD_RIGHT;
    /* a cell fits the largest icon */
    for (int i = 0; i < dock.n; i++)
        if (icons[i] && GetIconRectangleA(&scr->RastPort, icons[i], NULL, &r, NULL)) {
            int iw = r.MaxX - r.MinX + 1 + 8, ih = r.MaxY - r.MinY + 1 + 8;
            if (iw > big) big = iw;
            if (ih > big) big = ih;
        }
    cellw = big;
    cellh = big + label + 4;                       /* and the running mark */
    room = dock.hop ? HOP_ROOM : 0;
    along = 8;
    for (int i = 0; i < dock.n; i++) along += item_size(i);
    if (along < 8 + cellw) along = 8 + cellw;
    /* the hop room is on the side away from the edge */
    bx = 4 + (dock.place == OD_RIGHT ? room : 0);
    by = 4 + (dock.place == OD_BOTTOM ? room : 0);
    if (standing) { *w = cellw + 8 + room; *h = along; }
    else { *w = along; *h = cellh + 8 + room; }
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

/* The shelf: the window less the hop room. */
static void shelf(int W, int H, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = dock.place == OD_RIGHT ? room : 0;
    *y0 = dock.place == OD_BOTTOM ? room : 0;
    *x1 = W - 1 - (dock.place == OD_LEFT ? room : 0);
    *y1 = H - 1 - (dock.place == OD_TOP ? room : 0);
}

/* Everything, into the drawing, with button lift (if any) raised by lift pixels. */
static void render(int lifted, int lift)
{
    struct RastPort *rp = &brp;
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    UWORD *pens = dri ? dri->dri_Pens : NULL;
    int W = win->Width, H = win->Height, pos = standing ? by : bx, x0, y0, x1, y1;
    int bg = pens ? pens[BACKGROUNDPEN] : 0, shine = pens ? pens[SHINEPEN] : 2, shadow = pens ? pens[SHADOWPEN] : 1;
    int text = pens ? pens[TEXTPEN] : 1, fill = pens ? pens[FILLPEN] : 3;
    static UWORD frost[2] = { 0x5555, 0xaaaa };
    shelf(W, H, &x0, &y0, &x1, &y1);
    SetDrMd(rp, JAM1);
    /* behind the dock, then the shelf: rounded by leaving its corners see-through */
    if (behind) BltBitMap(behind, 0, 0, rp->BitMap, 0, 0, W, H, 0xc0, 0xff, NULL);
    else { SetAPen(rp, bg); RectFill(rp, 0, 0, W - 1, H - 1); }
    if (dock.background != OD_BG_CLEAR || !behind) {
        SetAPen(rp, dock.background == OD_BG_GLASS && behind ? shine : bg);
        if (dock.background == OD_BG_GLASS && behind) SetAfPt(rp, frost, 1);
        RectFill(rp, x0 + 2, y0, x1 - 2, y1);
        RectFill(rp, x0, y0 + 2, x0 + 1, y1 - 2);
        RectFill(rp, x1 - 1, y0 + 2, x1, y1 - 2);
        RectFill(rp, x0 + 1, y0 + 1, x0 + 1, y1 - 1);
        RectFill(rp, x1 - 1, y0 + 1, x1 - 1, y1 - 1);
        SetAfPt(rp, NULL, 0);
    }
    SetAPen(rp, shine); Move(rp, x0, y1 - 2); Draw(rp, x0, y0 + 2); Draw(rp, x0 + 2, y0); Draw(rp, x1 - 2, y0);
    SetAPen(rp, shadow); Move(rp, x1 - 1, y0 + 1); Draw(rp, x1, y0 + 2); Draw(rp, x1, y1 - 2); Draw(rp, x1 - 2, y1); Draw(rp, x0 + 2, y1);
    Draw(rp, x0 + 1, y1 - 1);
    for (int i = 0; i < dock.n; i++) {
        int s = item_size(i), x = standing ? bx : pos, y = standing ? pos : by;
        od_button *b = &dock.b[i];
        /* the drawing has no layer to clip it: a dock longer than the screen stops at its edge */
        if (pos + s > (standing ? H : W) - 4) break;
        if (b->kind == OD_SEPARATOR) {
            SetAPen(rp, shadow);
            if (standing) { Move(rp, x0 + 6, y + s / 2 - 1); Draw(rp, x1 - 6, y + s / 2 - 1); }
            else { Move(rp, x + s / 2 - 1, y0 + 6); Draw(rp, x + s / 2 - 1, y1 - 6); }
            SetAPen(rp, shine);
            if (standing) { Move(rp, x0 + 6, y + s / 2); Draw(rp, x1 - 6, y + s / 2); }
            else { Move(rp, x + s / 2, y0 + 6); Draw(rp, x + s / 2, y1 - 6); }
        } else {
            struct Rectangle r = { 0, 0, 0, 0 };
            int iw = 0, ih = 0, big = cellw, dx = 0, dy = 0;
            if (i == lifted) {
                /* away from the edge */
                if (dock.place == OD_BOTTOM) dy = -lift; else if (dock.place == OD_TOP) dy = lift;
                else if (dock.place == OD_LEFT) dx = lift; else dx = -lift;
            }
            if (icons[i] && GetIconRectangleA(rp, icons[i], NULL, &r, NULL)) { iw = r.MaxX - r.MinX + 1; ih = r.MaxY - r.MinY + 1; }
            {
                int ix = x + (cellw - iw) / 2 + dx, iy = y + (big - ih) / 2 + dy;
                static struct TagItem plain[] = {
                    { ICONDRAWA_Frameless, TRUE }, { ICONDRAWA_Borderless, TRUE }, { ICONDRAWA_EraseBackground, FALSE }, { TAG_DONE, 0 }
                };
                if (icons[i])
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
                    /* the Mac's dot, under the icon */
                    int my = y + cellh - 3, mx = x + cellw / 2;
                    SetAPen(rp, fill);
                    RectFill(rp, mx - 1, my - 1, mx + 1, my + 1);
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

/* A started program's icon hops, as on the Mac. */
static void hop(int i)
{
    static const int lifts[] = { 3, 6, 8, 9, 8, 6, 3, 0, 2, 3, 2, 0 };
    if (!dock.hop || !back) return;
    bubble_off();
    for (unsigned k = 0; k < sizeof lifts / sizeof lifts[0]; k++) { show(i, lifts[k]); Delay(1); }
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
        struct DrawInfo *dri = GetScreenDrawInfo(scr);
        struct RastPort *b = bubble->RPort;
        SetAPen(b, dri ? dri->dri_Pens[SHINEPEN] : 2); RectFill(b, 1, 1, bw - 2, bh - 2);
        SetAPen(b, dri ? dri->dri_Pens[SHADOWPEN] : 1);
        Move(b, 1, 0); Draw(b, bw - 2, 0); Move(b, bw - 1, 1); Draw(b, bw - 1, bh - 2);
        Move(b, bw - 2, bh - 1); Draw(b, 1, bh - 1); Move(b, 0, bh - 2); Draw(b, 0, 1);
        SetAPen(b, dri ? dri->dri_Pens[TEXTPEN] : 1); SetDrMd(b, JAM1);
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
        if (w == win || w == bubble) continue;
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
}

static int open_dock(struct Menu *menus)
{
    int w, h, x, y, depth = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    load_icons();
    layout(&w, &h);
    place_of(w, h, &x, &y);
    /* what is behind the dock, before it opens; and the bitmap it is drawn in */
    free_bitmaps();
    if (dock.background != OD_BG_SOLID || dock.hop) {
        if ((behind = AllocBitMap(w, h, depth, 0, scr->RastPort.BitMap)))
            BltBitMap(scr->RastPort.BitMap, x, y, behind, 0, 0, w, h, 0xc0, 0xff, NULL);
    }
    back = AllocBitMap(w, h, depth, 0, scr->RastPort.BitMap);
    InitRastPort(&brp);
    brp.BitMap = back;
    SetFont(&brp, scr->RastPort.Font);
    win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                         WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_ScreenTitle, (ULONG)"OpenDock 0.1",
                         WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_MENUPICK | IDCMP_REFRESHWINDOW | IDCMP_INACTIVEWINDOW, TAG_DONE);
    if (!win) { free_bitmaps(); return 0; }
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
        strncpy(b->command, path, sizeof b->command - 1);
        strncpy(b->label, (const char *)FilePart((STRPTR)path), sizeof b->label - 1);
        if (!b->label[0]) strncpy(b->label, path, sizeof b->label - 1);
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

    while (!quit && win) {
        ULONG got = Wait((1UL << win->UserPort->mp_SigBit) | (appport ? 1UL << appport->mp_SigBit : 0) |
                         (timer ? 1UL << tport->mp_SigBit : 0) | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
        struct IntuiMessage *m;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) { load(); relayout(menus); check_running(); }
        if (timer && CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            check_hover();
            if (behind_changed()) relayout(menus);
            if (++ticks >= 20) { ticks = 0; check_running(); }
            tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 100000;
            SendIO((struct IORequest *)tr);
        }
        if (appport) {
            struct AppMessage *am;
            int any = 0;
            while ((am = (struct AppMessage *)GetMsg(appport))) { dropped(am); ReplyMsg((struct Message *)am); any = 1; }
            if (any) { relayout(menus); check_running(); }
        }
        while (win && (m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            WORD mx = m->MouseX, my = m->MouseY;
            ReplyMsg((struct Message *)m);
            if (cls == IDCMP_REFRESHWINDOW) { BeginRefresh(win); draw(); EndRefresh(win, TRUE); }
            else if (cls == IDCMP_INACTIVEWINDOW && pressed >= 0) { pressed = -1; draw(); }
            else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) { pressed = hit(mx, my); if (pressed >= 0) draw(); }
            else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTUP) {
                int i = hit(mx, my), was = pressed;
                pressed = -1;
                if (was >= 0) draw();
                if (i >= 0 && i == was) {
                    if (removing) { removing = 0; remove_button(i); relayout(menus); check_running(); }
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
    CloseLibrary(IconBase);
    return rc;
}
