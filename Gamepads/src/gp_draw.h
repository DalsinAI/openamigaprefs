/* gp_draw: the Test view's controller, drawn with graphics.library (so
 * through OpenGfx where it runs) in the screen's own pens, plus one accent
 * colour for "pressed": the OpenLook theme's accent where the screen has
 * the colours, else the fill pen. Works on 8-bit and RTG screens alike.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef GP_DRAW_H
#define GP_DRAW_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>
#include "gp_pad.h"

typedef struct gpv_state {
    int kind;                       /* GP_PAD_* */
    int family;                     /* OIFAM_*: PlayStation face buttons get their shapes */
    int connected;
    ULONG buttons;                  /* OIBF(OIB_*) */
    WORD axes[6];                   /* OIAXIS_* */
    int deadzone;                   /* 0..32767, drawn round each stick's centre */
    const char *face[4];            /* the A, B, X, Y buttons' names on this pad ("A", "Cross", "Red") */
    const char *shoulder[2];        /* "LB" "RB", "L1" "R1", "Reverse" "Forward" */
    const char *trigger[2];         /* "LT" "RT", "L2" "R2", "ZL" "ZR" */
    const char *pill[2];            /* "Back" "Start" ("Play" on a CD32 pad) */
} gpv_state;

struct gpv_view;

/* Pens for this screen (released by gpv_close). */
struct gpv_view *gpv_open(struct Screen *scr, struct DrawInfo *dri);
void gpv_close(struct gpv_view *v);
/* Draw into x,y w*h of rp. force: everything (after a refresh, or another
 * pad); else only the parts whose state changed. */
void gpv_draw(struct gpv_view *v, struct RastPort *rp, int x, int y, int w, int h, const gpv_state *s, int force);

#endif
