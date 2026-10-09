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

/* 1 when no window but ours covers the bar between x0 and x1 */
static BP_UNUSED int bp_clear(struct Screen *scr, WORD x0, WORD x1, int (*ours)(struct Window *))
{
    struct Layer *l;
    int ok = 1;
    if (!scr->BarLayer) return 0;
    LockLayerInfo(&scr->LayerInfo);
    for (l = scr->BarLayer->front; l && ok; l = l->front) {
        struct Window *o = (struct Window *)l->Window;
        if (o && ours(o)) continue;
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

#endif
