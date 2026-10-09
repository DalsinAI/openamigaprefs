/* gp_pad: where the Test view's controller is drawn. Plain C with no Amiga
 * calls, so the layout is tested on the host (tests/test_gp_pad.c): every
 * part inside the area, nothing overlapping, left and right mirrored.
 *
 * The pad is laid out on a grid of 1000 x 600 units (a body with two
 * grips, PlayStation-style: the d-pad and the face buttons high, the
 * sticks low and inward), scaled to fit the area and centred in it, so it
 * grows with the window and the font.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef GP_PAD_H
#define GP_PAD_H

/* What kind of controller is drawn */
enum { GP_PAD_MODERN = 0, GP_PAD_CD32 = 1, GP_PAD_JOYSTICK = 2 };

typedef struct gp_rect { int x, y, w, h; } gp_rect;          /* w or h 0: not drawn */
typedef struct gp_circle { int cx, cy, r; } gp_circle;      /* r 0: not drawn */

typedef struct gp_padgeo {
    int kind;
    /* the body: a rounded rectangle, and two grips (ellipses) below it */
    gp_rect body;
    int body_radius;
    int grip_cx[2], grip_cy, grip_rx, grip_ry;
    /* the parts */
    gp_rect trigger[2];          /* LT, RT: bars with their value (modern only) */
    gp_rect shoulder[2];         /* LB, RB (Reverse, Forward on a CD32 pad) */
    gp_rect dpad;                /* the d-pad's square; the cross's arms are dpad_arm wide */
    int dpad_arm;
    gp_circle face[4];           /* A (bottom), B (right), X (left), Y (top), as OIB_A..OIB_Y */
    gp_circle stick[2];          /* left and right (modern only) */
    gp_rect middle;              /* the band in the middle of the body that holds the next three */
    gp_rect pill[2];             /* Back and Start (a CD32 pad: pill[0] is Play) */
    gp_circle guide;             /* Guide, between them (modern only) */
    int unit_x1000;              /* pixels per 1000 units, for things drawn in proportion */
} gp_padgeo;

/* Lay the pad out in an area w x h (pixels, from 0,0), for text fh high. */
void gp_pad_layout(int kind, int w, int h, int fh, gp_padgeo *g);

/* An ellipse's half-width on row dy from its centre, or -1 outside it. */
int gp_half_width(int rx, int ry, int dy);
/* Is (x,y) inside the body with its grips? For drawing the silhouette row by row. */
int gp_pad_inside(const gp_padgeo *g, int x, int y);
/* The body's row y as up to three spans [x0, x1] (below the rectangle the
 * two grips are apart); how many. */
int gp_pad_spans(const gp_padgeo *g, int y, int span[3][2]);

#endif
