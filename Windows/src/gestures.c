/* OpenWindows' action filter: the gesture table (gestures.h,
 * docs/design/action-filter.md). Pure: Windows/tests/test_gestures.c.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "gestures.h"
#include <string.h>
#include <stdio.h>

static const char *const target_names[GT_COUNT] = { "title", "body", "gadget", "edge", "backdrop", "screenbar" };
static const char *const button_names[GB_COUNT] = { "select", "menu", "middle" };
static const char *const kind_names[GK_COUNT] = { "click", "double", "move" };
static const char *const action_names[] = { "front", "back", "activate", "drag", "resize", "snap" };
#define ACTIONS ((int)(sizeof action_names / sizeof action_names[0]))
static const char *const qual_names[] = { "shift", "alt", "ctrl", "amiga" };

static void add(struct gesture_table *t, int target, int kind, unsigned actions, int take)
{
    struct gesture_row *r;
    if (t->n >= GESTURE_ROWS) return;
    r = &t->row[t->n++];
    r->target = (unsigned char)target; r->button = GB_SELECT; r->kind = (unsigned char)kind;
    r->take = (unsigned char)take; r->qual = 0; r->actions = (unsigned char)actions;
}

void gestures_default(struct gesture_table *t, int dblfront, int edges, int snap)
{
    t->n = 0;
    add(t, GT_TITLE, GK_MOVE, GA_DRAG | (snap ? GA_SNAP : 0), 0);   /* press and move: a drag at once */
    add(t, GT_TITLE, GK_CLICK, GA_FRONT, 0);                         /* a click, no move: to the front, at the release */
    if (dblfront) add(t, GT_TITLE, GK_DOUBLE, GA_FRONT, 0);
    add(t, GT_BODY, GK_CLICK, GA_FRONT, 0);
    if (edges) {
        add(t, GT_EDGE, GK_MOVE, GA_RESIZE, 1);
        add(t, GT_EDGE, GK_CLICK, GA_ACTIVATE, 1);
    }
}

static int word(const char **p, char *out, int size)
{
    int n = 0;
    while (**p == ' ' || **p == '\t') (*p)++;
    while (**p && **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r' && **p != ';') {
        if (n < size - 1) out[n++] = **p;
        (*p)++;
    }
    out[n] = 0;
    return n;
}

static int lookup(const char *const *names, int count, const char *w)
{
    int i;
    for (i = 0; i < count; i++) if (!strcmp(names[i], w)) return i;
    return -1;
}

static int parse_qual(const char *w, unsigned char *q)
{
    char part[16];
    const char *p = w;
    if (!strcmp(w, "none")) { *q = 0; return 1; }
    if (!strcmp(w, "any")) { *q = GQ_ANY; return 1; }
    *q = 0;
    while (*p) {
        int n = 0, i;
        while (*p && *p != '+') { if (n < 15) part[n++] = *p; p++; }
        part[n] = 0;
        if (*p == '+') p++;
        if ((i = lookup(qual_names, 4, part)) < 0) return 0;
        *q |= (unsigned char)(1u << i);
    }
    return 1;
}

static int parse_actions(const char *w, unsigned char *a)
{
    char part[16];
    const char *p = w;
    *a = 0;
    if (!strcmp(w, "none")) return 1;
    while (*p) {
        int n = 0, i;
        while (*p && *p != ',') { if (n < 15) part[n++] = *p; p++; }
        part[n] = 0;
        if (*p == ',') p++;
        if ((i = lookup(action_names, ACTIONS, part)) < 0) return 0;
        *a |= (unsigned char)(1u << i);
    }
    return 1;
}

int gestures_parse(struct gesture_table *t, const char *text)
{
    struct gesture_table n;
    const char *p = text;
    int line = 0;
    n.n = 0;
    while (*p) {
        char w[6][48];
        int k, got = 0, tg, bt, kd, tk;
        struct gesture_row *r;
        line++;
        for (k = 0; k < 6; k++) if (word(&p, w[k], sizeof w[k])) got++; else break;
        while (*p && *p != '\n') p++;                 /* the rest of the line: a comment */
        if (*p) p++;
        if (got == 0) continue;
        if (got < 6) return -line;
        tg = lookup(target_names, GT_COUNT, w[0]);
        bt = lookup(button_names, GB_COUNT, w[1]);
        kd = lookup(kind_names, GK_COUNT, w[3]);
        tk = !strcmp(w[5], "take") ? 1 : !strcmp(w[5], "pass") ? 0 : -1;
        if (tg < 0 || bt < 0 || kd < 0 || tk < 0 || n.n >= GESTURE_ROWS) return -line;
        r = &n.row[n.n];
        if (!parse_qual(w[2], &r->qual) || !parse_actions(w[4], &r->actions)) return -line;
        r->target = (unsigned char)tg; r->button = (unsigned char)bt; r->kind = (unsigned char)kd; r->take = (unsigned char)tk;
        n.n++;
    }
    *t = n;
    return n.n;
}

unsigned gestures_qual(unsigned q)
{
    unsigned g = 0;
    if (q & 0x0003) g |= GQ_SHIFT;                  /* IEQUALIFIER_LSHIFT | RSHIFT */
    if (q & 0x0030) g |= GQ_ALT;                    /* IEQUALIFIER_LALT | RALT */
    if (q & 0x0008) g |= GQ_CTRL;                   /* IEQUALIFIER_CONTROL */
    if (q & 0x00C0) g |= GQ_AMIGA;                  /* IEQUALIFIER_LCOMMAND | RCOMMAND */
    return g;
}

static int qual_fits(const struct gesture_row *r, unsigned qual)
{
    return r->qual == GQ_ANY || r->qual == qual;
}

unsigned gestures_actions(const struct gesture_table *t, int target, int button, int kind, unsigned qual, int *take)
{
    unsigned a = 0, click = 0;
    int i, found = 0, click_found = 0;
    if (take) *take = 0;
    for (i = 0; i < t->n; i++) {
        const struct gesture_row *r = &t->row[i];
        if (r->target != target || r->button != button || !qual_fits(r, qual)) continue;
        if (take && r->take) *take = 1;
        if (!found && r->kind == kind) { a = r->actions; found = 1; }   /* the first row that fits */
        if (!click_found && r->kind == GK_CLICK) { click = r->actions; click_found = 1; }
    }
    if (!found && kind == GK_DOUBLE) a = click;     /* no row for a double-click: the second click is a click */
    return a;
}

int gestures_moved(int x0, int y0, int x, int y)
{
    int dx = x - x0, dy = y - y0;
    return dx >= GESTURE_MOVE || dx <= -GESTURE_MOVE || dy >= GESTURE_MOVE || dy <= -GESTURE_MOVE;
}

void gestures_row_text(const struct gesture_row *r, char *out, int size)
{
    char q[40], a[64];
    int i;
    q[0] = a[0] = 0;
    if (r->qual == GQ_ANY) strcpy(q, "any");
    else if (!r->qual) strcpy(q, "none");
    else for (i = 0; i < 4; i++) if (r->qual & (1u << i)) { if (q[0]) strcat(q, "+"); strcat(q, qual_names[i]); }
    if (!r->actions) strcpy(a, "none");
    else for (i = 0; i < ACTIONS; i++) if (r->actions & (1u << i)) { if (a[0]) strcat(a, ","); strcat(a, action_names[i]); }
    snprintf(out, (size_t)size, "%s %s %s %s %s %s", target_names[r->target], button_names[r->button], q,
             kind_names[r->kind], a, r->take ? "take" : "pass");
}
