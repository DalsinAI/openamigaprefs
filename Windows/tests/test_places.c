/* OpenWindows' places (src/places.c) on the host: the keys, the placing rule,
 * the table, and the tags a window with a kept place opens with, read as
 * OpenIntuition reads them (a tag's last wins) and as an Intuition that reads
 * a tag's first would. Each kind of opener the lab tried on 24 October 2026
 * is here: tags (most programs, OpenBench's drawers), a NewWindow, a NewWindow
 * with tags, an ExtNewWindow, by inner size (the Shell's CON: window), with
 * no size gadget, and tag lists chained with TAG_MORE and TAG_SKIP.
 *   Windows/tests/run.sh
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <stdio.h>
#include <string.h>
#include "../src/places.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- two Intuitions ---- */

struct box { short l, t, w, h; };
#define BL 4                                /* the borders an inner size is given inside */
#define BR 18
#define BT 24
#define BB 4

static const struct TagItem *walk(const struct TagItem **t)
{
    while (*t) {
        const struct TagItem *x = *t;
        switch (x->ti_Tag) {
        case TAG_DONE: *t = NULL; return NULL;
        case TAG_IGNORE: *t = x + 1; break;
        case TAG_MORE: *t = (const struct TagItem *)x->ti_Data; break;
        case TAG_SKIP: *t = x + 1 + x->ti_Data; break;
        default: *t = x + 1; return x;
        }
    }
    return NULL;
}

/* OpenIntuition (window_core.c, read_tags): the NewWindow, then its Extension, then the tags, each tag as it
 * comes, so a tag's last wins; WA_Width after WA_InnerWidth wins, and the other way round */
static void open_last(const struct box *nw, const struct TagItem *ext, const struct TagItem *tags, struct box *out)
{
    const struct TagItem *lists[2], *t, *x;
    long iw = -1, ih = -1;
    int k;
    struct box b = { 0, 0, -1, -1 };
    if (nw) b = *nw;
    lists[0] = ext; lists[1] = tags;
    for (k = 0; k < 2; k++)
        for (t = lists[k]; (x = walk(&t)); ) {
            if (x->ti_Tag == WA_Left) b.l = (short)x->ti_Data;
            else if (x->ti_Tag == WA_Top) b.t = (short)x->ti_Data;
            else if (x->ti_Tag == WA_Width) { b.w = (short)x->ti_Data; iw = -1; }
            else if (x->ti_Tag == WA_Height) { b.h = (short)x->ti_Data; ih = -1; }
            else if (x->ti_Tag == WA_InnerWidth) iw = (long)x->ti_Data;
            else if (x->ti_Tag == WA_InnerHeight) ih = (long)x->ti_Data;
        }
    if (iw >= 0) b.w = (short)(iw + BL + BR);
    if (ih >= 0) b.h = (short)(ih + BT + BB);
    *out = b;
}

static const struct TagItem *first(const struct TagItem *list, unsigned long tag)
{
    const struct TagItem *t = list, *x;
    while ((x = walk(&t))) if (x->ti_Tag == tag) return x;
    return NULL;
}

/* An Intuition that reads a tag's first (FindTagItem), the tags before the Extension, either before the
 * NewWindow; an inner size, when given, wins */
static void open_first(const struct box *nw, const struct TagItem *ext, const struct TagItem *tags, struct box *out)
{
    static const unsigned long tag[6] = { WA_Left, WA_Top, WA_Width, WA_Height, WA_InnerWidth, WA_InnerHeight };
    long v[6];
    int k;
    struct box b = { 0, 0, -1, -1 };
    if (nw) b = *nw;
    v[0] = b.l; v[1] = b.t; v[2] = b.w; v[3] = b.h; v[4] = v[5] = -1;
    for (k = 0; k < 6; k++) {
        const struct TagItem *x = first(tags, tag[k]);
        if (!x) x = first(ext, tag[k]);
        if (x) v[k] = (long)x->ti_Data;
    }
    b.l = (short)v[0]; b.t = (short)v[1]; b.w = (short)v[2]; b.h = (short)v[3];
    if (v[4] >= 0) b.w = (short)(v[4] + BL + BR);
    if (v[5] >= 0) b.h = (short)(v[5] + BT + BB);
    *out = b;
}

/* ---- the patch, as ow_open_window does it ---- */

struct opening {
    const char *what;
    struct box nw; int has_nw;              /* a NewWindow, and its box */
    const struct TagItem *ext;              /* its ExtNewWindow's Extension */
    const struct TagItem *tags;
    struct place_ask ask;
};

/* What each Intuition makes of the opening with the plan applied: copies of the tags and the Extension, the
 * NewWindow's box changed in a copy */
static void open_planned(const struct opening *o, const struct place_plan *p, struct box *last, struct box *firstb)
{
    struct TagItem tags[64], ext[64];
    struct box nw = o->nw;
    int n = place_tags(tags, 64, p, o->tags, 1), m = o->ext ? place_tags(ext, 64, p, o->ext, 0) : 0;
    CHECK(n >= 2 && m >= 0, "%s: the tags made (%d %d)", o->what, n, m);
    if (o->has_nw) { nw.l = p->l; nw.t = p->t; if (p->size) { nw.w = p->w; nw.h = p->h; } }
    open_last(o->has_nw ? &nw : NULL, o->ext ? ext : NULL, tags, last);
    open_first(o->has_nw ? &nw : NULL, o->ext ? ext : NULL, tags, firstb);
}

static void check_opening(const struct opening *o, const struct place *pl, short sw, short sh, struct box want)
{
    struct place_plan p;
    struct box a, b;
    place_plan(pl, &o->ask, sw, sh, &p);
    open_planned(o, &p, &a, &b);
    CHECK(a.l == want.l && a.t == want.t && a.w == want.w && a.h == want.h,
          "%s, a tag's last (OpenIntuition): %d %d %d %d, wanted %d %d %d %d", o->what, a.l, a.t, a.w, a.h, want.l, want.t, want.w, want.h);
    CHECK(b.l == want.l && b.t == want.t && b.w == want.w && b.h == want.h,
          "%s, a tag's first: %d %d %d %d, wanted %d %d %d %d", o->what, b.l, b.t, b.w, b.h, want.l, want.t, want.w, want.h);
}

#define T(tag, v) { (tag), (unsigned long)(v) }

int main(void)
{
    char key[64];
    struct place pl, table[PLACES_MAX];
    int n, i, order[PLACES_MAX];

    /* ---- keys ---- */
    place_key("AmigaShell", "CON", key, sizeof key);
    CHECK(!strcmp(key, "CON:AmigaShell"), "the Shell's window: %s", key);
    place_key("AmigaShell [3]", "CON", key, sizeof key);
    CHECK(!strcmp(key, "CON:AmigaShell"), "cut at a bracket: %s", key);
    place_key("Work: - 17 items", "OpenBench", key, sizeof key);
    CHECK(!strcmp(key, "OpenBench:Work: -"), "a drawer's count cut off: %s", key);
    place_key("Untitled 2 - OpenWrite", "OpenWrite", key, sizeof key);
    CHECK(!strcmp(key, "OpenWrite:Untitled"), "a digit cuts, the spaces before it go: %s", key);
    place_key("\"quoted\"", "P", key, sizeof key);
    CHECK(!strcmp(key, "P:"), "a quote cuts: %s", key);
    place_key(NULL, "window", key, sizeof key);
    CHECK(!strcmp(key, "window:"), "no title: %s", key);
    place_key("An exceedingly long window title that goes on", "Prog", key, sizeof key);
    CHECK(!strcmp(key, "Prog:An exceedingly long window tit"), "30 characters of a title: %s", key);
    place_key("OpenPlay \xb7 Boing.mod", "OpenPlay", key, sizeof key);
    CHECK(!strcmp(key, "OpenPlay:OpenPlay \xb7 Boing.mod"), "Latin-1 kept: %s", key);
    place_key("Title", "AVeryLongProgramNameThatFillsTheKeyAlmostEntirelyOnItsOwnXYZ", key, 16);
    CHECK(strlen(key) == 15, "the key is cut at its size: %s", key);

    CHECK(place_candidate(0, 1, NULL), "a drag bar and no title: kept");
    CHECK(place_candidate(0, 0, "Title"), "a title and no drag bar: kept");
    CHECK(!place_candidate(0, 0, ""), "no drag bar and an empty title: a popup, never kept (it was \"window:\")");
    CHECK(!place_candidate(0, 0, NULL), "no drag bar, no title: never");
    CHECK(!place_candidate(1, 1, "Backdrop"), "a backdrop or GimmeZeroZero window: never");

    CHECK(place_workbench("Workbench", 0) && place_workbench("OpenBench", 0) && place_workbench("openbench", 0),
          "Workbench's and OpenBench's windows are the Workbench's");
    CHECK(place_workbench("Anything", 1), "WFLG_WBENCHWINDOW is the Workbench's");
    CHECK(!place_workbench("CON", 0) && !place_workbench("OpenBenchmark", 0), "others are not");

    /* ---- the placing rule ---- */
    memset(&pl, 0, sizeof pl);
    strcpy(pl.key, "CON:AmigaShell"); pl.l = 145; pl.t = 523; pl.w = 853; pl.h = 421;
    {
        short l, t, w = 640, h = 200;
        fit_place(&pl, 1, 64, 24, (short)0xffff, (short)0xffff, 1920, 1200, &l, &t, &w, &h);
        CHECK(l == 145 && t == 523 && w == 853 && h == 421, "sizeable: the kept corner and size (%d %d %d %d)", l, t, w, h);
        w = 640; h = 200;
        fit_place(&pl, 0, 64, 24, 0, 0, 1920, 1200, &l, &t, &w, &h);
        CHECK(l == 145 && t == 523 && w == 640 && h == 200, "no size gadget: the corner only (%d %d %d %d)", l, t, w, h);
        w = 640; h = 200;
        fit_place(&pl, 1, 900, 24, 0, 0, 1920, 1200, &l, &t, &w, &h);
        CHECK(w == 640 && h == 200, "a kept size under its minimum: not given (%d %d)", w, h);
        w = 640; h = 200;
        fit_place(&pl, 1, 64, 24, 800, 0, 1920, 1200, &l, &t, &w, &h);
        CHECK(w == 640, "over its maximum: not given (%d)", w);
        w = 640; h = 200;
        fit_place(&pl, 1, 64, 24, 0, 0, 800, 600, &l, &t, &w, &h);
        CHECK(l == 0 && t == 179 && w == 800 && h == 421, "a smaller screen: kept on it (%d %d %d %d)", l, t, w, h);
    }

    /* ---- the openers ---- */
    {
        static const struct TagItem prog_tags[] = {          /* most programs; OpenBench's drawers; the CON: handler */
            T(WA_Left, 0), T(WA_Top, 50), T(WA_Width, 640), T(WA_Height, 130), T(WA_Title, 0),
            T(WA_MinWidth, 64), T(WA_MinHeight, 24), T(WA_MaxWidth, ~0UL), T(WA_MaxHeight, ~0UL), T(TAG_DONE, 0) };
        static const struct TagItem inner_tags[] = {         /* the CON: handler from guest #472: no size asked */
            T(WA_Left, 0), T(WA_Top, 0), T(WA_InnerWidth, 640), T(WA_InnerHeight, 312),
            T(WA_MinWidth, 64), T(WA_MinHeight, 24), T(WA_MaxWidth, ~0UL), T(WA_MaxHeight, ~0UL), T(TAG_DONE, 0) };
        static const struct TagItem corner_tags[] = { T(WA_Left, 100), T(WA_Top, 100), T(TAG_DONE, 0) };
        static const struct TagItem tail[] = { T(WA_Top, 77), T(TAG_IGNORE, 5), T(WA_Height, 99), T(TAG_DONE, 0) };
        static struct TagItem chained[] = {                  /* a TAG_SKIP over a WA_Left, then TAG_MORE on */
            T(WA_Left, 10), T(TAG_SKIP, 1), T(WA_Left, 999), T(WA_Width, 300), T(TAG_MORE, 0) };
        struct opening o;
        chained[4].ti_Data = (unsigned long)tail;

        memset(&o, 0, sizeof o);
        o.what = "tags (a program, a drawer, the Shell)"; o.tags = prog_tags;
        o.ask.w = 640; o.ask.h = 130; o.ask.sizeable = 1; o.ask.minw = 64; o.ask.minh = 24; o.ask.maxw = o.ask.maxh = (short)0xffff;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 853, 421 });
        {   /* 0.9.1's way, ours in front of the program's: right only where a tag's first wins */
            struct TagItem old[16];
            struct box a, b;
            old[0] = (struct TagItem)T(WA_Left, 145); old[1] = (struct TagItem)T(WA_Top, 523);
            old[2] = (struct TagItem)T(WA_Width, 853); old[3] = (struct TagItem)T(WA_Height, 421);
            old[4] = (struct TagItem)T(TAG_MORE, (unsigned long)prog_tags);
            open_last(NULL, NULL, old, &a);
            open_first(NULL, NULL, old, &b);
            CHECK(a.l == 0 && a.t == 50 && a.w == 640, "0.9.1 on OpenIntuition: the program's corner and size won (the jump)");
            CHECK(b.l == 145 && b.t == 523, "0.9.1 where a tag's first wins: placed");
        }

        o.what = "tags, no size gadget"; o.ask.sizeable = 0;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 640, 130 });

        memset(&o, 0, sizeof o);
        o.what = "by inner size (CON:, ReAction)"; o.tags = inner_tags;
        o.ask.inner = 1; o.ask.sizeable = 1; o.ask.minw = 64; o.ask.minh = 24; o.ask.maxw = o.ask.maxh = (short)0xffff;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 853, 421 });
        o.what = "by inner size, no size gadget: moved, its own size"; o.ask.sizeable = 0;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 640 + BL + BR, 312 + BT + BB });

        memset(&o, 0, sizeof o);
        o.what = "a NewWindow (OpenWindow)"; o.has_nw = 1; o.nw = (struct box){ 100, 100, 400, 300 };
        o.ask.w = 400; o.ask.h = 300; o.ask.sizeable = 1; o.ask.minw = 80; o.ask.minh = 50; o.ask.maxw = o.ask.maxh = (short)0xffff;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 853, 421 });
        o.what = "a NewWindow and tags"; o.tags = corner_tags;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 853, 421 });
        o.what = "an ExtNewWindow"; o.tags = NULL; o.ext = prog_tags;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 853, 421 });
        o.what = "an ExtNewWindow and tags"; o.tags = corner_tags;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 853, 421 });

        memset(&o, 0, sizeof o);
        o.what = "tags chained with TAG_SKIP and TAG_MORE"; o.tags = chained;
        o.ask.w = 300; o.ask.h = 99; o.ask.sizeable = 1; o.ask.maxw = o.ask.maxh = 0;
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 853, 421 });
        o.ask.sizeable = 0; o.what = "chained, no size gadget";
        check_opening(&o, &pl, 1920, 1200, (struct box){ 145, 523, 300, 99 });

        {   /* the copy: every tag of the program's but the placing ones, flattened, in order */
            struct TagItem out[16];
            struct place_plan p = { 1, 2, 3, 4, 0 };
            n = place_tags(out, 16, &p, chained, 1);
            CHECK(n == 4 && out[0].ti_Tag == WA_Left && out[1].ti_Tag == WA_Top && out[2].ti_Tag == WA_Width &&
                  out[2].ti_Data == 300 && out[3].ti_Tag == WA_Height && out[4].ti_Tag == TAG_DONE,
                  "plan, then the program's own without its corner (%d)", n);
            CHECK(place_tags(out, 4, &p, chained, 1) < 0, "too small a buffer: refused");
            CHECK(place_tags_count(chained) == 4 && place_tags_count(NULL) == 0, "counted through TAG_SKIP and TAG_MORE");
        }
        {   /* a TAG_MORE loop: refused, not followed for ever */
            static struct TagItem loop[2];
            loop[0] = (struct TagItem)T(WA_Left, 1);
            loop[1] = (struct TagItem)T(TAG_MORE, (unsigned long)loop);
            CHECK(place_tags_count(loop) < 0, "a list that loops: too long");
        }

        /* the kept corner off the screen's right: kept on it, with the size it will have */
        memset(&o, 0, sizeof o);
        o.what = "off the screen"; o.tags = inner_tags;
        o.ask.inner = 1; o.ask.sizeable = 0;
        {
            struct place far = pl;
            far.l = 1800; far.t = 1100;
            check_opening(&o, &far, 1920, 1200, (struct box){ 1920 - 853, 1200 - 421, 640 + BL + BR, 312 + BT + BB });
        }
    }

    /* ---- the table ---- */
    n = 0;
    for (i = 0; i < PLACES_MAX; i++) {
        char k[16];
        int x;
        snprintf(k, sizeof k, "p:%d", i);
        x = place_index(table, &n, k, 1);
        table[x].used = (unsigned long)(i + 10);
    }
    CHECK(n == PLACES_MAX, "full: %d", n);
    table[5].used = 1;                                      /* used longest ago */
    i = place_index(table, &n, "new:one", 1);
    CHECK(i == 5 && n == PLACES_MAX && !strcmp(table[5].key, "new:one"), "full: the one used longest ago makes way (%d)", i);
    CHECK(place_index(table, &n, "new:one", 0) == 5 && place_index(table, &n, "no:such", 0) == -1, "found, and not");

    CHECK(place_parse("\"CON:AmigaShell\" 145 523 853 421", &pl) && !strcmp(pl.key, "CON:AmigaShell") &&
          pl.l == 145 && pl.t == 523 && pl.w == 853 && pl.h == 421, "a line read");
    CHECK(place_parse("\"window:\" 0 638 1920 100", &pl) && !strcmp(pl.key, "window:"), "an empty title read");
    CHECK(!place_parse("; a comment", &pl) && !place_parse("\"bad\" 1 2 0 4", &pl) && !place_parse("\"open 1 2 3 4", &pl),
          "a comment, no size, no closing quote: not places");

    /* the file's order: used longest ago first, so reading it back (used = the line's number) keeps which came last */
    {
        struct place t3[4];
        memset(t3, 0, sizeof t3);
        t3[0].used = 40; t3[1].used = 7; t3[2].used = 40; t3[3].used = 1;
        place_order(t3, 4, order);
        CHECK(order[0] == 3 && order[1] == 1 && order[2] == 0 && order[3] == 2, "oldest first, ties as they were (%d %d %d %d)",
              order[0], order[1], order[2], order[3]);
    }

    printf("test_places: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
