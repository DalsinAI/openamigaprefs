/* Host tests for src/om_kbd.c: OpenMenus' keyboard control on a strip
 * shaped like Workbench's (titles, separators, a disabled item, a submenu,
 * check marks and command keys).
 * Build and run: Menus/tests/run.sh
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "om_kbd.h"

#include <stdio.h>

static int fails, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define AT(p, m, i, s) ((p).menu == (m) && (p).item == (i) && (p).sub == (s))

static const omk_item volumes[] = {
    { "DF0", 0, 1, 0, 0, 0 },
    { "RAM Disk", 0, 1, 0, 0, 0 },
    { "System", 0, 1, 0, 0, 0 },
};
static const omk_item wb_items[] = {
    { "Backdrop", 'B', 1, 1, 0, 0 },          /* 0: a check mark */
    { "Execute command...", 'E', 1, 0, 0, 0 },
    { "Redraw all", 0, 1, 0, 0, 0 },
    { "", 0, 0, 0, 0, 0 },                    /* 3: a separator */
    { "Open volume", 0, 1, 0, 3, volumes },   /* 4: a submenu */
    { "Last message", 0, 0, 0, 0, 0 },        /* 5: disabled */
    { "About...", '?', 1, 0, 0, 0 },
    { "Quit...", 'Q', 1, 0, 0, 0 },
};
static const omk_item win_items[] = {
    { "New drawer", 'N', 1, 0, 0, 0 },
    { "Open parent", 'K', 1, 0, 0, 0 },
    { "Close", 'C', 1, 0, 0, 0 },
    { "Update", 0, 1, 0, 0, 0 },
    { "Select contents", 'A', 1, 0, 0, 0 },
};
static const omk_item none_items[] = { { "Nothing", 0, 1, 0, 0, 0 } };
static const omk_menu menus[] = {
    { "Workbench", 1, 8, wb_items },
    { "Window", 1, 5, win_items },
    { "Icons", 0, 1, none_items },            /* 2: the whole menu disabled */
};
static const omk_model wb = { 3, menus };

static void test_titles(void)
{
    omk_pos p = { 0, -1, -1 };
    CHECK(omk_first_menu(&wb) == 0);
    CHECK(omk_key(&wb, &p, OMK_RIGHT, 0) == OMK_MOVED && AT(p, 1, -1, -1));
    CHECK(omk_key(&wb, &p, OMK_TAB, 0) == OMK_MOVED && AT(p, 2, -1, -1));
    CHECK(omk_key(&wb, &p, OMK_RIGHT, 0) == OMK_MOVED && AT(p, 0, -1, -1));    /* wraps */
    CHECK(omk_key(&wb, &p, OMK_LEFT, 0) == OMK_MOVED && AT(p, 2, -1, -1));     /* wraps back */
    /* a disabled menu: shown, but nothing in it can be reached */
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_NONE && AT(p, 2, -1, -1));
    CHECK(omk_key(&wb, &p, OMK_RETURN, 0) == OMK_NONE);
    CHECK(omk_key(&wb, &p, OMK_ESC, 0) == OMK_CLOSE_ALL);
}

static void test_items(void)
{
    omk_pos p = { 0, -1, -1 };
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 0, -1));
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 1, -1));
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 2, -1));
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 4, -1));      /* over the separator */
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 6, -1));      /* over the disabled one */
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 7, -1));
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 0, -1));      /* wraps */
    CHECK(omk_key(&wb, &p, OMK_UP, 0) == OMK_MOVED && AT(p, 0, 7, -1));
    CHECK(omk_key(&wb, &p, OMK_FIRST, 0) == OMK_MOVED && AT(p, 0, 0, -1));
    CHECK(omk_key(&wb, &p, OMK_LAST, 0) == OMK_MOVED && AT(p, 0, 7, -1));
    /* Up from the title goes to the last item */
    p.item = -1;
    CHECK(omk_key(&wb, &p, OMK_UP, 0) == OMK_MOVED && AT(p, 0, 7, -1));
    /* Left and Right on an item go to the next title */
    CHECK(omk_key(&wb, &p, OMK_RIGHT, 0) == OMK_MOVED && AT(p, 1, -1, -1));
    /* Return on a title goes into its items, on an item chooses it */
    CHECK(omk_key(&wb, &p, OMK_RETURN, 0) == OMK_MOVED && AT(p, 1, 0, -1));
    CHECK(omk_key(&wb, &p, OMK_RETURN, 0) == OMK_CHOOSE && AT(p, 1, 0, -1));
    /* Esc: the item, then everything */
    CHECK(omk_key(&wb, &p, OMK_ESC, 0) == OMK_CLOSE_LEVEL && AT(p, 1, -1, -1));
    CHECK(omk_key(&wb, &p, OMK_ESC, 0) == OMK_CLOSE_ALL);
}

static void test_submenu(void)
{
    omk_pos p = { 0, 4, -1 };
    CHECK(omk_key(&wb, &p, OMK_RIGHT, 0) == OMK_OPEN_SUB && AT(p, 0, 4, 0));
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 4, 1));
    CHECK(omk_key(&wb, &p, OMK_RIGHT, 0) == OMK_NONE && AT(p, 0, 4, 1));       /* no deeper */
    CHECK(omk_key(&wb, &p, OMK_TAB, 0) == OMK_NONE);
    CHECK(omk_key(&wb, &p, OMK_LEFT, 0) == OMK_CLOSE_LEVEL && AT(p, 0, 4, -1));
    /* Return on an item with a submenu opens it, then chooses in it */
    CHECK(omk_key(&wb, &p, OMK_RETURN, 0) == OMK_OPEN_SUB && AT(p, 0, 4, 0));
    CHECK(omk_key(&wb, &p, OMK_DOWN, 0) == OMK_MOVED && AT(p, 0, 4, 1));
    CHECK(omk_key(&wb, &p, OMK_RETURN, 0) == OMK_CHOOSE && AT(p, 0, 4, 1));    /* RAM Disk */
    /* Esc in a submenu closes only the submenu */
    p.sub = 2;
    CHECK(omk_key(&wb, &p, OMK_ESC, 0) == OMK_CLOSE_LEVEL && AT(p, 0, 4, -1));
}

static void test_space_and_help(void)
{
    omk_pos p = { 0, 0, -1 };
    CHECK(omk_key(&wb, &p, OMK_SPACE, 0) == OMK_CHOOSE_STAY);                  /* Backdrop: a check mark */
    p.item = 1;
    CHECK(omk_key(&wb, &p, OMK_SPACE, 0) == OMK_NONE);                         /* not a check mark */
    CHECK(omk_key(&wb, &p, OMK_HELP, 0) == OMK_HELP_ITEM);
    p.item = -1;
    CHECK(omk_key(&wb, &p, OMK_HELP, 0) == OMK_NONE);
}

static void test_type_ahead(void)
{
    omk_pos p = { 1, -1, -1 };
    CHECK(omk_key(&wb, &p, OMK_CHAR, 'u') == OMK_MOVED && AT(p, 1, 3, -1));   /* Update */
    CHECK(omk_key(&wb, &p, OMK_CHAR, 'U') == OMK_NONE && AT(p, 1, 3, -1));     /* the only one: stays */
    p.item = -1; p.menu = 0;
    CHECK(omk_key(&wb, &p, OMK_CHAR, 'r') == OMK_MOVED && AT(p, 0, 2, -1));    /* Redraw all */
    CHECK(omk_key(&wb, &p, OMK_CHAR, 'e') == OMK_MOVED && AT(p, 0, 1, -1));    /* Execute command */
    CHECK(omk_key(&wb, &p, OMK_CHAR, 'l') == OMK_NONE);                        /* Last message is disabled */
    CHECK(omk_key(&wb, &p, OMK_CHAR, 'z') == OMK_NONE && AT(p, 0, 1, -1));
    /* in a submenu, type-ahead stays in it */
    p.item = 4; p.sub = 0;
    CHECK(omk_key(&wb, &p, OMK_CHAR, 's') == OMK_MOVED && AT(p, 0, 4, 2));
}

static void test_commands(void)
{
    omk_pos p = { 0, -1, -1 };
    CHECK(omk_key(&wb, &p, OMK_COMMAND, 'k') == OMK_CHOOSE && AT(p, 1, 1, -1));  /* Open parent, in another menu */
    CHECK(omk_key(&wb, &p, OMK_COMMAND, 'Q') == OMK_CHOOSE && AT(p, 0, 7, -1));
    p.menu = 0; p.item = 2; p.sub = -1;
    CHECK(omk_key(&wb, &p, OMK_COMMAND, 'x') == OMK_NONE && AT(p, 0, 2, -1));
}

static void test_odd_strips(void)
{
    static const omk_item seps[] = { { "", 0, 0, 0, 0, 0 }, { "", 0, 0, 0, 0, 0 } };
    static const omk_menu m1[] = { { "Only separators", 1, 2, seps } };
    static const omk_model only_seps = { 1, m1 };
    static const omk_menu m2[] = { { "Off", 0, 1, none_items }, { "On", 1, 1, none_items } };
    static const omk_model first_off = { 2, m2 };
    static const omk_model empty = { 0, 0 };
    omk_pos p = { 0, -1, -1 };
    CHECK(omk_key(&only_seps, &p, OMK_DOWN, 0) == OMK_NONE);
    CHECK(omk_key(&only_seps, &p, OMK_RIGHT, 0) == OMK_NONE && AT(p, 0, -1, -1));   /* one title: nowhere to go */
    CHECK(omk_first_menu(&first_off) == 1);
    CHECK(omk_key(&empty, &p, OMK_DOWN, 0) == OMK_CLOSE_ALL);
}

int main(void)
{
    test_titles();
    test_items();
    test_submenu();
    test_space_and_help();
    test_type_ahead();
    test_commands();
    test_odd_strips();
    printf("om_kbd: %d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
