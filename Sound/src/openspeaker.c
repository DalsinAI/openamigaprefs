/* OpenSpeaker 0.1: the speaker on the menu bar (Sound/DESIGN.md, "The
 * speaker on the menu bar"; Dale, 6 October 2026: "a menu bar speaker and
 * volume control").
 *
 * A small window on the Workbench screen's menu bar, at its far end, that
 * shows the volume. A click opens the levels: the volume, Paula's level,
 * AHI's level and mute, the same sliders as Sound prefs, and a button to
 * open Sound prefs. The mouse wheel over the speaker or its levels turns the
 * volume up and down, and the middle button mutes. A click on the speaker
 * leaves the active window active, as a click on OpenMenus' bar does.
 *
 * With the ACAHI board (AmigaChrome) the levels are the board's LEVELS,
 * which the PC's mixer applies and its mixer page moves too: the speaker
 * follows LEVEL_SEQ five times a second. On a real Amiga the volume is
 * AHI's output volume, and Paula's level is the speakers' knob.
 *
 * Where the menu bar is: OpenMenus' settings (ENV:OpenMenus/Menus, read
 * with the Menus editor's om_prefs.c): the screen's title bar, or
 * OpenMenus' own bar at an edge, where the speaker sits at the bar's end.
 * It asks OpenMenus for that room by writing its width to
 * ENV:OpenMenus/Tray and sending OpenMenus Ctrl-F; the file goes when the
 * speaker does.
 *
 *   OpenSpeaker     (started by "Sound USE" at boot when Sound prefs say
 *                    to show it; Ctrl-C quits, Ctrl-F reads the settings
 *                    again and quits when the speaker is switched off)
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/commodities.h>
#include <graphics/gfxbase.h>
#include <graphics/layers.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/commodities.h>

#include <string.h>
#include <stdio.h>

#include "sp_core.h"
#include "sp_amiga.h"
#include "om_prefs.h"

const char version[] __attribute__((used)) = "$VER: OpenSpeaker 0.1 (6.10.2026) OpenPrefs, Dalsin Limited";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase, *LayersBase, *CxBase;

#define MENUS_ENV "ENV:OpenMenus/Menus"
#define TRAY_DIR  "ENV:OpenMenus/Tray"        /* the tray: a file per program on the menu bar's end, "width order" */
#define TRAY_MINE "ENV:OpenMenus/Tray/Speaker"
#define MY_ORDER  0                           /* lower is nearer the bar's end: the speaker is rightmost, the clock (10) left of it */
#define SOUND_PREFS "SYS:Prefs/Sound"
#define NM_WHEEL_UP   0x7A                   /* the mouse wheel, as NewMouse and OS 3.2 send it */
#define NM_WHEEL_DOWN 0x7B
#define IECLASS_NEWMOUSE_ 0x16

static sp_settings cur;
static volatile ULONG *board;
static ULONG seen_seq;
static int bar_edge = OM_BAR_TITLE;

static struct Screen *scr;
static struct DrawInfo *dri;
static struct Window *spk;                   /* the speaker */
static WORD sx, sy, sw, sh;

static struct Window *pop;                   /* its levels */
static struct Gadget *pop_glist;
static APTR vi;
static struct Window *before;                /* the window that was active when the levels opened */

/* from the input handler to the task */
static struct Task *me;
static ULONG sig_input;
static volatile WORD wheel_steps;            /* up is positive */
static volatile UBYTE want_mute, want_toggle;
static volatile WORD hx0, hy0, hx1, hy1;     /* the speaker on the screen, for the handler */
static volatile WORD px0, py0, px1, py1;     /* its levels, while open */
static struct Screen *volatile hscr;

/* ---- the levels ------------------------------------------------------------------------------- */

static int level(void) { return cur.muted ? 0 : cur.volume; }

static void levels_out(int final)
{
    if (board) {
        sp_board_set(board, sp_levels_word(&cur));
        seen_seq = sp_board_seq(board);
    } else if (final) {
        sp_volume_to_units(&cur);            /* AHI hears it when a program next opens it */
        sp_store(&cur, 0);
    }
}

/* the board's levels, when the PC's mixer page or Sound prefs moved them: 1 when they changed */
static int levels_in(void)
{
    ULONG seq;
    if (!board || (seq = sp_board_seq(board)) == seen_seq) return 0;
    seen_seq = seq;
    sp_from_levels_word(&cur, sp_board_levels(board));
    return 1;
}

/* ---- the speaker -------------------------------------------------------------------------------- */

static int bar_pens(UWORD *fg, UWORD *bg)
{
    if (pop) { *fg = dri->dri_Pens[FILLTEXTPEN]; *bg = dri->dri_Pens[FILLPEN]; return 1; }
    *fg = dri->dri_Pens[BARDETAILPEN]; *bg = dri->dri_Pens[BARBLOCKPEN];
    return 0;
}

static void draw_speaker(void)
{
    struct RastPort *rp;
    UWORD fg, bg;
    char t[8];
    WORD cy, x, h;
    if (!spk) return;
    rp = spk->RPort;
    bar_pens(&fg, &bg);
    SetAPen(rp, bg);
    RectFill(rp, 0, 0, sw - 1, sh - 1);
    SetAPen(rp, fg);
    h = sh - (bar_edge == OM_BAR_TITLE ? 1 : 0);
    cy = h / 2;
    x = 5;
    RectFill(rp, x, cy - 1, x + 2, cy + 1);                       /* the speaker: its box and cone */
    for (int i = 0; i < 4; i++) { Move(rp, x + 3 + i, cy - 1 - i); Draw(rp, x + 3 + i, cy + 1 + i); }
    if (level() == 0) {                                          /* muted, or nothing: a cross */
        Move(rp, x + 9, cy - 2); Draw(rp, x + 13, cy + 2);
        Move(rp, x + 9, cy + 2); Draw(rp, x + 13, cy - 2);
    } else {
        Move(rp, x + 9, cy - 1); Draw(rp, x + 10, cy); Draw(rp, x + 9, cy + 1);
        if (level() > 50) { Move(rp, x + 12, cy - 3); Draw(rp, x + 13, cy - 2); Draw(rp, x + 13, cy + 2); Draw(rp, x + 12, cy + 3); }
    }
    if (cur.muted) strcpy(t, "Mute");
    else snprintf(t, sizeof t, "%d%%", cur.volume);
    SetBPen(rp, bg);
    SetDrMd(rp, JAM1);
    Move(rp, x + 17, (h - rp->TxHeight) / 2 + rp->TxBaseline);
    Text(rp, (STRPTR)t, strlen(t));
}

/* OpenMenus' settings: where the bar is (the screen's title bar when OpenMenus doesn't run or is off) */
static int read_bar_edge(void)
{
    om_prefs p;
    BPTR fh;
    char buf[2048];
    LONG n;
    struct MsgPort *port;
    Forbid();
    port = FindPort((STRPTR)"OpenMenus");
    Permit();
    if (!port) return OM_BAR_TITLE;
    om_defaults(&p);
    if ((fh = Open((STRPTR)MENUS_ENV, MODE_OLDFILE))) {
        n = Read(fh, buf, sizeof buf - 1);
        Close(fh);
        buf[n > 0 ? n : 0] = 0;
        om_parse(&p, buf);
    }
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
        if ((fh = Open((STRPTR)TRAY_MINE, MODE_NEWFILE))) { LONG n = snprintf(t, sizeof t, "%d %d\n", width, MY_ORDER); Write(fh, t, n); Close(fh); }
    } else DeleteFile((STRPTR)TRAY_MINE);
    tell_openmenus();
}

/* The room the tray's other programs nearer the bar's end take (none by default: the speaker is order 0):
 * their widths in pixels, and how many they are. */
static int tray_before(int *count)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR lock = Lock((STRPTR)TRAY_DIR, ACCESS_READ);
    int sum = 0;
    *count = 0;
    if (fib && lock && Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            char path[96], buf[24];
            BPTR fh;
            int w = 0, order = 0;
            LONG n;
            if (fib->fib_DirEntryType > 0 || !strcmp((const char *)fib->fib_FileName, "Speaker")) continue;
            snprintf(path, sizeof path, TRAY_DIR "/%s", (const char *)fib->fib_FileName);
            if (!(fh = Open((STRPTR)path, MODE_OLDFILE))) continue;
            n = Read(fh, buf, sizeof buf - 1);
            Close(fh);
            buf[n > 0 ? n : 0] = 0;
            if (sscanf(buf, "%d %d", &w, &order) < 1 || w <= 0 || w > 400) continue;
            if (order < MY_ORDER || (order == MY_ORDER && strcmp((const char *)fib->fib_FileName, "Speaker") < 0)) { sum += w; (*count)++; }
        }
    }
    if (lock) UnLock(lock);
    if (fib) FreeDosObject(DOS_FIB, fib);
    return sum;
}

/* where the speaker goes: the bar's far end (OpenMenus keeps 2 bar heights for the screen's depth gadget) */
static void place(void)
{
    struct RastPort rp;
    WORD bh = scr->BarHeight + 1;
    int n, off = tray_before(&n);            /* after any program with a lower order than ours */
    InitRastPort(&rp);
    SetFont(&rp, dri->dri_Font);
    sw = 5 + 17 + TextLength(&rp, (STRPTR)"100%", 4) + 6;
    switch (bar_edge) {
    case OM_BAR_BOTTOM: sh = bh; sx = scr->Width - sw - off; sy = scr->Height - bh; break;
    case OM_BAR_LEFT:   sh = bh; sx = 0; sy = scr->Height - bh * (n + 1); break;
    case OM_BAR_RIGHT:  sh = bh; sx = scr->Width - sw; sy = scr->Height - bh * (n + 1); break;
    case OM_BAR_TOP:    sh = bh; sx = scr->Width - 2 * scr->BarHeight - sw - off; sy = 0; break;
    default:            sh = scr->BarHeight; sx = scr->Width - 2 * scr->BarHeight - sw - off; sy = 0; break;   /* in the title bar, above its line */
    }
    hx0 = sx; hy0 = sy; hx1 = sx + sw; hy1 = sy + sh;
}

static void close_speaker(void)
{
    hscr = NULL;
    if (spk) { CloseWindow(spk); spk = NULL; }
    if (dri) { FreeScreenDrawInfo(scr, dri); dri = NULL; }
    scr = NULL;
}

static int open_speaker(void)
{
    struct Screen *s = LockPubScreen((STRPTR)"Workbench");
    if (!s) return 0;
    scr = s;
    if ((dri = GetScreenDrawInfo(s))) {
        bar_edge = read_bar_edge();
        place();
        spk = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, sx, WA_Top, sy, WA_Width, sw, WA_Height, sh,
                             WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                             WA_IDCMP, IDCMP_REFRESHWINDOW, TAG_DONE);
    }
    UnlockPubScreen(NULL, s);                /* the window keeps the screen open */
    if (!spk) { close_speaker(); return 0; }
    SetFont(spk->RPort, dri->dri_Font);
    draw_speaker();
    hscr = s;
    tray(sw);                                /* in the title bar too: the clock and others keep clear of us */
    return 1;
}

/* in front again when a window (not a borderless one: menus, bars, our levels) has come over it */
static void keep_in_front(void)
{
    struct Layer *l;
    int covered = 0;
    if (!spk) return;
    LockLayerInfo(&scr->LayerInfo);
    for (l = spk->WLayer->front; l && !covered; l = l->front) {
        struct Window *w = (struct Window *)l->Window;
        if (!w || (w->Flags & WFLG_BORDERLESS)) continue;
        covered = l->bounds.MinX < sx + sw && l->bounds.MaxX >= sx && l->bounds.MinY < sy + sh && l->bounds.MaxY >= sy;
    }
    UnlockLayerInfo(&scr->LayerInfo);
    if (covered) WindowToFront(spk);
}

/* ---- its levels ------------------------------------------------------------------------------------ */

enum { P_MUTE, P_VOL, P_PAULA, P_AHI, P_NOTE, P_PREFS, P_COUNT };
static struct Gadget *pg[P_COUNT];

#define PSET(id, ...) do { if (pg[id]) GT_SetGadgetAttrs(pg[id], pop, NULL, __VA_ARGS__, TAG_DONE); } while (0)

static void pop_show(void)
{
    PSET(P_MUTE, GTCB_Checked, cur.muted);
    PSET(P_VOL, GTSL_Level, cur.volume);
    PSET(P_PAULA, GTSL_Level, cur.paula);
    PSET(P_AHI, GTSL_Level, cur.ahi);
}

static void pop_draw_frame(void)
{
    struct RastPort *rp = pop->RPort;
    WORD w = pop->Width, h = pop->Height, fh = dri->dri_Font->tf_YSize;
    SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
    RectFill(rp, 1, 1, w - 2, h - 2);
    DrawBevelBox(rp, 0, 0, w, h, GT_VisualInfo, (ULONG)vi, TAG_DONE);
    SetAPen(rp, dri->dri_Pens[TEXTPEN]);
    SetDrMd(rp, JAM1);
    Move(rp, 10, 6 + rp->TxBaseline);
    Text(rp, (STRPTR)"Sound", 5);
    (void)fh;
}

static void pop_close(int activate_before)
{
    if (!pop) return;
    px0 = py0 = px1 = py1 = 0;
    CloseWindow(pop); pop = NULL;
    FreeGadgets(pop_glist); pop_glist = NULL;
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    sp_save_levels(&cur, 1);                 /* where the knob was left, kept */
    if (activate_before && before) {
        struct Window *w;
        ULONG lock = LockIBase(0);           /* only if it is still open */
        for (w = scr->FirstWindow; w && w != before; w = w->NextWindow) ;
        UnlockIBase(lock);
        if (w) ActivateWindow(w);
    }
    before = NULL;
    draw_speaker();
}

static void pop_open(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    WORD fh = dri->dri_Font->tf_YSize, lh = fh + 6, W = 260, L = 10, X = 96, row, x, y, H;
    if (pop || !(vi = GetVisualInfoA(scr, NULL))) return;
    memset(pg, 0, sizeof pg);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    g = CreateContext(&pop_glist);
#define PG(kind, id, gx, gy, gw, gh, text, flags, ...) \
    (ng.ng_LeftEdge = (gx), ng.ng_TopEdge = (gy), ng.ng_Width = (gw), ng.ng_Height = (gh), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = pg[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#define PSLIDER(id, gy, text) \
    PG(SLIDER_KIND, id, X, gy, W - X - 44, lh - 2, text, PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 100, GTSL_Level, 0, \
       GTSL_LevelFormat, (ULONG)"%3ld%%", GTSL_MaxLevelLen, 4, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, GA_Immediate, TRUE)
    PG(CHECKBOX_KIND, P_MUTE, W - 84, 4, 26, fh + 3, "Mute", PLACETEXT_RIGHT, GTCB_Scaled, TRUE);
    row = fh + 14;
    PSLIDER(P_VOL, row, board ? "Volume" : "Volume (AHI)"); row += lh + 2;
    if (board) {
        PSLIDER(P_PAULA, row, "Paula"); row += lh + 2;
        PSLIDER(P_AHI, row, "AHI"); row += lh + 4;
        PG(TEXT_KIND, P_NOTE, L, row, W - 2 * L, lh - 2, NULL, 0, GTTX_Text, (ULONG)"Plays on: PC speakers, through AmigaChrome");
    } else {
        row += 2;
        PG(TEXT_KIND, P_NOTE, L, row, W - 2 * L, lh - 2, NULL, 0, GTTX_Text, (ULONG)"Amiga sound (Paula): your speakers' knob");
    }
    row += lh + 4;
    PG(BUTTON_KIND, P_PREFS, L, row, W - 2 * L, lh, "Sound prefs...", 0, GA_Disabled, FALSE);
    row += lh + 8;
    H = row;
    if (!g) { FreeGadgets(pop_glist); pop_glist = NULL; FreeVisualInfo(vi); vi = NULL; return; }
    /* beside the speaker, towards the middle of the screen */
    x = sx + sw - W; if (x < 0) x = 0;
    if (bar_edge == OM_BAR_LEFT) x = 0;
    y = (bar_edge == OM_BAR_BOTTOM || bar_edge == OM_BAR_LEFT || bar_edge == OM_BAR_RIGHT) ? sy - H : sy + sh + (bar_edge == OM_BAR_TITLE ? 1 : 0);
    if (y < 0) y = 0;
    before = IntuitionBase->ActiveWindow;
    pop = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, W, WA_Height, H,
                         WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                         WA_IDCMP, IDCMP_INACTIVEWINDOW | IDCMP_RAWKEY | IDCMP_REFRESHWINDOW | SLIDERIDCMP | BUTTONIDCMP | CHECKBOXIDCMP,
                         TAG_DONE);
    if (!pop) { FreeGadgets(pop_glist); pop_glist = NULL; FreeVisualInfo(vi); vi = NULL; before = NULL; return; }
    SetFont(pop->RPort, dri->dri_Font);
    pop_draw_frame();
    AddGList(pop, pop_glist, (UWORD)~0, -1, NULL);
    RefreshGList(pop_glist, pop, NULL, -1);
    GT_RefreshWindow(pop, NULL);
    pop_show();
    px0 = x; py0 = y; px1 = x + W; py1 = y + H;
    draw_speaker();
}

static void open_sound_prefs(void)
{
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (in && out && SystemTags((STRPTR)SOUND_PREFS, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, TAG_DONE) != -1) return;
    if (in) Close(in);
    if (out) Close(out);
}

/* 1 when the speaker should go */
static int pop_events(void)
{
    struct IntuiMessage *m;
    int close = 0, prefs = 0;
    while (pop && (m = GT_GetIMsg(pop->UserPort))) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        struct Gadget *gg = (struct Gadget *)m->IAddress;
        GT_ReplyIMsg(m);
        if (cls == IDCMP_INACTIVEWINDOW) close = 1;
        else if (cls == IDCMP_RAWKEY && code == 0x45) close = 2;            /* Esc */
        else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(pop); pop_draw_frame(); GT_EndRefresh(pop, TRUE); }
        else if ((cls == IDCMP_MOUSEMOVE || cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN) && gg) {
            int final = cls == IDCMP_GADGETUP;
            switch (gg->GadgetID) {
            case P_VOL: cur.volume = (WORD)code; levels_out(final); break;
            case P_PAULA: cur.paula = (WORD)code; levels_out(final); break;
            case P_AHI: cur.ahi = (WORD)code; levels_out(final); break;
            case P_MUTE: cur.muted = (gg->Flags & GFLG_SELECTED) != 0; levels_out(1); break;
            case P_PREFS: prefs = 1; close = 2; break;
            }
            draw_speaker();
        }
    }
    if (close) pop_close(close == 2);
    if (prefs) open_sound_prefs();
    return 0;
}

/* ---- the input handler: the wheel and the buttons over the speaker or its levels ------------------ */

static int over(WORD x0, WORD y0, WORD x1, WORD y1, struct Screen *s)
{
    return x1 > x0 && s->MouseX >= x0 && s->MouseX < x1 && s->MouseY >= y0 && s->MouseY < y1;
}

static void custom(CxMsg *msg, CxObj *co)
{
    struct InputEvent *ie = (struct InputEvent *)CxMsgData(msg);
    struct Screen *s = IntuitionBase->FirstScreen;
    (void)co;
    if (!hscr || s != hscr) return;
    for (; ie; ie = ie->ie_NextEvent) {
        int on_spk = over(hx0, hy0, hx1, hy1, s), on_pop = over(px0, py0, px1, py1, s);
        UWORD code = ie->ie_Code;
        if ((ie->ie_Class == IECLASS_RAWKEY || ie->ie_Class == IECLASS_NEWMOUSE_) && (code == NM_WHEEL_UP || code == NM_WHEEL_DOWN) &&
            (on_spk || on_pop)) {
            wheel_steps += code == NM_WHEEL_UP ? 1 : -1;
            ie->ie_Class = IECLASS_NULL;
            Signal(me, sig_input);
        } else if (ie->ie_Class == IECLASS_RAWMOUSE && on_spk) {
            if (code == IECODE_MBUTTON) { want_mute = 1; Signal(me, sig_input); }
            else if (code == IECODE_LBUTTON) { want_toggle = 1; Signal(me, sig_input); }
            if (code == IECODE_MBUTTON || code == IECODE_LBUTTON || code == (IECODE_MBUTTON | IECODE_UP_PREFIX) ||
                code == (IECODE_LBUTTON | IECODE_UP_PREFIX))
                ie->ie_Class = IECLASS_NULL;     /* ours: the active window keeps the activation */
        } else if (ie->ie_Class == IECLASS_RAWMOUSE && on_pop && code == IECODE_MBUTTON) {
            want_mute = 1; ie->ie_Class = IECLASS_NULL; Signal(me, sig_input);
        }
    }
}

static struct NewBroker nb = {
    NB_VERSION, (STRPTR)"OpenSpeaker", (STRPTR)"OpenSpeaker 0.1", (STRPTR)"The speaker on the menu bar",
    NBU_UNIQUE | NBU_NOTIFY, 0, 10, NULL, 0     /* before OpenWindows, whose wheel would activate the window under the pointer */
};

/* ---- the task ---------------------------------------------------------------------------------- */

int main(void)
{
    struct MsgPort *port = NULL, *tport = NULL;
    struct timerequest *tr = NULL;
    CxObj *broker = NULL, *cust;
    LONG s1 = -1;
    int quit = 0, timer = 0, ticks = 0, save_in = 0;
    struct MsgPort *other;
    Forbid();
    other = FindPort((STRPTR)SP_SPEAKER_PORT);
    if (other && other->mp_SigTask) Signal((struct Task *)other->mp_SigTask, SIGBREAKF_CTRL_F);   /* one speaker: that one reads again */
    Permit();
    if (other) return RETURN_OK;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    CxBase = OpenLibrary((STRPTR)"commodities.library", 39);
    if (!IntuitionBase || !GfxBase || !GadToolsBase || !LayersBase || !CxBase) goto out;
    me = FindTask(NULL);
    if ((s1 = AllocSignal(-1)) < 0) goto out;
    sig_input = 1UL << s1;
    sp_load(&cur, NULL);
    if (!cur.speaker) goto out;
    board = sp_board();
    if (board) seen_seq = sp_board_seq(board);
    if (!(port = CreateMsgPort())) goto out;
    nb.nb_Port = port;
    if (!(broker = CxBroker(&nb, NULL))) goto out;
    port->mp_Node.ln_Name = (char *)SP_SPEAKER_PORT;       /* Sound prefs finds this task by it */
    AddPort(port);
    if (!(cust = CxCustom(custom, 0))) goto out;
    AttachCxObj(broker, cust);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    if (!timer || !open_speaker()) goto out;
    tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 200000;
    SendIO((struct IORequest *)tr);
    ActivateCxObj(broker, 1);
    while (!quit) {
        ULONG got = Wait((1UL << port->mp_SigBit) | (1UL << tport->mp_SigBit) | sig_input | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F |
                         (spk ? 1UL << spk->UserPort->mp_SigBit : 0) | (pop ? 1UL << pop->UserPort->mp_SigBit : 0));
        CxMsg *cm;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) {                       /* Sound prefs changed the settings */
            sp_load(&cur, NULL);
            if (!cur.speaker) quit = 1;
            else draw_speaker();
        }
        while ((cm = (CxMsg *)GetMsg(port))) {
            ULONG type = CxMsgType(cm), id = CxMsgID(cm);
            ReplyMsg((struct Message *)cm);
            if (type == CXM_COMMAND && id == CXCMD_KILL) quit = 1;
            else if (type == CXM_COMMAND && id == CXCMD_UNIQUE) { sp_load(&cur, NULL); draw_speaker(); }
        }
        if (spk) {
            struct IntuiMessage *m;
            while ((m = (struct IntuiMessage *)GetMsg(spk->UserPort))) {
                if (m->Class == IDCMP_REFRESHWINDOW) { BeginRefresh(spk); draw_speaker(); EndRefresh(spk, TRUE); }
                ReplyMsg((struct Message *)m);
            }
        }
        if (pop) pop_events();
        if (got & sig_input) {
            WORD steps;
            Disable(); steps = wheel_steps; wheel_steps = 0; Enable();
            if (want_mute) { want_mute = 0; cur.muted = !cur.muted; levels_out(1); save_in = 10; }
            if (steps) {
                int v = cur.volume + steps * cur.wheel;
                cur.volume = v < 0 ? 0 : v > 100 ? 100 : v;
                if (steps > 0) cur.muted = 0;               /* turning it up unmutes, as a knob does */
                levels_out(!board);
                save_in = 10;                               /* kept when the wheel has been still for 2 s */
            }
            if (want_toggle) { want_toggle = 0; if (pop) pop_close(1); else pop_open(); }
            if (pop) pop_show();
            draw_speaker();
        }
        if (CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            if (levels_in()) { draw_speaker(); if (pop) pop_show(); }
            if (save_in && !--save_in) sp_save_levels(&cur, 1);
            if (++ticks % 10 == 0 && !pop) {                 /* every 2 s: OpenMenus' bar moved? */
                int e = read_bar_edge();
                if (e != bar_edge) { close_speaker(); open_speaker(); }
                else if (spk) {                     /* the clock came or went, or changed its width */
                    WORD ox = sx, oy = sy;
                    place();
                    if (ox != sx || oy != sy) { ChangeWindowBox(spk, sx, sy, sw, sh); draw_speaker(); }
                }
            }
            keep_in_front();
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 200000;
            SendIO((struct IORequest *)tr);
        }
    }
out:
    if (broker) DeleteCxObjAll(broker);
    if (pop) pop_close(0);
    if (save_in) sp_save_levels(&cur, 1);
    if (spk) { close_speaker(); tray(0); }
    if (timer) { if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr); WaitIO((struct IORequest *)tr); CloseDevice((struct IORequest *)tr); }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (port) {
        struct Message *m;
        if (port->mp_Node.ln_Name) RemPort(port);
        while ((m = GetMsg(port))) ReplyMsg(m);
        DeleteMsgPort(port);
    }
    if (s1 >= 0) FreeSignal(s1);
    if (CxBase) CloseLibrary(CxBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return RETURN_OK;
}
