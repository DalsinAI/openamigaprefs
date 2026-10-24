/* Host tests for the Printing section's fields (pr_prefs.c), on pf_iff's list.
 *   Printing/tests/run.sh
 * The two samples are the files openamigaup's OpenPrinter part writes where there are none (tools/build_pack.py
 * printer_prefs() and printergfx_prefs(), openamigaup main 350d03a, with #161 merged).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "pf_iff.h"
#include "pr_prefs.h"

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
    uint8_t *b;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "rb"))) { fprintf(stderr, "can't read %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)*len);
    if (!b || fread(b, 1, (size_t)*len, f) != (size_t)*len) { fprintf(stderr, "can't read %s\n", path); exit(1); }
    fclose(f);
    return b;
}

/* f's bytes, as pf_write writes them */
static uint8_t *bytes(const pf_file *f, long *len)
{
    uint8_t *b;
    *len = pf_size(f);
    b = malloc((size_t)*len);
    CHECK(b && pf_write(f, b, *len) == *len, "write");
    return b;
}

/* f written and parsed again */
static void round_trip(const pf_file *f, pf_file *back)
{
    long n;
    uint8_t *b = bytes(f, &n);
    pf_init(back);
    CHECK(pf_parse(back, b, n), "parse");
    free(b);
}

static void parse_sample(pf_file *f, const char *name)
{
    long n;
    uint8_t *b = load(name, &n);
    pf_init(f);
    CHECK(pf_parse(f, b, n), "parse %s", name);
    free(b);
}

/* 1 when f's bytes are the sample's */
static int same_as(const pf_file *f, const char *name)
{
    long n, m;
    uint8_t *a = bytes(f, &n), *b = load(name, &m);
    int same = n == m && !memcmp(a, b, (size_t)n);
    free(a);
    free(b);
    return same;
}

/* With no file: OpenPrinter's files (OpenPrint installed), else printer.device's no-file state */
static void test_new(void)
{
    pf_file t, g, none;
    pr_prefs p, q;
    char route[96];
    pf_init(&t);
    pf_init(&g);
    pf_init(&none);
    CHECK(pr_new_txt(&t, 1) && pr_new_gfx(&g, 1), "new, with OpenPrint");
    CHECK(same_as(&t, "openprinter-printer.prefs"), "printer.prefs: OpenPrinter's own bytes");
    CHECK(same_as(&g, "openprinter-printergfx.prefs"), "printergfx.prefs: OpenPrinter's own bytes");
    CHECK(pr_get(&t, &g, &p) == 3, "both chunks found");
    CHECK(p.paper == PR_A4 && p.lines == 66 && p.left == 5 && p.right == 75 && p.aspect == PR_HORIZONTAL && p.density == 2,
          "OpenPrinter's settings: A4, 66 lines, 5 to 75, horizontal, density 2 (%d %d %d %d %d %d)",
          p.paper, p.lines, p.left, p.right, p.aspect, p.density);
    pr_route(&t, route, sizeof route);
    CHECK(!strcmp(route, "OpenPrint driver, oapspool.device unit 0"), "the route: %s", route);

    /* no file, no OpenPrint: printer.device's no-file state, the same with no file at all */
    CHECK(pr_get(NULL, NULL, &q) == 0, "nothing found");
    CHECK(q.paper == PR_US_LETTER && q.lines == 66 && q.left == 5 && q.right == 75 && q.aspect == PR_HORIZONTAL &&
          q.density == 1, "printer.device's no-file settings");
    CHECK(pr_get(&none, &none, &q) == 0 && q.paper == PR_US_LETTER && q.density == 1, "an empty file: the same");
    CHECK(pr_new_txt(&t, 0) && pr_new_gfx(&g, 0), "new, without OpenPrint");
    CHECK(t.n == 2 && t.ch[1].id == PR_PTXT && t.ch[1].len == PR_PTXT_LEN, "PRHD and PTXT only: no PUNT, no PDEV");
    CHECK(g.n == 2 && g.ch[1].id == PR_PGFX && g.ch[1].len == PR_PGFX_LEN, "PRHD and PGFX");
    CHECK(!memcmp(t.ch[1].data + 16, "generic\0", 8) && t.ch[1].data[46] == 0, "the generic driver, the parallel port");
    {
        static const uint8_t want[PR_PGFX_LEN] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0, 0,0, 0,0, 0,7, 0,0,0,0, 0,0,
                                                   1,0, 0,80, 0,110, 0,0 };
        CHECK(!memcmp(g.ch[1].data, want, PR_PGFX_LEN), "PGFX: threshold 7, density 1, 80 x 110");
    }
    CHECK(pr_get(&t, &g, &p) == 3 && !memcmp(&p, &q, sizeof p), "the new files read as no file");
    pr_route(&t, route, sizeof route);
    CHECK(!strcmp(route, "generic driver, parallel.device unit 0"), "the route: %s", route);
    pf_free(&t);
    pf_free(&g);
}

/* OpenPrinter's files, the page's settings put in, written, read back */
static void test_round_trip(void)
{
    pf_file t, g, t2, g2, t0, g0;
    pr_prefs p, r;
    int i;
    parse_sample(&t, "openprinter-printer.prefs");
    parse_sample(&g, "openprinter-printergfx.prefs");
    pf_init(&t0);
    pf_init(&g0);
    pf_copy(&t0, &t);
    pf_copy(&g0, &g);
    pr_get(&t, &g, &p);
    CHECK(pr_put(&t, &g, &p), "put unchanged");
    CHECK(same_as(&t, "openprinter-printer.prefs") && same_as(&g, "openprinter-printergfx.prefs"),
          "settings put back unchanged: the files' own bytes");
    p.paper = PR_A3;
    p.lines = 80;
    p.left = 10;
    p.right = 70;
    p.aspect = PR_VERTICAL;
    p.density = 5;
    CHECK(pr_put(&t, &g, &p), "put");
    round_trip(&t, &t2);
    round_trip(&g, &g2);
    CHECK(pr_get(&t2, &g2, &r) == 3 && !memcmp(&p, &r, sizeof p), "read back: %d %d %d %d %d %d",
          r.paper, r.lines, r.left, r.right, r.aspect, r.density);
    /* only the fields' bytes changed: the driver, port, paper type, pitch, spacing and quality as they were */
    CHECK(t2.n == 4 && t2.ch[1].len == PR_PTXT_LEN, "PRHD, PTXT, PUNT, PDEV");
    for (i = 0; i < PR_PTXT_LEN; i++)
        if (i < 50 || i >= 62) CHECK(t2.ch[1].data[i] == t0.ch[1].data[i], "PTXT byte %d kept", i);
    {
        static const uint8_t want[12] = { 0,8, 0,80, 0,0, 0,0, 0,10, 0,70 };   /* A3, 80 lines, pitch, spacing, 10, 70 */
        CHECK(!memcmp(t2.ch[1].data + 50, want, 12), "PTXT's fields' bytes");
    }
    CHECK(t2.ch[2].len == t0.ch[2].len && !memcmp(t2.ch[2].data, t0.ch[2].data, t0.ch[2].len), "PUNT kept whole");
    CHECK(t2.ch[3].len == t0.ch[3].len && !memcmp(t2.ch[3].data, t0.ch[3].data, t0.ch[3].len), "PDEV kept whole");
    for (i = 0; i < PR_PGFX_LEN; i++)
        if (i != 16 && i != 17 && i != 30) CHECK(g2.ch[1].data[i] == g0.ch[1].data[i], "PGFX byte %d kept", i);
    CHECK(g2.ch[1].data[16] == 0 && g2.ch[1].data[17] == 1 && g2.ch[1].data[30] == 5, "PGFX: vertical, density 5");
    pf_free(&t);
    pf_free(&g);
    pf_free(&t2);
    pf_free(&g2);
    pf_free(&t0);
    pf_free(&g0);
}

/* A chunk this section doesn't know stays where it was with its bytes; a longer PTXT keeps its length and tail */
static void test_unknown(void)
{
    pf_file t, b;
    pr_prefs p;
    uint8_t xtra[5] = { 1, 2, 3, 4, 5 }, longer[70];
    int k;
    parse_sample(&t, "openprinter-printer.prefs");
    CHECK(pf_insert(&t, 1, PF_ID('X', 'T', 'R', 'A'), xtra, sizeof xtra), "an odd-length chunk before PTXT");
    memset(longer, 0xab, sizeof longer);
    memcpy(longer, t.ch[2].data, PR_PTXT_LEN);
    CHECK(pf_set(&t, 2, longer, sizeof longer), "a longer PTXT");
    CHECK(pf_add(&t, PF_ID('P', 'P', 'S', 'X'), "later", 5), "a chunk at the end");
    pr_get(&t, NULL, &p);
    p.left = 12;
    CHECK(pr_put(&t, NULL, &p), "put");
    round_trip(&t, &b);
    CHECK(b.n == 6 && b.ch[1].id == PF_ID('X', 'T', 'R', 'A') && b.ch[1].len == 5 && !memcmp(b.ch[1].data, xtra, 5),
          "XTRA kept in its place");
    CHECK(b.ch[2].id == PR_PTXT && b.ch[2].len == sizeof longer && b.ch[2].data[69] == 0xab && b.ch[2].data[64] == 0xab,
          "PTXT's length and tail kept");
    CHECK(b.ch[2].data[58] == 0 && b.ch[2].data[59] == 12, "the left margin");
    CHECK(b.ch[5].id == PF_ID('P', 'P', 'S', 'X') && b.ch[5].len == 5 && !memcmp(b.ch[5].data, "later", 5), "PPSX kept");
    k = pf_find(&b, PR_PGFX, 0);
    CHECK(k < 0, "no PGFX added to printer.prefs");
    pf_free(&t);
    pf_free(&b);
}

/* A file with no PTXT or PGFX gets one, as printer.device takes a missing one; a short one grows with zeros */
static void test_missing_and_short(void)
{
    pf_file t, g;
    pr_prefs p;
    uint8_t s[54];
    pf_init(&t);
    pf_init(&g);
    pf_new(&t);
    pf_new(&g);
    CHECK(pf_add(&t, PR_PUNT, NULL, PR_PUNT_LEN), "a printer.prefs with PUNT only");
    CHECK(pr_get(&t, &g, &p) == 0, "neither chunk");
    p.paper = PR_A5;
    p.density = 3;
    CHECK(pr_put(&t, &g, &p), "put");
    CHECK(t.n == 3 && t.ch[2].id == PR_PTXT && t.ch[2].len == PR_PTXT_LEN && !memcmp(t.ch[2].data + 16, "generic", 7) &&
          t.ch[2].data[51] == PR_A5, "PTXT added after PUNT, the generic driver");
    CHECK(g.n == 2 && g.ch[1].id == PR_PGFX && g.ch[1].len == PR_PGFX_LEN && g.ch[1].data[30] == 3 && g.ch[1].data[23] == 7,
          "PGFX added, the threshold printer.device has with none");
    /* a PTXT cut short after the paper length: the margins read as zero (printer.device's way), so fixed */
    memcpy(s, t.ch[2].data, sizeof s);
    pf_set(&t, 2, s, sizeof s);
    pr_get(&t, NULL, &p);
    CHECK(p.paper == PR_A5 && p.lines == 66 && p.left == 1 && p.right == 1, "short: %d %d %d %d", p.paper, p.lines, p.left, p.right);
    p.left = 4;
    p.right = 60;
    CHECK(pr_put(&t, NULL, &p) && t.ch[2].len == PR_PTXT_LEN && t.ch[2].data[59] == 4 && t.ch[2].data[61] == 60 &&
          t.ch[2].data[63] == 0, "grown to 64 bytes, zeros past the old end");
    pf_free(&t);
    pf_free(&g);
}

static void test_ranges_and_names(void)
{
    pr_prefs p = { 20, 0, 0, -3, 7, 0 };
    int seen[PR_PAPERS], i;
    pr_fix(&p);
    CHECK(p.paper == PR_A4 && p.lines == 1 && p.left == 1 && p.right == 1 && p.aspect == PR_HORIZONTAL && p.density == 1,
          "low: %d %d %d %d %d %d", p.paper, p.lines, p.left, p.right, p.aspect, p.density);
    p.lines = 5000; p.left = 40; p.right = 30; p.density = 9; p.aspect = PR_VERTICAL;
    pr_fix(&p);
    CHECK(p.lines == PR_LINES_MAX && p.left == 40 && p.right == 40 && p.density == PR_DENSITY_MAX && p.aspect == PR_VERTICAL,
          "high: %d %d %d %d", p.lines, p.left, p.right, p.density);
    p.left = 2000; p.right = 3000;
    pr_fix(&p);
    CHECK(p.left == PR_COLUMN_MAX && p.right == PR_COLUMN_MAX, "columns");

    memset(seen, 0, sizeof seen);
    for (i = 0; i < PR_PAPERS; i++) seen[pr_paper_order[i]]++;
    for (i = 0; i < PR_PAPERS; i++) CHECK(seen[i] == 1, "paper %d listed once", i);
    CHECK(pr_paper_order[0] == PR_A4, "A4 first");
    CHECK(!strcmp(pr_paper_name(PR_US_LETTER), "US Letter") && !strcmp(pr_paper_name(PR_A4), "A4") &&
          !strcmp(pr_paper_name(99), "A4"), "names");
    CHECK(pr_paper_named("a4") == PR_A4 && pr_paper_named("Letter") == PR_US_LETTER && pr_paper_named("LEGAL") == PR_US_LEGAL &&
          pr_paper_named("us letter") == PR_US_LETTER && pr_paper_named("A8") == PR_A8 && pr_paper_named("custom") == PR_CUSTOM &&
          pr_paper_named("A9") < 0 && pr_paper_named("") < 0 && pr_paper_named(NULL) < 0, "the Shell's words");
    CHECK(pr_paper_openprint(PR_A4) && pr_paper_openprint(PR_A3) && pr_paper_openprint(PR_A5) &&
          pr_paper_openprint(PR_US_LETTER) && pr_paper_openprint(PR_US_LEGAL) && !pr_paper_openprint(PR_A0) &&
          !pr_paper_openprint(PR_N_TRACTOR), "the sizes OpenPrint's driver makes");
    CHECK(pr_aspect_named("horizontal") == PR_HORIZONTAL && pr_aspect_named("VERTICAL") == PR_VERTICAL &&
          pr_aspect_named("sideways") < 0 && pr_aspect_named(NULL) < 0, "the aspect's words");
    CHECK(pr_density_dpi(1) == 72 && pr_density_dpi(2) == 100 && pr_density_dpi(5) == 300 && pr_density_dpi(7) == 1200 &&
          pr_density_dpi(0) == 72, "OpenPrint's dots to the inch");
}

static void test_route(void)
{
    pf_file t;
    char route[96];
    uint8_t u[PR_PUNT_LEN];
    pr_new_txt(&t, 0);
    t.ch[1].data[46] = 1;                     /* PP_SERIAL */
    pr_route(&t, route, sizeof route);
    CHECK(!strcmp(route, "generic driver, serial.device unit 0"), "serial: %s", route);
    memset(u, 0, sizeof u);
    u[19] = 3;
    memcpy(u + 24, "printtofile.device", 18);
    pf_add(&t, PR_PUNT, u, sizeof u);
    memcpy(t.ch[1].data + 16, "EpsonQ\0", 7);
    pr_route(&t, route, sizeof route);
    CHECK(!strcmp(route, "EpsonQ driver, printtofile.device unit 3"), "a named device, \".device\" not doubled: %s", route);
    pf_free(&t);
}

int main(int argc, char **argv)
{
    dir = argc > 1 ? argv[1] : ".";
    test_new();
    test_round_trip();
    test_unknown();
    test_missing_and_short();
    test_ranges_and_names();
    test_route();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails != 0;
}
