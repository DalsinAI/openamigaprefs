/* Host tests for src/om_prefs.c: OpenMenus' own format, and taking over a
 * MagicMenu settings file written the way MagicMenu writes it.
 * Build and run: Menus/tests/run.sh
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "om_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *s;
    if (!f) { printf("FAIL cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    s = malloc(n + 1);
    if (fread(s, 1, n, f) != (size_t)n) { printf("FAIL short read %s\n", path); exit(1); }
    s[n] = 0;
    fclose(f);
    return s;
}

static void test_round_trip(void)
{
    om_prefs a, b;
    char text[4096];
    om_defaults(&a);
    a.open = OM_OPEN_POPUP;
    a.use[OM_PU] = OM_USE_CLICK;
    a.delay[OM_PD] = 3;
    a.background = OM_BG_SEEIMAGE;
    strcpy(a.image, "SYS:Prefs/Patterns/Menu.iff");
    a.colours = OM_COL_OWN;
    strcpy(a.colour[OM_C_SELECTED], "#12ab34");
    strcpy(a.key, "lalt space");
    strcpy(a.imported, "ENVARC:MagicMenu.prefs");
    CHECK(om_write(&a, text, sizeof text) > 0);
    CHECK(om_parse(&b, text));
    {
        char again[4096];
        CHECK(om_write(&b, again, sizeof again) > 0);
        CHECK(!strcmp(text, again));
    }
    CHECK(b.open == OM_OPEN_POPUP && b.use[OM_PU] == OM_USE_CLICK && b.delay[OM_PD] == 3);
    CHECK(b.background == OM_BG_SEEIMAGE && !strcmp(b.image, a.image) && !strcmp(b.key, "lalt space"));
    CHECK(om_write(&a, text, 40) == -1);
}

static void test_parse_tolerant(void)
{
    om_prefs p;
    om_parse(&p, "; a comment\nopen Pointer\nshadow.size 99\nfuture.setting 7\ncolour.text nonsense\ndelay.popup -4\n");
    CHECK(p.open == OM_OPEN_POINTER);
    CHECK(p.shadow_size == OM_SHADOW_SIZE_MAX);
    CHECK(!strcmp(p.colour[OM_C_TEXT], "#121825"));
    CHECK(p.delay[OM_PU] == 0);
    CHECK(!om_parse(&p, NULL) && p.enabled);
}

static void test_magicmenu(const char *dir)
{
    char path[512], err[120];
    char *text;
    om_prefs p;
    snprintf(path, sizeof path, "%s/sample-MagicMenu.prefs", dir);
    text = slurp(path);
    om_defaults(&p);
    CHECK(om_from_magicmenu(&p, text, err, sizeof err));
    CHECK(p.enabled == 1);
    CHECK(p.open == OM_OPEN_POPUP);
    CHECK(p.use[OM_PD] == OM_USE_HOLD);
    CHECK(p.use[OM_PU] == OM_USE_CLICK);
    CHECK(p.delay[OM_PD] == 0);              /* 255: none */
    CHECK(p.delay[OM_PU] == 3);
    CHECK(p.popup_last == 1);
    CHECK(p.sub_centre == 1);                /* PDCenterBox 2: on */
    CHECK(p.sub_mark == 1);                  /* unset: MarkSub */
    CHECK(p.border_double == 1);
    CHECK(p.shadow == 1);
    CHECK(p.shadow_size == 4);               /* PDShadowDist unset: ShadowDistance */
    CHECK(p.shadow_strength == 50);
    CHECK(p.background == OM_BG_IMAGE);
    CHECK(!strcmp(p.image, "Work:Backdrops/Menu\"s.iff"));
    CHECK(p.separators_bold == 1);
    CHECK(p.colours == OM_COL_OWN);
    CHECK(!strcmp(p.colour[OM_C_BACKGROUND], "#aaaaaa"));
    CHECK(!strcmp(p.colour[OM_C_SELECTED], "#6688bb"));
    CHECK(!strcmp(p.colour[OM_C_SHADOW], "#101010"));
    CHECK(!strcmp(p.key, "ramiga \\space"));
    CHECK(p.keyboard_top == 0);
    CHECK(p.keep_running == 1);
    free(text);

    CHECK(!om_from_magicmenu(&p, "Something else\n", err, sizeof err));
    CHECK(!om_from_magicmenu(&p, "\x01\x31\xcd\x52", err, sizeof err));
    CHECK(!om_from_magicmenu(&p, "MagicMenu/2:\n#\n", err, sizeof err));
    CHECK(!om_from_magicmenu(&p, NULL, err, sizeof err));
    /* Standard look: the screen's colours */
    om_defaults(&p);
    CHECK(om_from_magicmenu(&p, "MagicMenu/2:\n\tPDLook=0x00\n\tMenuType=0x00\n#\n", err, sizeof err));
    CHECK(p.colours == OM_COL_SCREEN && p.open == OM_OPEN_PULLDOWN);
}

int main(int argc, char **argv)
{
    test_round_trip();
    test_parse_tolerant();
    test_magicmenu(argc > 1 ? argv[1] : ".");
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
