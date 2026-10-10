/* OpenPrefs Windows: the editor for OpenWindows (snapping, Amiga+Tab, the
 * wheel under the pointer, remembered window places, drawer sizes, and a
 * double-click in a title bar bringing the window to the front).
 *
 *   Windows [FROM file] [USE] [SAVE] [ADVANCED]
 *
 * It opens in the Simple view (the on and off switches); View > Advanced (Amiga-A)
 * or ADVANCED shows every setting. The view is shared by all OpenPrefs
 * editors: "simple" or "advanced" in ENV:OpenAmiga/PrefsView (and ENVARC:).
 *
 * Save writes ENV:OpenPrefs/Windows and ENVARC:; Use writes ENV: only;
 * Cancel leaves both. Either way OpenWindows is told (Ctrl-F to the task of
 * its public port) or, when something is switched on and it isn't running,
 * started from SYS:Tools/Commodities.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char version[] __attribute__((used)) = "$VER: Windows 0.5 (10.10.2026) OpenPrefs, MIT, Copyright (c) 2026 Dalsin Limited";

#define PREFS_ENV "ENV:OpenPrefs/Windows"
#define PREFS_ENVARC "ENVARC:OpenPrefs/Windows"
#define PLACES_ENV "ENV:OpenPrefs/WindowPlaces"
#define PLACES_ENVARC "ENVARC:OpenPrefs/WindowPlaces"
#define TOOL "SYS:Tools/Commodities/OpenWindows"
#define VIEW_ENV "ENV:OpenAmiga/PrefsView"           /* shared by every OpenPrefs editor: simple or advanced */
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"

struct wprefs {
    int snap, snap_dist, halves, switcher, wheel, places, places_wb, drawer_w, drawer_h, dblfront, edges, edge_ptr, drive_title;
    char key[48];
    char never[160];
};
static struct wprefs cur;

/* ---- the settings file ---------------------------------------------------- */

static char *read_file(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf = NULL;
    if (fh) {
        LONG len;
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len >= 0 && (buf = AllocVec(len + 1, MEMF_ANY))) {
            if (Read(fh, buf, len) != len) { FreeVec(buf); buf = NULL; }
            else buf[len] = 0;
        }
        Close(fh);
    }
    return buf;
}

static int write_file(const char *path, const char *text)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    LONG len = strlen(text);
    if (!fh) {
        char dir[64];
        BPTR d;
        strcpy(dir, path);
        *strrchr(dir, '/') = 0;
        if ((d = CreateDir((STRPTR)dir))) UnLock(d);
        if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    }
    len = Write(fh, (APTR)text, len) == len;
    Close(fh);
    return len;
}

static void defaults(struct wprefs *p)
{
    memset(p, 0, sizeof *p);
    p->snap = 1; p->snap_dist = 12; p->switcher = 1; p->wheel = 1; p->places = 1; p->dblfront = 1; p->edges = 1; p->drive_title = 1;
    strcpy(p->key, "lcommand tab");
}

static void parse(struct wprefs *p, char *text)
{
    char *s, *line;
    defaults(p);
    if (!text) return;
    for (s = text; *s; ) {
        char *v;
        line = s;
        while (*s && *s != '\n') s++;
        if (*s) *s++ = 0;
        if (*line == ';' || !(v = strchr(line, ' '))) continue;
        *v++ = 0;
        if (!strcmp(line, "snap")) { p->snap = !strncmp(v, "on", 2); if (strchr(v, ' ')) p->snap_dist = atoi(strchr(v, ' ') + 1); }
        else if (!strcmp(line, "snap.halves")) p->halves = !strncmp(v, "on", 2);
        else if (!strcmp(line, "switcher")) p->switcher = !strncmp(v, "on", 2);
        else if (!strcmp(line, "switcher.key")) strncpy(p->key, v, sizeof p->key - 1);
        else if (!strcmp(line, "wheel")) p->wheel = !strncmp(v, "on", 2);
        else if (!strcmp(line, "doubleclick.front")) p->dblfront = !strncmp(v, "on", 2);
        else if (!strcmp(line, "edges")) p->edges = !strncmp(v, "on", 2);
        else if (!strcmp(line, "edges.pointer")) p->edge_ptr = !strncmp(v, "on", 2);   /* no gadget: kept as written */
        else if (!strcmp(line, "drive.title")) p->drive_title = !strncmp(v, "on", 2);
        else if (!strcmp(line, "places")) p->places = !strncmp(v, "on", 2);
        else if (!strcmp(line, "places.drawers")) p->places_wb = !strncmp(v, "on", 2);
        else if (!strcmp(line, "never")) {
            if (p->never[0] && strlen(p->never) + 2 < sizeof p->never) strcat(p->never, ", ");
            strncat(p->never, v, sizeof p->never - strlen(p->never) - 1);
        }
        else if (!strcmp(line, "drawer")) { p->drawer_w = atoi(v); if (strchr(v, ' ')) p->drawer_h = atoi(strchr(v, ' ') + 1); }
    }
}

static void make_text(const struct wprefs *p, char *out, int size)
{
    char never[160], *n, *e;
    int len = snprintf(out, size,
        "; OpenPrefs Windows 0.5: what OpenWindows does\n"
        "snap %s %d\nsnap.halves %s\nswitcher %s\nswitcher.key %s\nwheel %s\nplaces %s\nplaces.drawers %s\ndrawer %d %d\n"
        "doubleclick.front %s\nedges %s\nedges.pointer %s\ndrive.title %s\n",
        p->snap ? "on" : "off", p->snap_dist, p->halves ? "on" : "off", p->switcher ? "on" : "off", p->key,
        p->wheel ? "on" : "off", p->places ? "on" : "off", p->places_wb ? "on" : "off", p->drawer_w, p->drawer_h,
        p->dblfront ? "on" : "off", p->edges ? "on" : "off", p->edge_ptr ? "on" : "off", p->drive_title ? "on" : "off");
    strcpy(never, p->never);
    for (n = never; *n && len < size - 40; n = e) {
        while (*n == ' ' || *n == ',') n++;
        for (e = n; *e && *e != ','; e++) ;
        if (*e) *e++ = 0;
        { char *t = n + strlen(n); while (t > n && t[-1] == ' ') *--t = 0; }
        if (*n) len += snprintf(out + len, size - len, "never %s\n", n);
    }
}

static int running(void)
{
    int r;
    Forbid();
    r = FindPort((STRPTR)"OpenWindows") != NULL;
    Permit();
    return r;
}

static void tell(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)"OpenWindows")) && p->mp_SigTask) Signal((struct Task *)p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
    if (!p && (cur.snap || cur.switcher || cur.wheel || cur.places || cur.drawer_w || cur.dblfront || cur.edges || cur.drive_title)) {
        BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
        /* asynchronous: both handles are the new process's, closed when it ends */
        if (!in || !out || SystemTags((STRPTR)TOOL, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, SYS_UserShell, TRUE,
                                      NP_StackSize, 16384, TAG_DONE) == -1) {
            if (in) Close(in);
            if (out) Close(out);
        }
    }
}

static int put_in_place(int save)
{
    static char text[1024];
    int ok;
    make_text(&cur, text, sizeof text);
    ok = write_file(PREFS_ENV, text);
    if (save) ok = write_file(PREFS_ENVARC, text) && ok;
    tell();
    return ok;
}

/* ---- the window ---------------------------------------------------------------------- */

enum { G_SNAP, G_DIST, G_HALVES, G_SWITCH, G_KEY, G_WHEEL, G_DBLFRONT, G_EDGES, G_DRIVETITLE, G_PLACES, G_DRAWERS, G_NEVER, G_FORGET, G_DRAWW, G_DRAWH,
       G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };
static struct Gadget *gad[G_COUNT];
static struct Window *win;
static char status_text[120];

#define SET(id, ...) do { if (gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE); } while (0)
static int advanced;                     /* the view: Simple shows the four switches, Advanced every setting */

static int read_view(void)
{
    char *t = read_file(VIEW_ENV);
    int a = t && !strncmp(t, "advanced", 8);
    if (t) FreeVec(t);
    return a;
}

static void write_view(int a)
{
    write_file(VIEW_ENV, a ? "advanced\n" : "simple\n");
    write_file(VIEW_ENVARC, a ? "advanced\n" : "simple\n");
}

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_TITLE, (STRPTR)"View", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Advanced", (STRPTR)"A", CHECKIT | MENUTOGGLE, 0, (APTR)4 },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    SET(G_STATUS, GTTX_Text, (ULONG)status_text);
}

static void show(void)
{
    SET(G_SNAP, GTCB_Checked, cur.snap);
    SET(G_DIST, GTIN_Number, cur.snap_dist, GA_Disabled, !cur.snap);
    SET(G_HALVES, GTCB_Checked, cur.halves, GA_Disabled, !cur.snap);
    SET(G_SWITCH, GTCB_Checked, cur.switcher);
    SET(G_KEY, GTST_String, (ULONG)cur.key, GA_Disabled, !cur.switcher);
    SET(G_WHEEL, GTCB_Checked, cur.wheel);
    SET(G_DBLFRONT, GTCB_Checked, cur.dblfront);
    SET(G_EDGES, GTCB_Checked, cur.edges);
    SET(G_DRIVETITLE, GTCB_Checked, cur.drive_title);
    SET(G_PLACES, GTCB_Checked, cur.places);
    SET(G_NEVER, GTST_String, (ULONG)cur.never, GA_Disabled, !cur.places);
    SET(G_DRAWERS, GTCB_Checked, cur.places_wb, GA_Disabled, !cur.places);
    SET(G_DRAWW, GTIN_Number, cur.drawer_w);
    SET(G_DRAWH, GTIN_Number, cur.drawer_h);
}

static void forget_places(void)
{
    DeleteFile((STRPTR)PLACES_ENV);
    DeleteFile((STRPTR)PLACES_ENVARC);
    if (running()) {
        /* OpenWindows keeps its own copy: restart it so it starts empty */
        struct MsgPort *p;
        Forbid();
        if ((p = FindPort((STRPTR)"OpenWindows")) && p->mp_SigTask) Signal((struct Task *)p->mp_SigTask, SIGBREAKF_CTRL_C);
        Permit();
        Delay(25);
        tell();
    }
    status("Window places forgotten: programs open where they choose.");
}

/* 1 quit, 2 the view changed (open again) */
static int gui_once(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    int fh, lh, top, row, quit = 0, rc = 1, W = 480, X = 200, i;
    struct Menu *menu = NULL;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return -1; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    {   /* room for the longest label */
        static const char dbl[] = "Double-click a title bar to bring the window to the front";
        int need = X + 26 + 8 + TextLength(&scr->RastPort, (STRPTR)dbl, sizeof dbl - 1) + 16;
        if (W < need) W = need;
    }
    strcpy(status_text, running() ? "OpenWindows is running." : "OpenWindows isn't running: Use or Save starts it.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))

    row = top;
    G(CHECKBOX_KIND, G_SNAP, X, row, 26, lh, "Snap to edges", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    if (advanced) G(INTEGER_KIND, G_DIST, X + 160, row, 50, lh, "within", PLACETEXT_LEFT, GTIN_MaxChars, 2);
    row += lh + 4;
    if (advanced) { G(CHECKBOX_KIND, G_HALVES, X, row, 26, lh, "Fill a half", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 10; }
    G(CHECKBOX_KIND, G_SWITCH, X, row, 26, lh, "Window switcher", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    if (advanced) G(STRING_KIND, G_KEY, X + 100, row, 160, lh, "key", PLACETEXT_LEFT, GTST_MaxChars, 46);
    row += lh + (advanced ? 10 : 4);
    G(CHECKBOX_KIND, G_WHEEL, X, row, 26, lh, "Wheel under pointer", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 4;
    /* a long label: the box in the column with its words after it */
    G(CHECKBOX_KIND, G_DBLFRONT, X, row, 26, lh, "Double-click a title bar to bring the window to the front", PLACETEXT_RIGHT,
      GTCB_Scaled, TRUE);
    row += lh + 4;
    G(CHECKBOX_KIND, G_EDGES, X, row, 26, lh, "Resize a window from any edge but its title bar", PLACETEXT_RIGHT,
      GTCB_Scaled, TRUE);
    row += lh + 4;
    G(CHECKBOX_KIND, G_DRIVETITLE, X, row, 26, lh, "Drive windows titled with the name only", PLACETEXT_RIGHT, GTCB_Scaled, TRUE);
    row += lh + (advanced ? 10 : 4);
    G(CHECKBOX_KIND, G_PLACES, X, row, 26, lh, "Remember places", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    if (advanced) G(BUTTON_KIND, G_FORGET, X + 120, row, 140, lh, "Forget all", 0, GA_Disabled, FALSE);
    row += lh + 4;
    G(CHECKBOX_KIND, G_DRAWERS, X, row, 26, lh, "Workbench drawers too", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    if (advanced) {
        row += lh + 4;
        G(STRING_KIND, G_NEVER, X, row, 260, lh, "Never for", PLACETEXT_LEFT, GTST_MaxChars, 158); row += lh + 10;
        G(INTEGER_KIND, G_DRAWW, X, row, 60, lh, "Drawers at least", PLACETEXT_LEFT, GTIN_MaxChars, 4);
        G(INTEGER_KIND, G_DRAWH, X + 90, row, 60, lh, "x", PLACETEXT_LEFT, GTIN_MaxChars, 4);
    }
    row += lh + 10;
    G(TEXT_KIND, G_STATUS, 10, row, W - 20, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + 6;
    {
        static const char *const names[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 20 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], 10 + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = -1; goto out; }
    menus[6].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Windows", WA_ScreenTitle, (ULONG)"OpenPrefs Windows 0.4", WA_PubScreen, (ULONG)scr,
                         WA_Left, 60, WA_Top, scr->BarHeight + 20, WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE,
                         WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | STRINGIDCMP | CHECKBOXIDCMP | INTEGERIDCMP,
                         TAG_DONE);
    if (!win) { rc = -1; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    show();

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) quit = 1;
            else if (cls == IDCMP_MENUPICK) {
                UWORD code = m->Code;
                while (code != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: put_in_place(1); quit = 1; break;
                    case 2: put_in_place(0); quit = 1; break;
                    case 3: quit = 1; break;
                    case 4: advanced = (it->Flags & CHECKED) != 0; write_view(advanced); rc = 2; quit = 1; break;
                    }
                    code = it->NextSelect;
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_GADGETUP) {
                int sel = (gg->Flags & GFLG_SELECTED) != 0;
                LONG num = ((struct StringInfo *)gg->SpecialInfo) ? ((struct StringInfo *)gg->SpecialInfo)->LongInt : 0;
                const char *str = ((struct StringInfo *)gg->SpecialInfo) ? (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer : "";
                switch (gg->GadgetID) {
                case G_SNAP: cur.snap = sel; show(); break;
                case G_DIST: cur.snap_dist = num < 2 ? 2 : num > 64 ? 64 : num; show(); break;
                case G_HALVES: cur.halves = sel; break;
                case G_SWITCH: cur.switcher = sel; show(); break;
                case G_KEY:
                    if (*str) strncpy(cur.key, str, sizeof cur.key - 1);
                    status("The key in commodity words, for example: lcommand tab");
                    break;
                case G_WHEEL: cur.wheel = sel; break;
                case G_DBLFRONT: cur.dblfront = sel; break;
                case G_EDGES: cur.edges = sel; break;
                case G_DRIVETITLE: cur.drive_title = sel; break;
                case G_PLACES: cur.places = sel; show(); break;
                case G_DRAWERS: cur.places_wb = sel; break;
                case G_NEVER: strncpy(cur.never, str, sizeof cur.never - 1); break;
                case G_FORGET: forget_places(); break;
                case G_DRAWW: cur.drawer_w = num < 0 ? 0 : num; show(); break;
                case G_DRAWH: cur.drawer_h = num < 0 ? 0 : num; show(); break;
                case G_USE: put_in_place(0); quit = 1; break;
                case G_SAVE: put_in_place(1); quit = 1; break;
                case G_CANCEL: quit = 1; break;
                }
            }
        }
    }
out:
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

static int gui(void)
{
    int r;
    while ((r = gui_once()) == 2) ;
    return r < 0 ? RETURN_FAIL : RETURN_OK;
}

int main(int argc, char **argv)
{
    LONG args[4] = { 0, 0, 0, 0 };
    /* from Workbench (argc 0) there are no arguments: ReadArgs would read them from
     * libnix's console window, which opens empty and waits */
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S,ADVANCED/S", args, NULL) : NULL;
    char *text;
    int rc;
    (void)version;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Windows"); return RETURN_FAIL; }
    text = read_file(args[0] ? (const char *)args[0] : PREFS_ENV);
    parse(&cur, text);
    if (text) FreeVec(text);
    if (args[1] || args[2]) {                /* from the Shell: no window */
        rc = put_in_place(args[2] != 0) ? RETURN_OK : RETURN_ERROR;
        if (rd) FreeArgs(rd);
        return rc;
    }
    advanced = args[3] ? 1 : read_view();
    if (rd) FreeArgs(rd);
    return gui();
}
