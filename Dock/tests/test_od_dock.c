/* Host tests for od_dock.c. MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "od_dock.h"
#include "od_wake.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;
#define CHECK(x) do { checks++; if (!(x)) { failed++; printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)

static char *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) { printf("can't open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) exit(1);
    b[n] = 0;
    fclose(f);
    if (len) *len = n;
    return b;
}

static od_dock d, e;
static char buf[32768];

/* a button's place by its label, or -1 */
static int find(const od_dock *x, const char *label)
{
    for (int i = 0; i < x->n; i++)
        if (!strcmp(x->b[i].label, label)) return i;
    return -1;
}

/* a machine without Clock, Calculator or the trashcan */
static int fake_exists(const char *path)
{
    return strcmp(path, "SYS:Utilities/Clock") && strcmp(path, "SYS:Tools/Calculator") && strcmp(path, "SYS:Trashcan");
}

static void test_drop_missing(void)
{
    od_defaults(&d);
    od_starter(&d);
    /* the starter's own length, whatever it holds now (the Open apps joined it on 10 October) */
    int n0 = d.n;
    /* three programs go, then the separator that was left at the end */
    CHECK(od_drop_missing(&d, fake_exists) == 3);
    CHECK(d.n == n0 - 4 && d.b[0].kind == OD_CLI && !strcmp(d.b[2].label, "OpenView"));
    CHECK(find(&d, "Clock") < 0 && find(&d, "Calculator") < 0 && find(&d, "Trashcan") < 0);
    CHECK(d.b[d.n - 1].kind != OD_SEPARATOR && !strcmp(d.b[d.n - 1].label, "Preferences")
          && !strcmp(d.b[d.n - 1].command, "SYS:Preferences/OpenPrefs") && !strcmp(d.b[d.n - 2].label, "Prefs"));
    CHECK(od_drop_missing(&d, fake_exists) == 0 && d.n == n0 - 4);
}

static void test_own_format(void)
{
    od_defaults(&d);
    od_starter(&d);
    int n0 = d.n, clock = find(&d, "Clock");
    CHECK(n0 >= 9 && d.b[0].kind == OD_CLI && !strcmp(d.b[0].label, "Shell") && d.b[1].kind == OD_WB);
    CHECK(clock > 2 && !strcmp(d.b[clock].command, "SYS:Utilities/Clock") && !strcmp(d.b[clock + 1].command, "SYS:Tools/Calculator"));
    CHECK(d.b[n0 - 2].kind == OD_SEPARATOR && !strcmp(d.b[n0 - 1].label, "Trashcan") && !strcmp(d.b[n0 - 1].command, "SYS:Trashcan"));
    CHECK(!strcmp(d.b[1].label, "OpenFiles") && !strcmp(d.b[2].command, "SYS:Utilities/OpenPrint/OpenView"));
    /* ADD: once only, whatever the case, at the end */
    CHECK(od_ensure(&d, "sys:utilities/openfiles") == 0 && d.n == n0);
    CHECK(od_ensure(&d, "SYS:Utilities/MultiView") == 1 && d.n == n0 + 1 && !strcmp(d.b[n0].label, "MultiView") && d.b[n0].kind == OD_WB);
    CHECK(od_ensure(&d, "SYS:Utilities/MultiView") == 0 && d.n == n0 + 1);
    CHECK(d.background == OD_BG_GLASS && d.hover == 1 && d.hop == 1);
    d.place = OD_LEFT; d.size = OD_LARGE; d.labels = 1; d.running = 0; d.scale = 50; d.border = 0;
    d.background = OD_BG_CLEAR; d.hover = 0; d.hop = 0; d.opacity = 35;
    strcpy(d.imported, "ENVARC:ToolManager.prefs");
    d.b[1].kind = OD_SEPARATOR;
    d.b[2].kind = OD_CLI; strcpy(d.b[2].command, "Echo \"hi\" \\ there"); strcpy(d.b[2].dir, "RAM:"); d.b[2].stack = 20000;
    strcpy(d.b[6].icon, "SYS:Prefs/Look");
    CHECK(od_write(&d, buf, sizeof buf) > 0);
    od_parse(&e, buf);
    CHECK(e.place == OD_LEFT && e.size == OD_LARGE && e.labels == 1 && e.running == 0);
    CHECK(e.scale == 50 && e.border == 0);
    CHECK(e.background == OD_BG_CLEAR && e.hover == 0 && e.hop == 0 && e.opacity == 35);
    CHECK(!strcmp(e.imported, d.imported));
    CHECK(e.n == d.n && e.b[1].kind == OD_SEPARATOR);
    CHECK(e.b[2].kind == OD_CLI && !strcmp(e.b[2].command, d.b[2].command) && !strcmp(e.b[2].dir, "RAM:") && e.b[2].stack == 20000);
    CHECK(!strcmp(e.b[6].icon, "SYS:Prefs/Look") && !strcmp(e.b[6].label, d.b[6].label));
    CHECK(od_write(&d, buf, 40) == -1);
    od_parse(&e, "; comment\nplace nowhere\nsize huge\nfuture 7\nbutton telepathy \"x\"\nbutton wb \"Work:Thing\"\n");
    CHECK(e.place == OD_BOTTOM && e.size == OD_LARGE && e.n == 1 && !strcmp(e.b[0].label, "Thing"));
    CHECK(e.scale == 100 && e.border == 1 && e.opacity == 35);
    /* the opacity: a file from before it keeps its look; out of range is clamped */
    od_parse(&e, "background solid\n");
    CHECK(e.background == OD_BG_SOLID && e.opacity == 100);
    od_parse(&e, "opacity 20\nbackground solid\n");
    CHECK(e.opacity == 20);
    od_parse(&e, "background glass\nopacity 140\n");
    CHECK(e.opacity == 100);
    od_parse(&e, "opacity -5\n");
    CHECK(e.opacity == 0);
    od_parse(&e, "item-size 70\nborders off\n");
    CHECK(e.scale == 75 && e.border == 0 && od_scaled(64, 25) == 16);
    CHECK(!od_parse(&e, NULL) && e.n == 0);
    CHECK(od_cell(OD_SMALL) == 40 && od_cell(OD_LARGE) == 72);
}

static void test_toolmanager(const char *dir)
{
    char path[512], err[120];
    long len;
    unsigned char *data;
    od_report r;
    snprintf(path, sizeof path, "%s/sample-ToolManager.prefs", dir);
    data = (unsigned char *)slurp(path, &len);
    CHECK(od_sniff(data, len) == 1);
    od_defaults(&d);
    CHECK(od_from_toolmanager(&d, data, len, &r, err, sizeof err));
    CHECK(r.added == 3 && d.n == 3);
    CHECK(d.b[0].kind == OD_CLI && !strcmp(d.b[0].label, "Shell") && !strcmp(d.b[0].command, "NewShell") && !strcmp(d.b[0].dir, "SYS:"));
    CHECK(!strcmp(d.b[0].icon, "SYS:System/Shell"));         /* the image, without .info */
    CHECK(d.b[1].kind == OD_WB && !strcmp(d.b[1].command, "SYS:Utilities/MultiView"));
    CHECK(!strcmp(d.b[2].command, "List RAM:"));              /* "[]" taken out */
    CHECK(d.b[2].icon[0] == 0);
    /* ToolManager 3's form, and not ToolManager's at all */
    memcpy(data + 26, "TMPR", 4);           /* the first chunk after PRHD */
    CHECK(!od_from_toolmanager(&d, data, len, &r, err, sizeof err) && strstr(err, "ToolManager 3"));
    CHECK(!od_from_toolmanager(&d, (const unsigned char *)"FORM\0\0\0\4ILBM", 12, &r, err, sizeof err));
    free(data);
}

static void test_amidock(const char *dir)
{
    char path[512], err[120], *text;
    od_report r;
    snprintf(path, sizeof path, "%s/sample-AmiDock.xml", dir);
    text = slurp(path, NULL);
    CHECK(od_sniff((unsigned char *)text, (long)strlen(text)) == 2);
    od_defaults(&d);
    CHECK(od_from_amidock(&d, text, &r, err, sizeof err));
    /* Shell | Notes & Café, Ünits | MultiView: the SubDock left out */
    CHECK(d.n == 6);
    CHECK(!strcmp(d.b[0].label, "Shell") && d.b[0].kind == OD_WB);
    CHECK(d.b[1].kind == OD_SEPARATOR);
    CHECK(!strcmp(d.b[2].label, "Notes & Caf\xe9") && !strcmp(d.b[2].command, "Work:Notes & Things/Notes"));
    CHECK(!strcmp(d.b[3].label, "\xdcnits"));
    CHECK(d.b[4].kind == OD_SEPARATOR && !strcmp(d.b[5].label, "MultiView"));
    CHECK(r.added == 4 && r.skipped == 1);
    CHECK(!od_from_amidock(&d, "\x01\x02 binary", &r, err, sizeof err) && strstr(err, "3.9"));
    free(text);
}

static void test_amistart(const char *dir)
{
    char path[512], err[120], *text;
    od_report r;
    snprintf(path, sizeof path, "%s/sample-AmiStart.prefs", dir);
    text = slurp(path, NULL);
    CHECK(od_sniff((unsigned char *)text, (long)strlen(text)) == 3);
    od_defaults(&d);
    od_starter(&d);
    int n0 = d.n;
    CHECK(od_from_amistart(&d, text, "SYS:Utilities/AmiStart", &r, err, sizeof err));
    /* after the starter's buttons, a separator, then the taskbar's two programs */
    CHECK(d.n == n0 + 3 && d.b[n0].kind == OD_SEPARATOR);
    CHECK(d.b[n0 + 1].kind == OD_CLI && !strcmp(d.b[n0 + 1].command, "c:NewShell FROM s:Shell-amistart") && d.b[n0 + 1].stack == 8192);
    CHECK(!strcmp(d.b[n0 + 1].icon, "SYS:Utilities/AmiStart/icons/Default/Shell"));
    CHECK(d.b[n0 + 2].kind == OD_WB && !strcmp(d.b[n0 + 2].label, "Text Editor") && d.b[n0 + 2].icon[0] == 0);
    CHECK(r.added == 2 && r.skipped == 3);
    /* no taskbar programs: the start menu's */
    od_defaults(&d);
    CHECK(od_from_amistart(&d, "NEWDIR NAME=\"MAIN\"\nITEM NAME=\"Calc\" FILE=\"SYS:Tools/Calculator\" EXECMODE=\"0\"\nENDDIR\n", "", &r, err, sizeof err));
    CHECK(d.n == 1 && !strcmp(d.b[0].label, "Calc"));
    CHECK(!od_from_amistart(&d, "NEWDIR NAME=\"TASKBAR\"\nENDDIR\n", "", &r, err, sizeof err) && d.n == 1);
    free(text);
}

static void test_full(void)
{
    od_report r;
    char xml[16384], *o = xml, err[100];
    o += sprintf(o, "<?xml version=\"1.0\"?><pobjects><dict><key>Docks</key><array><dict><key>Categories</key><array><dict><key>Icons</key><array>");
    for (int i = 0; i < 60; i++) o += sprintf(o, "<dict><key>Name</key><string>P%d</string><key>FileName</key><string>Work:P%d</string></dict>", i, i);
    sprintf(o, "</array></dict></array></dict></array></dict></pobjects>");
    od_defaults(&d);
    CHECK(od_from_amidock(&d, xml, &r, err, sizeof err));
    CHECK(d.n == OD_MAX && r.added == OD_MAX && r.full == 60 - OD_MAX);
}

/* ---- 0.6.11: the timer's pace (od_wake.h), run as opendock.c's loop runs it ---- */

typedef struct { int wakes, stale, running; unsigned long first_busy; } wake_count;
static unsigned long busy_from, busy_to;           /* the pointer over the dock in [from, to), microseconds */
static unsigned busy_at(unsigned long t) { return t >= busy_from && t < busy_to ? OD_BUSY_POINTER : 0; }

/* span microseconds of wakes: each request's length from what is going on as it is sent */
static wake_count run_wakes(unsigned long span, int fixed_tenth)
{
    od_wake w;
    wake_count c = { 0, 0, 0, 0 };
    unsigned long t = 0, tick;
    od_wake_init(&w);
    tick = fixed_tenth ? 100000UL : od_wake_next(busy_at(0));
    while (t + tick <= span) {
        unsigned due;
        t += tick;
        c.wakes++;
        if (!c.first_busy && busy_at(t)) c.first_busy = t;
        due = od_wake_due(&w, tick);
        if (due & OD_DUE_STALE) c.stale++;
        if (due & OD_DUE_RUNNING) c.running++;
        tick = fixed_tenth ? 100000UL : od_wake_next(busy_at(t));
    }
    return c;
}

static void test_wake(void)
{
    wake_count before, rest, over, visit, late;
    od_wake w;
    CHECK(od_wake_next(0) == 1000000UL);
    CHECK(od_wake_next(OD_BUSY_POINTER) == 100000UL && od_wake_next(OD_BUSY_NAME) == 100000UL && od_wake_next(OD_BUSY_BEHIND) == 100000UL);

    /* a minute at rest: 0.6.10 woke 600 times (a tenth of a second, always), 0.6.11 60 */
    busy_from = busy_to = 0;
    before = run_wakes(60000000UL, 1);
    rest = run_wakes(60000000UL, 0);
    CHECK(before.wakes == 600 && rest.wakes == 60);
    /* the slow looks keep their pace either way: beside the shelf each second, the tasks every two */
    CHECK(before.stale == 60 && before.running == 30 && rest.stale == 60 && rest.running == 30);

    /* the pointer over the dock all the minute: ten a second, as before */
    busy_from = 0; busy_to = ~0UL;
    over = run_wakes(60000000UL, 0);
    CHECK(over.wakes == 600 && over.stale == 60 && over.running == 30);

    /* over it from 10 s to 15 s: 10 slow, 50 fast, 45 slow */
    busy_from = 10000000UL; busy_to = 15000000UL;
    visit = run_wakes(60000000UL, 0);
    CHECK(visit.wakes == 105 && visit.stale == 60 && visit.running == 30);

    /* the pointer reaches the dock at 10.5 s: the name's first look within a second (at 11 s) */
    busy_from = 10500000UL; busy_to = ~0UL;
    late = run_wakes(20000000UL, 0);
    CHECK(late.first_busy == 11000000UL);

    /* after a copy made for staleness, the next look beside the shelf waits three seconds */
    od_wake_init(&w);
    CHECK(od_wake_due(&w, 1000000UL) == OD_DUE_STALE);                     /* 1 s */
    od_wake_stale_hold(&w);
    CHECK(od_wake_due(&w, 1000000UL) == OD_DUE_RUNNING);                   /* 2 s: held */
    CHECK(od_wake_due(&w, 1000000UL) == 0);                                /* 3 s: held */
    CHECK(od_wake_due(&w, 1000000UL) == (OD_DUE_STALE | OD_DUE_RUNNING));  /* 4 s: three seconds after the copy */
    /* fast wakes add up to the same pace: twenty tenths make one look at the tasks */
    od_wake_init(&w);
    {
        int stale = 0, running = 0;
        for (int i = 0; i < 20; i++) {
            unsigned due = od_wake_due(&w, 100000UL);
            stale += !!(due & OD_DUE_STALE);
            running += !!(due & OD_DUE_RUNNING);
        }
        CHECK(stale == 2 && running == 1);
    }
    printf("wakes in a minute at rest: 0.6.10 %d, 0.6.11 %d; with the pointer over the dock for 5 s: %d\n",
           before.wakes, rest.wakes, visit.wakes);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    test_drop_missing();
    test_own_format();
    test_toolmanager(dir);
    test_amidock(dir);
    test_amistart(dir);
    test_full();
    test_wake();
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
