/* OpenWindows' gesture table (src/gestures.c) on the host: the built-in
 * table (OS 3.2.3's behaviour with the extras: a click fronts at its
 * release, a press and a move drags at once and never fronts first), a
 * Gestures file's rows, and the qualifiers.
 *   Windows/tests/run.sh
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <stdio.h>
#include <string.h>
#include "../src/gestures.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    struct gesture_table t;
    char text[96];
    int take, n, i;
    unsigned a;

    gestures_default(&t, 1, 1, 1);
    a = gestures_actions(&t, GT_TITLE, GB_SELECT, GK_MOVE, 0, &take);
    CHECK(a == (GA_DRAG | GA_SNAP) && !take, "title bar, press and move: a drag at once (Intuition's), snapped at the release; not taken");
    CHECK(!(a & GA_FRONT), "press and move: no front first");
    a = gestures_actions(&t, GT_TITLE, GB_SELECT, GK_CLICK, 0, NULL);
    CHECK(a == GA_FRONT, "title bar, a click with no move: to the front (at the release)");
    a = gestures_actions(&t, GT_BODY, GB_SELECT, GK_CLICK, 0, &take);
    CHECK(a == GA_FRONT && !take, "inside a window, a click: to the front; the press goes on");
    a = gestures_actions(&t, GT_BODY, GB_SELECT, GK_MOVE, 0, NULL);
    CHECK(a == 0, "inside a window, press and move (an icon or a selection): nothing of ours");
    a = gestures_actions(&t, GT_EDGE, GB_SELECT, GK_MOVE, 0, &take);
    CHECK(a == GA_RESIZE && take, "an edge: OpenWindows' own resize, the press taken");
    a = gestures_actions(&t, GT_TITLE, GB_SELECT, GK_DOUBLE, 0, NULL);
    CHECK(a == GA_FRONT, "a double-click on the title bar: to the front");
    a = gestures_actions(&t, GT_GADGET, GB_SELECT, GK_CLICK, 0, &take);
    CHECK(a == 0 && !take, "a border gadget (close, depth): Intuition's alone");
    a = gestures_actions(&t, GT_BACKDROP, GB_SELECT, GK_CLICK, 0, NULL);
    CHECK(a == 0, "the backdrop: nothing");
    a = gestures_actions(&t, GT_TITLE, GB_SELECT, GK_CLICK, GQ_SHIFT, NULL);
    CHECK(a == 0, "with Shift held: not the built-in rows (they are for no qualifier)");
    a = gestures_actions(&t, GT_TITLE, GB_MENU, GK_CLICK, 0, NULL);
    CHECK(a == 0, "the menu button: nothing");

    gestures_default(&t, 0, 0, 0);
    CHECK(gestures_actions(&t, GT_TITLE, GB_SELECT, GK_MOVE, 0, NULL) == GA_DRAG, "snap off: a drag alone");
    CHECK(gestures_actions(&t, GT_TITLE, GB_SELECT, GK_DOUBLE, 0, NULL) == GA_FRONT, "no double-click row: the second click is a click");
    gestures_actions(&t, GT_EDGE, GB_SELECT, GK_MOVE, 0, &take);
    CHECK(!take, "edges off: no edge taken");

    n = gestures_parse(&t, "; mine\n"
                           "title  select none   click  none            pass\n"
                           "title  select amiga  click  back            pass   ; Amiga-click: to the back\n"
                           "\n"
                           "body   select any    click  activate,front  pass\n"
                           "edge   select none   move   resize          take\n");
    CHECK(n == 4, "a Gestures file: 4 rows (%d)", n);
    CHECK(gestures_actions(&t, GT_TITLE, GB_SELECT, GK_CLICK, 0, NULL) == 0, "its title click: nothing");
    CHECK(gestures_actions(&t, GT_TITLE, GB_SELECT, GK_CLICK, GQ_AMIGA, NULL) == GA_BACK, "its Amiga-click: to the back");
    CHECK(gestures_actions(&t, GT_BODY, GB_SELECT, GK_CLICK, GQ_SHIFT | GQ_CTRL, NULL) == (GA_ACTIVATE | GA_FRONT), "any qualifiers");
    CHECK(gestures_actions(&t, GT_TITLE, GB_SELECT, GK_MOVE, 0, NULL) == 0, "no move row: nothing of ours (Intuition's drag goes on)");
    n = gestures_parse(&t, "title select none click front pass\nthis is not a row at all\n");
    CHECK(n == -2, "a line it can't read: its number (%d)", n);
    CHECK(gestures_actions(&t, GT_TITLE, GB_SELECT, GK_CLICK, GQ_AMIGA, NULL) == GA_BACK, "... and the table as it was");
    CHECK(gestures_parse(&t, "title select none click teleport pass\n") == -1, "an action it doesn't know");
    CHECK(gestures_parse(&t, "title select none click front\n") == -1, "a row short of its press");

    CHECK(gestures_qual(0x8000) == 0, "the qualifiers: a button held is none of them");
    CHECK(gestures_qual(0x0001) == GQ_SHIFT && gestures_qual(0x0020) == GQ_ALT && gestures_qual(0x0008) == GQ_CTRL &&
          gestures_qual(0x0040) == GQ_AMIGA, "Shift, Alt, Ctrl, Amiga");
    CHECK(!gestures_moved(100, 100, 103, 97) && gestures_moved(100, 100, 104, 100) && gestures_moved(100, 100, 100, 96),
          "the threshold: 4 pixels either way");

    gestures_default(&t, 1, 1, 1);
    for (i = 0; i < t.n; i++) { gestures_row_text(&t.row[i], text, sizeof text); printf("  %s\n", text); }
    gestures_row_text(&t.row[0], text, sizeof text);
    CHECK(!strcmp(text, "title select none move drag,snap pass"), "a row as text: %s", text);
    printf("test_gestures: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
