/* Host tests for tb_prefs.h (ENV:OpenPrefs/TitleBar) and tb_fade.h (the see-through bar's fade, and the pointer's
 * part in it).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "tb_prefs.h"
#include "tb_fade.h"
#include <stdio.h>
#include <string.h>

static int checks, failed;
#define CHECK(x) do { checks++; if (!(x)) { failed++; printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)

static void parse(tb_prefs *p, const char *text)
{
    static char buf[1024];
    strcpy(buf, text);
    tb_parse(p, buf);
}

static void prefs_tests(void)
{
    tb_prefs p, q;
    char out[256];
    int n;
    /* no file: the defaults; the see-through bar is on at 40% */
    tb_parse(&p, NULL);
    CHECK(p.seethrough == 1 && p.see_opacity == 40);
    CHECK(p.logo && p.memory && p.clock && p.date && p.network && p.cog && !p.border_black);
    /* a file written before 0.7.5 (no seethrough lines): on, at 40% */
    parse(&p, "; OpenPrefs Title bar 0.2: what OpenTitle shows\nlogo off\nmemory on\nclock on\nclock.date off\n"
              "network on\ncog on\nborder.black on\n");
    CHECK(!p.logo && p.memory && !p.date && p.border_black);
    CHECK(p.seethrough == 1 && p.see_opacity == 40);
    /* switched off; another level */
    parse(&p, "seethrough off\nseethrough.opacity 60\n");
    CHECK(p.seethrough == 0 && p.see_opacity == 60);
    /* the level kept to 10-100; not a number keeps the default */
    parse(&p, "seethrough.opacity 3\n");
    CHECK(p.see_opacity == 10);
    parse(&p, "seethrough.opacity 250\n");
    CHECK(p.see_opacity == 100);
    parse(&p, "seethrough.opacity lots\n");
    CHECK(p.see_opacity == 40);
    /* "seethrough" is not taken for "seethrough.opacity", nor the other way */
    parse(&p, "seethrough.opacity 70\n");
    CHECK(p.seethrough == 1 && p.see_opacity == 70);
    /* what the editor writes reads back the same, and fits its 256 bytes */
    tb_defaults(&p);
    p.logo = 0; p.border_black = 1; p.seethrough = 0; p.see_opacity = 55;
    n = tb_text(&p, out, sizeof out);
    CHECK(n > 0 && n < (int)sizeof out);
    CHECK(strstr(out, "\nseethrough off\n") && strstr(out, "\nseethrough.opacity 55\n"));
    parse(&q, out);
    CHECK(!memcmp(&p, &q, sizeof p));
    /* an older reader skips the new lines: each is "name value" on its own line, after the old ones */
    CHECK(strstr(out, "border.black on\nseethrough off\n") != NULL);
}

/* steps of dt ms with hot until the level is to, or max ms pass; answers the ms it took (-1: never) */
static int run(tb_fade *f, int dt, int hot, int to, int max)
{
    int t = 0;
    while (t <= max) {
        if (f->level == to) return t;
        tbf_step(f, dt, hot);
        t += dt;
    }
    return -1;
}

static void fade_tests(void)
{
    tb_fade f;
    int t, lvl, prev, i, wakes, ms;
    const int rest = 40 * 255 / 100;                       /* 102: tb_prefs' default */

    /* at rest: the rest level, and nothing to wake for */
    tbf_init(&f, rest, 0);
    CHECK(f.level == rest && tbf_next(&f) == 0);
    tbf_step(&f, 500, 0);
    CHECK(f.level == rest && tbf_next(&f) == 0);
    /* started with the pointer on the bar: solid at once */
    tbf_init(&f, rest, 1);
    CHECK(f.level == 255 && tbf_next(&f) == 0);

    /* the pointer comes: solid in about 150 ms, in steps of 40 ms, each one up */
    tbf_init(&f, rest, 0);
    CHECK(tbf_next(&f) == 0);
    tbf_step(&f, 0, 1);                                    /* the tick that sees it */
    CHECK(tbf_next(&f) == TBF_STEP_MS);
    prev = f.level;
    for (t = 0, i = 0; f.level < 255 && i < 20; i++) {
        lvl = tbf_step(&f, TBF_STEP_MS, 1);
        CHECK(lvl > prev);
        prev = lvl;
        t += TBF_STEP_MS;
    }
    CHECK(f.level == 255 && t >= 120 && t <= 160);
    CHECK(tbf_next(&f) == 0);                              /* solid while hot: the caller's own tick sees it leave */

    /* it leaves: solid for the 300 ms grace from the step that sees it gone, then back to rest in about 300 ms */
    tbf_step(&f, 100, 0);
    CHECK(f.level == 255 && tbf_next(&f) == TBF_GRACE_MS);
    tbf_step(&f, 100, 0);
    CHECK(f.level == 255 && tbf_next(&f) == TBF_GRACE_MS - 100);
    tbf_step(&f, 200, 0);
    CHECK(f.level == 255);                                 /* exactly the grace: not yet */
    CHECK(tbf_next(&f) == TBF_STEP_MS);
    prev = f.level;
    for (t = 0, i = 0; f.level > rest && i < 30; i++) {
        lvl = tbf_step(&f, TBF_STEP_MS, 0);
        CHECK(lvl < prev);
        prev = lvl;
        t += TBF_STEP_MS;
    }
    CHECK(f.level == rest && t >= 280 && t <= 320);
    CHECK(tbf_next(&f) == 0);

    /* a gap that crosses the grace counts only its part after the grace towards the fade out */
    tbf_init(&f, rest, 1);
    tbf_step(&f, 0, 0);
    tbf_step(&f, 280, 0);
    CHECK(f.level == 255);
    tbf_step(&f, 40, 0);                                   /* 20 ms of grace, 20 of fading */
    CHECK(f.level < 255 && f.level >= 255 - (255 - rest) * 30 / TBF_OUT_MS);

    /* back while fading out: turns round from where it is, no jump */
    tbf_init(&f, rest, 1);
    tbf_step(&f, 0, 0);
    tbf_step(&f, TBF_GRACE_MS, 0);
    tbf_step(&f, 120, 0);
    lvl = f.level;
    CHECK(lvl < 255 && lvl > rest);
    tbf_step(&f, 0, 1);
    CHECK(f.level == lvl);
    tbf_step(&f, TBF_STEP_MS, 1);
    CHECK(f.level > lvl && f.level - lvl <= (255 - rest) * TBF_STEP_MS / TBF_IN_MS + 1);
    CHECK(run(&f, TBF_STEP_MS, 1, 255, 200) >= 0);

    /* gone while fading in: it finishes going solid within the grace, then fades out */
    tbf_init(&f, rest, 0);
    tbf_step(&f, 500, 1);                                  /* the wait before the step that sees it isn't hot time */
    CHECK(f.level == rest);
    tbf_step(&f, TBF_STEP_MS, 1);
    CHECK(f.level > rest && f.level < 255);
    CHECK(run(&f, TBF_STEP_MS, 0, 255, TBF_GRACE_MS) >= 0);
    CHECK(run(&f, TBF_STEP_MS, 0, rest, TBF_GRACE_MS + TBF_OUT_MS + 2 * TBF_STEP_MS) >= 0);

    /* a menu (hot, the pointer elsewhere) keeps it solid however long */
    tbf_init(&f, rest, 1);
    for (i = 0; i < 100; i++) tbf_step(&f, 100, 1);
    CHECK(f.level == 255);

    /* a long gap (the task held up) counts as at most TBF_DT_MAX; a negative one as none */
    tbf_init(&f, rest, 1);
    tbf_step(&f, 0, 0);
    tbf_step(&f, 100000, 0);
    CHECK(f.level == rest);
    tbf_init(&f, rest, 0);
    tbf_step(&f, -50, 1);
    CHECK(f.level == rest);

    /* never outside rest..255, whatever comes */
    tbf_init(&f, rest, 0);
    {
        unsigned r = 12345;
        int ok = 1;
        for (i = 0; i < 20000; i++) {
            r = r * 1103515245u + 12345u;
            tbf_step(&f, (int)(r >> 8) % 700 - 50, (r >> 20) & 1);
            if (f.level < rest || f.level > 255 || f.cool < 0 || f.cool > TBF_GRACE_MS) ok = 0;
        }
        CHECK(ok);
    }

    /* opacity 100% (rest 255): always solid, nothing to wake for; 10% (the least): fades as well */
    tbf_init(&f, 255, 0);
    tbf_step(&f, 40, 1); tbf_step(&f, 1000, 0);
    CHECK(f.level == 255 && tbf_next(&f) == 0);
    tbf_init(&f, 10 * 255 / 100, 0);
    CHECK(run(&f, TBF_STEP_MS, 1, 255, 200) >= 0);

    /* OpenTitle's loop over a hover: how many wakes the bar adds. The loop wakes every 100 ms while the pointer is
     * on the bar and every 500 ms otherwise (as before 0.7.5), sooner only when tbf_next asks. The pointer is on
     * the bar from 1000 ms to 2000 ms. */
    tbf_init(&f, rest, 0);
    for (t = 0, ms = 0, wakes = 0; t < 5000; t += ms) {
        int hot = t >= 1000 && t < 2000, n, plain;
        tbf_step(&f, ms, hot);                             /* each wake: the time since the last, and the pointer now */
        plain = hot ? 100 : 500;
        n = tbf_next(&f);
        ms = n > 0 && n < plain ? (n < 20 ? 20 : n) : plain;
        if (ms != plain) wakes++;
        if (t < 1000 || t >= 4000) CHECK(ms == plain);    /* at rest: the loop's own pace */
    }
    printf("     a hover of 1 s adds %d wakes to the loop's own (the fade's steps, the grace's end); none at rest\n", wakes);
    CHECK(wakes > 0 && wakes <= 16);
    CHECK(f.level == rest && tbf_next(&f) == 0);
}

/* tbp_on_bar (OpenTitle 0.7.7): the pointer where the boot left it, on the bar, doesn't make the bar solid until it
 * has moved; then it counts as before */
static void pointer_tests(void)
{
    tb_ptr p = { 0, 0, 0, 0 };
    tb_fade f;
    const int rest = 40 * 255 / 100;
    int i;
    /* Instance-32 after a boot: the pointer at 0,0, on the 24-row bar, never moved */
    CHECK(!tbp_on_bar(&p, 0, 0, 1));                      /* the first look */
    for (i = 0; i < 50; i++) CHECK(!tbp_on_bar(&p, 0, 0, 1));
    /* the fade with it: the bar stays at rest */
    tbf_init(&f, rest, tbp_on_bar(&p, 0, 0, 1));
    for (i = 0; i < 20; i++) tbf_step(&f, 500, tbp_on_bar(&p, 0, 0, 1));
    CHECK(f.level == rest && tbf_next(&f) == 0);
    /* moved off the bar: still not on it */
    CHECK(!tbp_on_bar(&p, 900, 700, 0));
    /* back onto the bar: now it counts, and the bar goes solid */
    CHECK(tbp_on_bar(&p, 600, 10, 1));
    tbf_step(&f, 0, 1);
    for (i = 0; i < 10; i++) tbf_step(&f, TBF_STEP_MS, tbp_on_bar(&p, 600, 10, 1));
    CHECK(f.level == 255);
    /* even back at 0,0, now that it has moved */
    CHECK(tbp_on_bar(&p, 0, 0, 1));
    /* and off again: no longer, and the bar goes back to rest */
    CHECK(!tbp_on_bar(&p, 900, 700, 0));
    for (i = 0; i < 40; i++) tbf_step(&f, TBF_STEP_MS, tbp_on_bar(&p, 900, 700, 0));
    CHECK(f.level == rest);

    /* a move along the bar alone (x only) counts as a move */
    p = (tb_ptr){ 0, 0, 0, 0 };
    CHECK(!tbp_on_bar(&p, 0, 0, 1));
    CHECK(tbp_on_bar(&p, 1, 0, 1));
    /* so does one in y only */
    p = (tb_ptr){ 0, 0, 0, 0 };
    CHECK(!tbp_on_bar(&p, 5, 5, 1));
    CHECK(tbp_on_bar(&p, 5, 6, 1));
    /* a pointer first seen off the bar that is moved onto it counts at once */
    p = (tb_ptr){ 0, 0, 0, 0 };
    CHECK(!tbp_on_bar(&p, 960, 600, 0));
    CHECK(tbp_on_bar(&p, 960, 10, 1));
}

int main(void)
{
    prefs_tests();
    fade_tests();
    pointer_tests();
    printf("%s   %d checks%s\n", failed ? "FAIL" : "ok", checks, failed ? ", some failed" : " (settings, fade, pointer)");
    return failed != 0;
}
