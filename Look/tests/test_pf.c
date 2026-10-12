/* Host tests for the Appearance tabs' PREF reader and writer (pf_iff.c, pf_prefs.c).
 *   Look/tests/run.sh
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "pf_iff.h"
#include "pf_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

static const char *dir;

static uint8_t *load(const char *name, long *len)
{
    char path[512];
    FILE *f;
    uint8_t *p;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "rb"))) { fprintf(stderr, "can't read %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)*len);
    if (fread(p, 1, (size_t)*len, f) != (size_t)*len) { fprintf(stderr, "short %s\n", path); exit(1); }
    fclose(f);
    return p;
}

/* the file's bytes (malloc'd) and their length */
static uint8_t *bytes_of(const pf_file *f, long *len)
{
    uint8_t *p = malloc((size_t)pf_size(f));
    *len = pf_write(f, p, pf_size(f));
    return p;
}

static int same_as(const pf_file *f, const uint8_t *b, long len)
{
    long n;
    uint8_t *p = bytes_of(f, &n);
    int same = n == len && !memcmp(p, b, (size_t)len);
    free(p);
    return same;
}

/* how many bytes differ (the lengths the same), or -1 */
static long differing(const pf_file *f, const uint8_t *b, long len)
{
    long n, i, d = 0;
    uint8_t *p = bytes_of(f, &n);
    if (n != len) { free(p); return -1; }
    for (i = 0; i < len; i++) d += p[i] != b[i];
    free(p);
    return d;
}

static void parse_sample(pf_file *f, const char *name, uint8_t **b, long *len)
{
    *b = load(name, len);
    pf_init(f);
    CHECK(pf_parse(f, *b, *len), "%s parses", name);
}

/* ---- the list --------------------------------------------------------------------------------------- */

static void round_trips(void)
{
    static const char *const names[] = {
        "sample-font.prefs", "sample-asl.prefs", "sample-asl-default.prefs", "sample-workbench.prefs",
        "sample-workbench-default.prefs", "sample-wbpattern-openup.prefs", "sample-wbpattern-classic.prefs",
        "sample-palette-default.prefs", "sample-font-helvetica13-classic.prefs"
    };
    unsigned k;
    for (k = 0; k < sizeof names / sizeof names[0]; k++) {
        pf_file f, g;
        uint8_t *b;
        long len;
        parse_sample(&f, names[k], &b, &len);
        CHECK(f.n >= 2 && f.ch[0].id == PF_PRHD && f.ch[0].len == 6, "%s: PRHD first", names[k]);
        CHECK(same_as(&f, b, len), "%s writes back byte for byte", names[k]);
        pf_init(&g);
        CHECK(pf_copy(&g, &f) && pf_same(&f, &g), "%s copies", names[k]);
        pf_free(&f); pf_free(&g); free(b);
    }
}

static void damaged(void)
{
    static const uint8_t ilbm[12] = { 'F', 'O', 'R', 'M', 0, 0, 0, 4, 'I', 'L', 'B', 'M' };
    static const uint8_t empty[12] = { 'F', 'O', 'R', 'M', 0, 0, 0, 4, 'P', 'R', 'E', 'F' };
    static const uint8_t truncated[20] = { 'F', 'O', 'R', 'M', 0, 0, 0, 20, 'P', 'R', 'E', 'F', 'P', 'R', 'H', 'D', 0, 0, 0, 6 };
    static const uint8_t past[22] = { 'F', 'O', 'R', 'M', 0, 0, 0, 14, 'P', 'R', 'E', 'F', 'P', 'R', 'H', 'D', 0, 0, 0, 9, 0, 0 };
    static const uint8_t nested[24] = { 'F', 'O', 'R', 'M', 0, 0, 0, 16, 'P', 'R', 'E', 'F',
                                        'F', 'O', 'R', 'M', 0, 0, 0, 4, 'P', 'R', 'E', 'F' };
    static const uint8_t badid[20] = { 'F', 'O', 'R', 'M', 0, 0, 0, 12, 'P', 'R', 'E', 'F', 'P', 0, 'H', 'D', 0, 0, 0, 0 };
    pf_file f;
    pf_init(&f);
    CHECK(!pf_parse(&f, ilbm, sizeof ilbm) && f.n == 0, "an ILBM isn't a PREF");
    CHECK(!pf_parse(&f, (const uint8_t *)"FORM", 4), "too short");
    CHECK(!pf_parse(&f, truncated, sizeof truncated), "the FORM is longer than the file");
    CHECK(!pf_parse(&f, past, sizeof past), "a chunk runs past the FORM");
    CHECK(!pf_parse(&f, nested, sizeof nested), "a FORM inside is refused");
    CHECK(!pf_parse(&f, badid, sizeof badid), "an ID that isn't printable");
    CHECK(pf_parse(&f, empty, sizeof empty) && f.n == 0, "an empty PREF");
    pf_free(&f);
}

static void list(void)
{
    pf_file f, g;
    uint8_t out[64];
    long n;
    pf_init(&f); pf_init(&g);
    CHECK(pf_new(&f) && f.n == 1 && pf_find(&f, PF_PRHD, 0) == 0, "new: PRHD");
    CHECK(pf_add(&f, PF_ID('A', 'B', 'C', 'D'), "xyz", 3), "add");
    CHECK(pf_insert(&f, 1, PF_ID('E', 'F', 'G', 'H'), NULL, 2) && f.ch[1].data[0] == 0 && f.ch[2].len == 3, "insert zeros");
    CHECK(pf_size(&f) == 12 + 14 + 10 + 12, "an odd chunk is padded: %ld", pf_size(&f));
    n = pf_write(&f, out, sizeof out);
    CHECK(n == 48 && out[47] == 0 && !memcmp(out + 36, "ABCD", 4) && out[43] == 3, "written with its pad");
    CHECK(pf_write(&f, out, 40) == -1, "too small");
    CHECK(pf_parse(&g, out, n) && pf_same(&f, &g), "and read back");
    CHECK(pf_find(&f, PF_ID('A', 'B', 'C', 'D'), 1) == -1, "no second one");
    pf_remove(&f, 1);
    CHECK(f.n == 2 && f.ch[1].id == PF_ID('A', 'B', 'C', 'D') && !pf_same(&f, &g), "remove");
    CHECK(pf_set(&f, 1, "q", 1) && f.ch[1].len == 1 && f.ch[1].data[0] == 'q', "set");
    pf_free(&f); pf_free(&g);
}

/* ---- font.prefs ------------------------------------------------------------------------------------- */

static void fonts(void)
{
    pf_file f, g;
    pf_font wb, sys, scr, t;
    uint8_t *b;
    long len;
    parse_sample(&f, "sample-font.prefs", &b, &len);
    CHECK(pf_font_get(&f, PF_WBFONT, &wb) && !strcmp(wb.name, "CGTriumvirate.font") && wb.ysize == 13 && wb.front == 1 &&
          wb.back == 2 && wb.drawmode == 0 && wb.flags == 2, "icon font: %s %d", wb.name, wb.ysize);
    CHECK(pf_font_get(&f, PF_SYSFONT, &sys) && !strcmp(sys.name, "topaz.font") && sys.ysize == 8 && sys.drawmode == 1 && sys.flags == 1,
          "system font: %s %d", sys.name, sys.ysize);
    CHECK(pf_font_get(&f, PF_SCREENFONT, &scr) && !strcmp(scr.name, "CGTriumvirate.font") && scr.ysize == 13, "screen font");
    CHECK(pf_font_put(&f, &wb) && pf_font_put(&f, &sys) && pf_font_put(&f, &scr) && same_as(&f, b, len), "put back unchanged: the same bytes");
    t = scr;
    strcpy(t.name, "topaz.font"); t.ysize = 8; t.flags = 1;
    CHECK(pf_font_put(&f, &t), "screen font changed");
    CHECK(differing(&f, b, len) > 0 && differing(&f, b, len) <= 2 + 1 + 18, "only its size, flags and name: %ld bytes", differing(&f, b, len));
    CHECK(!memcmp(f.ch[1].data, b + 34, PF_FONT_LEN) && !memcmp(f.ch[2].data, b + 34 + 164, PF_FONT_LEN), "the other two as they were");
    CHECK(pf_font_get(&f, PF_SCREENFONT, &t) && !strcmp(t.name, "topaz.font") && t.ysize == 8, "read back");
    /* the screen font put back as it was gives the file's bytes again */
    CHECK(pf_font_put(&f, &scr) && same_as(&f, b, len), "and back again");
    /* a new file gets them in the OS's order */
    pf_init(&g);
    pf_new(&g);
    CHECK(pf_font_put(&g, &scr) && pf_font_put(&g, &wb) && pf_font_put(&g, &sys) && same_as(&g, b, len),
          "a new file, fonts put in any order, is the OS's file");
    /* bytes after a name's end are kept while the name stays */
    f.ch[1].data[28 + 19] = 'x';
    pf_font_get(&f, PF_WBFONT, &t);
    CHECK(!strcmp(t.name, "CGTriumvirate.font"), "the name ends at its 0");
    CHECK(pf_font_put(&f, &t) && f.ch[1].data[28 + 19] == 'x', "kept");
    strcpy(t.name, "XEN.font");
    CHECK(pf_font_put(&f, &t) && !strcmp((char *)f.ch[1].data + 28, "XEN.font") && f.ch[1].data[28 + 9] == 'r' &&
          f.ch[1].data[28 + 19] == 'x', "a new name over the old, the bytes after its 0 as they were (as Font copies it)");
    pf_free(&f); pf_free(&g); free(b);
}

/* OS 3.2.3's Font, from sample-font.prefs, Screen Text set to helvetica 13 in its window and Use: the same
 * change made here gives its file byte for byte (ASL gives both the flags 0x62). */
static void fonts_as_classic(void)
{
    pf_file f;
    pf_font scr;
    uint8_t *b, *want;
    long len, wlen;
    parse_sample(&f, "sample-font.prefs", &b, &len);
    want = load("sample-font-helvetica13-classic.prefs", &wlen);
    pf_font_get(&f, PF_SCREENFONT, &scr);
    strcpy(scr.name, "helvetica.font"); scr.ysize = 13; scr.style = 0; scr.flags = 0x62;
    CHECK(pf_font_put(&f, &scr) && same_as(&f, want, wlen), "Font's own file for helvetica 13: %ld bytes differ", differing(&f, want, wlen));
    pf_free(&f); free(b); free(want);
}

/* ---- palette.prefs ---------------------------------------------------------------------------------- */

static void palette(void)
{
    pf_file f, g;
    pf_palette p, q;
    uint8_t *b, rgb[3], c3[3] = { 0x10, 0x20, 0x30 };
    long len;
    int k;
    b = load("sample-palette-default.prefs", &len);
    pf_init(&f);
    pf_new(&f);
    CHECK(!pf_palette_get(&f, &p), "no PALT");
    CHECK(pf_palette_put(&f, &p) && same_as(&f, b, len), "the defaults are what Palette writes with no file: %ld", differing(&f, b, len));
    CHECK(pf_palette_get(&f, &q) && !memcmp(&p, &q, sizeof p), "read back");
    CHECK(pf_palette_colour(&q, 3, rgb) && rgb[0] == 0x66 && rgb[1] == 0x88 && rgb[2] == 0xbb, "colour 3");
    CHECK(!pf_palette_colour(&q, 8, rgb), "no colour 8");
    CHECK(pf_palette_pen(q.pens4, 5) == 3 && pf_palette_pen(q.pens8, 7) == 0 && pf_palette_pen(q.pens4, 12) == 2 &&
          pf_palette_pen(q.pens4, 13) == -1, "pens");
    CHECK(pf_palette_set_colour(&q, 3, c3) && pf_palette_put(&f, &q) && differing(&f, b, len) == 6, "one colour: six bytes");
    CHECK(pf_palette_set_pen(q.pens8, 5, 6) && !pf_palette_set_pen(q.pens8, 13, 1), "a pen; none past the end");
    CHECK(pf_palette_put(&f, &q) && differing(&f, b, len) == 7, "and a pen: one more");
    CHECK(pf_palette_set_colour(&q, 8, c3) && q.col[8].index == 8 && q.col[9].index == -1, "a ninth colour, the end after it");
    for (k = 9; k < 40; k++) pf_palette_set_colour(&q, k, c3);
    CHECK(q.col[30].index == 30 && q.col[31].index == -1, "31 colours and the end");
    CHECK(!pf_palette_set_colour(&q, 31, c3), "no room for a 32nd");
    pf_init(&g);
    CHECK(pf_parse(&g, b, len) && pf_palette_get(&g, &p) && pf_palette_put(&g, &p) && same_as(&g, b, len), "Palette's own file put back");
    pf_free(&f); pf_free(&g); free(b);
}

/* ---- wbpattern.prefs -------------------------------------------------------------------------------- */

static void wbpattern(void)
{
    pf_file f, g;
    pf_ptrn r, d, s;
    uint8_t *b, *o;
    long len, olen;
    const char *root = "SYS:Prefs/Presets/Backdrops/OpenUp/Graphite-light-chrome-logo-1600x1200.png";
    parse_sample(&f, "sample-wbpattern-classic.prefs", &b, &len);
    CHECK(pf_ptrn_get(&f, PF_WBP_ROOT, &r) && r.picture && !strcmp(r.name, root) && r.flags == 0x3a00 && r.datalen == 75, "root: %s", r.name);
    CHECK(pf_ptrn_get(&f, PF_WBP_DRAWER, &d) && d.picture && !strcmp(d.name, "Sys:Prefs/Presets/Backdrops/pattern.iff") && d.flags == 0x0500, "drawers");
    CHECK(pf_ptrn_get(&f, PF_WBP_SCREEN, &s) && !s.picture && s.datalen == 96, "screen: a pattern");
    CHECK(pf_ptrn_set_picture(&f, PF_WBP_ROOT, root, 0x3a00) && same_as(&f, b, len), "the same picture: the same bytes");
    CHECK(pf_ptrn_set_flags(&f, PF_WBP_ROOT, 0x3a10) && differing(&f, b, len) == 1, "remap off: one byte");
    pf_ptrn_set_flags(&f, PF_WBP_ROOT, 0x3a00);
    /* the root picture as WBPattern writes it, made afresh, is the chunk WBPattern wrote */
    pf_init(&g);
    pf_new(&g);
    CHECK(pf_ptrn_set_picture(&g, PF_WBP_DRAWER, d.name, d.flags) && pf_ptrn_set_picture(&g, PF_WBP_ROOT, root, 0x3a00), "afresh");
    CHECK(g.n == 3 && g.ch[1].len == f.ch[1].len && !memcmp(g.ch[1].data, f.ch[1].data, f.ch[1].len) &&
          g.ch[2].len == f.ch[2].len && !memcmp(g.ch[2].data, f.ch[2].data, f.ch[2].len), "in WBPattern's order, its bytes");
    CHECK(pf_ptrn_set_picture(&f, PF_WBP_ROOT, "SYS:Prefs/Presets/Backdrops/pattern.iff", 0x1000), "a new root picture");
    CHECK(pf_ptrn_get(&f, PF_WBP_ROOT, &r) && r.datalen == 39 && f.ch[1].len == 24 + 39 && r.flags == 0x1000, "its name without a 0");
    CHECK(pf_ptrn_get(&f, PF_WBP_DRAWER, &d) && pf_ptrn_get(&f, PF_WBP_SCREEN, &s) && s.datalen == 96, "the others as they were");
    pf_ptrn_remove(&f, PF_WBP_SCREEN);
    CHECK(!pf_ptrn_get(&f, PF_WBP_SCREEN, &s) && f.n == 3, "none for the screen");
    /* OpenUp's file names the picture with its 0: read the same, and kept while the picture stays */
    pf_free(&g);
    parse_sample(&g, "sample-wbpattern-openup.prefs", &o, &olen);
    CHECK(pf_ptrn_get(&g, PF_WBP_ROOT, &r) && !strcmp(r.name, root) && r.datalen == 76, "OpenUp's root");
    CHECK(pf_ptrn_set_picture(&g, PF_WBP_ROOT, root, 0x3a00) && same_as(&g, o, olen), "kept");
    pf_free(&f); pf_free(&g); free(b); free(o);
}

/* ---- workbench.prefs -------------------------------------------------------------------------------- */

static void workbench(void)
{
    pf_file f, g;
    pf_wbnc w, t;
    char title[128];
    uint8_t *b, *dflt;
    long len, dlen;
    parse_sample(&f, "sample-workbench.prefs", &b, &len);
    CHECK(pf_wbnc_get(&f, &w) && w.stack == 4096 && w.restart == 3 && w.precision == 16 && w.emboss[0] == -1 && w.emboss[3] == 1 &&
          w.borderless == 1 && w.maxname == 25 && w.newicons == 1 && w.coloricons == 1 && w.nogauge == 1 && w.titledelay == 50 &&
          !w.notitlebar && !w.copybuf && !w.flags, "fields");
    CHECK(pf_wbtf_get(&f, title, sizeof title) && strstr(title, "OpenBench 0.1 - Chip %mcek KB") != NULL, "title: %s", title);
    CHECK(pf_wbnc_put(&f, &w) && pf_wbtf_put(&f, title) && same_as(&f, b, len), "put back unchanged");
    t = w; t.borderless = 0;
    CHECK(pf_wbnc_put(&f, &t) && differing(&f, b, len) == 1, "borders: one byte");
    t.flags = PF_WBF_OLDDATESFIRST | 1;
    CHECK(pf_wbnc_put(&f, &t) && differing(&f, b, len) == 3, "the flags");
    CHECK(pf_wbtf_put(&f, "") && pf_find(&f, PF_WBTF, 0) < 0, "no title format: the OS's");
    CHECK(pf_wbtf_put(&f, "Workbench %wv") && f.ch[2].id == PF_WBTF && f.ch[2].len == 14 && f.ch[2].data[13] == 0, "a new one, with its 0");
    /* a chunk no tab knows (hidden devices) is carried */
    CHECK(pf_add(&f, PF_WBHD, "DF1:", 5) && pf_wbnc_put(&f, &w) && pf_find(&f, PF_WBHD, 0) == 3 && f.ch[3].len == 5, "WBHD carried");
    /* no file: the editor's defaults are what Workbench writes */
    dflt = load("sample-workbench-default.prefs", &dlen);
    pf_init(&g);
    pf_new(&g);
    CHECK(!pf_wbnc_get(&g, &t) && pf_wbnc_put(&g, &t) && same_as(&g, dflt, dlen), "defaults: %ld", differing(&g, dflt, dlen));
    /* a 30-byte chunk (before 3.9) grows only for a later field */
    pf_set(&g, 1, g.ch[1].data, PF_WBNC_BASIC);
    CHECK(pf_wbnc_get(&g, &t) && pf_wbnc_put(&g, &t) && g.ch[1].len == PF_WBNC_BASIC, "stays 30 bytes");
    t.nogauge = 1;
    CHECK(pf_wbnc_put(&g, &t) && g.ch[1].len == PF_WBNC_LEN && pf_wbnc_get(&g, &w) && w.nogauge == 1, "grows to 50");
    pf_free(&f); pf_free(&g); free(b); free(dflt);
}

/* The icon frame and the hidden devices, against what OS 3.2.3's Workbench editor wrote in a lab, each
 * from sample-workbench.prefs: Border Size Small, Medium, Large, then No Border, with DH1: and DF0: hidden. */
static void workbench_as_classic(void)
{
    static const struct { const char *file; int frame, n; const char *names[2]; } c[] = {
        { "sample-workbench-small-dh1.prefs", 1, 1, { "DH1:", NULL } },
        { "sample-workbench-medium-dh1.prefs", 2, 1, { "DH1:", NULL } },
        { "sample-workbench-large-dh1-df0.prefs", 3, 2, { "DH1:", "DF0:" } },
        { "sample-workbench-none-dh1-df0.prefs", 0, 2, { "DH1:", "DF0:" } },
    };
    unsigned k;
    char name[32];
    for (k = 0; k < sizeof c / sizeof c[0]; k++) {
        pf_file f, want;
        pf_wbnc w;
        uint8_t *b, *wb;
        long len, wlen;
        parse_sample(&f, "sample-workbench.prefs", &b, &len);
        parse_sample(&want, c[k].file, &wb, &wlen);
        CHECK(pf_wbnc_get(&f, &w) && pf_wbnc_frame(&w) == 0 && pf_wbhd_get(&f, 0, name, sizeof name) == 0, "%s: no border, none hidden", c[k].file);
        pf_wbnc_set_frame(&w, c[k].frame);
        CHECK(pf_wbnc_put(&f, &w) && pf_wbhd_set(&f, c[k].names, c[k].n) && same_as(&f, wb, wlen), "%s: %ld bytes differ", c[k].file,
              differing(&f, wb, wlen));
        CHECK(pf_wbnc_get(&want, &w) && pf_wbnc_frame(&w) == c[k].frame && pf_wbhd_is(&want, "dh1:") && !pf_wbhd_is(&want, "RAM:") &&
              pf_wbhd_get(&want, c[k].n - 1, name, sizeof name) && !strcmp(name, c[k].names[c[k].n - 1]), "%s read back", c[k].file);
        free(b); free(wb); pf_free(&f); pf_free(&want);
    }
    {
        pf_file f;
        pf_wbnc w;
        uint8_t *b;
        long len;
        static const char *const two[2] = { "df0:", "DH1:" }, *const one[1] = { "CD0:" };
        memset(&w, 0, sizeof w);
        parse_sample(&f, "sample-workbench-large-dh1-df0.prefs", &b, &len);
        CHECK(pf_wbhd_set(&f, two, 2) && same_as(&f, b, len), "the same devices in another order and case: the file as it is");
        CHECK(pf_wbhd_set(&f, one, 1) && pf_find(&f, PF_WBHD, 0) == 2 && pf_find(&f, PF_WBHD, 1) < 0 && f.ch[3].id == PF_WBTF &&
              f.ch[2].len == 5, "one device in place of two, where they were");
        CHECK(pf_wbhd_set(&f, NULL, 0) && pf_find(&f, PF_WBHD, 0) < 0 && f.n == 3, "none hidden");
        w.emboss[0] = -2; w.emboss[1] = -1; w.emboss[2] = 2; w.emboss[3] = 2; w.borderless = 0;
        CHECK(pf_wbnc_frame(&w) == -1, "a frame the editor doesn't offer");
        free(b); pf_free(&f);
    }
}

/* ---- asl.prefs -------------------------------------------------------------------------------------- */

static void asl(void)
{
    pf_file f, g;
    pf_asl a;
    uint8_t *b, *dflt;
    long len, dlen;
    parse_sample(&f, "sample-asl.prefs", &b, &len);
    CHECK(pf_asl_get(&f, &a) && a.sortby == 0 && a.sizepos == 1 && a.width == 0 && a.height == 0, "fields");
    CHECK(pf_asl_put(&f, &a) && same_as(&f, b, len), "unchanged");
    a.sortby = 2; a.left = -5;
    CHECK(pf_asl_put(&f, &a) && differing(&f, b, len) == 3, "sort by size, left -5: three bytes");
    CHECK(pf_asl_get(&f, &a) && a.sortby == 2 && a.left == -5, "read back");
    dflt = load("sample-asl-default.prefs", &dlen);
    pf_init(&g);
    pf_new(&g);
    CHECK(!pf_asl_get(&g, &a) && pf_asl_put(&g, &a) && same_as(&g, dflt, dlen), "defaults are what Asl writes");
    pf_free(&f); pf_free(&g); free(b); free(dflt);
}

/* ---- pointer.prefs ---------------------------------------------------------------------------------- */

/* A PNTR chunk: 16 x 4, depth 2, hot spot (x, y), plane 0 and 1 rows as given. */
static void pointer_chunk(pf_file *f, int which, int x, int y, const uint16_t p0[4], const uint16_t p1[4], uint32_t cut)
{
    uint8_t d[32 + 9 + 16];
    int k;
    memset(d, 0, sizeof d);
    pf_put16(d + 16, (uint16_t)which);
    pf_put16(d + 18, 5);
    pf_put16(d + 20, 16);
    pf_put16(d + 22, 4);
    pf_put16(d + 24, 2);
    pf_put16(d + 28, (uint16_t)x);
    pf_put16(d + 30, (uint16_t)y);
    d[32] = 0xe0; d[33] = 0x40; d[34] = 0x40; d[38] = 0xe0; d[39] = 0xe0; d[40] = 0xc0;
    for (k = 0; k < 4; k++) { pf_put16(d + 41 + 2 * k, p0[k]); pf_put16(d + 49 + 2 * k, p1[k]); }
    pf_add(f, PF_PNTR, d, sizeof d - cut);
}

static void pointer(void)
{
    static const uint16_t a0[4] = { 0x8000, 0xc000, 0xe000, 0xf000 }, a1[4] = { 0x0000, 0x4000, 0x6000, 0x7000 };
    static const uint16_t z[4] = { 0, 0, 0, 0 }, full[4] = { 0xffff, 0xffff, 0xffff, 0xffff };
    pf_file f, set;
    pf_pntr p;
    pf_init(&f); pf_init(&set);
    pf_new(&f);
    pointer_chunk(&f, 1, 0, 0, z, full, 0);
    pointer_chunk(&f, 0, -1, -1, a0, a1, 0);
    CHECK(pf_pntr_get(&f, 0, &p) && p.width == 16 && p.height == 4 && p.depth == 2 && p.ncolours == 3 && p.x == -1 && p.y == -1, "normal");
    CHECK(p.rgb[0] == 0xe0 && p.rgb[8] == 0xc0, "its colours");
    CHECK(pf_pntr_pixel(&p, 0, 0) == 1 && pf_pntr_pixel(&p, 1, 1) == 3 && pf_pntr_pixel(&p, 4, 3) == 0 && pf_pntr_pixel(&p, 3, 3) == 3 &&
          pf_pntr_pixel(&p, 99, 0) == 0, "pixels");
    CHECK(pf_pntr_get(&f, 1, &p) && pf_pntr_pixel(&p, 15, 3) == 2, "busy");
    CHECK(!pf_pntr_get(&f, 7, &p), "no type 7");
    CHECK(pf_pntr_set_hotspot(&f, 0, -3, -2) && pf_pntr_get(&f, 0, &p) && p.x == -3 && p.y == -2, "hot spot");
    pointer_chunk(&f, 9, 0, 0, a0, a1, 3);
    CHECK(!pf_pntr_get(&f, 9, &p), "a chunk too short for its planes is not taken as one");
    pf_remove(&f, 3);
    /* a set: types 5 and 0 */
    pf_new(&set);
    pointer_chunk(&set, 5, -2, -2, full, z, 0);
    pointer_chunk(&set, 0, 0, 0, full, full, 0);
    CHECK(pf_pntr_take(&f, &set, 5) && f.n == 4 && pf_get16(f.ch[1].data + 16) == 5 && pf_get16(f.ch[2].data + 16) == 1,
          "one type from a set, in the OS's order (last type first)");
    CHECK(!pf_pntr_take(&f, &set, 3), "a type the set hasn't");
    CHECK(pf_pntr_take_all(&f, &set) && f.n == 3 && f.ch[0].id == PF_PRHD && pf_get16(f.ch[1].data + 16) == 5 &&
          pf_get16(f.ch[2].data + 16) == 0 && pf_pntr_get(&f, 0, &p) && pf_pntr_pixel(&p, 0, 0) == 3, "the whole set");
    pf_free(&f); pf_free(&set);
}

/* A PNTR chunk as OS 3.2's sets have them: 16 x 24, depth 2, size 5, the classic colours, all clear. */
static void pointer_chunk24(pf_file *f, int which)
{
    uint8_t d[32 + 9 + 96];
    memset(d, 0, sizeof d);
    pf_put16(d + 16, (uint16_t)which);
    pf_put16(d + 18, 5);
    pf_put16(d + 20, 16);
    pf_put16(d + 22, 24);
    pf_put16(d + 24, 2);
    d[32] = 0xe0; d[33] = 0x40; d[34] = 0x40; d[38] = 0xe0; d[39] = 0xe0; d[40] = 0xc0;
    pf_add(f, PF_PNTR, d, sizeof d);
}

/* Drawing, against what OS 3.2.3's Pointer editor wrote in a lab (its file isn't kept here: the OS's pointers
 * are Hyperion's): colour 1's red 0xe0 to 0xe1 changed byte 32 of every PNTR chunk; pixel 10, 20 of Normal
 * in colour 2 changed byte 130 of Normal's chunk from 0x00 to 0x20 (plane 1, row 20, second byte, bit 2). */
static void pointer_drawing(void)
{
    static const uint8_t red[3] = { 0xe1, 0x40, 0x40 };
    pf_file f, was;
    pf_pntr p;
    int i, k, changed = 0;
    pf_init(&f); pf_init(&was);
    pf_new(&f);
    pointer_chunk24(&f, 40);
    pointer_chunk24(&f, 1);
    pointer_chunk24(&f, 16 | 0x4000);
    pointer_chunk24(&f, 0);
    pf_copy(&was, &f);
    CHECK(pf_pntr_set_colour(&f, 1, red), "colour 1");
    for (i = 1; i < f.n; i++)
        CHECK(f.ch[i].data[32] == 0xe1 && f.ch[i].data[33] == 0x40 && f.ch[i].data[38] == 0xe0, "colour 1 in chunk %d", i);
    CHECK(pf_pntr_set_pixel(&f, 0, 10, 20, 2), "a pixel");
    for (i = 1; i < f.n; i++)
        for (k = 0; k < (int)f.ch[i].len; k++)
            if (f.ch[i].data[k] != was.ch[i].data[k]) changed++;
    CHECK(f.ch[4].data[130] == 0x20 && changed == 4 + 1, "byte 130 of Normal only, and the four red bytes: %d", changed);
    CHECK(pf_pntr_get(&f, 0, &p) && pf_pntr_pixel(&p, 10, 20) == 2 && pf_pntr_pixel(&p, 11, 20) == 0, "read back");
    CHECK(pf_pntr_set_pixel(&f, 0, 10, 20, 3) && pf_pntr_get(&f, 0, &p) && pf_pntr_pixel(&p, 10, 20) == 3 && f.ch[4].data[82] == 0x20,
          "colour 3: plane 0 too");
    CHECK(pf_pntr_set_pixel(&f, 0, 10, 20, 0) && f.ch[4].data[130] == 0 && f.ch[4].data[82] == 0, "clear again");
    CHECK(!pf_pntr_set_pixel(&f, 0, 16, 0, 1) && !pf_pntr_set_pixel(&f, 0, 0, 24, 1) && !pf_pntr_set_pixel(&f, 0, 0, 0, 4) &&
          !pf_pntr_set_pixel(&f, 7, 0, 0, 1), "outside it, too deep a colour, no such pointer");
    CHECK(!pf_pntr_set_colour(&f, 4, red) && !pf_pntr_set_colour(&f, 0, red), "only colours 1 to 3");
    pf_pntr_set_pixel(&f, 1, 0, 0, 3);
    CHECK(pf_pntr_clear(&f, 1) && pf_pntr_get(&f, 1, &p) && pf_pntr_pixel(&p, 0, 0) == 0 && f.ch[2].data[32] == 0xe1, "clear: the colours kept");
    pf_free(&f); pf_free(&was);
}

int main(int argc, char **argv)
{
    dir = argc > 1 ? argv[1] : ".";
    round_trips();
    damaged();
    list();
    fonts();
    fonts_as_classic();
    palette();
    wbpattern();
    workbench();
    workbench_as_classic();
    asl();
    pointer();
    pointer_drawing();
    if (fails) { fprintf(stderr, "%d of %d checks failed\n", fails, checks); return 1; }
    printf("pf: all %d checks passed\n", checks);
    return 0;
}
