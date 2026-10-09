/* Host tests for gp_pad.c: the Test view's controller fits its area, its
 * parts don't overlap, the controls sit on the body, and left and right
 * mirror each other. With an argument, writes a picture of each layout
 * (PPM) there. MIT, Copyright (c) 2026 Dalsin Limited. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gp_pad.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct { int x0, y0, x1, y1; const char *name; } box;

static int nboxes;
static box boxes[32];
static void add(int x, int y, int w, int h, const char *name)
{
    if (w <= 0 || h <= 0) return;
    boxes[nboxes++] = (box){ x, y, x + w - 1, y + h - 1, name };
}
static void addc(gp_circle c, const char *name) { if (c.r > 0) add(c.cx - c.r, c.cy - c.r, 2 * c.r + 1, 2 * c.r + 1, name); }

static int on_body(const gp_padgeo *g, const box *b)
{
    /* the box's corners and edge midpoints (a circle's box corners may stick out: test its 4 extreme points instead) */
    int pts[8][2] = { { b->x0, b->y0 }, { b->x1, b->y0 }, { b->x0, b->y1 }, { b->x1, b->y1 },
                      { (b->x0 + b->x1) / 2, b->y0 }, { (b->x0 + b->x1) / 2, b->y1 }, { b->x0, (b->y0 + b->y1) / 2 }, { b->x1, (b->y0 + b->y1) / 2 } };
    int round = !strncmp(b->name, "face", 4) || !strncmp(b->name, "stick", 5);
    for (int i = round ? 4 : 0; i < 8; i++) if (!gp_pad_inside(g, pts[i][0], pts[i][1])) return 0;
    return 1;
}

static void picture(const char *dir, const char *name, const gp_padgeo *g, int w, int h)
{
    char path[512];
    unsigned char *px = calloc((size_t)w * h, 3);
    FILE *f;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        unsigned char *p = px + 3 * ((size_t)y * w + x);
        int in = gp_pad_inside(g, x, y);
        p[0] = p[1] = p[2] = in ? 240 : 170;
    }
    for (int i = 0; i < nboxes; i++)
        for (int y = boxes[i].y0; y <= boxes[i].y1; y++) for (int x = boxes[i].x0; x <= boxes[i].x1; x++)
            if (x >= 0 && y >= 0 && x < w && y < h && (y == boxes[i].y0 || y == boxes[i].y1 || x == boxes[i].x0 || x == boxes[i].x1)) {
                unsigned char *p = px + 3 * ((size_t)y * w + x); p[0] = 40; p[1] = 90; p[2] = 200;
            }
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    if ((f = fopen(path, "wb"))) { fprintf(f, "P6 %d %d 255\n", w, h); fwrite(px, 3, (size_t)w * h, f); fclose(f); }
    free(px);
}

static void one(int kind, int w, int h, int fh, const char *name, const char *dir)
{
    gp_padgeo g;
    gp_pad_layout(kind, w, h, fh, &g);
    nboxes = 0;
    add(g.trigger[0].x, g.trigger[0].y, g.trigger[0].w, g.trigger[0].h, "trigger L");
    add(g.trigger[1].x, g.trigger[1].y, g.trigger[1].w, g.trigger[1].h, "trigger R");
    add(g.shoulder[0].x, g.shoulder[0].y, g.shoulder[0].w, g.shoulder[0].h, "shoulder L");
    add(g.shoulder[1].x, g.shoulder[1].y, g.shoulder[1].w, g.shoulder[1].h, "shoulder R");
    int first_on_body = nboxes;
    add(g.dpad.x, g.dpad.y, g.dpad.w, g.dpad.h, "dpad");
    for (int i = 0; i < 4; i++) addc(g.face[i], "face");
    addc(g.stick[0], "stick L"); addc(g.stick[1], "stick R");
    add(g.pill[0].x, g.pill[0].y, g.pill[0].w, g.pill[0].h, "pill 0");
    add(g.pill[1].x, g.pill[1].y, g.pill[1].w, g.pill[1].h, "pill 1");
    addc(g.guide, "guide");

    CHECK(g.body.w > 0, "%s: no body", name);
    for (int i = 0; i < nboxes; i++) {
        CHECK(boxes[i].x0 >= 3 && boxes[i].y0 >= 3 && boxes[i].x1 < w - 3 && boxes[i].y1 < h - 3, "%s: %s in the margin or outside the area", name, boxes[i].name);
        for (int j = i + 1; j < nboxes; j++)
            CHECK(boxes[i].x1 < boxes[j].x0 || boxes[j].x1 < boxes[i].x0 || boxes[i].y1 < boxes[j].y0 || boxes[j].y1 < boxes[i].y0,
                  "%s: %s overlaps %s", name, boxes[i].name, boxes[j].name);
        if (i >= first_on_body) CHECK(on_body(&g, &boxes[i]), "%s: %s not on the body", name, boxes[i].name);
        else CHECK(boxes[i].y1 < g.body.y, "%s: %s not above the body", name, boxes[i].name);
    }
    /* the silhouette stays in the area */
    for (int y = 0; y < h; y++) { CHECK(!gp_pad_inside(&g, 2, y) && !gp_pad_inside(&g, w - 3, y), "%s: body in the side margins", name); }
    for (int x = 0; x < w; x++) CHECK(!gp_pad_inside(&g, x, h - 3) && !gp_pad_inside(&g, x, 2), "%s: body in the top or bottom margin", name);
    /* left and right mirror each other, about the body's centre line (to a pixel) */
    int axis2 = 2 * g.body.x + g.body.w - 1;
#define MIRROR(a, b) CHECK(abs((a) + (b) - axis2) <= 2, "%s: %s and %s not mirrored (%d + %d vs %d)", name, #a, #b, (a), (b), axis2)
    MIRROR(g.grip_cx[0], g.grip_cx[1]);
    if (kind != GP_PAD_JOYSTICK) {
        MIRROR(g.dpad.x + g.dpad.w / 2, g.face[0].cx);
        MIRROR(g.shoulder[0].x, g.shoulder[1].x + g.shoulder[1].w - 1);
        MIRROR(g.middle.x, g.middle.x + g.middle.w - 1);
        CHECK(g.face[0].r == g.face[1].r && g.face[1].r == g.face[2].r && g.face[2].r == g.face[3].r, "%s: face buttons differ", name);
        CHECK(g.face[2].cy == g.face[1].cy && g.face[0].cx == g.face[3].cx, "%s: face diamond crooked", name);
        CHECK(abs(g.dpad.y + g.dpad.h / 2 - g.face[1].cy) <= 1, "%s: d-pad and face buttons not level", name);
    }
    if (kind == GP_PAD_MODERN) {
        MIRROR(g.stick[0].cx, g.stick[1].cx);
        MIRROR(g.trigger[0].x, g.trigger[1].x + g.trigger[1].w - 1);
        CHECK(g.stick[0].cy == g.stick[1].cy && g.stick[0].r == g.stick[1].r, "%s: sticks differ", name);
    }
    /* the pills and Guide stay in the middle band, mirrored */
    for (int i = 0; i < 2; i++) if (g.pill[i].w)
        CHECK(g.pill[i].x >= g.middle.x && g.pill[i].x + g.pill[i].w <= g.middle.x + g.middle.w, "%s: pill %d out of the middle band", name, i);
    if (kind == GP_PAD_MODERN) { MIRROR(g.pill[0].x, g.pill[1].x + g.pill[1].w - 1); MIRROR(g.guide.cx, g.guide.cx); }
    if (kind == GP_PAD_CD32) MIRROR(g.pill[0].x, g.pill[0].x + g.pill[0].w - 1);
    if (dir) picture(dir, name, &g, w, h);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    static const struct { int w, h, fh; const char *n; } sizes[] = {
        { 540, 166, 8, "topaz8" }, { 540, 226, 13, "dejavu13" }, { 700, 260, 15, "wide15" }, { 380, 130, 8, "narrow8" }, { 1600, 700, 16, "huge" },
    };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        char n[64];
        static const char *const kinds[3] = { "modern", "cd32", "joystick" };
        for (int k = 0; k < 3; k++) {
            snprintf(n, sizeof n, "%s-%s", kinds[k], sizes[i].n);
            one(k, sizes[i].w, sizes[i].h, sizes[i].fh, n, dir);
        }
    }
    printf(fails ? "%d failed\n" : "gp_pad: all passed\n", fails);
    return fails != 0;
}
