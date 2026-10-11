/* barpaint.h: the screen bar's own colours behind a bar window (OpenTitle's
 * icons and clock, OpenSpeaker's speaker). The bar is a gradient the theme
 * draws straight into the screen (OpenLook), so a bar window filled with the
 * BARBLOCKPEN looks like a white box on it. These read the bar's colour, a
 * row at a time, from a strip in the middle of the bar (the commonest colour
 * of each row, so title text in the strip doesn't count) and paint it.
 *
 * Needs cybergraphics.library 41 (CyberGfxBase, opened by the program) and
 * draws the pen when there is none, or the bar isn't the screen's title bar.
 * A program includes this once.
 *
 * bp_paint_kept() and bp_text_pen_kept() (OpenSpeaker 0.1.3; the Team,
 * 11 October 2026: the speaker was seen in a white box on the black bar) never
 * fall back to the pen's white on the title bar. When a reading fails (the
 * bar covered as the program starts, or not read yet) they use the colours
 * last read well, or the colour the caller gives (the theme's screenbar)
 * when none ever was, and they can draw a shade of the bar for a pressed
 * look. bp_paint() and bp_text_pen() are as they were.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef BARPAINT_H
#define BARPAINT_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/layers.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <string.h>

#define BP_ROWS 48
#define BP_STRIP 64
#define BP_UNUSED __attribute__((unused))

static ULONG bp_rgb[BP_ROWS];                  /* the bar's colour in each row, 0xAARRGGBB */
static int bp_n;                               /* rows known; 0 = use the bar's pen */
static ULONG bp_last[BP_ROWS];                 /* the last reading that worked, kept when a later one fails */
static int bp_last_n;

/* 1 when no window but ours covers the bar between x0 and x1 */
static BP_UNUSED int bp_clear(struct Screen *scr, WORD x0, WORD x1, int (*ours)(struct Window *))
{
    struct Layer *l;
    int ok = 1;
    if (!scr->BarLayer) return 0;
    LockLayerInfo(&scr->LayerInfo);
    for (l = scr->BarLayer->front; l && ok; l = l->front) {
        struct Window *o = (struct Window *)l->Window;
        /* our own, and the other bar windows (borderless, in the bar's rows): they are the bar */
        if (o && (ours(o) || ((o->Flags & WFLG_BORDERLESS) && o->TopEdge <= 0 && o->Height <= scr->BarHeight + 1))) continue;
        if (l->bounds.MinX <= x1 && l->bounds.MaxX >= x0 && l->bounds.MinY <= scr->BarHeight) ok = 0;
    }
    UnlockLayerInfo(&scr->LayerInfo);
    return ok;
}

/* Reads the bar's colours again. title: the bar is the screen's title bar. 1 when something changed. */
static BP_UNUSED int bp_sample(struct Screen *scr, int title, int (*ours)(struct Window *))
{
    static ULONG buf[BP_ROWS * BP_STRIP];
    ULONG now[BP_ROWS];
    WORD x0, w = BP_STRIP, n = scr->BarHeight;
    int r, i, j, changed = 0;
    if (!CyberGfxBase || !title || n < 4 || n > BP_ROWS) {
        changed = bp_n != 0;
        bp_n = 0;
        return changed;
    }
    if (w > scr->Width) w = scr->Width;
    x0 = (scr->Width - w) / 2;
    if (!bp_clear(scr, x0, x0 + w - 1, ours)) return 0;      /* covered: keep what is known */
    ReadPixelArray(buf, 0, 0, BP_STRIP * 4, &scr->RastPort, x0, 0, w, n, RECTFMT_ARGB);
    for (r = 0; r < n; r++) {
        const ULONG *row = buf + r * BP_STRIP;
        int best = 0, bestn = 0;
        for (i = 0; i < w; i++) {
            int c = 0;
            for (j = 0; j < w; j++) if (row[j] == row[i]) c++;
            if (c > bestn) { bestn = c; best = i; }
        }
        now[r] = row[best];
    }
    if (bp_n != n || memcmp(now, bp_rgb, n * sizeof now[0])) changed = 1;
    memcpy(bp_rgb, now, n * sizeof now[0]);
    bp_n = n;
    memcpy(bp_last, now, n * sizeof now[0]);
    bp_last_n = n;
    return changed;
}

/* The bar's colour behind a box of a bar window (x, y, w, h in the window; the window is at the screen's top). */
static BP_UNUSED void bp_paint(struct Window *win, ULONG fallback_pen, WORD x, WORD y, WORD pw, WORD ph)
{
    static ULONG row[512];
    struct RastPort *rp = win->RPort;
    WORD i, j;
    if (bp_n && CyberGfxBase && pw > 0 && pw <= 512) {
        for (j = 0; j < ph; j++) {
            WORD sy = win->TopEdge + y + j;
            ULONG c = bp_rgb[sy < 0 ? 0 : sy >= bp_n ? bp_n - 1 : sy];
            for (i = 0; i < pw; i++) row[i] = c;
            WritePixelArray(row, 0, 0, pw * 4, rp, x, y + j, pw, 1, RECTFMT_ARGB);
        }
        return;
    }
    SetAPen(rp, fallback_pen);
    RectFill(rp, x, y, x + pw - 1, y + ph - 1);
}

/* The colour for text and glyphs on the bar: the bar's detail pen is black however dark the theme's bar is.
 * Light text on a dark bar, dark text on a light one, by the bar's own colour. Falls back to the pen. */
static LONG bp_pen = -1;
static ULONG bp_pen_rgb;
static struct Screen *bp_pen_scr;

static BP_UNUSED void bp_release(void)
{
    if (bp_pen >= 0 && bp_pen_scr) ReleasePen(bp_pen_scr->ViewPort.ColorMap, bp_pen);
    bp_pen = -1;
    bp_pen_scr = NULL;
}

static BP_UNUSED LONG bp_text_pen_for(struct Screen *scr, ULONG c, LONG fallback)
{
    ULONG want;
    int luma;
    luma = (int)((((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8);
    want = luma < 128 ? 0xe8ecf0UL : 0x1c242dUL;
    if (bp_pen < 0 || bp_pen_scr != scr || bp_pen_rgb != want) {
        bp_release();
        bp_pen = ObtainBestPen(scr->ViewPort.ColorMap, (want >> 16) * 0x01010101UL, ((want >> 8) & 255) * 0x01010101UL,
                               (want & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
        bp_pen_scr = scr;
        bp_pen_rgb = want;
    }
    return bp_pen >= 0 ? bp_pen : fallback;
}

static BP_UNUSED LONG bp_text_pen(struct Screen *scr, LONG fallback)
{
    if (!bp_n) return fallback;
    return bp_text_pen_for(scr, bp_rgb[bp_n / 2], fallback);
}

/* c a shade lighter on a dark bar, darker on a light one: shade 256ths of the way to white or black */
static BP_UNUSED ULONG bp_shade(ULONG c, int shade)
{
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, to = (r * 77 + g * 150 + b * 29) >> 8 < 128 ? 255 : 0;
    if (shade <= 0) return c;
    r += (to - r) * shade / 256; g += (to - g) * shade / 256; b += (to - b) * shade / 256;
    return 0xff000000UL | (ULONG)r << 16 | (ULONG)g << 8 | (ULONG)b;
}

/* The bar's colour in a screen row: as read now, or as last read well, or keep (0: none). 0 when unknown. */
static BP_UNUSED ULONG bp_row_kept(WORD sy, ULONG keep)
{
    const ULONG *src = bp_n ? bp_rgb : bp_last;
    int n = bp_n ? bp_n : bp_last_n;
    if (!n) return keep;
    return src[sy < 0 ? 0 : sy >= n ? n - 1 : sy];
}

/* As bp_paint() on the screen's title bar, but never the pen's white: the colours last read well when a reading
 * fails, or keep (an ARGB colour, the theme's for the bar) when none ever was; the pen only when neither is
 * there. shade (0 for none) is a pressed look, as bp_shade(). */
static BP_UNUSED void bp_paint_kept(struct Window *win, ULONG keep, ULONG fallback_pen, WORD x, WORD y, WORD pw, WORD ph, int shade)
{
    static ULONG row[512];
    struct RastPort *rp = win->RPort;
    WORD i, j;
    if (CyberGfxBase && pw > 0 && pw <= 512 && (bp_n || bp_last_n || keep)) {
        for (j = 0; j < ph; j++) {
            ULONG c = bp_shade(bp_row_kept(win->TopEdge + y + j, keep), shade);
            for (i = 0; i < pw; i++) row[i] = c;
            WritePixelArray(row, 0, 0, pw * 4, rp, x, y + j, pw, 1, RECTFMT_ARGB);
        }
        return;
    }
    SetAPen(rp, fallback_pen);
    RectFill(rp, x, y, x + pw - 1, y + ph - 1);
}

/* The pen for text on what bp_paint_kept() painted (the middle row's colour, shaded as it was). */
static BP_UNUSED LONG bp_text_pen_kept(struct Screen *scr, ULONG keep, int shade, LONG fallback)
{
    int n = bp_n ? bp_n : bp_last_n;
    if (!n && !keep) return fallback;
    return bp_text_pen_for(scr, bp_shade(bp_row_kept(n / 2, keep), shade), fallback);
}

#endif
