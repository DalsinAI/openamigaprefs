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
#include <workbench/workbench.h>
#include <workbench/startup.h>
#include <workbench/icon.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
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

static void load_icons(void)
{
    char prog[256];
    free_icons();
    for (int i = 0; i < dock.n; i++) {
        od_button *b = &dock.b[i];
        if (b->kind == OD_SEPARATOR) continue;
        if (b->icon[0]) icons[i] = GetDiskObject((STRPTR)b->icon);
        if (!icons[i]) { program_of(b, prog, sizeof prog); icons[i] = GetDiskObjectNew((STRPTR)prog); }
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

/* ---- the window ------------------------------------------------------------------------------- */

static int item_size(int i) { return dock.b[i].kind == OD_SEPARATOR ? SEP_W : (standing ? cellh : cellw); }

/* The button under a point in the window, or -1. */
static int hit(int x, int y)
{
    int pos = 4, at = standing ? y : x;
    (void)(standing ? x : y);
    for (int i = 0; i < dock.n; i++) {
        int s = item_size(i);
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
    along = 8;
    for (int i = 0; i < dock.n; i++) along += item_size(i);
    if (along < 8 + cellw) along = 8 + cellw;
    if (standing) { *w = cellw + 8; *h = along; }
    else { *w = along; *h = cellh + 8; }
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

static void draw(void)
{
    struct RastPort *rp = win->RPort;
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    UWORD *pens = dri ? dri->dri_Pens : NULL;
    int W = win->Width, H = win->Height, pos = 4;
    int bg = pens ? pens[BACKGROUNDPEN] : 0, shine = pens ? pens[SHINEPEN] : 2, shadow = pens ? pens[SHADOWPEN] : 1;
    int text = pens ? pens[TEXTPEN] : 1, fill = pens ? pens[FILLPEN] : 3;
    SetAPen(rp, bg); RectFill(rp, 0, 0, W - 1, H - 1);
    SetAPen(rp, shine); Move(rp, 0, H - 1); Draw(rp, 0, 0); Draw(rp, W - 1, 0);
    SetAPen(rp, shadow); Draw(rp, W - 1, H - 1); Draw(rp, 1, H - 1);
    for (int i = 0; i < dock.n; i++) {
        int s = item_size(i), x = standing ? 4 : pos, y = standing ? pos : 4;
        od_button *b = &dock.b[i];
        if (b->kind == OD_SEPARATOR) {
            SetAPen(rp, shadow);
            if (standing) { Move(rp, 6, y + s / 2 - 1); Draw(rp, W - 7, y + s / 2 - 1); }
            else { Move(rp, x + s / 2 - 1, 6); Draw(rp, x + s / 2 - 1, H - 7); }
            SetAPen(rp, shine);
            if (standing) { Move(rp, 6, y + s / 2); Draw(rp, W - 7, y + s / 2); }
            else { Move(rp, x + s / 2, 6); Draw(rp, x + s / 2, H - 7); }
        } else {
            struct Rectangle r = { 0, 0, 0, 0 };
            int iw = 0, ih = 0;
            if (icons[i] && GetIconRectangleA(rp, icons[i], NULL, &r, NULL)) { iw = r.MaxX - r.MinX + 1; ih = r.MaxY - r.MinY + 1; }
            {
                int big = cellw, ix = x + (cellw - iw) / 2, iy = y + (big - ih) / 2;
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
                    int my = y + cellh - 3;
                    SetAPen(rp, fill);
                    RectFill(rp, x + cellw / 2 - 3, my, x + cellw / 2 + 2, my + 1);
                }
            }
        }
        pos += s;
    }
    if (dri) FreeScreenDrawInfo(scr, dri);
}

static int open_dock(struct Menu *menus)
{
    int w, h, x, y;
    load_icons();
    layout(&w, &h);
    place_of(w, h, &x, &y);
    win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                         WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_ScreenTitle, (ULONG)"OpenDock 0.1",
                         WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_MENUPICK | IDCMP_REFRESHWINDOW | IDCMP_INACTIVEWINDOW, TAG_DONE);
    if (!win) return 0;
    if (menus) SetMenuStrip(win, menus);
    draw();
    return 1;
}

static void close_dock(void)
{
    if (!win) return;
    ClearMenuStrip(win);
    CloseWindow(win);
    win = NULL;
}

/* Settings changed: the window again, where the place says. */
static void relayout(struct Menu *menus)
{
    int w, h, x, y;
    load_icons();
    layout(&w, &h);
    place_of(w, h, &x, &y);
    ChangeWindowBox(win, x, y, w, h);
    (void)menus;
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
    struct MsgPort *port, *appport = NULL, *tport = NULL;
    struct timerequest *tr = NULL;
    struct AppWindow *aw = NULL;
    struct Menu *menus = NULL;
    APTR vi = NULL;
    int quit = 0, timer = 0, rc = RETURN_OK;

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
    if (!open_dock(menus)) { rc = RETURN_FAIL; goto out; }
    if (WorkbenchBase && (appport = CreateMsgPort())) aw = AddAppWindowA(0, 0, win, appport, NULL);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    check_running();
    if (timer) { tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 2; tr->tr_time.tv_micro = 0; SendIO((struct IORequest *)tr); }

    while (!quit) {
        ULONG got = Wait((1UL << win->UserPort->mp_SigBit) | (appport ? 1UL << appport->mp_SigBit : 0) |
                         (timer ? 1UL << tport->mp_SigBit : 0) | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
        struct IntuiMessage *m;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) { load(); relayout(menus); check_running(); }
        if (timer && CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            check_running();
            tr->tr_time.tv_secs = 2; tr->tr_time.tv_micro = 0;
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
                    else if (!(running[i] && to_front(running[i]))) start(&dock.b[i]);
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
    if (aw) RemoveAppWindow(aw);
    if (appport) { struct Message *m; while ((m = GetMsg(appport))) ReplyMsg(m); DeleteMsgPort(appport); }
    close_dock();
    if (menus) FreeMenus(menus);
    if (vi) FreeVisualInfo(vi);
    if (scr) UnlockPubScreen(NULL, scr);
    free_icons();
    if (port) { RemPort(port); DeleteMsgPort(port); }
    if (WorkbenchBase) CloseLibrary(WorkbenchBase);
    CloseLibrary(IconBase);
    return rc;
}
