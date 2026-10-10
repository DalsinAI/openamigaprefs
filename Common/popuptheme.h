/* popuptheme.h: the Open look for the Team's pop-up panels (OpenSpeaker's
 * levels, OpenTitle's network panel and its list of settings): the theme's
 * menu colour for the panel, its menu text colour, a slim frame in the
 * theme's frame colour, as OpenMenus' menus are drawn, instead of the OS's
 * grey with a bevel.
 *
 * pt_load() reads ENV:OpenGadTools/Look ("<theme> light|dark|auto") and the
 * theme file, once the program has a Workbench screen; pt_panel() fills a
 * window's inside and frames it; pt_text_pen() gives the pen for text.
 * With no theme (or the Classic one) pt_ok is 0 and the caller draws as it
 * did. The program links opengadtools' lib/ogt_theme.c (OGT=dir, as the
 * Menus engine does) and opens cybergraphics.library (CyberGfxBase).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef POPUPTHEME_H
#define POPUPTHEME_H

#include <exec/types.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <string.h>
#include <stdio.h>
#include "ogt_theme.h"

#define PT_UNUSED __attribute__((unused))

static ogt_theme pt_theme;
static int pt_ok;
static ogt_rgb pt_fill, pt_text, pt_frame, pt_hot, pt_hot_text;
static LONG pt_pens[4] = { -1, -1, -1, -1 };      /* text, frame, accent text, accent */
static struct Screen *pt_scr;

static PT_UNUSED char *pt_read(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf = NULL;
    LONG len;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len > 0 && len < 64 * 1024 && (buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR)) && Read(fh, buf, len) != len) { FreeVec(buf); buf = NULL; }
    Close(fh);
    return buf;
}

static PT_UNUSED void pt_free(void)
{
    int i;
    if (pt_scr)
        for (i = 0; i < 4; i++) if (pt_pens[i] >= 0) ReleasePen(pt_scr->ViewPort.ColorMap, pt_pens[i]);
    for (i = 0; i < 4; i++) pt_pens[i] = -1;
    if (pt_ok) ogt_theme_free(&pt_theme);
    pt_ok = 0;
    pt_scr = NULL;
}

static PT_UNUSED LONG pt_pen(struct Screen *scr, ogt_rgb c)
{
    return ObtainBestPen(scr->ViewPort.ColorMap, (ULONG)c.r * 0x01010101UL, (ULONG)c.g * 0x01010101UL, (ULONG)c.b * 0x01010101UL,
                         OBP_Precision, PRECISION_IMAGE, TAG_DONE);
}

/* 1 when a theme (not Classic) gives the colours */
static PT_UNUSED int pt_load(struct Screen *scr)
{
    char *look = pt_read("ENV:OpenGadTools/Look"), name[64] = "Classic", mode[8] = "light", path[120], err[64], *text;
    int dark = 0;
    pt_free();
    pt_scr = scr;
    if (look) { sscanf(look, "%63s %7s", name, mode); FreeVec(look); }
    if (!strcmp(name, "Classic")) return 0;
    if (!strcmp(mode, "dark")) dark = 1;
    else if (!strcmp(mode, "auto")) {
        struct DateStamp ds;
        DateStamp(&ds);
        dark = ds.ds_Minute / 60 >= 19 || ds.ds_Minute / 60 < 7;
    }
    snprintf(path, sizeof path, "SYS:Prefs/Presets/Themes/%s%s", name, strstr(name, ".theme") ? "" : ".theme");
    if (!(text = pt_read(path))) return 0;
    if (ogt_theme_parse(&pt_theme, text, err, sizeof err)) {
        int m = dark && pt_theme.has_dark ? OGT_DARK : OGT_LIGHT;
        pt_ok = !pt_theme.passthrough && ogt_theme_colour(&pt_theme, m, "menu", &pt_fill) && ogt_theme_colour(&pt_theme, m, "menu.text", &pt_text) &&
                ogt_theme_colour(&pt_theme, m, "frame.active", &pt_frame) && ogt_theme_colour(&pt_theme, m, "accent", &pt_hot) &&
                ogt_theme_colour(&pt_theme, m, "accent.text", &pt_hot_text);
        if (!pt_ok) ogt_theme_free(&pt_theme);
    }
    FreeVec(text);
    if (pt_ok) {
        pt_pens[0] = pt_pen(scr, pt_text);
        pt_pens[1] = pt_pen(scr, pt_frame);
        pt_pens[2] = pt_pen(scr, pt_hot_text);
        pt_pens[3] = pt_pen(scr, pt_hot);
    }
    return pt_ok;
}

static PT_UNUSED ULONG pt_argb(ogt_rgb c) { return 0xff000000UL | (ULONG)c.r << 16 | (ULONG)c.g << 8 | (ULONG)c.b; }

/* the panel: the theme's menu colour over the window's inside and a one-pixel frame */
static PT_UNUSED void pt_panel(struct RastPort *rp, WORD w, WORD h)
{
    if (CyberGfxBase && GetCyberMapAttr(rp->BitMap, CYBRMATTR_ISCYBERGFX) && GetCyberMapAttr(rp->BitMap, CYBRMATTR_DEPTH) > 8) {
        FillPixelArray(rp, 0, 0, w, h, pt_argb(pt_fill));
        FillPixelArray(rp, 0, 0, w, 1, pt_argb(pt_frame));
        FillPixelArray(rp, 0, h - 1, w, 1, pt_argb(pt_frame));
        FillPixelArray(rp, 0, 0, 1, h, pt_argb(pt_frame));
        FillPixelArray(rp, w - 1, 0, 1, h, pt_argb(pt_frame));
    } else {
        LONG fp = pt_pen(pt_scr, pt_fill);
        if (fp >= 0) { SetAPen(rp, fp); RectFill(rp, 0, 0, w - 1, h - 1); ReleasePen(pt_scr->ViewPort.ColorMap, fp); }
        SetAPen(rp, pt_pens[1] >= 0 ? pt_pens[1] : 1);
        Move(rp, 0, 0); Draw(rp, w - 1, 0); Draw(rp, w - 1, h - 1); Draw(rp, 0, h - 1); Draw(rp, 0, 0);
    }
}

/* the pens: panel text, frame, text on the accent, the accent; or -1 */
static PT_UNUSED LONG pt_text_pen(void) { return pt_ok ? pt_pens[0] : -1; }
static PT_UNUSED LONG pt_frame_pen(void) { return pt_ok ? pt_pens[1] : -1; }
static PT_UNUSED LONG pt_hot_text_pen(void) { return pt_ok ? pt_pens[2] : -1; }
static PT_UNUSED LONG pt_hot_pen(void) { return pt_ok ? pt_pens[3] : -1; }

#endif
