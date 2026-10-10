/* OpenBlanker: the screen blanker of the Open family. After the Amiga has
 * been left alone for a while it opens a screen of its own and plays a show;
 * a key or the mouse ends it. Its first show is "Gods and Angels", AmigaChrome's
 * homage to the people who made the Amiga and kept it alive: each card fades
 * in, holds and fades out, by the palette, as Amiga demos always have.
 *
 *   OpenBlanker [TIMEOUT=seconds] [NOW]
 *     TIMEOUT  seconds without a key or the mouse before the show (default 300;
 *              from Workbench, ENV:OpenPrefs/Blanker holds the number)
 *     NOW      play the show at once, then go on watching
 *
 * A commodity: Exchange can disable, enable or quit it. Started again, the
 * running one is left alone (1.1: it used to play the show at once, so a
 * startup that lists OpenBlanker twice blanked the screen at boot); started
 * again with NOW, it plays the show at once.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <graphics/text.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/diskfont.h>
#include <proto/commodities.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char version[] __attribute__((used)) = "$VER: OpenBlanker 1.1 (10.10.2026) MIT, Copyright (c) 2026 Dalsin Limited";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *CxBase, *DiskfontBase;

static struct Task *me;
static volatile ULONG idle_ticks;      /* seconds since the last key or mouse */
static volatile int showing, woken, on = 1;
#define SIG_WAKE SIGBREAKF_CTRL_E
#define SIG_PLAY SIGBREAKF_CTRL_D     /* a second OpenBlanker NOW asks the running one to play the show */
#define PORT_NAME "OpenBlanker"

/* ---- the show: cards of styled lines --------------------------------------------------------- */

enum { KICKER, TITLE, NAME, WHAT, DEDICATION, GROUP, LIST, RULE, WORDMARK, SMALL, THANKS };
typedef struct { int style; const char *text; } line;
typedef struct { int hold_frames; line lines[8]; } card;

/* Latin-1: the Amiga's fonts are ISO-8859-1 (\xFC u-umlaut, \xE9 e-acute). */
static const card show[] = {
    { 130, { { WORDMARK, "" }, { SMALL, "An homage" } } },
    { 330, { { DEDICATION, "And our undying love and respect to the Gods and Angels of the Amiga community, without whose dedication and undying love for the Amiga we would not be here to work on this today." }, { RULE, "" } } },
    { 120, { { KICKER, "THE GODS" }, { TITLE, "The ones who made the Amiga" } } },
    { 115, { { NAME, "Jay Miner" }, { WHAT, "the father of the Amiga, and its custom chips" } } },
    { 115, { { NAME, "Dave Morse" }, { WHAT, "who founded Amiga Corporation" } } },
    { 115, { { NAME, "RJ Mical" }, { WHAT, "Intuition" } } },
    { 115, { { NAME, "Carl Sassenrath" }, { WHAT, "Exec, the multitasking heart of it" } } },
    { 115, { { NAME, "Dale Luck" }, { WHAT, "the graphics library" } } },
    { 115, { { NAME, "Dave Needle and Joe Decuir" }, { WHAT, "the chipset, with Jay" } } },
    { 115, { { NAME, "Dave Haynie" }, { WHAT, "Commodore's Amiga hardware" } } },
    { 115, { { NAME, "The engineers of Commodore-Amiga" }, { WHAT, "who wrote down how every chip works" } } },
    { 120, { { KICKER, "THE GODS" }, { TITLE, "The ones who kept it alive" } } },
    { 125, { { NAME, "Michal Schulz" }, { WHAT, "Emu68: an Amiga's CPU, at full speed, on an ARM core" } } },
    { 125, { { NAME, "Stephen Leary" }, { WHAT, "TerribleFire: open accelerators for every Amiga" } } },
    { 125, { { NAME, "Claude Schwarz" }, { WHAT, "PiStorm: a Raspberry Pi in the CPU socket" } } },
    { 125, { { NAME, "Bernd Schmidt and Toni Wilen" }, { WHAT, "UAE and WinUAE: a quarter of a century of fidelity" } } },
    { 125, { { NAME, "Frode Solheim" }, { WHAT, "FS-UAE" } } },
    { 125, { { NAME, "The AROS Development Team" }, { WHAT, "an open AmigaOS, still being built" } } },
    { 125, { { NAME, "Thomas Richter, Olaf Barthel and the AmigaOS 3.2 team" }, { WHAT, "the system our Amigas run" } } },
    { 125, { { NAME, "Stefan Stuntz" }, { WHAT, "MUI" } } },
    { 125, { { NAME, "Alexander Kneer and Tobias Abt" }, { WHAT, "Picasso96" } } },
    { 125, { { NAME, "Urban M\xFCller" }, { WHAT, "Aminet, the Amiga's library of everything" } } },
    { 130, { { KICKER, "THE ANGELS" }, { TITLE, "The work we build on, and learned from" } } },
    { 230, { { GROUP, "THE TOOLCHAIN" }, { LIST, "Stefan \"Bebbo\" Franke, amiga-gcc  \xB7  libnix  \xB7  Szilard Biro, libpthread" }, { LIST, "Olaf Barthel, the TCP tools" } } },
    { 260, { { GROUP, "WHAT THE OPEN FAMILY IS MADE OF" }, { LIST, "zlib  \xB7  libpng  \xB7  libjpeg  \xB7  WebP  \xB7  FreeType  \xB7  HarfBuzz  \xB7  Fontconfig" }, { LIST, "cairo  \xB7  pixman  \xB7  curl  \xB7  SQLite  \xB7  libxml2  \xB7  Expat" }, { LIST, "WebKit  \xB7  ICU  \xB7  VLC  \xB7  SDL  \xB7  Mesa  \xB7  the open MUI classes" } } },
    { 230, { { GROUP, "ARCHIVES AND PACKERS" }, { LIST, "Teemu Suutari, ancient  \xB7  Simon Howard, lhasa" }, { LIST, "Andr\xE9 Rodrigues de la Rocha and Heikki Orsila, xDMS  \xB7  libarchive" } } },
    { 250, { { GROUP, "WHAT TAUGHT US" }, { LIST, "Thomas Harte, the 680x0 tests  \xB7  Martin Gierich, html.datatype" }, { LIST, "Damir Sijakovic, the Boxie icons  \xB7  Kevin Atkinson, SCOWL" }, { LIST, "the Document Liberation Project  \xB7  AmiSSL" } } },
    { 220, { { GROUP, "THE GAMES WE BROUGHT HOME" }, { LIST, "The Ur-Quan Masters  \xB7  Chocolate Doom  \xB7  SDLPoP" }, { LIST, "NXEngine-evo  \xB7  OpenJazz" } } },
    { 240, { { GROUP, "AND THE COMMUNITY" }, { LIST, "everyone who kept an Amiga running, recapped a board," }, { LIST, "archived a disk, or wrote the README nobody thanked them for" } } },
    { 160, { { THANKS, "Thank you." }, { RULE, "" } } },
    { 170, { { WORDMARK, "" }, { SMALL, "The AmigaChrome team  \xB7  Dalsin Limited" } } },
};
#define NCARDS (int)(sizeof show / sizeof show[0])
#define FADE 55      /* frames: about 1.1 s at 50 Hz */
#define GAP 18

/* pens 0 to 5: background, text, red, steel, dim; their full colours */
static const UBYTE full[6][3] = { {13,16,19}, {233,236,239}, {200,32,43}, {154,166,178}, {120,130,140}, {233,236,239} };
enum { P_BG, P_INK, P_RED, P_STEEL, P_DIM };

static struct Screen *scr;
static struct Window *win;
static UWORD *blank_sprite;
static struct TextFont *f_sans_big, *f_sans_mid, *f_sans_small, *f_serif, *f_serif_small, *f_title;

static ULONG c32(UBYTE v) { return (ULONG)v * 0x01010101UL; }

static void set_level(int level)   /* 0..256: how far the pens have come up from the background */
{
    int p;
    for (p = 1; p < 6; p++) {
        int r = full[0][0] + (full[p][0] - full[0][0]) * level / 256;
        int g = full[0][1] + (full[p][1] - full[0][1]) * level / 256;
        int b = full[0][2] + (full[p][2] - full[0][2]) * level / 256;
        SetRGB32(&scr->ViewPort, p, c32((UBYTE)r), c32((UBYTE)g), c32((UBYTE)b));
    }
}

static struct TextFont *font(const char *name, int px)
{
    struct TextAttr ta;
    struct TextFont *f;
    if (px < 8) px = 8;
    ta.ta_Name = (STRPTR)name; ta.ta_YSize = (UWORD)px; ta.ta_Style = FS_NORMAL; ta.ta_Flags = FPF_DISKFONT;
    f = DiskfontBase ? OpenDiskFont(&ta) : NULL;     /* an outline font: made at this size */
    if (!f) { ta.ta_Name = (STRPTR)"topaz.font"; ta.ta_YSize = 8; ta.ta_Flags = FPF_ROMFONT; f = OpenFont(&ta); }
    return f;
}

static void style_of(int style, struct TextFont **f, int *pen, int *soft)
{
    *soft = FS_NORMAL;
    switch (style) {
    case KICKER: *f = f_sans_small; *pen = P_RED; *soft = FSF_BOLD; break;
    case TITLE: *f = f_title; *pen = P_INK; *soft = FSF_BOLD; break;
    case NAME: *f = f_sans_big; *pen = P_INK; *soft = FSF_BOLD; break;
    case WHAT: *f = f_serif_small; *pen = P_STEEL; *soft = FSF_ITALIC; break;
    case DEDICATION: *f = f_serif; *pen = P_INK; *soft = FSF_ITALIC; break;
    case GROUP: *f = f_sans_small; *pen = P_DIM; break;
    case LIST: *f = f_sans_mid; *pen = P_INK; break;
    case SMALL: *f = f_sans_small; *pen = P_DIM; break;
    case THANKS: *f = f_title; *pen = P_INK; *soft = FSF_BOLD; break;
    default: *f = f_sans_mid; *pen = P_INK; break;
    }
}

/* the lines a text wraps into at width w; returns how many (at most 6) */
static int wrap(struct RastPort *rp, const char *s, int w, const char *start[6], int len[6])
{
    int n = 0;
    while (*s && n < 6) {
        const char *p = s, *best = NULL;
        while (*p) {
            const char *q = p;
            while (*q && *q != ' ') q++;
            if (TextLength(rp, (STRPTR)s, (ULONG)(q - s)) > w && best) break;
            best = q;
            p = *q ? q + 1 : q;
            if (!*q) break;
        }
        if (!best) best = s + strlen(s);
        start[n] = s; len[n] = (int)(best - s); n++;
        s = *best ? best + 1 : best;
    }
    return n;
}

static void draw_card(const card *c)
{
    struct RastPort *rp = &scr->RastPort;
    int W = scr->Width, H = scr->Height, maxw = W * 4 / 5, total = 0, i, y;
    struct { const char *s[6]; int len[6], n, lh, gap; struct TextFont *f; int pen, soft, style; } L[8];
    int nl = 0;
    SetRast(rp, P_BG);
    for (i = 0; i < 8 && (c->lines[i].text || c->lines[i].style); i++) {
        const line *ln = &c->lines[i];
        if (!ln->text) break;
        L[nl].style = ln->style;
        if (ln->style == RULE) { L[nl].n = 0; L[nl].lh = H / 40; L[nl].gap = H / 40; total += L[nl].lh + L[nl].gap; nl++; continue; }
        if (ln->style == WORDMARK) { L[nl].f = f_title; L[nl].n = 1; L[nl].lh = f_title->tf_YSize; L[nl].gap = H / 50; total += L[nl].lh + L[nl].gap; nl++; continue; }
        style_of(ln->style, &L[nl].f, &L[nl].pen, &L[nl].soft);
        SetFont(rp, L[nl].f);
        SetSoftStyle(rp, L[nl].soft, AskSoftStyle(rp));
        L[nl].n = wrap(rp, ln->text, maxw, L[nl].s, L[nl].len);
        L[nl].lh = L[nl].f->tf_YSize * 13 / 10;
        L[nl].gap = (ln->style == KICKER || ln->style == GROUP) ? H / 45 : H / 70;
        total += L[nl].n * L[nl].lh + L[nl].gap;
        nl++;
    }
    y = (H - total) / 2;
    for (i = 0; i < nl; i++) {
        int k;
        if (L[i].style == RULE) {
            SetAPen(rp, P_RED);
            RectFill(rp, W / 2 - W / 30, y + L[i].lh / 2, W / 2 + W / 30, y + L[i].lh / 2 + (H >= 400 ? 2 : 1));
            y += L[i].lh + L[i].gap;
            continue;
        }
        if (L[i].style == WORDMARK) {
            int a, b;
            SetFont(rp, f_title); SetSoftStyle(rp, FSF_BOLD, AskSoftStyle(rp));
            a = TextLength(rp, (STRPTR)"Amiga", 5); b = TextLength(rp, (STRPTR)"Chrome", 6);
            Move(rp, (W - a - b) / 2, y + f_title->tf_Baseline);
            SetAPen(rp, P_RED); Text(rp, (STRPTR)"Amiga", 5);
            SetAPen(rp, P_INK); Text(rp, (STRPTR)"Chrome", 6);
            y += L[i].lh + L[i].gap;
            continue;
        }
        SetFont(rp, L[i].f);
        SetSoftStyle(rp, L[i].soft, AskSoftStyle(rp));
        SetAPen(rp, L[i].pen);
        for (k = 0; k < L[i].n; k++) {
            int tw = TextLength(rp, (STRPTR)L[i].s[k], (ULONG)L[i].len[k]);
            Move(rp, (W - tw) / 2, y + L[i].f->tf_Baseline);
            Text(rp, (STRPTR)L[i].s[k], (ULONG)L[i].len[k]);
            y += L[i].lh;
        }
        y += L[i].gap;
    }
}

/* waits a frame; 0 once a key or the mouse has woken it */
static int frame(void)
{
    WaitTOF();
    return !woken;
}

static int open_show(void)
{
    struct Screen *wb = LockPubScreen((STRPTR)"Workbench");
    int w = wb ? wb->Width : 640, h = wb ? wb->Height : 512;
    ULONG mode;
    if (wb) UnlockPubScreen(NULL, wb);
    /* a 256-colour mode of the Workbench's size: AGA, or a CLUT RTG mode, so the palette fades */
    mode = BestModeID(BIDTAG_NominalWidth, w, BIDTAG_NominalHeight, h, BIDTAG_Depth, 8, TAG_END);
    if (mode == INVALID_ID) mode = BestModeID(BIDTAG_NominalWidth, 640, BIDTAG_NominalHeight, 512, BIDTAG_Depth, 8, TAG_END);
    scr = OpenScreenTags(NULL, mode != INVALID_ID ? SA_DisplayID : TAG_IGNORE, mode,
                         SA_Width, w, SA_Height, h, SA_Depth, 8, SA_Quiet, TRUE, SA_ShowTitle, FALSE,
                         SA_Type, CUSTOMSCREEN, SA_Exclusive, TRUE, SA_Behind, TRUE, SA_Draggable, FALSE, TAG_END);
    if (!scr) return 0;
    {
        int H = scr->Height;
        f_title = font("CGTriumvirate.font", H / 13);
        f_sans_big = font("CGTriumvirate.font", H / 15);
        f_sans_mid = font("CGTriumvirate.font", H / 30);
        f_sans_small = font("CGTriumvirate.font", H / 40);
        f_serif = font("CGTimes.font", H / 22);
        f_serif_small = font("CGTimes.font", H / 30);
    }
    SetRGB32(&scr->ViewPort, P_BG, c32(full[0][0]), c32(full[0][1]), c32(full[0][2]));
    set_level(0);
    SetRast(&scr->RastPort, P_BG);
    /* a window over the whole screen: the pointer is hidden in it, and the key
     * that wakes the show goes to it, not to what was active underneath */
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 0, WA_Top, 0, WA_Width, scr->Width, WA_Height, scr->Height,
                         WA_Borderless, TRUE, WA_Backdrop, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_NoCareRefresh, TRUE,
                         WA_SimpleRefresh, TRUE, WA_IDCMP, 0, TAG_END);
    if (win && (blank_sprite = (UWORD *)AllocVec(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR)))
        SetPointer(win, blank_sprite, 1, 1, 0, 0);
    ScreenToFront(scr);
    return 1;
}

/* The show ends cleanly (1.1): the pointer is the window's again, the window closes so Intuition has
 * the Workbench's windows to redraw, the Workbench screen is brought to the front before ours goes
 * (so the display is the Workbench's, not the show's last picture), the screen's bitmap is cleared
 * and closed, and then the fonts, the pointer's sprite and the rest go. */
static void close_show(void)
{
    struct TextFont **f[] = { &f_title, &f_sans_big, &f_sans_mid, &f_sans_small, &f_serif, &f_serif_small };
    unsigned i;
    int tries;
    if (scr) SetRast(&scr->RastPort, P_BG);           /* nothing of the last card left in the picture */
    if (win) { ClearPointer(win); CloseWindow(win); win = NULL; }
    {
        struct Screen *wb = LockPubScreen((STRPTR)"Workbench");
        if (wb) { ScreenToFront(wb); UnlockPubScreen(NULL, wb); }
    }
    if (scr) {
        WaitBlit();
        for (tries = 0; tries < 20 && !CloseScreen(scr); tries++) Delay(5);   /* a window of another program on it: wait for it */
        scr = NULL;
    }
    for (i = 0; i < sizeof f / sizeof f[0]; i++) if (*f[i]) { CloseFont(*f[i]); *f[i] = NULL; }
    if (blank_sprite) { FreeVec(blank_sprite); blank_sprite = NULL; }
}

static void play(void)
{
    int i, k;
    woken = 0;
    if (!open_show()) return;
    showing = 1;
    while (!woken)
        for (i = 0; i < NCARDS && !woken; i++) {
            set_level(0);
            draw_card(&show[i]);
            for (k = 1; k <= FADE && frame(); k++) set_level(256 * k / FADE);
            for (k = 0; k < show[i].hold_frames && frame(); k++) ;
            for (k = FADE - 1; k >= 0 && frame(); k--) set_level(256 * k / FADE);
            for (k = 0; k < GAP && frame(); k++) ;
        }
    showing = 0;
    close_show();
    idle_ticks = 0;
}

/* ---- the commodity --------------------------------------------------------------------------- */

static void custom(CxMsg *msg, CxObj *co)
{
    struct InputEvent *ie = (struct InputEvent *)CxMsgData(msg);
    (void)co;
    for (; ie; ie = ie->ie_NextEvent)
        if (ie->ie_Class == IECLASS_RAWKEY || ie->ie_Class == IECLASS_RAWMOUSE || ie->ie_Class == IECLASS_NEWPOINTERPOS) {
            idle_ticks = 0;
            if (showing && !woken) { woken = 1; Signal(me, SIG_WAKE); }
        }
}

static struct NewBroker nb = {
    NB_VERSION, (STRPTR)"OpenBlanker", (STRPTR)"OpenBlanker 1.0", (STRPTR)"Blanks the screen with a show: Gods and Angels",
    NBU_UNIQUE | NBU_NOTIFY, 0, 0, NULL, 0
};

static ULONG timeout_setting(LONG from_cli)
{
    char buf[16];
    LONG n;
    if (from_cli > 0) return (ULONG)from_cli;
    if (GetVar((STRPTR)"OpenPrefs/Blanker", (STRPTR)buf, sizeof buf, 0) > 0 && (n = atol(buf)) > 0) return (ULONG)n;
    return 300;
}

int main(int argc, char **argv)
{
    struct MsgPort *port = NULL, *tport = NULL;
    struct timerequest *tr = NULL;
    struct RDArgs *rd = NULL;
    LONG args[2] = { 0, 0 };
    CxObj *broker = NULL, *cust;
    ULONG timeout;
    int quit = 0, timer = 0, now = 0;
    (void)version; (void)argv;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    CxBase = OpenLibrary((STRPTR)"commodities.library", 39);
    DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 39);
    if (!IntuitionBase || !GfxBase || !CxBase) goto out;
    if (argc > 0 && (rd = ReadArgs((STRPTR)"TIMEOUT/N,NOW/S", args, NULL))) {
        now = args[1] != 0;
        timeout = timeout_setting(args[0] ? *(LONG *)args[0] : 0);
    } else timeout = timeout_setting(0);
    me = FindTask(NULL);
    {   /* already running: only OpenBlanker NOW does anything, and by a signal */
        struct MsgPort *other;
        Forbid();
        other = FindPort((STRPTR)PORT_NAME);
        if (other && other->mp_SigTask && now) Signal((struct Task *)other->mp_SigTask, SIG_PLAY);
        Permit();
        if (other) goto out;
    }
    if (!(port = CreateMsgPort())) goto out;
    port->mp_Node.ln_Name = (char *)PORT_NAME;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);
    nb.nb_Port = port;
    if (!(broker = CxBroker(&nb, NULL))) goto out;    /* already running: it got CXCMD_UNIQUE and plays */
    if (!(cust = CxCustom(custom, 0))) goto out;
    AttachCxObj(broker, cust);
    ActivateCxObj(broker, 1);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);
    if (timer) { tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0; SendIO((struct IORequest *)tr); }
    if (now) play();

    while (!quit) {
        ULONG got = Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F | SIG_WAKE | SIG_PLAY |
                         (timer ? 1UL << tport->mp_SigBit : 0));
        CxMsg *m;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) timeout = timeout_setting(0);    /* the settings changed */
        if ((got & SIG_PLAY) && on) play();
        while ((m = (CxMsg *)GetMsg(port))) {
            ULONG type = CxMsgType(m), id = CxMsgID(m);
            ReplyMsg((struct Message *)m);
            if (type == CXM_COMMAND) switch (id) {
            case CXCMD_DISABLE: ActivateCxObj(broker, 0); on = 0; break;
            case CXCMD_ENABLE: ActivateCxObj(broker, 1); on = 1; idle_ticks = 0; break;
            case CXCMD_KILL: quit = 1; break;
            case CXCMD_UNIQUE: break;                      /* started again: nothing (OpenBlanker NOW asks by signal) */
            }
        }
        if (timer && CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            if (on && ++idle_ticks >= timeout) play();
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
        }
    }
out:
    if (broker) ActivateCxObj(broker, 0);
    if (timer) { AbortIO((struct IORequest *)tr); WaitIO((struct IORequest *)tr); CloseDevice((struct IORequest *)tr); }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (broker) DeleteCxObjAll(broker);
    if (port) { struct Message *msg; if (port->mp_Node.ln_Name) RemPort(port); while ((msg = GetMsg(port))) ReplyMsg(msg); DeleteMsgPort(port); }
    if (rd) FreeArgs(rd);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (CxBase) CloseLibrary(CxBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
