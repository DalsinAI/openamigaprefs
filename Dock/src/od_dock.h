/* od_dock: OpenDock's settings (DESIGN.md section 2, format 1), and taking
 * over other docks' settings: ToolManager 2, AmiDock (OS4's XML) and
 * AmiStart's taskbar.
 *
 * Plain C with no Amiga calls, so the same code is tested on the host
 * (tests/test_od_dock.c) and runs on the Amiga, in the Dock editor and in
 * OpenDock alike. Each reader is written from its file's layout, with no
 * code from the program that wrote it.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OD_DOCK_H
#define OD_DOCK_H

enum { OD_BOTTOM = 0, OD_TOP = 1, OD_LEFT = 2, OD_RIGHT = 3 };
enum { OD_SMALL = 0, OD_MEDIUM = 1, OD_LARGE = 2 };          /* 40, 56, 72 pixel cells */
enum { OD_WB = 0, OD_CLI = 1, OD_AREXX = 2, OD_SEPARATOR = 3 };
enum { OD_BG_SOLID = 0, OD_BG_CLEAR = 1, OD_BG_GLASS = 2 };   /* the screen's colour; see-through; see-through, frosted */

#define OD_MAX 48                                         /* buttons on one dock */

typedef struct od_button {
    int kind;                                             /* OD_WB, OD_CLI, OD_AREXX, OD_SEPARATOR */
    char label[48];
    char command[256];                                    /* a program or project (WB), a command line (CLI), a script (ARexx) */
    char icon[160];                                       /* without ".info"; empty: the command's own icon */
    char dir[128];                                        /* CLI's current drawer; empty: the program's own */
    long stack;
} od_button;

typedef struct od_dock {
    int place;                                            /* OD_BOTTOM ... */
    int size;                                             /* OD_SMALL ... */
    int labels;                                           /* names under the icons */
    int hover;                                            /* a name above the icon the pointer is on */
    int hop;                                              /* an icon hops when its program starts */
    int background;                                       /* OD_BG_* */
    int running;                                          /* a mark under programs that are running */
    int magnify;                                          /* reserved: never with Lite */
    char imported[96];                                    /* the file taken over, so the first start does it once */
    int n;
    od_button b[OD_MAX];
} od_dock;

typedef struct od_report {
    int added;                                            /* buttons added */
    int skipped;                                          /* entries with nothing a dock button can do (hotkeys, dockies, menus) */
    int full;                                             /* entries left out because the dock was full */
} od_report;

int od_cell(int size);                                    /* the cell's width and height in pixels */

void od_defaults(od_dock *d);                             /* the place and size; no buttons */
void od_starter(od_dock *d);                              /* the buttons a new dock starts with */
/* Reads the settings text; unknown lines are skipped. 1, or 0 when text is NULL. */
int od_parse(od_dock *d, const char *text);
/* Writes the settings text. Its length, or -1 when out is too small. */
int od_write(const od_dock *d, char *out, int size);

/* Buttons from another dock's settings, added after d's own (a separator
 * between); 1, or 0 with the reason in err. */
int od_from_toolmanager(od_dock *d, const unsigned char *data, long len, od_report *r, char *err, int errlen);
int od_from_amidock(od_dock *d, const char *text, od_report *r, char *err, int errlen);
/* base: the drawer AmiStart's relative icon paths start from (its program drawer). */
int od_from_amistart(od_dock *d, const char *text, const char *base, od_report *r, char *err, int errlen);

/* Which reader a file needs, by its first bytes: 1 ToolManager, 2 AmiDock,
 * 3 AmiStart, 0 none of them. */
int od_sniff(const unsigned char *data, long len);

#endif
