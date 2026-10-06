/* Host tests for od_dock.c. MIT, Copyright (c) 2026 Dalsin Limited. */
#include "od_dock.h"
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

static void test_own_format(void)
{
    od_defaults(&d);
    od_starter(&d);
    CHECK(d.n == 5 && d.b[0].kind == OD_CLI && !strcmp(d.b[0].label, "Shell") && d.b[1].kind == OD_WB);
    CHECK(!strcmp(d.b[1].label, "OpenFiles") && !strcmp(d.b[2].command, "SYS:Utilities/OpenPrint/OpenView"));
    /* ADD: once only, whatever the case, at the end */
    CHECK(od_ensure(&d, "sys:utilities/openfiles") == 0 && d.n == 5);
    CHECK(od_ensure(&d, "SYS:Utilities/MultiView") == 1 && d.n == 6 && !strcmp(d.b[5].label, "MultiView") && d.b[5].kind == OD_WB);
    CHECK(od_ensure(&d, "SYS:Utilities/MultiView") == 0 && d.n == 6);
    CHECK(d.background == OD_BG_GLASS && d.hover == 1 && d.hop == 1);
    d.place = OD_LEFT; d.size = OD_LARGE; d.labels = 1; d.running = 0; d.scale = 50; d.border = 0;
    d.background = OD_BG_CLEAR; d.hover = 0; d.hop = 0;
    strcpy(d.imported, "ENVARC:ToolManager.prefs");
    d.b[1].kind = OD_SEPARATOR;
    d.b[2].kind = OD_CLI; strcpy(d.b[2].command, "Echo \"hi\" \\ there"); strcpy(d.b[2].dir, "RAM:"); d.b[2].stack = 20000;
    strcpy(d.b[4].icon, "SYS:Prefs/Look");
    CHECK(od_write(&d, buf, sizeof buf) > 0);
    od_parse(&e, buf);
    CHECK(e.place == OD_LEFT && e.size == OD_LARGE && e.labels == 1 && e.running == 0);
    CHECK(e.scale == 50 && e.border == 0);
    CHECK(e.background == OD_BG_CLEAR && e.hover == 0 && e.hop == 0);
    CHECK(!strcmp(e.imported, d.imported));
    CHECK(e.n == 6 && e.b[1].kind == OD_SEPARATOR);
    CHECK(e.b[2].kind == OD_CLI && !strcmp(e.b[2].command, d.b[2].command) && !strcmp(e.b[2].dir, "RAM:") && e.b[2].stack == 20000);
    CHECK(!strcmp(e.b[4].icon, "SYS:Prefs/Look") && !strcmp(e.b[4].label, "Look"));
    CHECK(od_write(&d, buf, 40) == -1);
    od_parse(&e, "; comment\nplace nowhere\nsize huge\nfuture 7\nbutton telepathy \"x\"\nbutton wb \"Work:Thing\"\n");
    CHECK(e.place == OD_BOTTOM && e.size == OD_MEDIUM && e.n == 1 && !strcmp(e.b[0].label, "Thing"));
    CHECK(e.scale == 100 && e.border == 1);
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
    CHECK(od_from_amistart(&d, text, "SYS:Utilities/AmiStart", &r, err, sizeof err));
    /* after the five it had, a separator, then the taskbar's two programs */
    CHECK(d.n == 8 && d.b[5].kind == OD_SEPARATOR);
    CHECK(d.b[6].kind == OD_CLI && !strcmp(d.b[6].command, "c:NewShell FROM s:Shell-amistart") && d.b[6].stack == 8192);
    CHECK(!strcmp(d.b[6].icon, "SYS:Utilities/AmiStart/icons/Default/Shell"));
    CHECK(d.b[7].kind == OD_WB && !strcmp(d.b[7].label, "Text Editor") && d.b[7].icon[0] == 0);
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

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    test_own_format();
    test_toolmanager(dir);
    test_amidock(dir);
    test_amistart(dir);
    test_full();
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
