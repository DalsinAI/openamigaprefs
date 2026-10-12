/* OpenLook, the editor: Theme (Look), Windows, Title bar and Menus as the pages of one window, and since 1.1
 * the Appearance tabs of the combined preferences: Pointer, Fonts, Colours, Backdrops, Workbench and File
 * requesters (Look/src/appearance.c), which edit the OS's own files in ENV:Sys.
 *
 *   OpenLook [LOOK | THEME | WINDOWS | TITLEBAR | MENUS | POINTER | FONTS | COLOURS | BACKDROPS | WORKBENCH |
 *             FILEREQUESTERS]                         the page to open first (Look)
 *   OpenLook FROM file USE | SAVE ...                   the Look editor's own arguments, as before
 *
 * Each of the first four pages is the editor it always was (Look/src/look.c, Windows/src/windows.c,
 * TitleBar/src/titlebar.c, Menus/src/menus.c), built with LT_ONE and its main renamed; a click on a tab returns
 * from that editor with lt_next set, and the next page's gadgets go into the same window (Common/looktabs.h:
 * lt_open, lt_close). The window keeps its place and size across pages, growing only when a page needs more.
 * The user, 10 October 2026: "openlook shouldn't open new windows for the tabs", "the overall window should
 * remain the same size".
 *
 * 1.2 (12 October 2026): a pointer drawn on the Pointer tab, the icon frame and hidden devices on the Workbench
 * tab, and every page in 640 x 256 with Topaz 8 (Look/DESIGN.md).
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

/* The window runs on 32 KB however OpenLook is started: from a Shell (Run, a script) it had the Shell's 4 KB,
 * and opening the window ended in a software failure (#8000000A) on the Team's desktop (12 October 2026).
 * libnix's start code swaps to a stack of this size when the one it was given is smaller. */
unsigned long __stack = 32768;

int look_main(int argc, char **argv);
int windows_main(int argc, char **argv);
int titlebar_main(int argc, char **argv);
int menus_main(int argc, char **argv);
int appearance_main(int tab);

static const char version[] __attribute__((used)) = "$VER: OpenLook 1.2 (12.10.2026) OpenPrefs, Dalsin Limited";

/* The pages, numbered as Common/looktabs.h numbers them (LT_LOOK ... LT_ASL): the first four are editors of
 * their own, the rest Appearance's tabs. Each page by name, and by a second name. */
enum { P_LOOK, P_WINDOWS, P_TITLEBAR, P_MENUS, P_POINTER, P_FONTS, P_COLOURS, P_BACKDROPS, P_WORKBENCH, P_ASL, P_N };
static const struct { const char *name; int page; } names[] = {
    { "LOOK", P_LOOK }, { "THEME", P_LOOK }, { "WINDOWS", P_WINDOWS }, { "TITLEBAR", P_TITLEBAR }, { "MENUS", P_MENUS },
    { "POINTER", P_POINTER }, { "FONTS", P_FONTS }, { "FONT", P_FONTS }, { "COLOURS", P_COLOURS }, { "COLORS", P_COLOURS },
    { "PALETTE", P_COLOURS }, { "BACKDROPS", P_BACKDROPS }, { "WBPATTERN", P_BACKDROPS }, { "WORKBENCH", P_WORKBENCH },
    { "FILEREQUESTERS", P_ASL }, { "ASL", P_ASL }
};

int main(int argc, char **argv)
{
    static int (*const mains[4])(int, char **) = { look_main, windows_main, titlebar_main, menus_main };
    int page = P_LOOK, rc = 0, first = 1;
    unsigned i;
    (void)version;
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 39);
    if (!UtilityBase) return 20;
    if (argc == 2)                           /* OpenLook WINDOWS: that page first, nothing else to parse */
        for (i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!Stricmp((CONST_STRPTR)argv[1], (CONST_STRPTR)names[i].name)) { page = names[i].page; first = 0; }
    for (;;) {
        lt_next = -1;
        if (page < P_POINTER) rc = mains[page](first ? argc : 0, first ? argv : NULL);
        else rc = appearance_main(page);
        first = 0;
        if (lt_next < 0 || lt_next >= P_N) break;
        page = lt_next;
    }
    if (lt_win) { CloseWindow(lt_win); lt_win = NULL; }
    CloseLibrary(UtilityBase);
    return rc;
}
