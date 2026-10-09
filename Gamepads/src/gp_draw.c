/* gp_draw: the Test view's controller (gp_draw.h).
 *
 * The look follows the Open family's (OpenLook's flat themes): a light
 * pad (SHINEPEN) with a dark outline (SHADOWPEN) on the panel
 * (BACKGROUNDPEN); controls at rest in the panel's pen with an outline;
 * a pressed control in the one accent colour, with its name in the
 * accent's text colour. The accent is the OpenLook theme's (or the user's
 * own accent from ENV:OpenGadTools/Look) where the screen has 64 colours
 * or more, and FILLPEN and FILLTEXTPEN otherwise (Classic, 4 to 32
 * colours). A CD32 pad's buttons keep their colours, dim at rest and
 * bright when pressed, when the screen has the colours for them.
 *
 * Everything is RectFill, Move/Draw, DrawEllipse and Text, so it goes
 * through OpenGfx where it runs, and needs no TmpRas or area buffers.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdlib.h>

#include <libraries/openinput.h>
#include "gp_draw.h"

#define NO_PEN (-1)

struct gpv_view {
    struct Screen *scr;
    struct ColorMap *cm;
    WORD panel, body, line, text, rest;     /* the screen's own pens */
    WORD accent, accent_text;               /* the accent: obtained, or FILLPEN/FILLTEXTPEN */
    WORD got_accent, got_accent_text;       /* obtained (to release) */
    WORD cd32_dim[4], cd32_lit[4];          /* red, blue, green, yellow (A, B, X, Y); NO_PEN: none */
    /* what was drawn last, so a change redraws only its part */
    int x, y, w, h, kind, family;
    ULONG buttons;
    WORD axes[6];
    int connected;
    gp_padgeo g;
    int valid;
};

/* ---- the accent, from the OpenLook theme --------------------------------------------- */

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static LONG parse_rgb(const char *s)        /* "#rrggbb" -> 0xRRGGBB, or -1 */
{
    LONG v = 0;
    int i;
    while (*s == ' ' || *s == '\t') s++;
    if (*s++ != '#') return -1;
    for (i = 0; i < 6; i++) { int h = hexv(s[i]); if (h < 0) return -1; v = v << 4 | h; }
    return v;
}

static char *read_small(const char *path, LONG max)
{
    BPTR fh;
    char *buf;
    LONG n;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR win = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    fh = Open((STRPTR)path, MODE_OLDFILE);
    me->pr_WindowPtr = win;
    if (!fh) return NULL;
    if ((buf = AllocVec(max + 1, MEMF_ANY))) { n = Read(fh, buf, max); buf[n < 0 ? 0 : n] = 0; }
    Close(fh);
    return buf;
}

/* key's value in the [section] of a theme (or anywhere, section NULL) */
static LONG theme_rgb(const char *t, const char *section, const char *key)
{
    const char *l = t;
    int in = section == NULL;
    size_t kl = strlen(key);
    while (*l) {
        if (*l == '[') in = section && !strncmp(l + 1, section, strlen(section)) && l[1 + strlen(section)] == ']';
        else if (in && !strncmp(l, key, kl) && (l[kl] == ' ' || l[kl] == '\t')) return parse_rgb(l + kl);
        while (*l && *l != '\n') l++;
        if (*l) l++;
    }
    return -1;
}

/* The accent and its text colour from ENV:OpenGadTools/Look and the theme; FALSE for Classic or none. */
static BOOL look_accent(LONG *accent, LONG *text)
{
    char *look = read_small("ENV:OpenGadTools/Look", 2048), *t = NULL, name[48], mode[16], path[96];
    const char *section;
    int i = 0, j = 0;
    LONG own = -1;
    *accent = *text = -1;
    if (!look) return FALSE;
    while (look[i] && look[i] != ' ' && look[i] != '\n' && j < (int)sizeof name - 1) name[j++] = look[i++];
    name[j] = 0;
    while (look[i] == ' ') i++;
    for (j = 0; look[i] && look[i] != '\n' && look[i] != ' ' && j < (int)sizeof mode - 1; ) mode[j++] = look[i++];
    mode[j] = 0;
    {   const char *l = look;                                     /* the user's own accent */
        while (*l) { if (!strncmp(l, "accent ", 7)) own = parse_rgb(l + 7); while (*l && *l != '\n') l++; if (*l) l++; }
    }
    FreeVec(look);
    if (!name[0] || !strcmp(name, "Classic")) return FALSE;
    if (!strcmp(mode, "dark")) section = "dark";
    else if (!strcmp(mode, "auto")) {                             /* by the clock, as OpenLook does: dark from 7 pm to 7 am */
        struct DateStamp ds;
        DateStamp(&ds);
        section = ds.ds_Minute >= 19 * 60 || ds.ds_Minute < 7 * 60 ? "dark" : "light";
    } else section = "light";
    strcpy(path, "SYS:Prefs/Presets/Themes/");
    strncat(path, name, sizeof path - strlen(path) - 8);
    strcat(path, ".theme");
    if ((t = read_small(path, 16384))) {
        *accent = theme_rgb(t, section, "accent");
        if (*accent < 0) *accent = theme_rgb(t, NULL, "accent");
        *text = theme_rgb(t, section, "accent.text");
        if (*text < 0) *text = theme_rgb(t, NULL, "accent.text");
        FreeVec(t);
    }
    if (own >= 0) { *accent = own; *text = -1; }
    if (*text < 0 && *accent >= 0) {                              /* white or black, whichever reads */
        LONG r = *accent >> 16 & 255, g = *accent >> 8 & 255, b = *accent & 255;
        *text = (r * 3 + g * 6 + b) / 10 > 140 ? 0x000000 : 0xffffff;
    }
    return *accent >= 0;
}

static WORD obtain(struct ColorMap *cm, LONG rgb)
{
    ULONG r = (ULONG)(rgb >> 16 & 255), g = (ULONG)(rgb >> 8 & 255), b = (ULONG)(rgb & 255);
    LONG p = ObtainBestPen(cm, r * 0x01010101UL, g * 0x01010101UL, b * 0x01010101UL, OBP_Precision, PRECISION_GUI, TAG_DONE);
    return (WORD)p;
}

struct gpv_view *gpv_open(struct Screen *scr, struct DrawInfo *dri)
{
    struct gpv_view *v = AllocVec(sizeof *v, MEMF_ANY | MEMF_CLEAR);
    int depth = dri->dri_Depth, i;
    LONG accent, text;
    static const LONG lit[4] = { 0xe2312b, 0x2b62d6, 0x2fa84f, 0xf2c318 };   /* CD32: red, blue, green, yellow */
    static const LONG dim[4] = { 0xb77977, 0x778ab3, 0x78a384, 0xbdac70 };   /* the same, two thirds grey: at rest */
    if (!v) return NULL;
    v->scr = scr;
    v->cm = scr->ViewPort.ColorMap;
    v->panel = dri->dri_Pens[BACKGROUNDPEN];
    v->body = dri->dri_Pens[SHINEPEN];
    v->line = dri->dri_Pens[SHADOWPEN];
    v->text = dri->dri_Pens[TEXTPEN];
    v->rest = dri->dri_Pens[BACKGROUNDPEN];
    v->accent = dri->dri_Pens[FILLPEN];
    v->accent_text = dri->dri_Pens[FILLTEXTPEN];
    v->got_accent = v->got_accent_text = NO_PEN;
    for (i = 0; i < 4; i++) v->cd32_dim[i] = v->cd32_lit[i] = NO_PEN;
    if (depth >= 6 && v->cm) {                                    /* 64 colours or more: the theme's accent and the CD32 colours */
        if (look_accent(&accent, &text)) {
            WORD a = obtain(v->cm, accent), t = obtain(v->cm, text);
            if (a >= 0 && t >= 0) { v->accent = v->got_accent = a; v->accent_text = v->got_accent_text = t; }
            else { if (a >= 0) ReleasePen(v->cm, a); if (t >= 0) ReleasePen(v->cm, t); }
        }
        for (i = 0; i < 4; i++) { v->cd32_lit[i] = obtain(v->cm, lit[i]); v->cd32_dim[i] = obtain(v->cm, dim[i]); }
    }
    return v;
}

void gpv_close(struct gpv_view *v)
{
    int i;
    if (!v) return;
    if (v->cm) {
        if (v->got_accent >= 0) ReleasePen(v->cm, v->got_accent);
        if (v->got_accent_text >= 0) ReleasePen(v->cm, v->got_accent_text);
        for (i = 0; i < 4; i++) {
            if (v->cd32_lit[i] >= 0) ReleasePen(v->cm, v->cd32_lit[i]);
            if (v->cd32_dim[i] >= 0) ReleasePen(v->cm, v->cd32_dim[i]);
        }
    }
    FreeVec(v);
}

/* ---- shapes ----------------------------------------------------------------------------- */

static void fill_ellipse(struct RastPort *rp, int cx, int cy, int rx, int ry)
{
    int dy;
    for (dy = -ry; dy <= ry; dy++) {
        int hw = gp_half_width(rx, ry, dy);
        if (hw >= 0) RectFill(rp, cx - hw, cy + dy, cx + hw, cy + dy);
    }
}

static void ring(struct RastPort *rp, int cx, int cy, int r, int thick)
{
    int i;
    for (i = 0; i < thick && r - i > 0; i++) DrawEllipse(rp, cx, cy, r - i, r - i);
}

/* a rounded rectangle with round ends (a pill), filled in pen fill and outlined in pen line */
static int pill_inset(int r, int h, int dy)
{
    int d = dy < r ? r - dy : dy > h - 1 - r ? dy - (h - 1 - r) : 0;
    return d ? r - gp_half_width(r, r, d) : 0;
}

static void pill(struct RastPort *rp, int x, int y, int w, int h, WORD fill, WORD line)
{
    int r = h / 2, dy;
    if (w < h) w = h;
    SetAPen(rp, fill);
    for (dy = 0; dy < h; dy++) { int in = pill_inset(r, h, dy); RectFill(rp, x + in, y + dy, x + w - 1 - in, y + dy); }
    SetAPen(rp, line);
    for (dy = 0; dy < h; dy++) {
        int in = pill_inset(r, h, dy);
        int up = dy > 0 ? pill_inset(r, h, dy - 1) : w, down = dy < h - 1 ? pill_inset(r, h, dy + 1) : w;
        int far = up > down ? up : down, hi;
        if (dy == 0 || dy == h - 1) { RectFill(rp, x + in, y + dy, x + w - 1 - in, y + dy); continue; }
        hi = far - 1 > in ? far - 1 : in;
        RectFill(rp, x + in, y + dy, x + hi, y + dy);
        RectFill(rp, x + w - 1 - hi, y + dy, x + w - 1 - in, y + dy);
    }
}

/* text centred on cx,cy, in pen; shortened from the end until it fits maxw (nothing if not a letter fits) */
static void text_centred(struct RastPort *rp, int cx, int cy, const char *s, int maxw, WORD pen)
{
    int n = s ? strlen(s) : 0, tw;
    if (!n) return;
    while (n > 0 && (tw = TextLength(rp, (STRPTR)s, n)) > maxw) n--;
    if (!n) return;
    tw = TextLength(rp, (STRPTR)s, n);
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    Move(rp, cx - tw / 2, cy - rp->TxHeight / 2 + rp->TxBaseline);
    Text(rp, (STRPTR)s, n);
}

/* ---- the parts ----------------------------------------------------------------------------- */

static void draw_body(struct gpv_view *v, struct RastPort *rp, int ox, int oy)
{
    const gp_padgeo *g = &v->g;
    int y, top = g->body.y, bottom = g->grip_cy + g->grip_ry, i, j;
    int prev[3][2], cur[3][2], next[3][2], np, nc, nn;
    SetAPen(rp, v->body);
    for (y = top; y <= bottom; y++) {
        nc = gp_pad_spans(g, y, cur);
        for (i = 0; i < nc; i++) RectFill(rp, ox + cur[i][0], oy + y, ox + cur[i][1], oy + y);
    }
    /* the outline: each span's ends, and the stretches of it the row above or below doesn't cover */
    SetAPen(rp, v->line);
    np = gp_pad_spans(g, top - 1, prev);
    nc = gp_pad_spans(g, top, cur);
    for (y = top; y <= bottom; y++) {
        nn = gp_pad_spans(g, y + 1, next);
        for (i = 0; i < nc; i++) {
            int x;
            WritePixel(rp, ox + cur[i][0], oy + y);
            WritePixel(rp, ox + cur[i][1], oy + y);
            for (x = cur[i][0]; x <= cur[i][1]; x++) {
                int up = 0, down = 0, run;
                for (j = 0; j < np; j++) if (x >= prev[j][0] && x <= prev[j][1]) up = 1;
                for (j = 0; j < nn; j++) if (x >= next[j][0] && x <= next[j][1]) down = 1;
                if (up && down) continue;
                run = x;                                          /* a run of edge pixels, as one RectFill */
                while (run + 1 <= cur[i][1]) {
                    int u = 0, d = 0;
                    for (j = 0; j < np; j++) if (run + 1 >= prev[j][0] && run + 1 <= prev[j][1]) u = 1;
                    for (j = 0; j < nn; j++) if (run + 1 >= next[j][0] && run + 1 <= next[j][1]) d = 1;
                    if (u && d) break;
                    run++;
                }
                RectFill(rp, ox + x, oy + y, ox + run, oy + y);
                x = run;
            }
        }
        memcpy(prev, cur, sizeof cur); np = nc;
        memcpy(cur, next, sizeof next); nc = nn;
    }
}

static void draw_dpad(struct gpv_view *v, struct RastPort *rp, int ox, int oy, ULONG b)
{
    const gp_rect *d = &v->g.dpad;
    int a = v->g.dpad_arm, x0 = ox + d->x, y0 = oy + d->y, x1 = x0 + d->w - 1, y1 = y0 + d->h - 1;
    int cx = x0 + d->w / 2, cy = y0 + d->h / 2, h = a / 2;
    int ax0 = cx - h, ax1 = cx + h, ay0 = cy - h, ay1 = cy + h;
    /* the cross: the centre and four arms, each lit when pressed */
    SetAPen(rp, v->rest); RectFill(rp, ax0, ay0, ax1, ay1);
    SetAPen(rp, b & OIBF(OIB_DPAD_UP) ? v->accent : v->rest);    RectFill(rp, ax0, y0, ax1, ay0 - 1);
    SetAPen(rp, b & OIBF(OIB_DPAD_DOWN) ? v->accent : v->rest);  RectFill(rp, ax0, ay1 + 1, ax1, y1);
    SetAPen(rp, b & OIBF(OIB_DPAD_LEFT) ? v->accent : v->rest);  RectFill(rp, x0, ay0, ax0 - 1, ay1);
    SetAPen(rp, b & OIBF(OIB_DPAD_RIGHT) ? v->accent : v->rest); RectFill(rp, ax1 + 1, ay0, x1, ay1);
    /* its outline, twelve corners */
    SetAPen(rp, v->line);
    Move(rp, ax0, y0); Draw(rp, ax1, y0); Draw(rp, ax1, ay0); Draw(rp, x1, ay0); Draw(rp, x1, ay1); Draw(rp, ax1, ay1);
    Draw(rp, ax1, y1); Draw(rp, ax0, y1); Draw(rp, ax0, ay1); Draw(rp, x0, ay1); Draw(rp, x0, ay0); Draw(rp, ax0, ay0); Draw(rp, ax0, y0);
    /* a small arrow in each arm, in the line pen (the text pen on the accent) */
    {
        int t = a / 4 > 1 ? a / 4 : 1, k, m = (y0 + ay0) / 2;
        for (k = 0; k < 4; k++) {
            ULONG bit = k == 0 ? OIBF(OIB_DPAD_UP) : k == 1 ? OIBF(OIB_DPAD_DOWN) : k == 2 ? OIBF(OIB_DPAD_LEFT) : OIBF(OIB_DPAD_RIGHT);
            int i;
            SetAPen(rp, b & bit ? v->accent_text : v->line);
            for (i = 0; i < t; i++) {
                if (k == 0) { m = (y0 + ay0) / 2; RectFill(rp, cx - i, m - t / 2 + i, cx + i, m - t / 2 + i); }
                if (k == 1) { m = (ay1 + y1) / 2; RectFill(rp, cx - i, m + t / 2 - i, cx + i, m + t / 2 - i); }
                if (k == 2) { m = (x0 + ax0) / 2; RectFill(rp, m - t / 2 + i, cy - i, m - t / 2 + i, cy + i); }
                if (k == 3) { m = (ax1 + x1) / 2; RectFill(rp, m + t / 2 - i, cy - i, m + t / 2 - i, cy + i); }
            }
        }
    }
}

/* a PlayStation face button's shape, in pen */
static void ps_shape(struct RastPort *rp, int which, int cx, int cy, int r, WORD pen)
{
    int s = r / 2 > 2 ? r / 2 : 2;
    SetAPen(rp, pen);
    switch (which) {
    case 0: Move(rp, cx - s, cy - s); Draw(rp, cx + s, cy + s); Move(rp, cx + s, cy - s); Draw(rp, cx - s, cy + s); break;   /* cross */
    case 1: DrawEllipse(rp, cx, cy, s, s); break;                                                                          /* circle */
    case 2: Move(rp, cx - s, cy - s); Draw(rp, cx + s, cy - s); Draw(rp, cx + s, cy + s); Draw(rp, cx - s, cy + s); Draw(rp, cx - s, cy - s); break;  /* square */
    case 3: Move(rp, cx, cy - s); Draw(rp, cx + s, cy + s * 3 / 4); Draw(rp, cx - s, cy + s * 3 / 4); Draw(rp, cx, cy - s); break;   /* triangle */
    }
}

static void draw_face(struct gpv_view *v, struct RastPort *rp, int ox, int oy, int i, const gpv_state *s)
{
    const gp_circle *c = &v->g.face[i];
    int on = (s->buttons & OIBF(OIB_A + i)) != 0, cx = ox + c->cx, cy = oy + c->cy;
    int cd32 = s->kind == GP_PAD_CD32 && v->cd32_lit[i] >= 0 && v->cd32_dim[i] >= 0;
    WORD fill = cd32 ? (on ? v->cd32_lit[i] : v->cd32_dim[i]) : on ? v->accent : v->rest;
    if (c->r <= 0) return;
    SetAPen(rp, v->body); fill_ellipse(rp, cx, cy, c->r + 1, c->r + 1);   /* clear its place (a thick ring last time) */
    SetAPen(rp, fill); fill_ellipse(rp, cx, cy, c->r, c->r);
    SetAPen(rp, v->line); ring(rp, cx, cy, c->r, cd32 && on ? 2 : 1);
    if (cd32) return;                                                     /* a CD32 pad's buttons are their colours */
    if (s->family == OIFAM_PLAYSTATION && s->kind == GP_PAD_MODERN) { ps_shape(rp, i, cx, cy, c->r, on ? v->accent_text : v->text); return; }
    {   /* the pad's own name: a letter or two fit; a longer name gives its first letter */
        const char *name = s->face[i] ? s->face[i] : "";
        char one[2];
        if (TextLength(rp, (STRPTR)name, strlen(name)) > 2 * c->r - 2) { one[0] = name[0]; one[1] = 0; name = one; }
        text_centred(rp, cx, cy, name, 2 * c->r, on ? v->accent_text : v->text);
    }
}

static void draw_stick(struct gpv_view *v, struct RastPort *rp, int ox, int oy, int i, const gpv_state *s)
{
    const gp_circle *c = &v->g.stick[i];
    int cx = ox + c->cx, cy = oy + c->cy, r = c->r, dot = r / 4 > 2 ? r / 4 : 2, travel = r - dot - 2;
    int click = (s->buttons & OIBF(i ? OIB_RIGHTSTICK : OIB_LEFTSTICK)) != 0;
    LONG ax = s->axes[i ? OIAXIS_RIGHTX : OIAXIS_LEFTX], ay = s->axes[i ? OIAXIS_RIGHTY : OIAXIS_LEFTY];
    int px = cx + (int)(ax * travel / 32768), py = cy + (int)(ay * travel / 32768);
    int dz = s->deadzone > 0 ? (int)((LONG)travel * s->deadzone / 32767) : 0;
    int moved = (ax < 0 ? -ax : ax) > s->deadzone || (ay < 0 ? -ay : ay) > s->deadzone;
    if (r <= 0) return;
    SetAPen(rp, v->body); fill_ellipse(rp, cx, cy, r + 1, r + 1);
    SetAPen(rp, v->rest); fill_ellipse(rp, cx, cy, r, r);                /* the well */
    if (dz > 0) {                                                         /* the dead zone: where the thumb's centre still reads as centred */
        int zr = dz + dot + 1;
        SetAPen(rp, v->body); fill_ellipse(rp, cx, cy, zr, zr);
        SetAPen(rp, v->line); DrawEllipse(rp, cx, cy, zr, zr);
    }
    SetAPen(rp, click ? v->accent : v->line); ring(rp, cx, cy, r, click ? 3 : 1);   /* lit when the stick is pressed down */
    SetAPen(rp, moved ? v->accent : v->text); fill_ellipse(rp, px, py, dot, dot);   /* the thumb */
    SetAPen(rp, v->line); DrawEllipse(rp, px, py, dot, dot);
}

static void draw_trigger(struct gpv_view *v, struct RastPort *rp, int ox, int oy, int i, const gpv_state *s)
{
    const gp_rect *t = &v->g.trigger[i];
    int x = ox + t->x, y = oy + t->y, value = s->axes[i ? OIAXIS_TRIGGERRIGHT : OIAXIS_TRIGGERLEFT], fill;
    char txt[16];
    int pct;
    if (t->w <= 0) return;
    if (value < 0) value = 0;
    pct = (int)((LONG)value * 100 / 32767);
    fill = (int)((LONG)(t->w - 2) * value / 32767);
    SetAPen(rp, v->rest); RectFill(rp, x + 1, y + 1, x + t->w - 2, y + t->h - 2);
    if (fill > 0) { SetAPen(rp, v->accent); RectFill(rp, i ? x + t->w - 1 - fill : x + 1, y + 1, i ? x + t->w - 2 : x + fill, y + t->h - 2); }
    SetAPen(rp, v->line);
    Move(rp, x, y); Draw(rp, x + t->w - 1, y); Draw(rp, x + t->w - 1, y + t->h - 1); Draw(rp, x, y + t->h - 1); Draw(rp, x, y);
    /* the name and the amount, e.g. "LT 72%", in the pen that reads on what is under its middle */
    {
        const char *n = s->trigger[i] ? s->trigger[i] : (i ? "RT" : "LT");
        int k = 0, p = pct, digits[3], nd = 0;
        while (*n && k < 6) txt[k++] = *n++;
        txt[k++] = ' ';
        do { digits[nd++] = p % 10; p /= 10; } while (p && nd < 3);
        while (nd) txt[k++] = (char)('0' + digits[--nd]);
        txt[k++] = '%'; txt[k] = 0;
        if (TextLength(rp, (STRPTR)txt, k) > t->w - 4) { txt[k - 1] = 0; k = 0; while (txt[k] && txt[k] != ' ') k++; txt[k] = 0; }
        text_centred(rp, x + t->w / 2, y + t->h / 2, txt, t->w - 4, fill > t->w / 2 ? v->accent_text : v->text);
    }
}

static void draw_button_pill(struct gpv_view *v, struct RastPort *rp, int x, int y, int w, int h, int on, const char *name, WORD under)
{
    SetAPen(rp, under); RectFill(rp, x, y, x + w - 1, y + h - 1);
    pill(rp, x, y, w, h, on ? v->accent : v->rest, v->line);
    if (name && TextLength(rp, (STRPTR)name, strlen(name)) <= w - h / 2 - 2 && rp->TxHeight <= h)
        text_centred(rp, x + w / 2, y + h / 2, name, w - 2, on ? v->accent_text : v->text);
}

static void draw_shoulder(struct gpv_view *v, struct RastPort *rp, int ox, int oy, int i, const gpv_state *s)
{
    const gp_rect *r = &v->g.shoulder[i];
    const char *n = s->shoulder[i];
    if (r->w <= 0) return;
    if (n && TextLength(rp, (STRPTR)n, strlen(n)) > r->w - r->h / 2 - 2) n = i ? "RB" : "LB";
    draw_button_pill(v, rp, ox + r->x, oy + r->y, r->w, r->h, (s->buttons & OIBF(i ? OIB_RIGHTSHOULDER : OIB_LEFTSHOULDER)) != 0, n, v->panel);
}

static void draw_middle(struct gpv_view *v, struct RastPort *rp, int ox, int oy, const gpv_state *s)
{
    const gp_padgeo *g = &v->g;
    if (s->kind == GP_PAD_MODERN) {
        int on = (s->buttons & OIBF(OIB_GUIDE)) != 0, i, both = 1;
        for (i = 0; i < 2; i++)                                   /* both names, or neither: the pair stays symmetric */
            if (!s->pill[i] || TextLength(rp, (STRPTR)s->pill[i], strlen(s->pill[i])) > g->pill[i].w - g->pill[i].h / 2 - 2 || rp->TxHeight > g->pill[i].h)
                both = 0;
        draw_button_pill(v, rp, ox + g->pill[0].x, oy + g->pill[0].y, g->pill[0].w, g->pill[0].h, (s->buttons & OIBF(OIB_BACK)) != 0, both ? s->pill[0] : NULL, v->body);
        draw_button_pill(v, rp, ox + g->pill[1].x, oy + g->pill[1].y, g->pill[1].w, g->pill[1].h, (s->buttons & OIBF(OIB_START)) != 0, both ? s->pill[1] : NULL, v->body);
        if (g->guide.r > 0) {
            SetAPen(rp, on ? v->accent : v->rest); fill_ellipse(rp, ox + g->guide.cx, oy + g->guide.cy, g->guide.r, g->guide.r);
            SetAPen(rp, v->line); DrawEllipse(rp, ox + g->guide.cx, oy + g->guide.cy, g->guide.r, g->guide.r);
        }
    } else if (s->kind == GP_PAD_CD32 && g->pill[0].w)
        draw_button_pill(v, rp, ox + g->pill[0].x, oy + g->pill[0].y, g->pill[0].w, g->pill[0].h, (s->buttons & OIBF(OIB_START)) != 0, s->pill[0], v->body);
}

/* ---- the whole view ---------------------------------------------------------------------- */

void gpv_draw(struct gpv_view *v, struct RastPort *rp, int x, int y, int w, int h, const gpv_state *s, int force)
{
    ULONG b = s->connected ? s->buttons : 0, changed;
    int i, fh = rp->TxHeight;
    gpv_state now = *s;
    if (!v || w <= 0 || h <= 0) return;
    now.buttons = b;
    if (!s->connected) for (i = 0; i < 6; i++) now.axes[i] = 0;
    if (!v->valid || v->x != x || v->y != y || v->w != w || v->h != h || v->kind != s->kind || v->family != s->family
        || v->connected != s->connected) force = 1;
    if (force) {
        gp_pad_layout(s->kind, w, h, fh, &v->g);
        SetAPen(rp, v->panel); RectFill(rp, x, y, x + w - 1, y + h - 1);
        draw_body(v, rp, x, y);
        v->x = x; v->y = y; v->w = w; v->h = h; v->kind = s->kind; v->family = s->family; v->connected = s->connected;
        v->valid = 1;
    }
    changed = force ? ~0UL : b ^ v->buttons;
    if (changed & (OIBF(OIB_DPAD_UP) | OIBF(OIB_DPAD_DOWN) | OIBF(OIB_DPAD_LEFT) | OIBF(OIB_DPAD_RIGHT))) draw_dpad(v, rp, x, y, b);
    for (i = 0; i < 4; i++) if (changed & OIBF(OIB_A + i)) draw_face(v, rp, x, y, i, &now);
    for (i = 0; i < 2; i++) {
        if (changed & OIBF(i ? OIB_RIGHTSHOULDER : OIB_LEFTSHOULDER)) draw_shoulder(v, rp, x, y, i, &now);
        if (force || (changed & OIBF(i ? OIB_RIGHTSTICK : OIB_LEFTSTICK)) || now.axes[i * 2] != v->axes[i * 2] || now.axes[i * 2 + 1] != v->axes[i * 2 + 1])
            draw_stick(v, rp, x, y, i, &now);
        if (force || now.axes[OIAXIS_TRIGGERLEFT + i] != v->axes[OIAXIS_TRIGGERLEFT + i]) draw_trigger(v, rp, x, y, i, &now);
    }
    if (changed & (OIBF(OIB_BACK) | OIBF(OIB_GUIDE) | OIBF(OIB_START))) draw_middle(v, rp, x, y, &now);
    v->buttons = b;
    for (i = 0; i < 6; i++) v->axes[i] = now.axes[i];
}
