/* Host tests for the Screen mode section's fields (dp_prefs.c), on pf_iff's list.
 *   Display/tests/run.sh
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "pf_iff.h"
#include "dp_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

/* f written and parsed again */
static void round_trip(const pf_file *f, pf_file *back)
{
    long n = pf_size(f);
    uint8_t *buf = malloc((size_t)n);
    CHECK(pf_write(f, buf, n) == n, "write");
    pf_init(back);
    CHECK(pf_parse(back, buf, n), "parse");
    free(buf);
}

static void test_mode(void)
{
    pf_file f, g;
    dp_mode m, r;
    pf_init(&f);
    CHECK(pf_new(&f), "new");
    CHECK(!dp_mode_get(&f, &m), "no SCRM yet");
    CHECK(m.id == DP_NOFILE_ID && m.width == DP_SIZE_DEFAULT && m.height == DP_SIZE_DEFAULT && m.depth == 2,
          "IPrefs' own default");
    m.id = 0x00029004UL;                   /* PAL high res laced */
    m.width = 640; m.height = DP_SIZE_DEFAULT; m.depth = 3; m.autoscroll = 1;
    CHECK(dp_mode_put(&f, &m), "put");
    CHECK(f.n == 2 && f.ch[1].len == DP_SCRM_LEN, "one SCRM of 28 bytes after PRHD");
    {
        static const uint8_t want[28] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0x00,0x02,0x90,0x04, 0x02,0x80, 0xff,0xff, 0,3, 0,1 };
        CHECK(!memcmp(f.ch[1].data, want, 28), "SCRM's bytes");
    }
    round_trip(&f, &g);
    CHECK(dp_mode_get(&g, &r) && r.id == m.id && r.width == 640 && r.height == DP_SIZE_DEFAULT && r.depth == 3 && r.autoscroll,
          "read back");
    /* reserved bytes and a longer chunk's tail stay */
    {
        uint8_t odd[32];
        memset(odd, 0, sizeof odd);
        odd[3] = 7; odd[27] = 0x82; odd[31] = 9;   /* a reserved long, a Control bit we don't know, a tail */
        pf_set(&g, 1, odd, sizeof odd);
        r.autoscroll = 1;
        CHECK(dp_mode_put(&g, &r), "put over");
        CHECK(g.ch[1].len == 32 && g.ch[1].data[3] == 7 && g.ch[1].data[27] == 0x83 && g.ch[1].data[31] == 9,
              "other bytes kept, autoscroll set beside the unknown bit");
        r.autoscroll = 0;
        dp_mode_put(&g, &r);
        CHECK(g.ch[1].data[27] == 0x82, "autoscroll cleared, the unknown bit kept");
    }
    pf_free(&f);
    pf_free(&g);
}

static void test_oscan(void)
{
    pf_file f, g;
    dp_oscan o, p, r;
    pf_init(&f);
    pf_new(&f);
    CHECK(dp_native(0x00029004UL) && dp_native(0x00008000UL) && dp_native(0x00031004UL) && dp_native(0x000a9004UL),
          "PAL, the default monitor, VGA and DblPAL are the chipset's");
    CHECK(!dp_native(0x50001000UL) && !dp_native(0x40120000UL) && !dp_native(0xffffffffUL), "RTG and INVALID_ID aren't");
    CHECK(!dp_oscan_get(&f, 0x00029004UL, &o) && o.id == 0x00029004UL, "no OSCN yet");
    o.hstart = 0x81; o.hstop = 0x1c1; o.vstart = 0x2c; o.vstop = 0x12c;
    o.viewx = 129; o.viewy = 44; o.textw = 640; o.texth = 256;
    o.std.minx = -16; o.std.miny = -8; o.std.maxx = 687; o.std.maxy = 271;
    CHECK(dp_oscan_put(&f, &o), "put PAL");
    memset(&p, 0, sizeof p);
    p.id = 0x00019004UL; p.textw = 640; p.texth = 200; p.std.maxx = 703; p.std.maxy = 240;
    CHECK(dp_oscan_put(&f, &p), "put NTSC");
    CHECK(f.n == 3, "two OSCN chunks, one per monitor");
    CHECK(pf_get32(f.ch[1].data + 4) == DP_OSCAN_MAGIC, "the magic");
    round_trip(&f, &g);
    CHECK(dp_oscan_get(&g, 0x00021000UL, &r), "found by another PAL mode");
    CHECK(r.id == 0x00029004UL && r.hstart == 0x81 && r.vstop == 0x12c && r.viewx == 129 && r.textw == 640 &&
          r.texth == 256 && r.std.minx == -16 && r.std.miny == -8 && r.std.maxx == 687 && r.std.maxy == 271,
          "PAL read back, negative edges too");
    r.textw = 704;
    dp_oscan_put(&g, &r);
    CHECK(g.n == 3 && dp_oscan_get(&g, 0x00029004UL, &r) && r.textw == 704, "replaced in place");
    dp_oscan_remove(&g, 0x00011000UL);
    CHECK(g.n == 2 && dp_oscan_find(&g, 0x00019004UL) < 0 && dp_oscan_find(&g, 0x00029004UL) >= 0, "NTSC's removed, PAL's kept");
    pf_free(&f);
    pf_free(&g);
}

int main(void)
{
    test_mode();
    test_oscan();
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails != 0;
}
