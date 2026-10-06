/* OpenMenus: the Open family's own menus (MIT). A commodity, with no
 * Intuition patches: it takes the right mouse button from the input stream
 * when the active window has a menu strip, draws the menus itself in
 * borderless windows that never take the activation, follows the mouse, and
 * gives the program its choice as Intuition would, an IDCMP_MENUPICK
 * message on the window's port.
 *
 *   OpenMenus            (started at boot; Ctrl-C or Exchange quits it)
 *
 * Settings: ENV:OpenMenus/Menus, format 1 (Menus/DESIGN.md), read with the
 * editor's own om_prefs.c; Ctrl-F to the task of the public port "OpenMenus"
 * reads them again. Colours from the OpenLook theme, the screen's pens, or
 * the user's. Lite (Look) leaves out the shadow.
 *
 * 0.1 (6 October 2026): pull-down from the screen bar, pop-up at the pointer,
 * or by where the pointer is; hold and release, sticky on move, sticky on
 * click; submenus with an arrow; check marks and mutual exclusion; command
 * keys; ghosted items; MENUVERIFY asked first, as Intuition does; a shadow.
 * Not yet: keyboard control, opening delays, see-through and pictures.
 *
 * Safe against a program changing its menus while one is open: the strip is
 * copied when the menu opens, and before a choice is given or a check mark
 * changed the item is looked up again in the window's strip as it is then.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/commodities.h>
#include <proto/timer.h>
#include <proto/rexxsyslib.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <dos/dostags.h>
#include <libraries/asl.h>
#include <proto/asl.h>

#include <string.h>
#include <stdarg.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "om_prefs.h"
#include "ogt_theme.h"

static const char version[] = "$VER: OpenMenus 0.2 (6.10.2026) MIT, Copyright (c) 2026 Dalsin Limited";

#define PREFS_ENV "ENV:OpenMenus/Menus"
#define LOOK_ENV "ENV:OpenGadTools/Look"
#define THEME_DIR "SYS:Prefs/Presets/Themes"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *CxBase, *LayersBase;
struct Device *TimerBase;
struct RxsLib *RexxSysBase;
struct Library *AslBase;

/* ---- files and settings --------------------------------------------------- */

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

static om_prefs prefs;                   /* static: Run gives a commodity 4 KB of stack */
static ogt_theme theme;
static int theme_loaded, theme_dark, lite_on;

static void read_settings(void)
{
    char *t = read_file(PREFS_ENV), *look, name[48] = "Open", mode[8] = "light", path[128], err[80];
    om_defaults(&prefs);
    if (t) { om_parse(&prefs, t); FreeVec(t); }
    if (theme_loaded) { ogt_theme_free(&theme); theme_loaded = 0; }
    lite_on = 0;
    if ((look = read_file(LOOK_ENV))) {
        sscanf(look, "%47s %7s", name, mode);
        lite_on = strstr(look, "\nlite on") != NULL;
        FreeVec(look);
    }
    theme_dark = !strcmp(mode, "dark");
    if (!strcmp(mode, "auto")) {
        struct DateStamp ds;
        DateStamp(&ds);
        theme_dark = ds.ds_Minute / 60 >= 19 || ds.ds_Minute / 60 < 7;
    }
    snprintf(path, sizeof path, THEME_DIR "/%s.theme", name);
    if ((t = read_file(path))) {
        if (ogt_theme_parse(&theme, t, err, sizeof err)) theme_loaded = 1;
        FreeVec(t);
    }
}

/* ---- the input stream (input.device's task: no waiting, no DOS) ----------------- */

static struct Task *me;
static ULONG sig_start, sig_event;
static volatile int on = 1, session, cancelled;
static volatile int rbutton, lbutton, rdown_seen, ldown_seen, moved;
static volatile WORD start_x, start_y;
static struct Window *volatile rc_window;        /* the Workbench window right-clicked (session 2) */

static int menu_window(struct Window *w)
{
    return w && w->MenuStrip && !(w->Flags & WFLG_RMBTRAP) && w->WScreen == IntuitionBase->FirstScreen;
}

static void custom(CxMsg *msg, CxObj *co)
{
    struct InputEvent *ie = (struct InputEvent *)CxMsgData(msg);
    (void)co;
    if (!on || !prefs.enabled) return;
    for (; ie; ie = ie->ie_NextEvent) {
        if (ie->ie_Class == IECLASS_RAWMOUSE) {
            UWORD code = ie->ie_Code;
            if (!session) {
                if (code == IECODE_RBUTTON && prefs.rightclick) {
                    /* on a Workbench window's icons or background (not its title bar, not
                     * the screen bar): the icon or desktop menu (the layers are read
                     * unlocked here: only to choose, and the choice is checked again) */
                    struct Screen *s = IntuitionBase->FirstScreen;
                    struct Layer *l = s ? WhichLayer(&s->LayerInfo, s->MouseX, s->MouseY) : NULL;
                    struct Window *w = l ? (struct Window *)l->Window : NULL;
                    if (w && (w->Flags & WFLG_WBENCHWINDOW) && s->MouseY > s->BarHeight &&
                        s->MouseY >= w->TopEdge + w->BorderTop && s->MouseX >= w->LeftEdge + w->BorderLeft &&
                        s->MouseX < w->LeftEdge + w->Width - w->BorderRight && s->MouseY < w->TopEdge + w->Height - w->BorderBottom) {
                        session = 2; rc_window = w; rbutton = 1; lbutton = 0; rdown_seen = ldown_seen = moved = 0;
                        start_x = s->MouseX; start_y = s->MouseY;
                        ie->ie_Class = IECLASS_NULL;
                        Signal(me, sig_start);
                        continue;
                    }
                }
                if (code == IECODE_RBUTTON && menu_window(IntuitionBase->ActiveWindow)) {
                    struct Screen *s = IntuitionBase->FirstScreen;
                    session = 1; rbutton = 1; lbutton = 0; rdown_seen = ldown_seen = moved = 0;
                    start_x = s->MouseX; start_y = s->MouseY;
                    ie->ie_Class = IECLASS_NULL;       /* Intuition's own menus stay shut */
                    Signal(me, sig_start);
                }
                continue;
            }
            /* a menu is open: the buttons are OpenMenus', the moves go on (the pointer moves) */
            if (code == IECODE_RBUTTON) { rbutton = 1; rdown_seen = 1; ie->ie_Class = IECLASS_NULL; }
            else if (code == (IECODE_RBUTTON | IECODE_UP_PREFIX)) { rbutton = 0; ie->ie_Class = IECLASS_NULL; }
            else if (code == IECODE_LBUTTON) { lbutton = 1; ldown_seen = 1; ie->ie_Class = IECLASS_NULL; }
            else if (code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) { lbutton = 0; ie->ie_Class = IECLASS_NULL; }
            else moved = 1;
            Signal(me, sig_event);
        } else if (session && ie->ie_Class == IECLASS_RAWKEY && (ie->ie_Code & 0x7f) == 0x45) {
            if (!(ie->ie_Code & IECODE_UP_PREFIX)) { cancelled = 1; Signal(me, sig_event); }  /* Esc */
            ie->ie_Class = IECLASS_NULL;
        }
    }
}

/* ---- the menus, copied ----------------------------------------------------------------- */

#define OM_HEADER 0x8000         /* our own items (icon menus): the name at the top */
#define OM_SEPARATOR 0x0800      /* ... and a separator line */
#define MAX_MENUS 31
#define MAX_POOL 640             /* items and subitems, all menus together */
#define MAX_TEXTS 2

struct ctext { WORD left, top; UBYTE pen, mode; char s[64]; };
struct citem {
    WORD left, top, width, height;
    UWORD flags;                 /* ITEMTEXT, COMMSEQ, CHECKIT, CHECKED, ITEMENABLED, MENUTOGGLE */
    LONG exclude;
    char command;
    UBYTE ntexts;
    struct ctext text[MAX_TEXTS];
    struct Image *image;         /* when not ITEMTEXT: the program's image, drawn as given */
    UWORD nsubs;
    struct citem *subs;          /* in the pool, or NULL */
};
struct cmenu { WORD left, width; UWORD flags; char name[64]; int nitems; struct citem *items; };

static struct cmenu cmenus[MAX_MENUS];
static struct citem pool[MAX_POOL];
static int ncmenus, npool;

static void copy_item(struct citem *c, struct MenuItem *it)
{
    c->left = it->LeftEdge; c->top = it->TopEdge; c->width = it->Width; c->height = it->Height;
    c->flags = it->Flags; c->exclude = it->MutualExclude; c->command = it->Command;
    c->ntexts = 0; c->image = NULL; c->nsubs = 0; c->subs = NULL;
    if (it->Flags & ITEMTEXT) {
        struct IntuiText *t;
        for (t = (struct IntuiText *)it->ItemFill; t && c->ntexts < MAX_TEXTS; t = t->NextText) {
            struct ctext *ct = &c->text[c->ntexts++];
            ct->left = t->LeftEdge; ct->top = t->TopEdge; ct->pen = t->FrontPen; ct->mode = t->DrawMode;
            strncpy(ct->s, t->IText ? (const char *)t->IText : "", sizeof ct->s - 1);
            ct->s[sizeof ct->s - 1] = 0;
        }
    } else c->image = (struct Image *)it->ItemFill;
}

/* Copies the strip under Forbid(): the program can't change it half way. */
static int copy_strip(struct Menu *strip)
{
    struct Menu *m;
    int i = 0;
    npool = 0;
    Forbid();
    for (m = strip; m && i < MAX_MENUS; m = m->NextMenu, i++) {
        struct cmenu *c = &cmenus[i];
        struct MenuItem *it;
        int j = 0;
        c->left = m->LeftEdge; c->width = m->Width; c->flags = m->Flags;
        strncpy(c->name, m->MenuName ? (const char *)m->MenuName : "", sizeof c->name - 1);
        c->name[sizeof c->name - 1] = 0;
        c->items = &pool[npool];
        for (it = m->FirstItem; it && npool < MAX_POOL; it = it->NextItem) { copy_item(&pool[npool++], it); j++; }
        c->nitems = j;
        for (j = 0, it = m->FirstItem; j < c->nitems; it = it->NextItem, j++) {
            struct MenuItem *sub;
            c->items[j].subs = &pool[npool];
            for (sub = it->SubItem; sub && npool < MAX_POOL; sub = sub->NextItem) { copy_item(&pool[npool++], sub); c->items[j].nsubs++; }
            if (!c->items[j].nsubs) c->items[j].subs = NULL;
        }
    }
    Permit();
    return ncmenus = i;
}

/* The window's item as it is now, or NULL if the program changed its menus. */
static struct MenuItem *live_item(struct Window *w, struct Menu *strip, int mi, int ii, int si)
{
    struct Menu *m;
    struct MenuItem *it;
    int i;
    if (w->MenuStrip != strip) return NULL;
    for (m = strip, i = 0; m && i < mi; m = m->NextMenu, i++) ;
    if (!m) return NULL;
    for (it = m->FirstItem, i = 0; it && i < ii; it = it->NextItem, i++) ;
    if (!it || si < 0) return it;
    for (it = it->SubItem, i = 0; it && i < si; it = it->NextItem, i++) ;
    return it;
}

/* ---- drawing ------------------------------------------------------------------------------ */

struct panel {
    struct Window *win, *shadow;
    WORD x, y, w, h;             /* on the screen */
    WORD ox, oy;                 /* where item (0,0) is, in the window */
    int kind;                    /* 0 bar, 1 titles (pop-up), 2 items, 3 subitems */
    int menu, item;              /* what it shows */
    int hot;                     /* the highlighted entry, or -1 */
};

static struct Screen *scr;
static struct DrawInfo *dri;
static struct TextFont *font;
static LONG pen[OM_C_COUNT];
static LONG obtained[OM_C_COUNT];
static int nobtained;
static struct Window *target;
static struct Menu *target_strip;
static struct panel panels[4];   /* bar or titles, items, subitems */
static int npanels;

static LONG pen_rgb(ogt_rgb c)
{
    LONG p = ObtainBestPen(scr->ViewPort.ColorMap, (ULONG)c.r * 0x01010101UL, (ULONG)c.g * 0x01010101UL,
                           (ULONG)c.b * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    if (p >= 0 && nobtained < OM_C_COUNT) obtained[nobtained++] = p;
    return p;
}

static void choose_pens(void)
{
    static const char *const keys[OM_C_COUNT] = { "menu", "menu.text", "accent", "accent.text", "frame.highlight", "menu.line", NULL };
    static const int dpen[OM_C_COUNT] = { BARBLOCKPEN, BARDETAILPEN, FILLPEN, FILLTEXTPEN, SHINEPEN, SHADOWPEN, SHADOWPEN };
    for (int k = 0; k < OM_C_COUNT; k++) {
        ogt_rgb c;
        LONG p = -1;
        if (prefs.colours == OM_COL_OWN && ogt_parse_colour(prefs.colour[k], &c)) p = pen_rgb(c);
        else if (prefs.colours == OM_COL_THEME && theme_loaded && !theme.passthrough && keys[k] &&
                 ogt_theme_colour(&theme, theme_dark ? OGT_DARK : OGT_LIGHT, keys[k], &c)) p = pen_rgb(c);
        pen[k] = p >= 0 ? p : dri->dri_Pens[dpen[k]];
    }
}

static void free_pens(void)
{
    for (int i = 0; i < nobtained; i++) ReleasePen(scr->ViewPort.ColorMap, obtained[i]);
    nobtained = 0;
}

static struct Window *open_box(WORD x, WORD y, WORD w, WORD h)
{
    if (x + w > scr->Width) x = scr->Width - w;
    if (y + h > scr->Height) y = scr->Height - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                          WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                          WA_NoCareRefresh, TRUE, WA_IDCMP, 0, TAG_DONE);
}

static void frame(struct RastPort *rp, WORD w, WORD h)
{
    SetAPen(rp, pen[OM_C_BACKGROUND]);
    RectFill(rp, 0, 0, w - 1, h - 1);
    SetAPen(rp, pen[OM_C_DARK]);
    Move(rp, 0, 0); Draw(rp, w - 1, 0); Draw(rp, w - 1, h - 1); Draw(rp, 0, h - 1); Draw(rp, 0, 0);
    if (prefs.border_double) { Move(rp, 2, 2); Draw(rp, w - 3, 2); Draw(rp, w - 3, h - 3); Draw(rp, 2, h - 3); Draw(rp, 2, 2); }
}

static UWORD ghost_pat[2] = { 0x8888, 0x2222 };

static void ghost(struct RastPort *rp, WORD x0, WORD y0, WORD x1, WORD y1)
{
    SetAfPt(rp, ghost_pat, 1);
    SetAPen(rp, pen[OM_C_BACKGROUND]);
    RectFill(rp, x0, y0, x1, y1);
    SetAfPt(rp, NULL, 0);
}

/* An item at (x, y) in the panel's window, highlighted or not. */
static void draw_item(struct RastPort *rp, const struct citem *c, WORD x, WORD y, int hot, int enabled)
{
    WORD x1 = x + c->width - 1, y1 = y + c->height - 1;
    LONG text = hot ? pen[OM_C_SELECTED_TEXT] : pen[OM_C_TEXT];
    SetAPen(rp, hot && enabled ? pen[OM_C_SELECTED] : pen[OM_C_BACKGROUND]);
    RectFill(rp, x, y, x1, y1);
    if (c->flags & ITEMTEXT) {
        for (int k = 0; k < c->ntexts; k++) {
            const struct ctext *t = &c->text[k];
            UBYTE fp = (UBYTE)(hot && enabled ? text : prefs.colours == OM_COL_SCREEN ? t->pen : text);
            struct IntuiText it = { fp, 0, JAM1, t->left, t->top, NULL, (UBYTE *)t->s, NULL };
            PrintIText(rp, &it, x, y);
        }
    } else if (c->image) {
        DrawImage(rp, c->image, x, y);
    }
    if (c->flags & CHECKIT) {
        if (c->flags & CHECKED) {
            struct Image *chk = target->CheckMark;
            if (chk) DrawImage(rp, chk, x, y + (c->height - chk->Height) / 2);
        }
    }
    if (c->flags & COMMSEQ) {
        char key[2] = { c->command, 0 };
        WORD cw = TextLength(rp, (STRPTR)key, 1);
        struct Image *akey = dri->dri_AmigaKey;
        WORD aw = akey ? akey->Width : 0;
        WORD tx = x1 - cw - 2;
        SetAPen(rp, text); SetDrMd(rp, JAM1);
        if (akey) DrawImage(rp, akey, tx - aw - 1, y + (c->height - akey->Height) / 2);
        Move(rp, tx, y + (c->height - rp->TxHeight) / 2 + rp->TxBaseline);
        Text(rp, (STRPTR)key, 1);
    }
    if (c->nsubs && prefs.sub_mark) {
        WORD ay = y + c->height / 2;
        SetAPen(rp, text);
        for (int k = 0; k < 4; k++) { Move(rp, x1 - 7 + k, ay - 3 + k); Draw(rp, x1 - 7 + k, ay + 3 - k); }
    }
    if (c->flags & OM_SEPARATOR) {             /* a line, the item's whole box */
        SetAPen(rp, pen[OM_C_BACKGROUND]); RectFill(rp, x, y, x1, y1);
        SetAPen(rp, pen[OM_C_DARK]); Move(rp, x + 2, y + c->height / 2); Draw(rp, x1 - 2, y + c->height / 2);
        return;
    }
    if (c->flags & OM_HEADER) {                /* the icon's name: dimmer, never chosen */
        SetAPen(rp, pen[OM_C_BACKGROUND]); RectFill(rp, x, y, x1, y1);
        SetAPen(rp, pen[OM_C_DARK]); SetDrMd(rp, JAM1);
        Move(rp, x + c->text[0].left, y + c->text[0].top + rp->TxBaseline);
        Text(rp, (STRPTR)c->text[0].s, strlen(c->text[0].s));
        Move(rp, x + 2, y1); Draw(rp, x1 - 2, y1);
        return;
    }
    if (!enabled) ghost(rp, x, y, x1, y1);
}

static int item_enabled(int mi, const struct citem *c)
{
    return (cmenus[mi].flags & MENUENABLED) && (c->flags & ITEMENABLED);
}

/* The box a menu's items take, relative to their own (0,0). */
static void items_box(const struct citem *items, int n, WORD *minx, WORD *miny, WORD *maxx, WORD *maxy)
{
    *minx = *miny = 0x7fff; *maxx = *maxy = -0x7fff;
    for (int i = 0; i < n; i++) {
        if (items[i].left < *minx) *minx = items[i].left;
        if (items[i].top < *miny) *miny = items[i].top;
        if (items[i].left + items[i].width > *maxx) *maxx = items[i].left + items[i].width;
        if (items[i].top + items[i].height > *maxy) *maxy = items[i].top + items[i].height;
    }
    if (*minx > *maxx) { *minx = *miny = 0; *maxx = *maxy = 1; }
}

static void draw_panel(struct panel *p)
{
    struct RastPort *rp = p->win->RPort;
    SetFont(rp, font);
    if (p->kind == 0) {                      /* the screen bar's titles */
        SetAPen(rp, pen[OM_C_BACKGROUND]);
        RectFill(rp, 0, 0, p->w - 1, p->h - 1);
        SetAPen(rp, pen[OM_C_DARK]);
        Move(rp, 0, p->h - 1); Draw(rp, p->w - 1, p->h - 1);
        for (int i = 0; i < ncmenus; i++) {
            WORD x = cmenus[i].left + scr->BarHBorder;
            if (i == p->hot) { SetAPen(rp, pen[OM_C_SELECTED]); RectFill(rp, x, 0, x + cmenus[i].width - 1, p->h - 2); }
            SetAPen(rp, i == p->hot ? pen[OM_C_SELECTED_TEXT] : pen[OM_C_TEXT]); SetDrMd(rp, JAM1);
            Move(rp, x + 4, scr->BarVBorder + rp->TxBaseline);
            Text(rp, (STRPTR)cmenus[i].name, strlen(cmenus[i].name));
            if (!(cmenus[i].flags & MENUENABLED)) ghost(rp, x, 0, x + cmenus[i].width - 1, p->h - 2);
        }
        return;
    }
    frame(rp, p->w, p->h);
    if (p->kind == 1) {                      /* pop-up: the menus' titles, one a line */
        WORD lh = font->tf_YSize + 4;
        for (int i = 0; i < ncmenus; i++) {
            WORD y = p->oy + i * lh;
            SetAPen(rp, i == p->hot ? pen[OM_C_SELECTED] : pen[OM_C_BACKGROUND]);
            RectFill(rp, 3, y, p->w - 4, y + lh - 1);
            SetAPen(rp, i == p->hot ? pen[OM_C_SELECTED_TEXT] : pen[OM_C_TEXT]); SetDrMd(rp, JAM1);
            Move(rp, 8, y + 2 + rp->TxBaseline);
            Text(rp, (STRPTR)cmenus[i].name, strlen(cmenus[i].name));
            if (prefs.sub_mark) {
                WORD ay = y + lh / 2, x1 = p->w - 6;
                for (int k = 0; k < 4; k++) { Move(rp, x1 - 7 + k, ay - 3 + k); Draw(rp, x1 - 7 + k, ay + 3 - k); }
            }
            if (!(cmenus[i].flags & MENUENABLED)) ghost(rp, 3, y, p->w - 4, y + lh - 1);
        }
        return;
    }
    {
        const struct citem *items = p->kind == 2 ? cmenus[p->menu].items : cmenus[p->menu].items[p->item].subs;
        int n = p->kind == 2 ? cmenus[p->menu].nitems : cmenus[p->menu].items[p->item].nsubs;
        for (int i = 0; i < n; i++)
            draw_item(rp, &items[i], p->ox + items[i].left, p->oy + items[i].top, i == p->hot,
                      item_enabled(p->menu, &items[i]) && (p->kind == 2 || item_enabled(p->menu, &cmenus[p->menu].items[p->item])));
    }
}

static void close_panel(int k)
{
    while (npanels > k) {
        struct panel *p = &panels[--npanels];
        if (p->win) CloseWindow(p->win);
        if (p->shadow) CloseWindow(p->shadow);
        p->win = p->shadow = NULL;
    }
}

static int open_panel(int kind, int menu, int item, WORD x, WORD y, WORD w, WORD h, WORD ox, WORD oy)
{
    struct panel *p = &panels[npanels];
    int sh = prefs.shadow && !lite_on && kind != 0 ? prefs.shadow_size : 0;
    memset(p, 0, sizeof *p);
    p->kind = kind; p->menu = menu; p->item = item; p->hot = -1; p->ox = ox; p->oy = oy;
    if (x + w + sh > scr->Width) x = scr->Width - w - sh;
    if (y + h + sh > scr->Height) y = scr->Height - h - sh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    p->x = x; p->y = y; p->w = w; p->h = h;
    if (sh > 0 && (p->shadow = open_box(x + sh, y + sh, w, h))) {
        SetAPen(p->shadow->RPort, pen[OM_C_SHADOW]);
        RectFill(p->shadow->RPort, 0, 0, w - 1, h - 1);
    }
    if (!(p->win = open_box(x, y, w, h))) { if (p->shadow) CloseWindow(p->shadow); p->shadow = NULL; return 0; }
    npanels++;
    draw_panel(p);
    return 1;
}

/* The items of menu mi, below its title (pull-down) or beside the titles (pop-up). */
static void open_items(int mi)
{
    WORD minx, miny, maxx, maxy, x, y, pad = prefs.border_double ? 4 : 2;
    struct panel *from = &panels[0];
    items_box(cmenus[mi].items, cmenus[mi].nitems, &minx, &miny, &maxx, &maxy);
    if (!cmenus[mi].nitems) return;
    if (from->kind == 0) { x = cmenus[mi].left + scr->BarHBorder + minx - pad; y = scr->BarHeight + 1 + miny - pad; }
    else {
        x = from->x + from->w - 2; y = from->y + from->oy + mi * (font->tf_YSize + 4) - pad;
        if (x + (maxx - minx + 2 * pad) > scr->Width) x = from->x - (maxx - minx + 2 * pad) + 2;   /* no room: on the left */
    }
    open_panel(2, mi, -1, x, y, maxx - minx + 2 * pad, maxy - miny + 2 * pad, pad - minx, pad - miny);
}

static void open_subs(int mi, int ii)
{
    const struct citem *it = &cmenus[mi].items[ii];
    WORD minx, miny, maxx, maxy, pad = prefs.border_double ? 4 : 2;
    struct panel *ip = &panels[npanels - 1];
    WORD x, y;
    items_box(it->subs, it->nsubs, &minx, &miny, &maxx, &maxy);
    if (prefs.sub_centre) { x = ip->x + ip->ox + it->left + it->width - 4; y = ip->y + ip->oy + it->top + it->height / 2 - (maxy - miny) / 2 - pad; }
    else { x = ip->x + ip->ox + it->left + minx - pad; y = ip->y + ip->oy + it->top + miny - pad; }
    if (x + (maxx - minx + 2 * pad) > scr->Width) x = ip->x + ip->ox + it->left - (maxx - minx + 2 * pad) + 4;   /* no room: on the left */
    open_panel(3, mi, ii, x, y, maxx - minx + 2 * pad, maxy - miny + 2 * pad, pad - minx, pad - miny);
}

/* ---- following the mouse --------------------------------------------------------------------- */

static int hit_panel(WORD sx, WORD sy)
{
    for (int k = npanels - 1; k >= 0; k--)
        if (sx >= panels[k].x && sy >= panels[k].y && sx < panels[k].x + panels[k].w && sy < panels[k].y + panels[k].h) return k;
    return -1;
}

static int hit_entry(struct panel *p, WORD sx, WORD sy)
{
    WORD x = sx - p->x, y = sy - p->y;
    if (p->kind == 0) {
        for (int i = 0; i < ncmenus; i++) {
            WORD l = cmenus[i].left + scr->BarHBorder;
            if (x >= l && x < l + cmenus[i].width) return i;
        }
        return -1;
    }
    if (p->kind == 1) {
        int i = (y - p->oy) / (font->tf_YSize + 4);
        return y >= p->oy && i < ncmenus ? i : -1;
    }
    {
        const struct citem *items = p->kind == 2 ? cmenus[p->menu].items : cmenus[p->menu].items[p->item].subs;
        int n = p->kind == 2 ? cmenus[p->menu].nitems : cmenus[p->menu].items[p->item].nsubs;
        for (int i = 0; i < n; i++) {
            WORD ix = p->ox + items[i].left, iy = p->oy + items[i].top;
            if (x >= ix && y >= iy && x < ix + items[i].width && y < iy + items[i].height) return i;
        }
    }
    return -1;
}

static void set_hot(int k, int e)
{
    if (panels[k].hot == e) return;
    panels[k].hot = e;
    draw_panel(&panels[k]);
}

/* Moves the highlight to where the pointer is, opening and closing what follows from it. */
static void follow(WORD sx, WORD sy)
{
    int k = hit_panel(sx, sy), e;
    if (k < 0) {                              /* outside: the deepest panel loses its highlight */
        if (npanels && panels[npanels - 1].kind >= 2) set_hot(npanels - 1, -1);
        return;
    }
    e = hit_entry(&panels[k], sx, sy);
    if (panels[k].kind <= 1) {
        if (e >= 0 && e != panels[k].hot) { close_panel(k + 1); set_hot(k, e); if (cmenus[e].flags & MENUENABLED) open_items(e); }
        return;
    }
    if (panels[k].kind == 2) {
        if (e != panels[k].hot) {
            close_panel(k + 1);
            set_hot(k, e);
            if (e >= 0 && cmenus[panels[k].menu].items[e].nsubs && item_enabled(panels[k].menu, &cmenus[panels[k].menu].items[e]))
                open_subs(panels[k].menu, e);
        }
        return;
    }
    set_hot(k, e);
}

/* What is chosen: menu, item and sub (or -1); 0 when nothing can be. */
static int chosen(int *mi, int *ii, int *si)
{
    struct panel *p;
    if (!npanels) return 0;
    p = &panels[npanels - 1];
    if (p->kind == 3 && p->hot >= 0) {
        *mi = p->menu; *ii = p->item; *si = p->hot;
        return item_enabled(*mi, &cmenus[*mi].items[*ii]) && item_enabled(*mi, &cmenus[*mi].items[*ii].subs[*si]);
    }
    if (p->kind == 3) p = &panels[npanels - 2];
    if (p->kind == 2 && p->hot >= 0) {
        *mi = p->menu; *ii = p->hot; *si = -1;
        return !cmenus[*mi].items[*ii].nsubs && item_enabled(*mi, &cmenus[*mi].items[*ii]);
    }
    return 0;
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

/* ---- telling the program ---------------------------------------------------------------------- */

static struct MsgPort *reply_port;
static int out_msgs;

static void reap(void)
{
    struct Message *m;
    while ((m = GetMsg(reply_port))) { FreeVec(m); out_msgs--; }
}

static struct IntuiMessage *new_msg(struct Window *w, ULONG cls, UWORD code)
{
    struct IntuiMessage *m = AllocVec(sizeof(struct ExtIntuiMessage), MEMF_PUBLIC | MEMF_CLEAR);
    ULONG s, mi;
    if (!m) return NULL;
    m->ExecMessage.mn_ReplyPort = reply_port;
    m->ExecMessage.mn_Length = sizeof(struct ExtIntuiMessage);
    m->Class = cls; m->Code = code; m->Qualifier = 0;
    m->MouseX = w->MouseX; m->MouseY = w->MouseY;
    CurrentTime(&s, &mi);
    m->Seconds = s; m->Micros = mi;
    m->IDCMPWindow = w;
    return m;
}

/* MENUVERIFY first, as Intuition asks it: 1 to go on, 0 when the program says no. */
static int verify(struct Window *w)
{
    struct IntuiMessage *m;
    int ok = 1, n;
    if (!(w->IDCMPFlags & IDCMP_MENUVERIFY) || !w->UserPort) return 1;
    if (!(m = new_msg(w, IDCMP_MENUVERIFY, MENUHOT))) return 1;
    out_msgs++;
    PutMsg(w->UserPort, (struct Message *)m);
    for (n = 0; n < 100; n++) {               /* up to 2 seconds */
        struct Message *r;
        while ((r = GetMsg(reply_port))) {
            if (r == (struct Message *)m) ok = ((struct IntuiMessage *)r)->Code != MENUCANCEL;
            FreeVec(r); out_msgs--;
            if (r == (struct Message *)m) return ok;
        }
        Delay(1);
    }
    return 1;                                 /* no answer: Intuition goes on too */
}

/* The choice, and the check marks, put in the window's own strip as it is now. */
static void pick(struct Window *w, int mi, int ii, int si)
{
    struct MenuItem *it = mi >= 0 ? live_item(w, target_strip, mi, ii, si) : NULL;
    UWORD code = MENUNULL;
    struct IntuiMessage *m;
    if (it) {
        if (it->Flags & CHECKIT) {
            if (it->Flags & MENUTOGGLE) it->Flags ^= CHECKED;
            else it->Flags |= CHECKED;
            if (it->MutualExclude) {
                /* the items of the same menu (or submenu) the exclusion names lose theirs */
                struct MenuItem *o = si >= 0 ? live_item(w, target_strip, mi, ii, -1)->SubItem : live_item(w, target_strip, mi, 0, -1);
                for (int n = 0; o && n < 32; o = o->NextItem, n++)
                    if (o != it && (it->MutualExclude & (1UL << n))) o->Flags &= ~CHECKED;
            }
        }
        it->NextSelect = MENUNULL;
        code = (UWORD)FULLMENUNUM(mi, ii, si >= 0 ? si : NOSUB);
    }
    if (!w->UserPort || !(w->IDCMPFlags & IDCMP_MENUPICK)) return;
    if ((m = new_msg(w, IDCMP_MENUPICK, code))) { out_msgs++; PutMsg(w->UserPort, (struct Message *)m); }
}

/* ---- a menu, from the button going down to the choice ------------------------------------------ */

static struct timerequest *tr;
static struct MsgPort *tport;

static void menu_session(void)
{
    struct Window *w = IntuitionBase->ActiveWindow;
    WORD mx, my;
    int mode, use, sticky = 0, done = 0, mi = -1, ii = -1, si = -1, any_moved = 0;
    ULONG s0, m0, s1, m1;
    if (!menu_window(w)) { session = 0; return; }
    target = w; target_strip = w->MenuStrip;
    if (!verify(w) || !copy_strip(target_strip)) { session = 0; return; }
    scr = w->WScreen;
    if (!(dri = GetScreenDrawInfo(scr))) { session = 0; return; }
    font = OpenFont(scr->Font);
    if (!font) font = dri->dri_Font;
    choose_pens();
    mx = start_x; my = start_y;
    mode = prefs.open == OM_OPEN_POINTER ? (my <= scr->BarHeight ? OM_OPEN_PULLDOWN : OM_OPEN_POPUP) : prefs.open;
    use = prefs.use[mode == OM_OPEN_PULLDOWN ? OM_PD : OM_PU];
    npanels = 0;
    if (mode == OM_OPEN_PULLDOWN) {
        open_panel(0, -1, -1, 0, 0, scr->Width, scr->BarHeight + 1, 0, 0);
    } else {
        WORD lh = font->tf_YSize + 4, wmax = 0;
        struct RastPort trp;
        InitRastPort(&trp);
        SetFont(&trp, font);
        for (int i = 0; i < ncmenus; i++) { WORD t = TextLength(&trp, (STRPTR)cmenus[i].name, strlen(cmenus[i].name)); if (t > wmax) wmax = t; }
        open_panel(1, -1, -1, mx - 8, my - lh / 2 - 3, wmax + 30, ncmenus * lh + 6, 0, 3);
    }
    if (!npanels) goto out;
    follow(mx, my);
    CurrentTime(&s0, &m0);
    while (!done) {
        ULONG got = Wait(sig_event | (1UL << tport->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (CheckIO((struct IORequest *)tr)) {        /* every 20 ms: the pointer, as it is */
            WaitIO((struct IORequest *)tr);
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 20000;
            SendIO((struct IORequest *)tr);
        }
        reap();
        if (cancelled || (got & SIGBREAKF_CTRL_C)) { mi = -1; break; }
        if (scr->MouseX != mx || scr->MouseY != my) {
            if (abs(scr->MouseX - start_x) > 3 || abs(scr->MouseY - start_y) > 3) any_moved = 1;
            mx = scr->MouseX; my = scr->MouseY;
            follow(mx, my);
        }
        if (!sticky && !rbutton) {
            CurrentTime(&s1, &m1);
            /* let go quickly without moving: sticky; on move after a quick click: sticky too */
            ULONG ms = (s1 - s0) * 1000 + (m1 / 1000) - (m0 / 1000);
            if (use != OM_USE_HOLD && ms < 400 && (!any_moved || use == OM_USE_STICKY)) { sticky = 1; rdown_seen = ldown_seen = 0; continue; }
            if (!chosen(&mi, &ii, &si)) mi = -1;
            done = 1;
        } else if (sticky && (rdown_seen || ldown_seen) && !rbutton && !lbutton) {
            if (hit_panel(mx, my) < 0 || !chosen(&mi, &ii, &si)) mi = -1;
            if (hit_panel(mx, my) >= 0 && mi < 0 && panels[hit_panel(mx, my)].kind <= 1) { rdown_seen = ldown_seen = 0; continue; }
            done = 1;
        }
    }
out:
    close_panel(0);
    if (is_window(target)) pick(target, mi, ii, si);   /* not if the program closed it meanwhile */
    free_pens();
    if (font && font != dri->dri_Font) CloseFont(font);
    font = NULL;
    FreeScreenDrawInfo(scr, dri);
    dri = NULL;
    cancelled = 0;
    session = 0;
}

/* ---- right-click on Workbench's icons and desktop -------------------------------------------- *
 *
 * Workbench is asked through its own ARexx port (WORKBENCH): which of its
 * windows was clicked, where its icons are and which are selected; it is
 * told to select the icon clicked and to carry out the menu item chosen
 * ("MENU WINDOW ... INVOKE ICONS.INFORMATION"), as its own menus would. Other
 * entries run a program on the selected files. Nothing of Workbench's is
 * patched (Menus/DESIGN.md, "Right-click on icons and the desktop"). */

/* One command to Workbench; its result in out. RC_OK is 0. */
static LONG wbx(char *out, int size, const char *fmt, ...)
{
    char cmd[400];
    struct MsgPort *wb, *rp;
    struct RexxMsg *rm;
    LONG rc = 20;
    va_list ap;
    if (out && size) out[0] = 0;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (!RexxSysBase && !(RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36))) return 20;
    if (!(rp = CreateMsgPort())) return 20;
    if ((rm = CreateRexxMsg(rp, NULL, NULL))) {
        rm->rm_Action = RXCOMM | RXFF_RESULT;
        if ((rm->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)cmd, strlen(cmd)))) {
            Forbid();
            if ((wb = FindPort((STRPTR)"WORKBENCH"))) PutMsg(wb, (struct Message *)rm);
            Permit();
            if (wb) {
                /* Workbench busy with a requester answers nothing until it is
                   closed: give up after a second, so the mouse is never held */
                int t;
                for (t = 0; t < 50 && !GetMsg(rp); t++) Delay(1);
                if (t == 50) return 20;          /* the message and its port stay: a late reply lands there */
                rc = rm->rm_Result1;
                if (!rc && rm->rm_Result2) {
                    if (out) { strncpy(out, (const char *)rm->rm_Result2, size - 1); out[size - 1] = 0; }
                    DeleteArgstring((UBYTE *)rm->rm_Result2);
                }
            }
            DeleteArgstring((UBYTE *)rm->rm_Args[0]);
        }
        DeleteRexxMsg(rm);
    }
    DeleteMsgPort(rp);
    return rc;
}

static LONG wbnum(const char *fmt, const char *win, int i)
{
    char r[32];
    if (wbx(r, sizeof r, fmt, i, win)) return -1;
    return atol(r);
}

#define RC_MAX_ICONS 256
struct rc_icon { char name[108]; WORD x, y, w, h; UBYTE selected, kind; };
enum { K_OTHER, K_DISK, K_DRAWER, K_TOOL, K_PROJECT, K_GARBAGE, K_APPICON };
static struct rc_icon rc_icons[RC_MAX_ICONS];
static int rc_nicons;
static char rc_win[256];                 /* Workbench's name for the window: "root" or a path */
static char rc_paths[2048];              /* the selected files, each "quoted", for a command line */
static int rc_npaths;

/* An action: what an item does. */
enum { A_NONE, A_WB, A_RUNFILES, A_RUN, A_EXTRACT, A_CHOOSE, A_SEND };
struct rc_action { UBYTE kind; const char *arg; };
static struct rc_action rc_acts[64];
static int rc_nacts;

static int add_act(int kind, const char *arg)
{
    if (rc_nacts >= 64) return 0;
    rc_acts[rc_nacts].kind = (UBYTE)kind; rc_acts[rc_nacts].arg = arg;
    return rc_nacts++;
}

static WORD rc_lh, rc_w;

static struct citem *rc_item(struct citem *it, int *n, int top_index, const char *text, int kind, const char *arg, UWORD extra)
{
    struct citem *c = &it[(*n)++];
    memset(c, 0, sizeof *c);
    c->left = 0; c->top = (WORD)(top_index * rc_lh); c->width = rc_w; c->height = rc_lh;
    c->flags = ITEMTEXT | extra | ((extra & (OM_HEADER | OM_SEPARATOR)) ? 0 : ITEMENABLED);
    if (extra & OM_SEPARATOR) c->height = 6;
    c->ntexts = 1;
    c->text[0].left = 8; c->text[0].top = 2; c->text[0].pen = 1;
    strncpy(c->text[0].s, text, sizeof c->text[0].s - 1);
    c->exclude = kind ? add_act(kind, arg) : -1;
    return c;
}

static int exists_file(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

static int is_archive(const char *name)
{
    static const char *const ext[] = { ".lha", ".lzh", ".zip", ".tar", ".gz", ".tgz", ".adf", ".adz", ".dms", NULL };
    int n = strlen(name);
    for (int i = 0; ext[i]; i++) {
        int e = strlen(ext[i]);
        if (n > e && !strcasecmp(name + n - e, ext[i])) return 1;
    }
    return 0;
}

/* The selected icons' full paths, quoted, in rc_paths (volumes in the root window get a colon). */
static void selected_paths(void)
{
    rc_paths[0] = 0; rc_npaths = 0;
    for (int i = 0; i < rc_nicons; i++) {
        char p[300];
        if (!rc_icons[i].selected || rc_icons[i].kind == K_APPICON) continue;
        if (!strcmp(rc_win, "root")) snprintf(p, sizeof p, rc_icons[i].kind == K_DISK ? "%s:" : "%s", rc_icons[i].name);
        else snprintf(p, sizeof p, "%s%s%s", rc_win, rc_win[strlen(rc_win) - 1] == ':' ? "" : "/", rc_icons[i].name);
        if (strlen(rc_paths) + strlen(p) + 4 < sizeof rc_paths) {
            strcat(rc_paths, rc_paths[0] ? " \"" : "\""); strcat(rc_paths, p); strcat(rc_paths, "\"");
            rc_npaths++;
        }
    }
}

static void run_async(const char *cmd)
{
    BPTR nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL, SYS_Asynch, TRUE, NP_StackSize, 32768, TAG_DONE) == -1 && nil) Close(nil);
}

static void do_action(int a)
{
    char cmd[2400];
    if (a < 0 || a >= rc_nacts) return;
    switch (rc_acts[a].kind) {
    case A_WB:
        /* Workbench carries a menu item out on its active window (6 Oct 2026:
           MENU WINDOW <name> INVOKE did nothing on 3.2.3) */
        wbx(NULL, 0, "WINDOW \"%s\" ACTIVATE", rc_win);
        wbx(NULL, 0, "MENU INVOKE %s", rc_acts[a].arg);
        break;
    case A_RUN: run_async(rc_acts[a].arg); break;
    case A_RUNFILES:
        selected_paths();
        if (rc_npaths) { snprintf(cmd, sizeof cmd, "%s %s", rc_acts[a].arg, rc_paths); run_async(cmd); }
        break;
    case A_SEND:
        selected_paths();
        if (rc_npaths) { snprintf(cmd, sizeof cmd, "C:ACDrop SEND %s", rc_paths); run_async(cmd); }
        break;
    case A_EXTRACT: {
        /* each archive into the drawer it is in */
        const char *dir = strcmp(rc_win, "root") ? rc_win : "RAM:";
        selected_paths();
        if (rc_npaths) { snprintf(cmd, sizeof cmd, "SYS:Utilities/OpenCompress x %s \"%s\"", rc_paths, dir); run_async(cmd); }
        break;
    }
    case A_CHOOSE: {
        struct FileRequester *fr;
        if (!AslBase) AslBase = OpenLibrary((STRPTR)"asl.library", 38);
        if (AslBase && (fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)"Open with which program?",
                                                ASLFR_InitialDrawer, (ULONG)"SYS:Utilities", TAG_DONE))) {
            if (AslRequest(fr, NULL)) {
                char prog[300];
                strncpy(prog, (const char *)fr->fr_Drawer, sizeof prog - 1); prog[sizeof prog - 1] = 0;
                AddPart((STRPTR)prog, fr->fr_File, sizeof prog);
                selected_paths();
                snprintf(cmd, sizeof cmd, "\"%s\" %s", prog, rc_paths);
                run_async(cmd);
            }
            FreeAslRequest(fr);
        }
        break;
    }
    }
}

/* Workbench's name for Intuition's window w, and its icons with their places on the screen. */
static int rc_read(struct Window *w)
{
    LONG n = wbnum("GETATTR WINDOWS.COUNT%.0d%.0s", "", 0), vl, vt;
    char r[300];
    rc_win[0] = 0; rc_nicons = 0;
    for (int i = 0; i < n; i++) {
        char name[256];
        if (wbx(name, sizeof name, "GETATTR WINDOWS.%ld", (LONG)i)) continue;
        if (wbnum("GETATTR WINDOW.LEFT%.0d NAME \"%s\"", name, 0) == w->LeftEdge &&
            wbnum("GETATTR WINDOW.TOP%.0d NAME \"%s\"", name, 0) == w->TopEdge &&
            wbnum("GETATTR WINDOW.WIDTH%.0d NAME \"%s\"", name, 0) == w->Width) { strcpy(rc_win, name); break; }
    }
    if (!rc_win[0]) return 0;
    vl = wbnum("GETATTR WINDOW.VIEW.LEFT%.0d NAME \"%s\"", rc_win, 0);
    vt = wbnum("GETATTR WINDOW.VIEW.TOP%.0d NAME \"%s\"", rc_win, 0);
    if (vl < 0) vl = 0;
    if (vt < 0) vt = 0;
    n = wbnum("GETATTR WINDOW.ICONS.ALL.COUNT%.0d NAME \"%s\"", rc_win, 0);
    for (int i = 0; i < n && rc_nicons < RC_MAX_ICONS; i++) {
        struct rc_icon *ic = &rc_icons[rc_nicons];
        if (wbx(ic->name, sizeof ic->name, "GETATTR WINDOW.ICONS.ALL.%ld.NAME NAME \"%s\"", (LONG)i, rc_win)) continue;
        ic->x = (WORD)(w->LeftEdge + w->BorderLeft + wbnum("GETATTR WINDOW.ICONS.ALL.%d.LEFT NAME \"%s\"", rc_win, i) - vl);
        ic->y = (WORD)(w->TopEdge + w->BorderTop + wbnum("GETATTR WINDOW.ICONS.ALL.%d.TOP NAME \"%s\"", rc_win, i) - vt);
        ic->w = (WORD)wbnum("GETATTR WINDOW.ICONS.ALL.%d.WIDTH NAME \"%s\"", rc_win, i);
        ic->h = (WORD)wbnum("GETATTR WINDOW.ICONS.ALL.%d.HEIGHT NAME \"%s\"", rc_win, i);
        wbx(r, sizeof r, "GETATTR WINDOW.ICONS.ALL.%ld.STATUS NAME \"%s\"", (LONG)i, rc_win);
        ic->selected = !strncmp(r, "SELECTED", 8);
        wbx(r, sizeof r, "GETATTR WINDOW.ICONS.ALL.%ld.TYPE NAME \"%s\"", (LONG)i, rc_win);
        ic->kind = !strcmp(r, "DISK") ? K_DISK : !strcmp(r, "DRAWER") ? K_DRAWER : !strcmp(r, "TOOL") ? K_TOOL :
                   !strcmp(r, "PROJECT") ? K_PROJECT : !strcmp(r, "GARBAGE") ? K_GARBAGE : !strcmp(r, "APPICON") ? K_APPICON : K_OTHER;
        rc_nicons++;
    }
    return 1;
}

/* The icon under the pointer (its picture, and its name below it), or -1. */
static int rc_hit(WORD x, WORD y)
{
    WORD lh = font ? font->tf_YSize + 2 : 10;
    for (int i = rc_nicons - 1; i >= 0; i--) {
        struct rc_icon *ic = &rc_icons[i];
        WORD x0 = ic->x - 8, x1 = ic->x + ic->w + 8;
        if (x >= x0 && x < x1 && y >= ic->y && y < ic->y + ic->h + lh) return i;
    }
    return -1;
}

/* Builds the menu in cmenus[0] for the icon hit (or the background). 1 when there is one. */
static int rc_build(int hit)
{
    struct citem *it = pool, *st = pool + 200;   /* the menu's items, and the submenus' */
    int n = 0, ns = 0, row = 0, nsel = 0, root = !strcmp(rc_win, "root");
    struct RastPort trp;
    char head[120];
    static const char *const viewer[] = { "C:OpenView", "SYS:Utilities/MultiView", NULL };
    InitRastPort(&trp); SetFont(&trp, font);
    rc_lh = font->tf_YSize + 4; rc_w = 200;
    rc_nacts = 0; npool = 0;
    for (int i = 0; i < rc_nicons; i++) nsel += rc_icons[i].selected;
#define ITEM(text, kind, arg) rc_item(it, &n, row++, text, kind, arg, 0)
#define SEP() do { struct citem *s_ = rc_item(it, &n, 0, "", 0, NULL, OM_SEPARATOR); s_->top = (WORD)(row * rc_lh); row++; } while (0)
    if (hit >= 0 && rc_icons[hit].kind == K_APPICON) return 0;          /* a program's own icon: its own menu */
    if (hit >= 0) {
        const struct rc_icon *ic = &rc_icons[hit];
        int several = nsel > 1;
        if (prefs.rightclick_name) {
            if (several) snprintf(head, sizeof head, "%d icons", nsel); else strncpy(head, ic->name, sizeof head - 1), head[sizeof head - 1] = 0;
            rc_item(it, &n, row++, head, 0, NULL, OM_HEADER);
        }
        ITEM("Open", A_WB, "ICONS.OPEN");
        if (!several && ic->kind == K_PROJECT && prefs.rightclick_extras) {
            struct citem *ow = ITEM("Open with", 0, NULL);
            int k = 0;
            ow->subs = &st[ns];
            for (int v = 0; viewer[v]; v++)
                if (exists_file(viewer[v])) rc_item(st, &ns, k++, (const char *)FilePart((STRPTR)viewer[v]), A_RUNFILES, viewer[v], 0);
            rc_item(st, &ns, k++, "Choose a program...", A_CHOOSE, NULL, 0);
            ow->nsubs = (UWORD)k;
        }
        ITEM("Information...", A_WB, "ICONS.INFORMATION");
        if (!several) ITEM("Rename...", A_WB, "ICONS.RENAME");
        if (ic->kind != K_DISK && ic->kind != K_GARBAGE) ITEM("Copy", A_WB, "ICONS.COPY");
        ITEM("Snapshot", A_WB, "ICONS.SNAPSHOT");
        if (ic->kind != K_DISK) ITEM(root ? "Put away" : "Leave out", A_WB, root ? "ICONS.PUTAWAY" : "ICONS.LEAVEOUT");
        if (prefs.rightclick_extras && ic->kind != K_DISK) {
            int any = 0;
            if (is_archive(ic->name) && exists_file("SYS:Utilities/OpenCompress")) {
                SEP(); any = 1;
                ITEM("Extract here", A_EXTRACT, NULL);
                ITEM("Open in OpenCompress", A_RUNFILES, "SYS:Utilities/OpenCompress");
            }
            if (exists_file("C:ACDrop") && ic->kind != K_GARBAGE) { if (!any) SEP(); ITEM("Send to PC", A_SEND, NULL); }
        }
        SEP();
        if (ic->kind == K_DISK) { ITEM("Format disk...", A_WB, "ICONS.FORMATDISK"); ITEM("Eject", A_WB, "ICONS.EJECTDISK"); }
        else if (ic->kind == K_GARBAGE) ITEM("Empty trash", A_WB, "ICONS.EMPTYTRASH");
        else ITEM("Delete...", A_WB, "ICONS.DELETE");
    } else {
        struct citem *sub;
        int k;
        if (prefs.rightclick_name) {
            strncpy(head, root ? "Workbench" : rc_win, sizeof head - 1); head[sizeof head - 1] = 0;
            rc_item(it, &n, row++, head, 0, NULL, OM_HEADER);
        }
        if (root) { ITEM("Execute command...", A_WB, "WORKBENCH.EXECUTE"); ITEM("Shell", A_RUN, "NewShell"); }
        else { ITEM("New drawer...", A_WB, "WINDOW.NEWDRAWER"); ITEM("Open parent", A_WB, "WINDOW.OPENPARENT"); }
        SEP();
        sub = ITEM("Arrange icons", 0, NULL); sub->subs = &st[ns]; k = 0;
        { static const char *const t[] = { "Clean up", "By name", "By date", "By size", "By type" };
          static const char *const m[] = { "WINDOW.CLEANUPBY.COLUMN", "WINDOW.CLEANUPBY.NAME", "WINDOW.CLEANUPBY.DATE", "WINDOW.CLEANUPBY.SIZE", "WINDOW.CLEANUPBY.TYPE" };
          for (int j = 0; j < 5; j++) rc_item(st, &ns, k++, t[j], A_WB, m[j], 0); }
        sub->nsubs = (UWORD)k;
        sub = ITEM("Show", 0, NULL); sub->subs = &st[ns]; k = 0;
        rc_item(st, &ns, k++, "Only icons", A_WB, "WINDOW.SHOW.ONLYICONS", 0);
        rc_item(st, &ns, k++, "All files", A_WB, "WINDOW.SHOW.ALLFILES", 0);
        sub->nsubs = (UWORD)k;
        if (!root) {
            sub = ITEM("View by", 0, NULL); sub->subs = &st[ns]; k = 0;
            { static const char *const t[] = { "Icon", "Name", "Date", "Size", "Type" };
              static const char *const m[] = { "WINDOW.VIEWBY.ICON", "WINDOW.VIEWBY.NAME", "WINDOW.VIEWBY.DATE", "WINDOW.VIEWBY.SIZE", "WINDOW.VIEWBY.TYPE" };
              for (int j = 0; j < 5; j++) rc_item(st, &ns, k++, t[j], A_WB, m[j], 0); }
            sub->nsubs = (UWORD)k;
        }
        ITEM("Select contents", A_WB, "WINDOW.SELECTCONTENTS");
        if (root) { ITEM("Redraw all", A_WB, "WORKBENCH.REDRAWALL"); ITEM("Update all", A_WB, "WORKBENCH.UPDATEALL"); }
        else { ITEM("Update", A_WB, "WINDOW.UPDATE"); ITEM("Snapshot window", A_WB, "WINDOW.SNAPSHOT.WINDOW"); }
        if (root) {
            SEP();
            if (exists_file("SYS:Prefs/WBPattern")) ITEM("Backdrop picture...", A_RUN, "SYS:Prefs/WBPattern");
            if (exists_file("SYS:Prefs/Look")) ITEM("Look...", A_RUN, "SYS:Prefs/Look");
            if (exists_file("SYS:Prefs/ScreenMode")) ITEM("Screen mode...", A_RUN, "SYS:Prefs/ScreenMode");
            SEP();
            ITEM("Backdrop", A_WB, "WORKBENCH.BACKDROP");
            ITEM("About...", A_WB, "WORKBENCH.ABOUT");
        } else { SEP(); ITEM("Close", A_WB, "WINDOW.CLOSE"); }
    }
#undef ITEM
#undef SEP
    /* the widths: the widest text; each submenu beside its item */
    {
        WORD wmax = 120, smax = 100;
        for (int i = 0; i < n; i++) { WORD t = TextLength(&trp, (STRPTR)it[i].text[0].s, strlen(it[i].text[0].s)) + 34; if (t > wmax) wmax = t; }
        for (int i = 0; i < ns; i++) { WORD t = TextLength(&trp, (STRPTR)st[i].text[0].s, strlen(st[i].text[0].s)) + 24; if (t > smax) smax = t; }
        for (int i = 0; i < n; i++) it[i].width = wmax;
        for (int i = 0; i < ns; i++) st[i].width = smax;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < it[i].nsubs; j++) { it[i].subs[j].left = wmax - 6; it[i].subs[j].top = (WORD)(j * rc_lh); }
        rc_w = wmax;
    }
    cmenus[0].left = 0; cmenus[0].width = rc_w; cmenus[0].flags = MENUENABLED; cmenus[0].name[0] = 0;
    cmenus[0].items = it; cmenus[0].nitems = n;
    ncmenus = 1;
    npool = 200 + ns;
    return n;
}

static void rc_session(void)
{
    int chosen_act = -1;
    struct Window *w = rc_window;
    WORD mx = start_x, my = start_y, pad = prefs.border_double ? 4 : 2, h = 0;
    int hit, sticky = 0, done = 0, mi = -1, ii = -1, si = -1, any_moved = 0, use = prefs.use[OM_PU];
    ULONG s0, m0, s1, m1;
    if (!w || !is_window(w)) { session = 0; return; }
    scr = w->WScreen;
    if (!(dri = GetScreenDrawInfo(scr))) { session = 0; return; }
    font = OpenFont(scr->Font);
    if (!font) font = dri->dri_Font;
    target = w; target_strip = NULL;
    if (!rc_read(w)) goto out;
    hit = rc_hit(mx, my);
    if (hit >= 0 && (!rc_icons[hit].selected || !prefs.rightclick_selection)) {
        /* the icon clicked becomes the selection, as a click on it would make it */
        LONG nw = wbnum("GETATTR WINDOWS.COUNT%.0d%.0s", "", 0);
        for (int i = 0; i < nw; i++) {
            char name[256];
            if (!wbx(name, sizeof name, "GETATTR WINDOWS.%ld", (LONG)i)) wbx(NULL, 0, "MENU WINDOW \"%s\" INVOKE WINDOW.CLEARSELECTION", name);
        }
        wbx(NULL, 0, "ICON WINDOW \"%s\" NAMES \"%s\" SELECT", rc_win, rc_icons[hit].name);
        for (int i = 0; i < rc_nicons; i++) rc_icons[i].selected = i == hit;
    }
    if (!rc_build(hit)) goto out;
    choose_pens();
    for (int i = 0; i < cmenus[0].nitems; i++) if (cmenus[0].items[i].top + cmenus[0].items[i].height > h) h = cmenus[0].items[i].top + cmenus[0].items[i].height;
    npanels = 0;
    open_panel(2, 0, -1, mx - 4, my - 4, rc_w + 2 * pad, h + 2 * pad, pad, pad);
    if (!npanels) goto out;
    follow(mx, my);
    CurrentTime(&s0, &m0);
    while (!done) {
        ULONG got = Wait(sig_event | (1UL << tport->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (CheckIO((struct IORequest *)tr)) {
            WaitIO((struct IORequest *)tr);
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 20000;
            SendIO((struct IORequest *)tr);
        }
        if (cancelled || (got & SIGBREAKF_CTRL_C)) { mi = -1; break; }
        if (scr->MouseX != mx || scr->MouseY != my) {
            if (abs(scr->MouseX - start_x) > 3 || abs(scr->MouseY - start_y) > 3) any_moved = 1;
            mx = scr->MouseX; my = scr->MouseY;
            follow(mx, my);
        }
        if (!sticky && !rbutton) {
            ULONG ms;
            CurrentTime(&s1, &m1);
            ms = (s1 - s0) * 1000 + (m1 / 1000) - (m0 / 1000);
            /* a quick click without moving leaves the menu open, as a pop-up's does */
            if (ms < 400 && (!any_moved || use == OM_USE_STICKY)) { sticky = 1; rdown_seen = ldown_seen = 0; continue; }
            if (!chosen(&mi, &ii, &si)) mi = -1;
            done = 1;
        } else if (sticky && (rdown_seen || ldown_seen) && !rbutton && !lbutton) {
            if (hit_panel(mx, my) < 0 || !chosen(&mi, &ii, &si)) mi = -1;
            done = 1;
        }
    }
    close_panel(0);
    if (mi == 0 && ii >= 0) {
        const struct citem *c = si >= 0 ? &cmenus[0].items[ii].subs[si] : &cmenus[0].items[ii];
        chosen_act = (int)c->exclude;
    }
out:
    close_panel(0);
    free_pens();
    if (font && font != dri->dri_Font) CloseFont(font);
    font = NULL;
    FreeScreenDrawInfo(scr, dri);
    dri = NULL;
    cancelled = 0;
    session = 0;                             /* the mouse is the user's again, then the action */
    if (chosen_act >= 0) do_action(chosen_act);
}

/* ---- the commodity ----------------------------------------------------------------------------- */

static struct NewBroker nb = {
    NB_VERSION, (STRPTR)"OpenMenus", (STRPTR)"OpenMenus 0.2", (STRPTR)"The Open family's own menus",
    NBU_UNIQUE | NBU_NOTIFY, 0, 0, NULL, 0
};

int main(void)
{
    struct MsgPort *port = NULL;
    CxObj *broker = NULL, *cust;
    LONG s1 = -1, s2 = -1;
    int quit = 0, timer = 0;
    (void)version;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    CxBase = OpenLibrary((STRPTR)"commodities.library", 39);
    if (!IntuitionBase || !GfxBase || !LayersBase || !CxBase) goto out;
    me = FindTask(NULL);
    if ((s1 = AllocSignal(-1)) < 0 || (s2 = AllocSignal(-1)) < 0) goto out;
    sig_start = 1UL << s1; sig_event = 1UL << s2;
    if (!(port = CreateMsgPort()) || !(reply_port = CreateMsgPort())) goto out;
    nb.nb_Port = port;
    if (!(broker = CxBroker(&nb, NULL))) goto out;
    port->mp_Node.ln_Name = (char *)"OpenMenus";      /* the Menus editor finds this task by it */
    AddPort(port);
    read_settings();
    if (!(cust = CxCustom(custom, 0))) goto out;
    AttachCxObj(broker, cust);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0);
    if (!timer) goto out;
    TimerBase = tr->tr_node.io_Device;
    tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = 20000;
    SendIO((struct IORequest *)tr);
    ActivateCxObj(broker, 1);
    while (!quit) {
        ULONG got = Wait((1UL << port->mp_SigBit) | sig_start | sig_event | (1UL << reply_port->mp_SigBit) | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
        CxMsg *m;
        if (got & SIGBREAKF_CTRL_C) quit = 1;
        if (got & SIGBREAKF_CTRL_F) read_settings();
        reap();
        if ((got & sig_start) && session == 2) rc_session();
        else if ((got & sig_start) && session) menu_session();
        while ((m = (CxMsg *)GetMsg(port))) {
            ULONG type = CxMsgType(m), id = CxMsgID(m);
            ReplyMsg((struct Message *)m);
            if (type == CXM_COMMAND) {
                switch (id) {
                case CXCMD_DISABLE: ActivateCxObj(broker, 0); on = 0; break;
                case CXCMD_ENABLE: ActivateCxObj(broker, 1); on = 1; break;
                case CXCMD_KILL: quit = 1; break;
                case CXCMD_UNIQUE: read_settings(); break;
                }
            }
        }
    }
out:
    if (broker) DeleteCxObjAll(broker);
    if (timer) { AbortIO((struct IORequest *)tr); WaitIO((struct IORequest *)tr); CloseDevice((struct IORequest *)tr); }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (reply_port) {
        /* the programs' replies to what was sent them: wait for them, briefly */
        for (int n = 0; n < 100 && out_msgs > 0; n++) { reap(); Delay(2); }
        if (!out_msgs) DeleteMsgPort(reply_port);       /* else it stays: a late reply must land somewhere */
    }
    if (port) {
        struct Message *mm;
        if (port->mp_Node.ln_Name) RemPort(port);
        while ((mm = GetMsg(port))) ReplyMsg(mm);
        DeleteMsgPort(port);
    }
    if (s2 >= 0) FreeSignal(s2);
    if (s1 >= 0) FreeSignal(s1);
    if (theme_loaded) ogt_theme_free(&theme);
    if (RexxSysBase) CloseLibrary((struct Library *)RexxSysBase);
    if (AslBase) CloseLibrary(AslBase);
    if (CxBase) CloseLibrary(CxBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
