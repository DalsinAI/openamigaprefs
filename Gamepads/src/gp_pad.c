/* gp_pad: the Test view's controller layout (gp_pad.h).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "gp_pad.h"

/* The body's grid: 1000 units wide, 500 high (the rectangle 0..340, the
 * grips' bottoms at 500). */
#define GRID_W 1000
#define GRID_H 500

static int sc(int units, int s1000) { return (units * s1000 + 500) / 1000; }

void gp_pad_layout(int kind, int w, int h, int fh, gp_padgeo *g)
{
    int top_t = kind == GP_PAD_MODERN ? fh + 4 : 0;          /* the triggers' row */
    int top_s = kind == GP_PAD_JOYSTICK ? 0 : fh + 4;        /* the shoulders' row */
    int gap = 3;
    int bh, s, sx, ox, oy, i;
    int y_s = top_t ? top_t + gap : 0;
    int y_b = top_s ? y_s + top_s + 2 : y_s;
    char *p = (char *)g;
    const int m = 3;                                         /* a margin all round, inside the panel */
    for (i = 0; i < (int)sizeof *g; i++) p[i] = 0;
    g->kind = kind;
    w -= 2 * m; h -= 2 * m;
    y_s += m; y_b += m;
    bh = h - (y_b - m) - 1;
    if (bh < 20 || w < 40) return;
    /* pixels per 1000 units: the body fits the height left, and the width;
     * a wide area stretches it sideways by up to a half (the parts keep
     * their size and the spaces between them grow) */
    s = w * 1000 / GRID_W;
    if (bh * 1000 / GRID_H < s) s = bh * 1000 / GRID_H;
    sx = s * 3 / 2;
    if (sx * GRID_W / 1000 > w) sx = w * 1000 / GRID_W;
    g->unit_x1000 = s;
    ox = m + (w - sc(GRID_W, sx)) / 2;
    oy = y_b;
#define X(u) (ox + sc((u), sx))
#define Y(u) (oy + sc((u), s))
#define L(u) sc((u), s)
    g->body.x = X(75); g->body.y = Y(0); g->body.w = X(925) - X(75) + 1; g->body.h = L(340);
    g->body_radius = L(120);
    g->grip_cx[0] = X(195); g->grip_cx[1] = X(805);
    g->grip_cy = Y(330); g->grip_rx = sc(145, sx); g->grip_ry = Y(500) - 1 - g->grip_cy;   /* to the grid's last row, whatever the rounding */

    /* the d-pad, and the face buttons mirroring it */
    g->dpad.w = g->dpad.h = L(160) | 1;                      /* odd: a centre pixel */
    g->dpad.x = X(230) - g->dpad.w / 2; g->dpad.y = Y(140) - g->dpad.h / 2;
    g->dpad_arm = L(56) | 1;
    if (kind == GP_PAD_JOYSTICK) {                           /* fire and fire 2, side by side where the face buttons are */
        g->face[0].cx = X(730); g->face[0].cy = Y(170); g->face[0].r = L(50);
        g->face[1].cx = X(840); g->face[1].cy = Y(110); g->face[1].r = L(50);
    } else {
        int cx = X(770), cy = Y(140), d = L(80), r = L(36);
        g->face[0].cx = cx;     g->face[0].cy = cy + d; g->face[0].r = r;   /* A, bottom */
        g->face[1].cx = cx + d; g->face[1].cy = cy;     g->face[1].r = r;   /* B, right */
        g->face[2].cx = cx - d; g->face[2].cy = cy;     g->face[2].r = r;   /* X, left */
        g->face[3].cx = cx;     g->face[3].cy = cy - d; g->face[3].r = r;   /* Y, top */
    }
    if (kind == GP_PAD_MODERN) {
        g->stick[0].cx = X(380); g->stick[1].cx = X(620);
        g->stick[0].cy = g->stick[1].cy = Y(268);
        g->stick[0].r = g->stick[1].r = L(64);
        /* the triggers above the shoulders, over the grips */
        g->trigger[0].x = X(110); g->trigger[0].w = X(330) - X(110);
        g->trigger[1].w = g->trigger[0].w; g->trigger[1].x = X(890) - g->trigger[1].w + 1;
        g->trigger[0].y = g->trigger[1].y = m; g->trigger[0].h = g->trigger[1].h = top_t;
    }
    if (kind != GP_PAD_JOYSTICK) {
        int pw = sc(96, sx), ph = L(30), mid = X(500), cy;   /* the pills widen with the body, for their names */
        if (pw < 14) pw = 14;
        if (ph < 7) ph = 7;
        ph |= 1;
        g->shoulder[0].x = X(130); g->shoulder[0].w = X(330) - X(130);
        g->shoulder[1].w = g->shoulder[0].w; g->shoulder[1].x = X(870) - g->shoulder[1].w + 1;
        g->shoulder[0].y = g->shoulder[1].y = y_s; g->shoulder[0].h = g->shoulder[1].h = top_s;
        /* the middle band, between the d-pad and the face buttons */
        g->middle.x = X(345); g->middle.w = X(655) - X(345) + 1;
        g->middle.h = fh + 4 > ph ? fh + 4 : ph;
        g->middle.y = Y(80) - g->middle.h / 2;
        if (g->middle.y < g->body.y + 2) g->middle.y = g->body.y + 2;
        cy = g->middle.y + g->middle.h / 2;
        if (kind == GP_PAD_MODERN) {                         /* Back and Start pills either side of a round Guide */
            g->guide.cx = mid; g->guide.cy = cy; g->guide.r = ph / 2 + L(8);
            g->pill[0].w = g->pill[1].w = pw; g->pill[0].h = g->pill[1].h = ph;
            g->pill[0].x = mid - g->guide.r - L(30) - pw; g->pill[1].x = 2 * mid - g->pill[0].x - pw + 1;
            g->pill[0].y = g->pill[1].y = cy - ph / 2;
        } else {                                             /* a CD32 pad's Play in the middle */
            g->pill[0].w = pw; g->pill[0].h = ph;
            g->pill[0].x = mid - pw / 2; g->pill[0].y = cy - ph / 2;
        }
    }
#undef X
#undef Y
#undef L
}

/* the ellipse's half-width on row dy from its centre, or -1 outside */
int gp_half_width(int rx, int ry, int dy)
{
    long long r2, v, x, n;
    if (ry <= 0 || rx <= 0 || dy < -ry || dy > ry) return -1;
    r2 = (long long)ry * ry;
    v = (long long)rx * rx * (r2 - (long long)dy * dy) / r2;    /* x^2 */
    if (v <= 0) return 0;
    x = rx;                                                    /* Newton's method, from above */
    for (;;) {
        n = (x + v / x) / 2;
        if (n >= x) break;
        x = n;
    }
    return (int)x;
}

int gp_pad_spans(const gp_padgeo *g, int y, int span[3][2])
{
    int iv[3][2], n = 0, i, j, out = 0;
    const gp_rect *b = &g->body;
    if (b->w <= 0) return 0;
    if (y >= b->y && y < b->y + b->h) {                  /* the rounded rectangle */
        int r = g->body_radius, inset = 0;
        int dy = y < b->y + r ? b->y + r - y : y > b->y + b->h - 1 - r ? y - (b->y + b->h - 1 - r) : 0;
        if (dy) inset = r - gp_half_width(r, r, dy);
        iv[n][0] = b->x + inset; iv[n][1] = b->x + b->w - 1 - inset; n++;
    }
    for (i = 0; i < 2; i++) {                           /* the grips */
        int hw = gp_half_width(g->grip_rx, g->grip_ry, y - g->grip_cy);
        if (hw < 0) continue;
        iv[n][0] = g->grip_cx[i] - hw; iv[n][1] = g->grip_cx[i] + hw; n++;
    }
    for (i = 1; i < n; i++)                             /* sort by start, then merge */
        for (j = i; j > 0 && iv[j][0] < iv[j - 1][0]; j--) {
            int t0 = iv[j][0], t1 = iv[j][1];
            iv[j][0] = iv[j - 1][0]; iv[j][1] = iv[j - 1][1]; iv[j - 1][0] = t0; iv[j - 1][1] = t1;
        }
    for (i = 0; i < n; i++) {
        if (out && iv[i][0] <= span[out - 1][1] + 1) { if (iv[i][1] > span[out - 1][1]) span[out - 1][1] = iv[i][1]; }
        else { span[out][0] = iv[i][0]; span[out][1] = iv[i][1]; out++; }
    }
    return out;
}

int gp_pad_inside(const gp_padgeo *g, int x, int y)
{
    int sp[3][2], n = gp_pad_spans(g, y, sp), i;
    for (i = 0; i < n; i++) if (x >= sp[i][0] && x <= sp[i][1]) return 1;
    return 0;
}
