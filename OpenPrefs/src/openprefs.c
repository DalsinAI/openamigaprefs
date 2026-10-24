/* OpenPrefs: every OpenPrefs editor as a section of one program with one window and one icon (Dale,
 * 17 October 2026, 08:18: "We should just combine them into a single app, prefs editors that is"). It
 * replaces the nine-editor plan of docs/combined-prefs/DESIGN.md (revision of 17 October 2026).
 *
 *   OpenPrefs [PUBSCREEN=name] [SECTION [arguments]]   the window, at SECTION (Theme when none); with
 *                                                       arguments, the section's editor takes them
 *                                                       (OpenPrefs SOUND USE is OpenSound USE)
 *   OpenPrefs APPLY                                     at boot: what OpenSound USE, OpenGamepads USE (when
 *                                                       openinput.library is there) and OpenSystem APPLY did
 *
 * SECTION: THEME (LOOK), SCREENMODE (OVERSCAN), POINTER, FONTS, COLOURS, BACKDROPS (WBPATTERN), WORKBENCH,
 * FILEREQUESTERS, WINDOWS, TITLEBAR, MENUS, ICONTROL, DOCK, SOUND, INPUT (MOUSE, KEYBOARD, KEYMAP), GAMEPADS,
 * DATETIME, SERIAL, TYPES, SYSTEM, BLANKER; or the page's name as the list shows it ("Date & Time", "Title Bar").
 * From Workbench, a project icon whose default tool is OpenPrefs (SYS:Preferences' plain-named icons, 24 October
 * 2026) opens the page its SECTION tooltype names, else the page of its name.
 *
 * Mouse and keys (now Input), Screen mode with Overscan, and IControl joined on 21 October 2026 (Dale: "fold
 * mouse and keys into a single app", "overscan should be under screenmode", IControl "fold into openprefs").
 *
 * Its place is SYS:Preferences/OpenPrefs (SYS:Prefs/OpenPrefs on an older disk); the ROM has no copy.
 *
 * The old names work as they did, by the name OpenPrefs runs as (a copy under that name; the stubs OpenUp
 * put in SYS:Prefs are archived since 24 October 2026): Look,
 * OpenLook, Windows, TitleBar, Menus, OpenDock, OpenSound, OpenInput,
 * OpenGamepads, OpenDateTime, OpenSerial, OpenTypes and OpenSystem, from the Shell with their own arguments
 * (OpenSound USE applies Sound's settings with no window, as before) and from Workbench at their section.
 *
 * Each page is the editor it was (its own source, built with LT_OP: Common/looktabs.h, opshift.h), its main
 * renamed; a click on another section returns from that editor with lt_next set, and the next page's gadgets
 * go into the same window (opframe.c).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <string.h>

#define LT_OP 1
#include "looktabs.h"
#include "openprefs.h"

static const char version[] __attribute__((used)) = "$VER: OpenPrefs " OP_VERSION " (24.10.2026) MIT, Copyright (c) 2026 AmigaChrome Team";

struct Library *UtilityBase;

/* The window runs on 32 KB however OpenPrefs is started (OpenLook's measure, 12 October 2026: a Shell's
 * 4 KB ended in #8000000A); libnix's start code swaps to a stack of this size when the one given is smaller. */
unsigned long __stack = 32768;

extern const char *lt_args, *lt_pubname, *lt_prog;
void lt_shutdown(void);
void lt_report(void);

/* The sections by name: OpenPrefs SECTION, OpenLook PAGE */
static const struct { const char *name; int page; } sections[] = {
    { "THEME", LT_LOOK }, { "LOOK", LT_LOOK }, { "POINTER", LT_POINTER }, { "FONTS", LT_FONTS }, { "FONT", LT_FONTS },
    { "COLOURS", LT_COLOURS }, { "COLORS", LT_COLOURS }, { "PALETTE", LT_COLOURS }, { "BACKDROPS", LT_BACKDROPS },
    { "WBPATTERN", LT_BACKDROPS }, { "WORKBENCH", LT_WORKBENCH }, { "FILEREQUESTERS", LT_ASL }, { "ASL", LT_ASL },
    { "WINDOWS", LT_WINDOWS }, { "TITLEBAR", LT_TITLEBAR }, { "MENUS", LT_MENUS }, { "DOCK", LT_DOCK },
    { "SOUND", LT_SOUND }, { "INPUT", LT_INPUT }, { "GAMEPADS", LT_GAMEPADS }, { "DATETIME", LT_DATETIME },
    { "TIME", LT_DATETIME }, { "DATE", LT_DATETIME }, { "SERIAL", LT_SERIAL }, { "TYPES", LT_TYPES },
    { "FILETYPES", LT_TYPES }, { "OPENTYPES", LT_TYPES }, { "SYSTEM", LT_SYSTEM }, { "STACK", LT_SYSTEM },
    { "BLANKER", LT_BLANKER }, { "MOUSE", LT_INPUT }, { "KEYBOARD", LT_INPUT }, { "KEYS", LT_INPUT },
    { "KEYMAP", LT_INPUT }, { "SCREENMODE", LT_SCREENMODE }, { "OVERSCAN", LT_SCREENMODE }, { "DISPLAY", LT_SCREENMODE },
    { "ICONTROL", LT_ICONTROL }, { "ICONS", LT_ICONS }, { "ICONLAYOUT", LT_ICONS }, { "ICONLABELS", LT_ICONS },
};

/* The old editors' names, and a name for each other section: each opens its section, and takes its own arguments */
enum { N_OPENPREFS = -1, N_OPENLOOK = -2 };
static const struct { const char *name; int page; } old_names[] = {
    { "OpenPrefs", N_OPENPREFS }, { "OpenLook", N_OPENLOOK }, { "Look", LT_LOOK }, { "Windows", LT_WINDOWS },
    { "TitleBar", LT_TITLEBAR }, { "Menus", LT_MENUS }, { "OpenDock", LT_DOCK }, { "Dock", LT_DOCK },
    { "OpenSound", LT_SOUND }, { "Sound", LT_SOUND }, { "OpenInput", LT_INPUT }, { "OpenGamepads", LT_GAMEPADS },
    { "Gamepads", LT_GAMEPADS }, { "OpenDateTime", LT_DATETIME }, { "OpenSerial", LT_SERIAL },
    { "OpenTypes", LT_TYPES }, { "OpenSystem", LT_SYSTEM },
    /* the sections no editor had a name for (Dale, 21 October 2026: an icon per section) */
    { "OpenScreenMode", LT_SCREENMODE }, { "OpenPointer", LT_POINTER }, { "OpenFonts", LT_FONTS },
    { "OpenColours", LT_COLOURS }, { "OpenBackdrops", LT_BACKDROPS }, { "OpenWorkbench", LT_WORKBENCH },
    { "OpenFileRequesters", LT_ASL }, { "OpenBlanker", LT_BLANKER }, { "OpenIControl", LT_ICONTROL },
};

/* A section by its word (SOUND, DATETIME) or its page's name ("Sound", "Date & Time") */
static int section_of(const char *s)
{
    unsigned i;
    for (i = 0; i < sizeof sections / sizeof sections[0]; i++)
        if (!Stricmp((CONST_STRPTR)s, (CONST_STRPTR)sections[i].name)) return sections[i].page;
    return lt_page_named(s);
}

/* From Workbench with a project icon (sm_ArgList[1], whose default tool is OpenPrefs: the plain-named icons in
 * SYS:Preferences): its SECTION tooltype, else its name, picks the page; -1 for none */
static int project_page(struct WBStartup *wb)
{
    struct Library *IconBase;
    struct DiskObject *dob;
    BPTR old;
    int page = -1;
    if (wb->sm_NumArgs < 2) return -1;
    if ((IconBase = OpenLibrary((STRPTR)"icon.library", 37))) {
        old = CurrentDir(wb->sm_ArgList[1].wa_Lock);
        if ((dob = GetDiskObject((STRPTR)wb->sm_ArgList[1].wa_Name))) {
            STRPTR t = FindToolType((CONST_STRPTR *)dob->do_ToolTypes, (STRPTR)"SECTION");
            if (t) page = section_of((const char *)t);
            FreeDiskObject(dob);
        }
        CurrentDir(old);
        CloseLibrary(IconBase);
    }
    if (page < 0) page = section_of((const char *)FilePart((STRPTR)wb->sm_ArgList[1].wa_Name));
    return page;
}

int op_page(int page, int argc, char **argv)
{
    switch (page) {
    case LT_LOOK: return look_main(argc, argv);
    case LT_WINDOWS: return windows_main(argc, argv);
    case LT_TITLEBAR: return titlebar_main(argc, argv);
    case LT_MENUS: return menus_main(argc, argv);
    case LT_POINTER: case LT_FONTS: case LT_COLOURS: case LT_BACKDROPS: case LT_WORKBENCH: case LT_ASL:
        return appearance_main(page);
    case LT_DOCK: return dock_main(argc, argv);
    case LT_SOUND: return sound_main(argc, argv);
    case LT_INPUT: return input_main(argc, argv);
    case LT_GAMEPADS: return gamepads_main(argc, argv);
    case LT_DATETIME: return datetime_main(argc, argv);
    case LT_SERIAL: return serial_main(argc, argv);
    case LT_TYPES: return types_main(argc, argv);
    case LT_SYSTEM: return system_main(argc, argv);
    case LT_BLANKER: return blanker_main(argc, argv);
    case LT_SCREENMODE: return screenmode_main(argc, argv);
    case LT_ICONTROL: return icontrol_main(argc, argv);
    case LT_ICONS: return icons_main(argc, argv);
    }
    return RETURN_FAIL;
}

/* The window from page on: the first page with the arguments given (argc 0: none, as from Workbench), then
 * whichever section is picked */
static int run_window(int page, int argc, char **argv)
{
    int rc = RETURN_OK, first = 1;
    for (;;) {
        lt_next = -1;
        rc = op_page(page, first ? argc : 0, first ? argv : NULL);
        first = 0;
        lt_args = NULL;
        if (lt_next < 0 || lt_next >= LT_N) break;
        page = lt_next;
    }
    lt_shutdown();
    return rc;
}

/* OpenPrefs's own arguments */
static int as_openprefs(void)
{
    static char line[512];
    LONG a[4] = { 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"PUBSCREEN/K,APPLY/S,SECTION,ARGS/F", a, NULL);
    int rc, page = LT_LOOK;
    if (!rd) { PrintFault(IoErr(), (STRPTR)"OpenPrefs"); return RETURN_FAIL; }
    if (a[0]) { static char pub[140]; strncpy(pub, (const char *)a[0], sizeof pub - 1); lt_pubname = pub; }
    if (a[1]) { FreeArgs(rd); return op_apply(); }
    if (a[2] && (page = section_of((const char *)a[2])) < 0) {
        Printf((STRPTR)"OpenPrefs: no section %s; the sections are THEME, SCREENMODE, POINTER, FONTS, COLOURS, BACKDROPS,\n"
               "WORKBENCH, ICONS, FILEREQUESTERS, WINDOWS, TITLEBAR, MENUS, ICONTROL, DOCK, SOUND, INPUT, GAMEPADS, DATETIME,\n"
               "SERIAL, TYPES, SYSTEM, BLANKER\n", a[2]);
        FreeArgs(rd);
        return RETURN_ERROR;
    }
    if (a[3] && *(const char *)a[3]) {         /* the section's own arguments */
        strncpy(line, (const char *)a[3], sizeof line - 2);
        strcat(line, "\n");
        lt_args = line;
        FreeArgs(rd);
        if (page >= LT_POINTER && page <= LT_ASL) { lt_args = NULL; return run_window(page, 0, NULL); }   /* no arguments of their own */
        rc = run_window(page, 1, op_argv);
        lt_args = NULL;
        return rc;
    }
    FreeArgs(rd);
    return run_window(page, 0, NULL);
}

/* OpenLook [PAGE] or Look's own arguments, as OpenLook 1.2 took them */
static int as_openlook(int argc, char **argv)
{
    int page;
    if (argc == 2 && (page = section_of(argv[1])) >= 0) return run_window(page, 0, NULL);
    return run_window(LT_LOOK, argc, argv);
}

int main(int argc, char **argv)
{
    const char *name = NULL;
    int rc = RETURN_OK, page = N_OPENPREFS;
    unsigned i;
    (void)version;
    if (!(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39))) return RETURN_FAIL;
    if (argc > 0) name = argv[0];
    else if (argv) {                           /* from Workbench: the icon's name (OpenPrefs, or an old name) */
        struct WBStartup *wb = (struct WBStartup *)argv;
        if (wb->sm_NumArgs > 0) name = (const char *)wb->sm_ArgList[0].wa_Name;
    }
    if (name) name = (const char *)FilePart((STRPTR)name);
    for (i = 0; name && i < sizeof old_names / sizeof old_names[0]; i++)
        if (!Stricmp((CONST_STRPTR)name, (CONST_STRPTR)old_names[i].name)) { page = old_names[i].page; lt_prog = old_names[i].name; break; }
    if (page == N_OPENPREFS && argc == 0 && argv && (i = (unsigned)project_page((struct WBStartup *)argv)) < LT_N)
        rc = run_window((int)i, 0, NULL);
    else if (page == N_OPENPREFS) rc = argc > 0 ? as_openprefs() : run_window(LT_LOOK, 0, NULL);
    else if (page == N_OPENLOOK) rc = as_openlook(argc, argv);
    else rc = run_window(page, argc, argv);
    lt_report();
    CloseLibrary(UtilityBase);
    return rc;
}
