/* Host tests for the IControl section's fields (ic_prefs.c), on pf_iff's list.
 *   IControl/tests/run.sh
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "pf_iff.h"
#include "ic_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

int main(void)
{
    pf_file f, g;
    ic_prefs p, r;
    long n;
    uint8_t *buf;
    pf_init(&f);
    pf_new(&f);
    CHECK(!ic_get(&f, &p) && p.timeout == 50 && p.metadrag == IC_Q_LCOMMAND && !memcmp(p.keys, "NMVB", 4), "the defaults");
    p.timeout = 100;
    p.metadrag = IC_Q_LCOMMAND | IC_Q_LSHIFT;
    p.flags = IC_COERCE_LACE | IC_MODEPROMOTE;
    memcpy(p.keys, "NMYZ", 4);
    CHECK(ic_put(&f, &p), "put");
    CHECK(f.n == 2 && f.ch[1].len == IC_ICTL_LEN, "one ICTL of 28 bytes");
    {
        static const uint8_t want[28] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,100, 0,0x41, 0,0,0,0x12, 'N','M','Y','Z' };
        CHECK(!memcmp(f.ch[1].data, want, 28), "ICTL's bytes");
    }
    n = pf_size(&f);
    buf = malloc((size_t)n);
    pf_write(&f, buf, n);
    pf_init(&g);
    CHECK(pf_parse(&g, buf, n), "parse");
    free(buf);
    CHECK(ic_get(&g, &r) && r.timeout == 100 && r.metadrag == 0x41 && r.flags == p.flags && !memcmp(r.keys, "NMYZ", 4), "read back");
    {
        /* OS 3.2's longer chunk: the version, hover and geometry fields after the first 28 bytes stay */
        uint8_t big[38];
        memset(big, 0, sizeof big);
        memcpy(big, g.ch[1].data, 28);
        big[20] = 0x80; big[29] = 2; big[33] = 0x5a; big[37] = 1;   /* ICF_VERSIONED, ic_Version 2, hover, a pointer */
        pf_set(&g, 1, big, sizeof big);
        ic_get(&g, &r);
        r.flags |= IC_MENUSNAP;
        CHECK(ic_put(&g, &r), "put over");
        CHECK(g.ch[1].len == 38 && g.ch[1].data[20] == 0x80 && g.ch[1].data[23] == 0x1a && g.ch[1].data[29] == 2 &&
              g.ch[1].data[33] == 0x5a && g.ch[1].data[37] == 1, "3.2's fields kept, the versioned bit too");
    }
    pf_free(&f);
    pf_free(&g);
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails != 0;
}
