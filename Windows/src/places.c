/* OpenWindows' places: keys, the placing rule, and the tags that open a window
 * at its place (places.h). Pure C: the host tests run it as it is.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <string.h>
#include <stdlib.h>
#include "places.h"

static int ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

void place_key(const char *title, const char *prog, char *key, int size)
{
    char cut[40];
    int n = 0, k = 0;
    if (title)
        while (*title && n < 30 && !(*title >= '0' && *title <= '9') && *title != '(' && *title != '[' && *title != '"')
            cut[n++] = *title++;
    while (n && cut[n - 1] == ' ') n--;
    cut[n] = 0;
    /* "%s:%s" without snprintf: libnix's pulls in more than this needs */
    while (prog && *prog && k < size - 1) key[k++] = *prog++;
    if (k < size - 1) key[k++] = ':';
    for (n = 0; cut[n] && k < size - 1; n++) key[k++] = cut[n];
    key[k] = 0;
}

int place_candidate(int backdrop_or_gzz, int dragbar, const char *title)
{
    return !backdrop_or_gzz && (dragbar || (title && *title));
}

int place_workbench(const char *prog, int wbench_flag)
{
    return wbench_flag || ci_eq(prog, "Workbench") || ci_eq(prog, "OpenBench");
}

static int size_fits(const struct place *pl, int sizeable, short minw, short minh, short maxw, short maxh)
{
    return sizeable && pl->w >= minw && pl->h >= minh &&
           (!maxw || (unsigned short)maxw == 0xffff || pl->w <= (unsigned short)maxw) &&
           (!maxh || (unsigned short)maxh == 0xffff || pl->h <= (unsigned short)maxh);
}

void fit_place(const struct place *pl, int sizeable, short minw, short minh, short maxw, short maxh,
               short sw, short sh, short *l, short *t, short *wd, short *ht)
{
    *l = pl->l; *t = pl->t;
    if (size_fits(pl, sizeable, minw, minh, maxw, maxh)) { *wd = pl->w; *ht = pl->h; }
    if (sw > 0 && *wd > sw) *wd = sw;
    if (sh > 0 && *ht > sh) *ht = sh;
    if (*l < 0) *l = 0;
    if (*t < 0) *t = 0;
    if (sw > 0 && *wd > 0 && *l + *wd > sw) *l = sw - *wd;
    if (sh > 0 && *ht > 0 && *t + *ht > sh) *t = sh - *ht;
}

void place_plan(const struct place *pl, const struct place_ask *a, short sw, short sh, struct place_plan *p)
{
    /* the size it will have: its own when it gives one, else (asked by its inner size, or not at all)
     * the kept one, which was its outer size when it was left: near enough to keep its corner on the screen */
    short wd = a->inner || a->w <= 0 ? pl->w : a->w, ht = a->inner || a->h <= 0 ? pl->h : a->h;
    p->size = size_fits(pl, a->sizeable, a->minw, a->minh, a->maxw, a->maxh);
    fit_place(pl, a->sizeable, a->minw, a->minh, a->maxw, a->maxh, sw, sh, &p->l, &p->t, &wd, &ht);
    p->w = wd; p->h = ht;
}

/* ---- tag lists ---- */

struct walk { const struct TagItem *t; int steps; };

static const struct TagItem *next_tag(struct walk *w)
{
    while (w->t && w->steps++ < 4 * PLACE_TAGS_MAX) {
        const struct TagItem *t = w->t;
        switch (t->ti_Tag) {
        case TAG_DONE: w->t = NULL; return NULL;
        case TAG_IGNORE: w->t = t + 1; break;
        case TAG_MORE: w->t = (const struct TagItem *)t->ti_Data; break;
        case TAG_SKIP: w->t = t + 1 + t->ti_Data; break;
        default: w->t = t + 1; return t;
        }
    }
    w->t = NULL;
    return NULL;
}

int place_tags_count(const struct TagItem *list)
{
    struct walk w;
    int n = 0;
    w.t = list; w.steps = 0;
    while (next_tag(&w))
        if (++n > PLACE_TAGS_MAX) return -1;
    return w.steps >= 4 * PLACE_TAGS_MAX ? -1 : n;
}

static int dropped(unsigned long tag, const struct place_plan *p)
{
    if (tag == WA_Left || tag == WA_Top) return 1;
    return p->size && (tag == WA_Width || tag == WA_Height || tag == WA_InnerWidth || tag == WA_InnerHeight);
}

int place_tags(struct TagItem *out, int max, const struct place_plan *plan, const struct TagItem *list, int front)
{
    struct walk w;
    const struct TagItem *t;
    int n = 0;
    if (front) {
        if (max < 5) return -1;
        out[n].ti_Tag = WA_Left; out[n++].ti_Data = (unsigned long)(long)plan->l;
        out[n].ti_Tag = WA_Top; out[n++].ti_Data = (unsigned long)(long)plan->t;
        if (plan->size) {
            out[n].ti_Tag = WA_Width; out[n++].ti_Data = (unsigned long)(long)plan->w;
            out[n].ti_Tag = WA_Height; out[n++].ti_Data = (unsigned long)(long)plan->h;
        }
    }
    w.t = list; w.steps = 0;
    while ((t = next_tag(&w))) {
        if (dropped(t->ti_Tag, plan)) continue;
        if (n >= max - 1) return -1;
        out[n++] = *t;
    }
    if (n >= max) return -1;
    out[n].ti_Tag = TAG_DONE; out[n].ti_Data = 0;
    return n;
}

/* ---- the table ---- */

int place_index(struct place *pl, int *n, const char *key, int make)
{
    int i, oldest = 0;
    for (i = 0; i < *n; i++) if (!strcmp(pl[i].key, key)) return i;
    if (!make) return -1;
    if (*n < PLACES_MAX) i = (*n)++;
    else { for (i = 1; i < *n; i++) if (pl[i].used < pl[oldest].used) oldest = i; i = oldest; }
    memset(&pl[i], 0, sizeof pl[i]);
    strncpy(pl[i].key, key, sizeof pl[i].key - 1);
    return i;
}

int place_parse(const char *line, struct place *pl)
{
    const char *q;
    char *e;
    long l, t, w, h;
    int k;
    if (*line != '"' || !(q = strchr(line + 1, '"'))) return 0;
    /* four numbers (no sscanf: libnix's pulls in FPU code a 68020 without an FPU lacks) */
    l = strtol(q + 1, &e, 10); t = strtol(e, &e, 10); w = strtol(e, &e, 10); h = strtol(e, &e, 10);
    if (w <= 0 || h <= 0) return 0;
    memset(pl, 0, sizeof *pl);
    k = (int)(q - line - 1);
    if (k > (int)sizeof pl->key - 1) k = sizeof pl->key - 1;
    memcpy(pl->key, line + 1, k);
    pl->l = (short)l; pl->t = (short)t; pl->w = (short)w; pl->h = (short)h;
    return 1;
}

void place_order(const struct place *pl, int n, int *order)
{
    int i, j;
    for (i = 0; i < n; i++) {
        int k = i;
        for (j = i; j > 0 && pl[order[j - 1]].used > pl[k].used; j--) order[j] = order[j - 1];
        order[j] = k;
    }
}
