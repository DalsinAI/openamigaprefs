/* OpenLook, the editor: Look, Windows, Title bar and Menus as the pages of one window.
 *
 *   OpenLook [LOOK | WINDOWS | TITLEBAR | MENUS]        the page to open first (Look)
 *   OpenLook FROM file USE | SAVE ...                   the Look editor's own arguments, as before
 *
 * Each page is the editor it always was (Look/src/look.c, Windows/src/windows.c, TitleBar/src/titlebar.c,
 * Menus/src/menus.c), built with LT_ONE and its main renamed; a click on a tab returns from that editor with
 * lt_next set, and the next page's gadgets go into the same window (Common/looktabs.h: lt_open, lt_close). The
 * window keeps its place and size across pages, growing only when a page needs more.
 * The user, 10 October 2026: "openlook shouldn't open new windows for the tabs", "the overall window should
 * remain the same size".
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>

struct Library *UtilityBase;              /* for Stricmp (libnix has no stricmp); opened below */

struct Window *lt_win;
int lt_next = -1;
int lt_win_w, lt_win_h;

int look_main(int argc, char **argv);
int windows_main(int argc, char **argv);
int titlebar_main(int argc, char **argv);
int menus_main(int argc, char **argv);

static const char version[] __attribute__((used)) = "$VER: OpenLook 1.0 (10.10.2026) OpenPrefs, Dalsin Limited";
static const char *const names[4] = { "LOOK", "WINDOWS", "TITLEBAR", "MENUS" };

int main(int argc, char **argv)
{
    static int (*const mains[4])(int, char **) = { look_main, windows_main, titlebar_main, menus_main };
    int page = 0, rc = 0, first = 1, i;
    (void)version;
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 39);
    if (!UtilityBase) return 20;
    if (argc == 2)                           /* OpenLook WINDOWS: that page first, nothing else to parse */
        for (i = 0; i < 4; i++)
            if (!Stricmp((CONST_STRPTR)argv[1], (CONST_STRPTR)names[i])) { page = i; first = 0; }
    for (;;) {
        lt_next = -1;
        rc = mains[page](first ? argc : 0, first ? argv : NULL);
        first = 0;
        if (lt_next < 0 || lt_next > 3) break;
        page = lt_next;
    }
    if (lt_win) { CloseWindow(lt_win); lt_win = NULL; }
    CloseLibrary(UtilityBase);
    return rc;
}
