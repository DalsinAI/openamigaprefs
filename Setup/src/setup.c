/* OpenUp Setup 0.1: the first-start wizard (openamigaup DESIGN.md section 6).
 * It runs once, from WBStartup, the first time Workbench starts after OpenUp
 * is installed, and stays in SYS:Prefs to run again.
 *
 *   1. Welcome: what this machine is (OpenUpTool DETECT's summary).
 *   2. Desktop: a desktop profile, or "choose by machine at every start",
 *      put in place through Look; and the OpenPrefs editors, one button each.
 *   3. Screen, printer and network: the OS's and the Open parts' own editors.
 *   4. Startup: every line OpenUp put in its marked block in S:User-Startup,
 *      in plain words, each with a switch that takes it out cleanly.
 *   5. Done.
 *
 * Simple (the default) is a theme and done: Welcome, Desktop's profiles, Done.
 * Advanced shows every page and the editors' buttons. The choice is also the
 * view every OpenPrefs editor opens in (ENVARC:OpenAmiga/PrefsView, "simple"
 * or "advanced"; DESIGN.md).
 *
 * Nothing is changed until Finish. Settings stay with the editors that own
 * them: Setup writes no settings of its own but its startup switches
 * (ENVARC:OpenAmiga/Startup.off) and that it has run (ENVARC:OpenAmiga/Setup.done).
 *
 *   OpenUp-Setup [FIRSTSTART] [ADVANCED]
 *
 * FIRSTSTART (or being started from SYS:WBStartup) does nothing once Setup
 * has been finished or told not to come back.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

const char version[] __attribute__((used)) = "$VER: OpenUp-Setup 0.1 (6.10.2026) OpenPrefs, Dalsin Limited";

#define USER_STARTUP "S:User-Startup"
#define BLOCK_BEGIN ";BEGIN OpenUp"
#define BLOCK_END ";END OpenUp"
#define OFF_MARK ";OFF "
#define OFF_LIST "ENVARC:OpenAmiga/Startup.off"
#define DONE_FILE "ENVARC:OpenAmiga/Setup.done"
#define PROFILE_DIR "SYS:Prefs/Presets/OpenPrefs"
#define LOOK_TOOL "SYS:Prefs/Look"
#define LOOK_TEMP "T:OpenUp-Setup.profile"
#define VIEW_ENV "ENV:OpenAmiga/PrefsView"
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"

extern struct WBStartup *_WBenchMsg;

/* ---- files -------------------------------------------------------------------------------- */

static char *read_file(const char *path, LONG *lenp)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG len;
    char *buf;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len < 0 || len > 256 * 1024 || !(buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) { Close(fh); return NULL; }
    if (Read(fh, buf, len) != len) { FreeVec(buf); Close(fh); return NULL; }
    buf[len] = 0;
    Close(fh);
    if (lenp) *lenp = len;
    return buf;
}

static int write_file(const char *path, const void *data, LONG len)
{
    char dir[96], *slash;
    BPTR fh, lock;
    strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    if ((slash = strrchr(dir, '/'))) {              /* make the drawer; a file at a volume's root needs none */
        *slash = 0;
        if ((lock = Lock((STRPTR)dir, ACCESS_READ))) UnLock(lock);
        else if ((lock = CreateDir((STRPTR)dir))) UnLock(lock);
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    if (Write(fh, (APTR)data, len) != len) { Close(fh); return 0; }
    Close(fh);
    return 1;
}

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

static void run_tool(const char *path)
{
    char cmd[160];
    snprintf(cmd, sizeof cmd, "Run >NIL: <NIL: \"%s\"", path);
    SystemTags((STRPTR)cmd, TAG_DONE);
}

/* ---- the view: simple or advanced, shared by every OpenPrefs editor ------------------------ */

static int advanced;

static void read_view(void)
{
    char *t = read_file(VIEW_ENV, NULL);
    if (!t) t = read_file(VIEW_ENVARC, NULL);
    if (t) { advanced = !strncmp(t, "advanced", 8); FreeVec(t); }
}

static void write_view(void)
{
    const char *v = advanced ? "advanced\n" : "simple\n";
    write_file(VIEW_ENV, v, strlen(v));
    write_file(VIEW_ENVARC, v, strlen(v));
}

/* ---- the machine ------------------------------------------------------------------------ */

static char summary[600];

static void read_summary(void)
{
    if (GetVar((STRPTR)"OpenUp/Summary", (STRPTR)summary, sizeof summary, GVF_GLOBAL_ONLY) <= 0) {
        UWORD attn = ((struct ExecBase *)SysBase)->AttnFlags;
        snprintf(summary, sizeof summary, "This is an Amiga with a %s and %s.\n",
                 attn & AFF_68060 ? "68060" : attn & AFF_68040 ? "68040" : attn & AFF_68030 ? "68030" : attn & AFF_68020 ? "68020" : "68000",
                 attn & (AFF_68881 | AFF_FPU40) ? "an FPU" : "no FPU");
    }
}

/* ---- desktop profiles ------------------------------------------------------------------- */

#define MAX_PROFILES 16

static char profile_names[MAX_PROFILES][48];
static char profile_about[MAX_PROFILES][100];
static int nprofiles, profile_sel = -1, by_machine = 1;

static void read_profiles(void)
{
    BPTR lock = Lock((STRPTR)PROFILE_DIR, ACCESS_READ);
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    if (lock && fib && Examine(lock, fib))
        while (ExNext(lock, fib) && nprofiles < MAX_PROFILES) {
            size_t l = strlen((char *)fib->fib_FileName);
            if (fib->fib_DirEntryType < 0 && l > 8 && l - 8 < 48 && !strcmp((char *)fib->fib_FileName + l - 8, ".profile")) {
                char path[160], *text, *c;
                memcpy(profile_names[nprofiles], fib->fib_FileName, l - 8);
                profile_names[nprofiles][l - 8] = 0;
                /* the profile's first comment line says what it is for */
                snprintf(path, sizeof path, PROFILE_DIR "/%s", (char *)fib->fib_FileName);
                profile_about[nprofiles][0] = 0;
                if ((text = read_file(path, NULL))) {
                    if ((c = strstr(text, "; OpenPrefs desktop profile")) && (c = strchr(c, ':'))) {
                        int n = 0;
                        for (c++; *c == ' '; c++) ;
                        while (c[n] && c[n] != '\n' && n < 99) n++;
                        memcpy(profile_about[nprofiles], c, n);
                        profile_about[nprofiles][n] = 0;
                    }
                    FreeVec(text);
                }
                nprofiles++;
            }
        }
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (lock) UnLock(lock);
    for (int i = 1; i < nprofiles; i++)
        for (int j = i; j > 0 && strcmp(profile_names[j - 1], profile_names[j]) > 0; j--) {
            char t[100];
            strcpy(t, profile_names[j]); strcpy(profile_names[j], profile_names[j - 1]); strcpy(profile_names[j - 1], t);
            strcpy(t, profile_about[j]); strcpy(profile_about[j], profile_about[j - 1]); strcpy(profile_about[j - 1], t);
        }
}

/* The chosen profile through Look (Look FROM <file> SAVE), with "profile auto"
 * when by machine. With no profile chosen, by machine still applies: to the
 * Look settings in use. 1 when done, or when there is nothing to do. */
static int apply_profile(void)
{
    char path[160], cmd[220], *text, *both, *o;
    const char *p;
    if (profile_sel < 0 && !by_machine) return 1;
    if (!exists(LOOK_TOOL)) return profile_sel < 0;
    if (profile_sel >= 0) {
        snprintf(path, sizeof path, PROFILE_DIR "/%s.profile", profile_names[profile_sel]);
        text = read_file(path, NULL);
    } else if (!(text = read_file("ENV:OpenGadTools/Look", NULL)))
        text = read_file("ENVARC:OpenGadTools/Look", NULL);
    if (!text) return profile_sel < 0;              /* no Look settings yet: OpenLook chooses as it starts */
    if (!(both = AllocVec(strlen(text) + 32, MEMF_ANY))) { FreeVec(text); return 0; }
    /* the text without its own profile lines, then ours */
    for (o = both, p = text; *p; ) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) + 1 : (int)strlen(p);
        if (strncmp(p, "profile ", 8)) { memcpy(o, p, n); o += n; }
        p += n;
    }
    *o = 0;
    if (o > both && o[-1] != '\n') strcat(both, "\n");
    if (by_machine) strcat(both, "profile auto\n");
    FreeVec(text);
    if (!write_file(LOOK_TEMP, both, strlen(both))) { FreeVec(both); return 0; }
    FreeVec(both);
    snprintf(cmd, sizeof cmd, "\"%s\" FROM \"%s\" SAVE", LOOK_TOOL, LOOK_TEMP);
    {
        LONG rc = SystemTags((STRPTR)cmd, TAG_DONE);
        DeleteFile((STRPTR)LOOK_TEMP);
        return rc == 0;
    }
}

/* ---- startup switches ---------------------------------------------------------------------- */

#define MAX_LINES 32

typedef struct sline {
    char text[256];                 /* the command, without ";OFF " */
    int on, was_on;
    char shown[120];                /* what the list shows */
} sline;

static sline lines[MAX_LINES];
static int nlines, line_sel = -1;

/* Plain words for the lines OpenUp's parts put there. */
static const char *about_line(const char *t)
{
    static const struct { const char *word, *about; } known[] = {
        { "OpenLook", "The theme on windows and gadgets (OpenLook)" },
        { "OpenMenus", "Our menus (OpenMenus)" },
        { "OpenDock", "The dock (OpenDock)" },
        { "OpenRTG", "Graphics card screens (OpenRTG)" },
        { "OpenSocket", "The network (OpenSocket)" },
        { "AddNetInterface", "The network connection" },
        { "Nursery", "The Cradle's services (Nursery)" },
        { "openservice", "The Cradle's services (Nursery)" },
        { "accontrol", "AmigaChrome's shared clipboard and files" },
        { "OpenTypes", "Open with... in the Tools menu (OpenTypes)" },
        { "OpenPrint", "Printing (OpenPrint)" },
        { "Windows", "Window snapping and the switcher (OpenPrefs Windows)" },
    };
    for (unsigned i = 0; i < sizeof known / sizeof known[0]; i++)
        if (strstr(t, known[i].word)) return known[i].about;
    return NULL;
}

static void show_line(sline *l)
{
    const char *a = about_line(l->text);
    snprintf(l->shown, sizeof l->shown, "%s  %s", l->on ? "[On] " : "[Off]", a ? a : l->text);
}

static void read_startup(void)
{
    char *text = read_file(USER_STARTUP, NULL), *p;
    int in = 0;
    nlines = 0;
    if (!text) return;
    for (p = text; *p && nlines < MAX_LINES; ) {
        char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        while (len && (p[len - 1] == '\r' || p[len - 1] == ' ')) len--;
        if (!in && len == (int)strlen(BLOCK_BEGIN) && !strncmp(p, BLOCK_BEGIN, len)) in = 1;
        else if (in && len == (int)strlen(BLOCK_END) && !strncmp(p, BLOCK_END, len)) in = 0;
        else if (in && len) {
            int off = !strncmp(p, OFF_MARK, strlen(OFF_MARK));
            const char *s = off ? p + strlen(OFF_MARK) : p;
            int n = len - (int)(s - p);
            /* a command too long to hold whole isn't offered: a cut one would never match */
            if ((off || p[0] != ';') && n < (int)sizeof lines[0].text) {
                sline *l = &lines[nlines++];
                memcpy(l->text, s, n); l->text[n] = 0;
                l->on = l->was_on = !off;
                show_line(l);
            }
        }
        if (!e) break;
        p = e + 1;
    }
    FreeVec(text);
}

/* Rewrites the block's lines with the switches, and lists what is off so
 * OpenUpTool STARTUP keeps them off when it writes the block again. */
static int write_startup(void)
{
    char *text, *out, *o, *p;
    int in = 0, changed = 0;
    LONG len = 0;
    for (int i = 0; i < nlines; i++) changed |= lines[i].on != lines[i].was_on;
    if (!changed) return 1;
    if (!(text = read_file(USER_STARTUP, &len))) return 0;
    if (!(out = AllocVec(len + nlines * 8 + 16, MEMF_ANY))) { FreeVec(text); return 0; }
    for (o = out, p = text; *p; ) {
        char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p), trim = n;
        while (trim && (p[trim - 1] == '\r' || p[trim - 1] == ' ')) trim--;
        if (!in && trim == (int)strlen(BLOCK_BEGIN) && !strncmp(p, BLOCK_BEGIN, trim)) in = 1;
        else if (in && trim == (int)strlen(BLOCK_END) && !strncmp(p, BLOCK_END, trim)) in = 0;
        else if (in && trim && (p[0] != ';' || !strncmp(p, OFF_MARK, strlen(OFF_MARK)))) {
            const char *s = !strncmp(p, OFF_MARK, strlen(OFF_MARK)) ? p + strlen(OFF_MARK) : p;
            int sn = trim - (int)(s - p);
            for (int i = 0; i < nlines; i++)
                if ((int)strlen(lines[i].text) == sn && !strncmp(lines[i].text, s, sn)) {
                    if (!lines[i].on) { strcpy(o, OFF_MARK); o += strlen(OFF_MARK); }
                    memcpy(o, s, sn); o += sn;
                    if (e) *o++ = '\n';
                    goto next;
                }
        }
        memcpy(o, p, n); o += n;
        if (e) *o++ = '\n';
    next:
        if (!e) break;
        p = e + 1;
    }
    *o = 0;
    FreeVec(text);
    {
        int ok = write_file(USER_STARTUP, out, strlen(out));
        FreeVec(out);
        if (!ok) return 0;
    }
    {
        static char list[MAX_LINES * 260];
        char *l = list;
        *l = 0;
        strcpy(l, "; Lines of OpenUp's startup block switched off in OpenUp Setup. OpenUpTool STARTUP keeps them off.\n");
        l += strlen(l);
        for (int i = 0; i < nlines; i++)
            if (!lines[i].on) { strcpy(l, lines[i].text); l += strlen(l); *l++ = '\n'; *l = 0; }
        /* without the list, OpenUpTool STARTUP would put the lines back on */
        if (!write_file(OFF_LIST, list, strlen(list))) return 0;
    }
    return 1;
}

/* ---- the window ------------------------------------------------------------------------------ */

enum { P_WELCOME, P_DESKTOP, P_DEVICES, P_STARTUP, P_DONE, P_COUNT };

enum {
    G_NOSHOW, G_VIEW,
    G_PROFILES, G_BYMACHINE, G_ED_LOOK, G_ED_MENUS, G_ED_WINDOWS, G_ED_DOCK, G_ED_TYPES,
    G_SCREENMODE, G_PRINTER, G_NETWORK, G_FONTS, G_ED_OPENPRINT,
    G_LINES, G_SWITCH,
    G_BACK, G_NEXT, G_FINISH, G_CANCEL, G_COUNT
};

static const char *const page_titles[P_COUNT] = {
    "Welcome to the Open family", "Your desktop", "Screen, printer and network", "What starts with the machine", "All set"
};

static const struct { int id; const char *label, *path; } editors[] = {
    { G_ED_LOOK, "Look...", "SYS:Prefs/Look" },
    { G_ED_MENUS, "Menus...", "SYS:Prefs/Menus" },
    { G_ED_WINDOWS, "Windows...", "SYS:Prefs/Windows" },
    { G_ED_DOCK, "Dock...", "SYS:Prefs/Dock" },
    { G_ED_TYPES, "File types...", "SYS:Prefs/OpenTypes" },
    { G_SCREENMODE, "Screen mode...", "SYS:Prefs/ScreenMode" },
    { G_PRINTER, "Printer...", "SYS:Prefs/Printer" },
    { G_ED_OPENPRINT, "OpenPrint...", "SYS:Prefs/OpenPrint" },
    { G_NETWORK, "Network...", "SYS:Prefs/OpenSocket" },
    { G_FONTS, "Fonts...", "SYS:Prefs/Font" },
};

static struct Gadget *gad[G_COUNT];
/* each page's gadgets, its Advanced-only gadgets, and the foot's */
static struct Gadget *page_glist[P_COUNT], *adv_glist[P_COUNT], *nav_glist;
static struct Window *win;
static struct List profile_list, line_list;
static struct Node profile_nodes[MAX_PROFILES], line_nodes[MAX_LINES];
static int page, noshow, cx, cy, cw, ch;          /* the page's area */
static int lists_on;                              /* the page's gadgets are in the window */
static void refresh_lists(void);

#define SET(id, ...) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE)

/* Simple goes Welcome, Desktop, Done. */
static int page_shown(int p) { return advanced || (p != P_DEVICES && p != P_STARTUP); }
static int page_step(int to, int dir) { do to += dir; while (to > 0 && to < P_DONE && !page_shown(to)); return to; }

static void count_steps(int *at, int *of)
{
    *at = *of = 0;
    for (int p = 0; p < P_COUNT; p++) if (page_shown(p)) { (*of)++; if (p <= page) (*at)++; }
}

static void new_list(struct List *l)
{
    l->lh_Head = (struct Node *)&l->lh_Tail; l->lh_Tail = NULL; l->lh_TailPred = (struct Node *)&l->lh_Head;
}

static void build_lists(void)
{
    new_list(&profile_list);
    for (int i = 0; i < nprofiles; i++) { profile_nodes[i].ln_Name = profile_names[i]; AddTail(&profile_list, &profile_nodes[i]); }
    new_list(&line_list);
    for (int i = 0; i < nlines; i++) { line_nodes[i].ln_Name = lines[i].shown; AddTail(&line_list, &line_nodes[i]); }
}

static int say_limit;                             /* no words below this row, when set */

/* Words in the page area, wrapped to its width; returns the next free row. */
static int say(struct RastPort *rp, int x, int y, int w, const char *text, int pen)
{
    const char *p = text;
    SetAPen(rp, pen); SetDrMd(rp, JAM1);
    while (*p && (!say_limit || y + rp->TxHeight <= say_limit)) {
        int n = 0, fit = 0;
        while (p[n] && p[n] != '\n') {
            n++;
            if (!p[n] || p[n] == ' ' || p[n] == '\n') {
                if (TextLength(rp, (STRPTR)p, n) > w) break;
                fit = n;
            }
        }
        if (!fit) fit = n ? n : 0;
        Move(rp, x, y + rp->TxBaseline);
        Text(rp, (STRPTR)p, fit);
        y += rp->TxHeight + 2;
        p += fit;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
    }
    return y;
}

static void draw_page(void)
{
    struct RastPort *rp = win->RPort;
    struct DrawInfo *dri = GetScreenDrawInfo(win->WScreen);
    int text = dri ? dri->dri_Pens[TEXTPEN] : 1, hi = dri ? dri->dri_Pens[HIGHLIGHTTEXTPEN] : 2, y = cy;
    char step[32];
    int at, of;
    EraseRect(rp, cx, cy, cx + cw - 1, cy + ch - 1);
    count_steps(&at, &of);
    snprintf(step, sizeof step, "Step %d of %d", at, of);
    y = say(rp, cx, y, cw, page_titles[page], hi);
    y = say(rp, cx, y, cw, step, text) + 6;
    switch (page) {
    case P_WELCOME:
        /* a long summary is cut short above the Simple and Advanced choice */
        say_limit = cy + ch - (rp->TxHeight + 6) - 2 * (rp->TxHeight + 6) - 14;
        y = say(rp, cx, y, cw, summary, text) + 6;
        y = say(rp, cx, y, cw, "OpenUp Setup takes you through the few choices that make this Amiga yours. Nothing changes until you choose Finish, and you can run it again from Prefs at any time.", text) + 6;
        say(rp, cx, y, cw, "How much would you like to see? You can change this later in any OpenPrefs editor's View menu.", text);
        say_limit = 0;
        break;
    case P_DESKTOP:
        say(rp, cx + 230, y + 2, cw - 230,
            profile_sel >= 0 && profile_about[profile_sel][0] ? profile_about[profile_sel] :
            advanced ? "Choose how the desktop looks. Each editor below sets one part of it in more detail." :
                       "Choose how the desktop looks. That's all you need; the rest is ready.", text);
        break;
    case P_DEVICES:
        say(rp, cx, y, cw, "The screen mode, the printer and the network each have their own editor. Open the ones you want to set now; the rest can wait.", text);
        break;
    case P_STARTUP:
        say(rp, cx, y, cw, "These start with the machine. Switch one off to take it out cleanly; switch it on again to put it back. The change takes effect at the next start.", text);
        break;
    case P_DONE:
        say(rp, cx, y, cw, "Choose Finish to put your choices in place. OpenUp Setup stays in SYS:Prefs if you want to change them later.", text);
        break;
    }
    if (dri) FreeScreenDrawInfo(win->WScreen, dri);
    refresh_lists();                              /* the erase went over them */
}

static int adv_on;                                /* advanced, as when the lists went in */

static void refresh_lists(void)
{
    if (!lists_on) return;
    if (page_glist[page]) RefreshGList(page_glist[page], win, NULL, -1);
    if (adv_on && adv_glist[page]) RefreshGList(adv_glist[page], win, NULL, -1);
}

static void put_lists(int add)
{
    if (add == lists_on) return;
    if (add) adv_on = advanced;
    if (page_glist[page]) { if (add) AddGList(win, page_glist[page], -1, -1, NULL); else RemoveGList(win, page_glist[page], -1); }
    if (adv_on && adv_glist[page]) { if (add) AddGList(win, adv_glist[page], -1, -1, NULL); else RemoveGList(win, adv_glist[page], -1); }
    lists_on = add;
    refresh_lists();
}

static void show_page(int to)
{
    put_lists(0);
    page = to;
    draw_page();
    put_lists(1);
    GT_RefreshWindow(win, NULL);
    SET(G_BACK, GA_Disabled, page == 0);
}

static int finish(void)
{
    int ok = apply_profile();
    write_view();
    ok = write_startup() && ok;
    /* only when everything is in place, so the next start offers it again otherwise */
    if (ok) ok = write_file(DONE_FILE, "OpenUp Setup finished\n", 22);
    return ok;
}

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *g;
    struct NewGadget ng;
    int fh, lh, top, W = 560, H, L = 12, quit = 0, rc = RETURN_OK, i, row, wtop;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    cx = L; cy = top; cw = W - 2 * L;
    /* as tall as the tallest page (Desktop: its list, the tick and two rows of
     * buttons), so the window fits a 256-line PAL Workbench with an 8-point font */
    ch = 2 * (fh + 2) + 8 + 8 * (fh + 1) + 20 + lh + (lh + 6) + lh + 4;
    H = cy + ch + 10 + lh + 8;
    build_lists();
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
    row = cy + 2 * (fh + 2) + 8;          /* below the page's title and step */

    /* the pages' gadgets, one list each */
    g = CreateContext(&page_glist[P_WELCOME]);
    {
        static STRPTR views[] = { (STRPTR)"Simple: choose a theme and go", (STRPTR)"Advanced: every page and setting", NULL };
        G(MX_KIND, G_VIEW, cx + 4, cy + ch - lh - 2 * (fh + 6) - 10, 17, fh + 1, NULL, PLACETEXT_RIGHT,
          GTMX_Labels, (ULONG)views, GTMX_Active, advanced, GTMX_Spacing, 4, GTMX_Scaled, TRUE);
    }
    G(CHECKBOX_KIND, G_NOSHOW, cx, cy + ch - lh, 26, lh, "Don't show this at start again", PLACETEXT_RIGHT, GTCB_Scaled, TRUE);

    g = CreateContext(&page_glist[P_DESKTOP]);
    G(LISTVIEW_KIND, G_PROFILES, cx, row + fh + 4, 210, 6 * (fh + 1) + 4, "Desktop profile", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&profile_list, GTLV_ShowSelected, NULL, GTLV_Selected, (ULONG)profile_sel);
    G(CHECKBOX_KIND, G_BYMACHINE, cx, row + 8 * (fh + 1) + 12, 26, lh, "Choose by machine at every start", PLACETEXT_RIGHT,
      GTCB_Scaled, TRUE, GTCB_Checked, by_machine);
    g = CreateContext(&adv_glist[P_DESKTOP]);
    for (i = 0; i < 5; i++)
        G(BUTTON_KIND, editors[i].id, cx + (i % 3) * ((cw + 8) / 3), row + 8 * (fh + 1) + 20 + lh + (i / 3) * (lh + 6),
          (cw + 8) / 3 - 8, lh, editors[i].label, 0, GA_Disabled, !exists(editors[i].path));

    g = CreateContext(&page_glist[P_DEVICES]);
    for (i = 5; i < 10; i++)
        G(BUTTON_KIND, editors[i].id, cx + ((i - 5) % 2) * (cw / 2), row + 3 * (fh + 2) + ((i - 5) / 2) * (lh + 6),
          cw / 2 - 8, lh, editors[i].label, 0, GA_Disabled, !exists(editors[i].path));

    g = CreateContext(&page_glist[P_STARTUP]);
    G(LISTVIEW_KIND, G_LINES, cx, row + 3 * (fh + 2), cw, 7 * (fh + 1) + 4, NULL, 0,
      GTLV_Labels, (ULONG)&line_list, GTLV_ShowSelected, NULL);
    G(BUTTON_KIND, G_SWITCH, cx, row + 3 * (fh + 2) + 7 * (fh + 1) + 10, 200, lh, "_Switch on or off", 0,
      GT_Underscore, '_', GA_Disabled, TRUE);

    /* the buttons along the foot: Back and Later always there, Next on
     * every page but the last, which has Finish in its place */
    {
        int bw = (W - 2 * L - 20) / 3, y = H - lh - 8, ok = 1;
        for (int p = 0; p < P_COUNT; p++) {
            struct Gadget *last = page_glist[p];
            if (p == P_DONE) g = CreateContext(&page_glist[p]);
            else { for (g = last; g && g->NextGadget; g = g->NextGadget) ; }
            if (p == P_DONE) G(BUTTON_KIND, G_FINISH, L + bw + 10, y, bw, lh, "_Finish", 0, GT_Underscore, '_');
            else G(BUTTON_KIND, G_NEXT, L + bw + 10, y, bw, lh, "_Next", 0, GT_Underscore, '_');
            ok &= g != NULL;
        }
        g = CreateContext(&nav_glist);
        G(BUTTON_KIND, G_BACK, L, y, bw, lh, "_Back", 0, GT_Underscore, '_', GA_Disabled, TRUE);
        G(BUTTON_KIND, G_CANCEL, L + 2 * (bw + 10), y, bw, lh, "_Later", 0, GT_Underscore, '_');
        if (!ok) g = NULL;
    }
    if (!g || !page_glist[P_WELCOME] || !page_glist[P_DESKTOP] || !adv_glist[P_DESKTOP] || !page_glist[P_DEVICES] || !page_glist[P_STARTUP]) { rc = RETURN_FAIL; goto out; }

    {
        /* the whole window on the screen: higher up, over the screen bar if need be */
        int tall = H + scr->WBorBottom;
        wtop = scr->BarHeight + 20;
        if (wtop + tall > scr->Height) wtop = scr->Height - tall > 0 ? scr->Height - tall : 0;
    }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"OpenUp Setup", WA_ScreenTitle, (ULONG)"OpenUp Setup 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, wtop,
                         WA_InnerWidth, W, WA_InnerHeight, H - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)nav_glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_Activate, TRUE, WA_SmartRefresh, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | BUTTONIDCMP | LISTVIEWIDCMP | CHECKBOXIDCMP | MXIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    GT_RefreshWindow(win, NULL);
    page = P_WELCOME;
    draw_page();
    put_lists(1);
    GT_RefreshWindow(win, NULL);

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int id = -1;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) id = G_CANCEL;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); draw_page(); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_VANILLAKEY) {
                int k = code | 0x20;
                id = k == 'n' && page < P_DONE ? G_NEXT : k == 'f' && page == P_DONE ? G_FINISH : k == 'b' ? G_BACK : k == 'l' || code == 27 ? G_CANCEL :
                     k == 's' && page == P_STARTUP ? G_SWITCH : -1;
                if (id == G_BACK && page == 0) id = -1;
            } else if (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN || cls == IDCMP_MOUSEMOVE) id = gg->GadgetID;
            switch (id) {
            case G_NOSHOW: noshow = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_VIEW: advanced = code == 1; draw_page(); break;
            case G_PROFILES: profile_sel = code; draw_page(); break;
            case G_BYMACHINE: by_machine = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_LINES: line_sel = code; SET(G_SWITCH, GA_Disabled, FALSE); break;
            case G_SWITCH:
                if (line_sel >= 0 && line_sel < nlines) {
                    SET(G_LINES, GTLV_Labels, ~0UL);
                    lines[line_sel].on = !lines[line_sel].on;
                    show_line(&lines[line_sel]);
                    SET(G_LINES, GTLV_Labels, (ULONG)&line_list, GTLV_Selected, line_sel);
                }
                break;
            case G_BACK: if (page > 0) show_page(page_step(page, -1)); break;
            case G_NEXT: if (page < P_DONE) show_page(page_step(page, 1)); break;
            case G_FINISH:
                if (!finish()) {
                    say(win->RPort, cx, cy + ch - 2 * (fh + 2), cw, "Some choices couldn't be written: is the system disk write-protected?", 1);
                    Delay(150);
                    rc = RETURN_WARN;
                }
                quit = 1;
                break;
            case G_CANCEL:
                if (noshow) write_file(DONE_FILE, "OpenUp Setup: not shown at start\n", 33);
                quit = 1;
                break;
            default:
                for (i = 0; i < (int)(sizeof editors / sizeof editors[0]); i++)
                    if (editors[i].id == id) run_tool(editors[i].path);
            }
        }
    }
out:
    if (win) {
        put_lists(0);
        CloseWindow(win);
    }
    for (i = 0; i < P_COUNT; i++) { FreeGadgets(page_glist[i]); FreeGadgets(adv_glist[i]); }
    FreeGadgets(nav_glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

/* Started from SYS:WBStartup: the first start. The tool itself there, or a
 * project icon there whose default tool is SYS:Prefs/OpenUp-Setup. */
static int from_wbstartup(void)
{
    char path[256];
    if (!_WBenchMsg) return 0;
    for (LONG i = 0; i < _WBenchMsg->sm_NumArgs; i++) {
        if (!_WBenchMsg->sm_ArgList[i].wa_Lock || !NameFromLock(_WBenchMsg->sm_ArgList[i].wa_Lock, (STRPTR)path, sizeof path)) continue;
        for (char *s = path; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
        if (strstr(path, "WBSTARTUP")) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int first = 0, rc;
    if (argc > 0) {                              /* from the Shell */
        LONG args[2] = { 0, 0 };
        struct RDArgs *rd = ReadArgs((STRPTR)"FIRSTSTART/S,ADVANCED/S", args, NULL);
        if (!rd) { PrintFault(IoErr(), (STRPTR)"OpenUp-Setup"); return RETURN_FAIL; }
        first = args[0] != 0;
        read_view();
        if (args[1]) advanced = 1;
        FreeArgs(rd);
    } else { first = from_wbstartup(); read_view(); }
    (void)argv;
    if (first && exists(DONE_FILE)) return RETURN_OK;
    read_summary();
    read_profiles();
    read_startup();
    rc = gui();
    return rc;
}
