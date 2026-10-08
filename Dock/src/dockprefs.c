/* OpenPrefs Dock 0.2: the editor for OpenDock (MIT). A GadTools prefs
 * editor, as the OS's own: Save, Use, Test (put back after 15 seconds
 * unless kept) and Cancel. What it sets lives in ENV:OpenDock/Dock
 * (ENVARC: when saved), in the format of od_dock.h; OpenDock draws.
 *
 *   - where the dock sits (bottom, top, left, right), its size, names under
 *     the icons, and its shelf: solid, clear or glass, and how see-through
 *   - its buttons, in order: moved up and down, removed, separators added
 *   - another dock's buttons, taken over: ToolManager 2, AmiDock (AmigaOS 4's)
 *     or AmiStart's taskbar; on the first start, the first one found
 *   - in the Advanced view, each button's kind (Workbench, Shell, ARexx),
 *     name, command, icon, drawer and stack, the running marks, the names
 *     that pop up under the pointer and the hop when a program starts
 *
 *   Dock [FROM file] [TOOLMANAGER file] [AMIDOCK file] [AMISTART file] [USE] [SAVE] [ADVANCED] [ADD program ...]
 *
 * From the Shell, TOOLMANAGER, AMIDOCK or AMISTART takes that file over and
 * USE or SAVE puts it in place with no window: OpenUp's part does this once.
 * ADD puts a button at the end for each program that's on this machine and
 * not on the dock yet, so OpenUp can add OpenFiles and OpenView to a dock
 * someone already has without touching the rest.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/workbench.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <proto/icon.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "od_dock.h"

const char version[] __attribute__((used)) = "$VER: Dock 0.2 (7.10.2026) OpenPrefs, Dalsin Limited";

#define PREFS_ENV "ENV:OpenDock/Dock"
#define PREFS_ENVARC "ENVARC:OpenDock/Dock"
#define VIEW_ENV "ENV:OpenAmiga/PrefsView"
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"
#define TEST_SECONDS 15

struct Library *AslBase, *IconBase;

static od_dock cur, orig;                /* orig: what is in ENV: now, which Test and Cancel put back */
static int advanced;

/* ---- files -------------------------------------------------------------------------------- */

static char *read_file(const char *path, LONG *lenp)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG len;
    char *buf;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len < 0 || len > 512 * 1024 || !(buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) { Close(fh); return NULL; }
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
    if ((slash = strrchr(dir, '/'))) {
        *slash = 0;
        if ((lock = Lock((STRPTR)dir, ACCESS_READ))) UnLock(lock);
        else if ((lock = CreateDir((STRPTR)dir))) UnLock(lock);
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    if (Write(fh, (APTR)data, len) != len) { Close(fh); return 0; }
    Close(fh);
    return 1;
}

/* Looking for files on volumes that may not be there: no "Please insert" requesters. */
static APTR quiet(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR old = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    return old;
}
static void unquiet(APTR old) { ((struct Process *)FindTask(NULL))->pr_WindowPtr = old; }

static int exists(const char *path)
{
    APTR q = quiet();
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    unquiet(q);
    if (l) UnLock(l);
    return l != 0;
}

static int tell_opendock(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)"OpenDock"))) Signal(p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
    return p != NULL;
}

static int write_prefs(const od_dock *d, int save)
{
    static char text[32768];
    int n = od_write(d, text, sizeof text);
    if (n < 0) return 0;
    if (!write_file(PREFS_ENV, text, n)) return 0;
    if (save && !write_file(PREFS_ENVARC, text, n)) return 0;
    return 1;
}

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

/* ---- other docks' settings ------------------------------------------------------------------ */

enum { SRC_TOOLMANAGER, SRC_AMIDOCK, SRC_AMISTART, SRC_COUNT };
static const char *src_labels[] = { "ToolManager", "AmiDock (AmigaOS 4)", "AmiStart", NULL };

/* Where AmiStart might be: its icon's PREFS tooltype says which file (PROGDIR:
 * is the icon's drawer), DATAPATH where its relative icons are. */
static int find_amistart(char *file, int fsize, char *base, int bsize)
{
    static const char *const tools[] = {
        "SYS:WBStartup/AmiStart", "SYS:Utilities/AmiStart/AmiStart", "SYS:Tools/AmiStart/AmiStart",
        "Work:AmiStart/AmiStart", "Work:Utilities/AmiStart/AmiStart", "AmiKit:Utilities/EXPANSION/AmiStart/AmiStart", NULL
    };
    static const char *const files[] = { "AmiKit:Utilities/EXPANSION/AmiStart/sm.config", "ENVARC:Icaros/sm.prefs", NULL };
    APTR q = quiet();
    int found = 0;
    for (int i = 0; tools[i] && !found && IconBase; i++) {
        struct DiskObject *dob = GetDiskObject((STRPTR)tools[i]);
        char dir[160], *tt;
        if (!dob) continue;
        strncpy(dir, tools[i], sizeof dir - 1); dir[sizeof dir - 1] = 0;
        *PathPart((STRPTR)dir) = 0;
        tt = dob->do_ToolTypes ? (char *)FindToolType((CONST_STRPTR *)dob->do_ToolTypes, (STRPTR)"PREFS") : NULL;
        if (tt && !strnicmp(tt, "PROGDIR:", 8)) { strncpy(file, dir, fsize - 1); file[fsize - 1] = 0; AddPart((STRPTR)file, (STRPTR)tt + 8, fsize); }
        else if (tt) { strncpy(file, tt, fsize - 1); file[fsize - 1] = 0; }
        else { strncpy(file, dir, fsize - 1); file[fsize - 1] = 0; AddPart((STRPTR)file, (STRPTR)"sm.prefs", fsize); }
        tt = dob->do_ToolTypes ? (char *)FindToolType((CONST_STRPTR *)dob->do_ToolTypes, (STRPTR)"DATAPATH") : NULL;
        strncpy(base, tt ? tt : dir, bsize - 1); base[bsize - 1] = 0;
        FreeDiskObject(dob);
        found = exists(file);
    }
    for (int i = 0; files[i] && !found; i++)
        if (exists(files[i])) {
            strncpy(file, files[i], fsize - 1); file[fsize - 1] = 0;
            strncpy(base, files[i], bsize - 1); base[bsize - 1] = 0;
            *PathPart((STRPTR)base) = 0;
            found = 1;
        }
    unquiet(q);
    return found;
}

/* The file a source keeps its settings in, if there is one here. */
static int find_source(int src, char *file, int fsize, char *base, int bsize)
{
    base[0] = 0;
    if (src == SRC_TOOLMANAGER) {
        const char *f = exists("ENVARC:ToolManager.prefs") ? "ENVARC:ToolManager.prefs" : exists("ENV:ToolManager.prefs") ? "ENV:ToolManager.prefs" : NULL;
        if (f) { strncpy(file, f, fsize - 1); file[fsize - 1] = 0; }
        return f != NULL;
    }
    if (src == SRC_AMIDOCK) {
        const char *f = exists("ENVARC:Sys/AmiDock.amiga.com.xml") ? "ENVARC:Sys/AmiDock.amiga.com.xml" :
                        exists("ENVARC:Sys/amidock.prefs") ? "ENVARC:Sys/amidock.prefs" : NULL;
        if (f) { strncpy(file, f, fsize - 1); file[fsize - 1] = 0; }
        return f != NULL;
    }
    return find_amistart(file, fsize, base, bsize);
}

/* Workbench buttons whose program isn't on this machine go: a dock of
 * empty buttons helps no one. The number taken out. */
static int drop_missing(od_dock *d)
{
    int gone = 0;
    for (int i = 0; i < d->n; ) {
        if (d->b[i].kind == OD_WB && !exists(d->b[i].command)) {
            memmove(&d->b[i], &d->b[i + 1], (d->n - i - 1) * sizeof d->b[0]);
            d->n--;
            gone++;
        } else i++;
    }
    /* no separator left at an end, or beside another */
    for (int i = 0; i < d->n; ) {
        if (d->b[i].kind == OD_SEPARATOR && (i == 0 || i == d->n - 1 || d->b[i + 1].kind == OD_SEPARATOR)) {
            memmove(&d->b[i], &d->b[i + 1], (d->n - i - 1) * sizeof d->b[0]);
            d->n--;
        } else i++;
    }
    return gone;
}

/* A file's buttons after d's own. 1, or 0 with the reason in err. */
static int take_over(od_dock *d, const char *file, const char *base, od_report *r, char *err, int errlen)
{
    LONG len = 0;
    char *text = read_file(file, &len);
    int ok;
    if (!text) { snprintf(err, errlen, "%s can't be read", file); return 0; }
    switch (od_sniff((const unsigned char *)text, len)) {
    case 1: ok = od_from_toolmanager(d, (const unsigned char *)text, len, r, err, errlen); break;
    case 2: ok = od_from_amidock(d, text, r, err, errlen); break;
    case 3: {
        char b[160];
        if (base && base[0]) { strncpy(b, base, sizeof b - 1); b[sizeof b - 1] = 0; }
        else { strncpy(b, file, sizeof b - 1); b[sizeof b - 1] = 0; *PathPart((STRPTR)b) = 0; }
        ok = od_from_amistart(d, text, b, r, err, errlen);
        break;
    }
    default:
        ok = 0;
        snprintf(err, errlen, "it isn't ToolManager's, AmiDock's (AmigaOS 4) or AmiStart's");
    }
    FreeVec(text);
    if (ok) {
        int gone = drop_missing(d);
        strncpy(d->imported, file, sizeof d->imported - 1); d->imported[sizeof d->imported - 1] = 0;
        if (r) { r->added -= gone; r->skipped += gone; }
        if (r && r->added <= 0) { snprintf(err, errlen, "none of its programs are on this machine"); ok = 0; }
    }
    return ok;
}

/* ---- the window ---------------------------------------------------------------------------- */

enum {
    G_PLACE, G_SIZE, G_ITEMS, G_BORDER, G_LABELS, G_RUNNING, G_SHELF, G_OPACITY, G_HOVER, G_HOP, G_LIST, G_UP, G_DOWN, G_REMOVE, G_SEPARATOR,
    G_SOURCE, G_TAKEOVER, G_KIND, G_NAME, G_COMMAND, G_ICON, G_DIR, G_STACK,
    G_STATUS, G_SAVE, G_USE, G_TEST, G_CANCEL, G_COUNT
};

static const char *place_labels[] = { "Bottom", "Top", "Left", "Right", NULL };
static const char *size_labels[] = { "Small", "Medium", "Large", NULL };
static const char *items_labels[] = { "100%", "75%", "50%", "25%", NULL };   /* the icons' size on the dock */
static const int items_scale[] = { 100, 75, 50, 25 };
static const char *kind_labels[] = { "Workbench", "Shell", "ARexx", NULL };
/* OD_BG_ order. "Clear" is the file's see-through: tinted, not frosted (the slider says how see-through) */
static const char *shelf_labels[] = { "Solid", "Clear", "Glass", NULL };

static struct Gadget *gad[G_COUNT];
static struct Window *win;
static struct List list;
static struct Node nodes[OD_MAX];
static char shown[OD_MAX][64];
static int sel = -1, source;
static char status_text[120];

#define SET(id, ...) (gad[id] ? GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE) : (void)0)

static void status(const char *s)
{
    if (s != status_text) { strncpy(status_text, s, sizeof status_text - 1); status_text[sizeof status_text - 1] = 0; }
    SET(G_STATUS, GTTX_Text, (ULONG)status_text);
}

static void build_list(void)
{
    list.lh_Head = (struct Node *)&list.lh_Tail; list.lh_Tail = NULL; list.lh_TailPred = (struct Node *)&list.lh_Head;
    for (int i = 0; i < cur.n; i++) {
        const od_button *b = &cur.b[i];
        if (b->kind == OD_SEPARATOR) strcpy(shown[i], "----------");
        else if (advanced) snprintf(shown[i], sizeof shown[i], "%s  (%s)", b->label, b->kind == OD_CLI ? "Shell" : b->kind == OD_AREXX ? "ARexx" : "Workbench");
        else snprintf(shown[i], sizeof shown[i], "%s", b->label);
        nodes[i].ln_Name = shown[i];
        AddTail(&list, &nodes[i]);
    }
}

static void show(void)
{
    const od_button *b = sel >= 0 && sel < cur.n ? &cur.b[sel] : NULL;
    int prog = b && b->kind != OD_SEPARATOR;
    SET(G_LIST, GTLV_Labels, ~0UL);
    build_list();
    SET(G_LIST, GTLV_Labels, (ULONG)&list, GTLV_Selected, b ? (ULONG)sel : ~0UL);
    SET(G_PLACE, GTCY_Active, cur.place);
    SET(G_SIZE, GTCY_Active, cur.size);
    SET(G_ITEMS, GTCY_Active, cur.scale == 75 ? 1 : cur.scale == 50 ? 2 : cur.scale == 25 ? 3 : 0);
    SET(G_BORDER, GTCB_Checked, cur.border);
    SET(G_LABELS, GTCB_Checked, cur.labels);
    SET(G_RUNNING, GTCB_Checked, cur.running);
    SET(G_SHELF, GTCY_Active, cur.background);
    SET(G_OPACITY, GTSL_Level, cur.background == OD_BG_SOLID ? 100 : cur.opacity, GA_Disabled, cur.background == OD_BG_SOLID);
    SET(G_HOVER, GTCB_Checked, cur.hover);
    SET(G_HOP, GTCB_Checked, cur.hop);
    SET(G_UP, GA_Disabled, !b || sel == 0);
    SET(G_DOWN, GA_Disabled, !b || sel == cur.n - 1);
    SET(G_REMOVE, GA_Disabled, !b);
    SET(G_SEPARATOR, GA_Disabled, cur.n >= OD_MAX);
    SET(G_SOURCE, GTCY_Active, source);
    SET(G_KIND, GTCY_Active, prog ? b->kind : 0, GA_Disabled, !prog);
    SET(G_NAME, GTST_String, (ULONG)(prog ? b->label : ""), GA_Disabled, !prog);
    SET(G_COMMAND, GTST_String, (ULONG)(prog ? b->command : ""), GA_Disabled, !prog);
    SET(G_ICON, GTST_String, (ULONG)(prog ? b->icon : ""), GA_Disabled, !prog);
    SET(G_DIR, GTST_String, (ULONG)(prog ? b->dir : ""), GA_Disabled, !prog || b->kind != OD_CLI);
    SET(G_STACK, GTIN_Number, prog ? b->stack : 4096, GA_Disabled, !prog);
}

static ULONG now_seconds(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400 + (ULONG)ds.ds_Minute * 60 + (ULONG)ds.ds_Tick / TICKS_PER_SECOND;
}

static ULONG test_until;

static int put_in_place(int save)
{
    if (!write_prefs(&cur, save)) {
        status(save ? "The settings couldn't be saved. Is the system disk write-protected or full?" : "The settings couldn't be written. Is ENV: full?");
        return 0;
    }
    if (!tell_opendock() && cur.n) status("OpenDock isn't running: it starts with the machine, or run C:OpenDock.");
    return 1;
}

static void go_back(void)
{
    write_prefs(&orig, 0);
    tell_opendock();
}

static void take_over_source(void)
{
    char file[256], base[160], err[120];
    od_report r;
    if (!find_source(source, file, sizeof file, base, sizeof base)) {
        /* not where it usually is: ask */
        struct FileRequester *fr = AslBase ? AllocAslRequestTags(ASL_FileRequest, ASLFR_TitleText, (ULONG)"Which settings file?",
                                                                 ASLFR_Window, (ULONG)win, ASLFR_InitialDrawer, (ULONG)"ENVARC:", TAG_DONE) : NULL;
        int got = 0;
        if (fr && AslRequest(fr, NULL)) {
            strncpy(file, (char *)fr->fr_Drawer, sizeof file - 1); file[sizeof file - 1] = 0;
            AddPart((STRPTR)file, fr->fr_File, sizeof file);
            got = 1;
        }
        if (fr) FreeAslRequest(fr);
        if (!got) return;
        base[0] = 0;
    }
    if (take_over(&cur, file, base, &r, err, sizeof err)) {
        snprintf(status_text, sizeof status_text, "Took over %d button%s from %s%s. Use, Test or Save to put them in place.",
                 r.added, r.added == 1 ? "" : "s", src_labels[source], r.full ? " (the dock is full)" : "");
        status(status_text);
    } else {
        snprintf(status_text, sizeof status_text, "Couldn't take that over: %s.", err);
        status(status_text);
    }
}

/* The gadgets for the view; the window's inner height, or 0. */
static int make_gadgets(struct Screen *scr, APTR vi, struct Gadget **glist, int W)
{
    struct Gadget *g;
    struct NewGadget ng;
    int fh = scr->Font->ta_YSize, lh = fh + 6, top = scr->WBorTop + fh + 1 + 6, L = 10, R = 300, i, row, row2;
    int gp = scr->Height < 320 ? 2 : 4, listh = 7 * (fh + 1) + 4;
    memset(gad, 0, sizeof gad);
    g = CreateContext(glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))

    build_list();
    /* the left column: the buttons */
    row = top + fh + 4;
    G(LISTVIEW_KIND, G_LIST, L, row, R - L - 20, listh, "Buttons", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&list, GTLV_ShowSelected, NULL, GTLV_Selected, sel >= 0 ? (ULONG)sel : ~0UL);
    row += listh + gp;
    {
        int bw = (R - L - 20 - 3 * 4) / 4;
        G(BUTTON_KIND, G_UP, L, row, bw, lh, "Up", 0, GA_Disabled, TRUE);
        G(BUTTON_KIND, G_DOWN, L + (bw + 4), row, bw, lh, "Down", 0, GA_Disabled, TRUE);
        G(BUTTON_KIND, G_REMOVE, L + 2 * (bw + 4), row, bw, lh, "Remove", 0, GA_Disabled, TRUE);
        G(BUTTON_KIND, G_SEPARATOR, L + 3 * (bw + 4), row, bw, lh, "Line", 0, GA_Disabled, FALSE);
    }
    row += lh + 2 * gp;
    /* how the dock looks: the shelf, and in Advanced the names that pop up and the hop */
    G(CYCLE_KIND, G_SHELF, L + 60, row, R - L - 80, lh, "Shelf", PLACETEXT_LEFT, GTCY_Labels, (ULONG)shelf_labels); row += lh + gp;
    {
        /* how see-through the shelf is: from clear at the left to solid at the right (the file's opacity) */
        struct RastPort *srp = &scr->RastPort;
        int lw = TextLength(srp, (STRPTR)"See-through", 11) + 8, sw = TextLength(srp, (STRPTR)"Solid", 5) + 8;
        G(SLIDER_KIND, G_OPACITY, L + lw, row, R - L - 20 - lw - sw, lh, "See-through", PLACETEXT_LEFT,
          GTSL_Min, 0, GTSL_Max, 100, GTSL_Level, cur.background == OD_BG_SOLID ? 100 : cur.opacity,
          GTSL_LevelFormat, (ULONG)"Solid", GTSL_MaxLevelLen, 5, GTSL_LevelPlace, PLACETEXT_RIGHT,
          GA_RelVerify, TRUE, GA_Disabled, cur.background == OD_BG_SOLID);
        row += lh + gp;
    }
    if (advanced) {
        G(CHECKBOX_KIND, G_HOVER, L + 110, row, 26, lh, "Pop-up names", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        G(CHECKBOX_KIND, G_HOP, L + 220, row, 26, lh, "Hop", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        row += lh + gp;
    }
    /* taking over another dock */
    row += gp;
    G(CYCLE_KIND, G_SOURCE, L + 60, row, R - L - 80, lh, "From", PLACETEXT_LEFT, GTCY_Labels, (ULONG)src_labels); row += lh + gp;
    G(BUTTON_KIND, G_TAKEOVER, L + 60, row, R - L - 80, lh, "Take over its buttons", 0, GA_Disabled, FALSE); row += lh + gp;

    /* the right column: the dock; in Advanced, the chosen button. Both columns
     * are kept short enough for a 256-line PAL Workbench. */
    row2 = top;
    G(CYCLE_KIND, G_PLACE, R + 80, row2, W - R - 90, lh, "Place", PLACETEXT_LEFT, GTCY_Labels, (ULONG)place_labels); row2 += lh + gp;
    G(CYCLE_KIND, G_SIZE, R + 80, row2, W - R - 90, lh, "Size", PLACETEXT_LEFT, GTCY_Labels, (ULONG)size_labels); row2 += lh + gp;
    G(CYCLE_KIND, G_ITEMS, R + 80, row2, W - R - 90, lh, "Items", PLACETEXT_LEFT, GTCY_Labels, (ULONG)items_labels); row2 += lh + gp;
    G(CHECKBOX_KIND, G_BORDER, R + 80, row2, 26, lh, "Edges", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row2 += lh + gp;
    G(CHECKBOX_KIND, G_LABELS, R + 80, row2, 26, lh, "Names", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    if (advanced) G(CHECKBOX_KIND, G_RUNNING, R + 220, row2, 26, lh, "Running", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    row2 += lh + 2 * gp;
    if (advanced) {
        /* the chosen button, in full */
        G(CYCLE_KIND, G_KIND, R + 80, row2, W - R - 200, lh, "Starts", PLACETEXT_LEFT, GTCY_Labels, (ULONG)kind_labels);
        G(INTEGER_KIND, G_STACK, W - 70, row2, 60, lh, "Stack", PLACETEXT_LEFT, GTIN_MaxChars, 7, GTIN_Number, 4096); row2 += lh + gp;
        G(STRING_KIND, G_NAME, R + 80, row2, W - R - 90, lh, "Name", PLACETEXT_LEFT, GTST_MaxChars, 46); row2 += lh + gp;
        G(STRING_KIND, G_COMMAND, R + 80, row2, W - R - 90, lh, "Command", PLACETEXT_LEFT, GTST_MaxChars, 254); row2 += lh + gp;
        G(STRING_KIND, G_ICON, R + 80, row2, W - R - 90, lh, "Icon", PLACETEXT_LEFT, GTST_MaxChars, 158); row2 += lh + gp;
        G(STRING_KIND, G_DIR, R + 80, row2, W - R - 90, lh, "Drawer", PLACETEXT_LEFT, GTST_MaxChars, 126); row2 += lh + gp;
    }
    if (row2 > row) row = row2;
    row += gp;
    G(TEXT_KIND, G_STATUS, L, row, W - 20, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + gp + 2;
    {
        static const char *const names[4] = { "_Save", "_Use", "_Test", "_Cancel" };
        static const int ids[4] = { G_SAVE, G_USE, G_TEST, G_CANCEL };
        int bw = (W - 20 - 3 * 10) / 4;
        for (i = 0; i < 4; i++) G(BUTTON_KIND, ids[i], L + i * (bw + 10), row, bw, lh, names[i], 0, GT_Underscore, '_');
        row += lh + gp + 2;
    }
#undef G
    return g ? row - scr->WBorTop - fh - 1 : 0;
}

enum { M_QUIT = 1, M_ADVANCED };
static struct NewMenu newmenus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, (STRPTR)"View", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Advanced", (STRPTR)"A", CHECKIT | MENUTOGGLE, 0, (APTR)M_ADVANCED },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static int handle_key(UWORD code)
{
    switch (code | 0x20) {
    case 's': return G_SAVE;
    case 'u': return G_USE;
    case 't': return G_TEST;
    case 'c': return G_CANCEL;
    }
    return code == 27 ? G_CANCEL : -1;
}

static void swap(int a, int b)
{
    od_button t = cur.b[a];
    cur.b[a] = cur.b[b];
    cur.b[b] = t;
}

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL;
    struct Menu *menus = NULL;
    int quit = 0, rc = RETURN_OK, W = 600, inner, wx = 40, wy;
    struct MsgPort *tport = NULL;
    struct timerequest *tr = NULL;
    int timer = 0;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    wy = scr->BarHeight + 10;
    if (!status_text[0]) strcpy(status_text, "Drop icons on the dock to add them. Test shows a change for 15 seconds.");
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);

  reopen:
    newmenus[3].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    if (!(inner = make_gadgets(scr, vi, &glist, W))) { rc = RETURN_FAIL; goto out; }
    if (!(menus = CreateMenus(newmenus, GTMN_FullMenu, TRUE, TAG_DONE)) || !LayoutMenus(menus, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) {
        rc = RETURN_FAIL; goto out;
    }
    {
        int tall = inner + scr->WBorTop + scr->Font->ta_YSize + 1 + scr->WBorBottom;
        if (wy + tall > scr->Height) wy = scr->Height - tall > 0 ? scr->Height - tall : 0;
    }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Dock", WA_ScreenTitle, (ULONG)"OpenPrefs Dock 0.2", WA_PubScreen, (ULONG)scr,
                         WA_Left, wx, WA_Top, wy, WA_InnerWidth, W, WA_InnerHeight, inner,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_MENUPICK | BUTTONIDCMP | CYCLEIDCMP |
                                   STRINGIDCMP | INTEGERIDCMP | CHECKBOXIDCMP | LISTVIEWIDCMP | SLIDERIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    SetMenuStrip(win, menus);
    GT_RefreshWindow(win, NULL);
    show();

    while (!quit) {
        struct IntuiMessage *m;
        ULONG tsig = timer ? 1UL << tport->mp_SigBit : 0;
        int switch_view = 0;
        if (timer && test_until) {
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 1; tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
        }
        Wait((1UL << win->UserPort->mp_SigBit) | tsig);
        if (timer && test_until) {
            if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
            WaitIO((struct IORequest *)tr);
            if (now_seconds() >= test_until) {
                go_back();
                test_until = 0;
                status("The test is over: the dock is back as it was.");
            } else {
                snprintf(status_text, sizeof status_text, "Testing: back in %lu s unless you choose Use or Save.",
                         (unsigned long)(test_until - now_seconds()));
                status(status_text);
            }
        }
        while (!quit && !switch_view && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int id = -1, redraw = 1;
            od_button *b = sel >= 0 && sel < cur.n ? &cur.b[sel] : NULL;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) id = G_CANCEL;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_VANILLAKEY) id = handle_key(code);
            else if (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN) id = gg->GadgetID;
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL) {
                    struct MenuItem *it = ItemAddress(menus, code);
                    if (!it) break;
                    if ((ULONG)GTMENUITEM_USERDATA(it) == M_QUIT) id = G_CANCEL;
                    else if ((ULONG)GTMENUITEM_USERDATA(it) == M_ADVANCED) {
                        advanced = (it->Flags & CHECKED) != 0;
                        write_view();
                        switch_view = 1;
                    }
                    code = it->NextSelect;
                }
            }
            if (id < 0) continue;
            switch (id) {
            case G_PLACE: cur.place = code; break;
            case G_SIZE: cur.size = code; break;
            case G_ITEMS: cur.scale = items_scale[code < 4 ? code : 0]; break;
            case G_BORDER: cur.border = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_LABELS: cur.labels = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_RUNNING: cur.running = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_SHELF: cur.background = code; break;
            case G_OPACITY: cur.opacity = (WORD)code; redraw = 0; break;
            case G_HOVER: cur.hover = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_HOP: cur.hop = (gg->Flags & GFLG_SELECTED) != 0; break;
            case G_LIST: sel = code; break;
            case G_UP: if (b && sel > 0) { swap(sel, sel - 1); sel--; } break;
            case G_DOWN: if (b && sel < cur.n - 1) { swap(sel, sel + 1); sel++; } break;
            case G_REMOVE:
                if (b) {
                    memmove(&cur.b[sel], &cur.b[sel + 1], (cur.n - sel - 1) * sizeof cur.b[0]);
                    cur.n--;
                    if (sel >= cur.n) sel = cur.n - 1;
                }
                break;
            case G_SEPARATOR:
                if (cur.n < OD_MAX) {
                    int at = b ? sel + 1 : cur.n;
                    memmove(&cur.b[at + 1], &cur.b[at], (cur.n - at) * sizeof cur.b[0]);
                    memset(&cur.b[at], 0, sizeof cur.b[0]);
                    cur.b[at].kind = OD_SEPARATOR;
                    cur.n++;
                    sel = at;
                }
                break;
            case G_SOURCE: source = code; redraw = 0; break;
            case G_TAKEOVER: take_over_source(); break;
            case G_KIND: if (b && b->kind != OD_SEPARATOR) b->kind = code; break;
            case G_NAME: if (b) strncpy(b->label, (char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof b->label - 1); break;
            case G_COMMAND: if (b) strncpy(b->command, (char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof b->command - 1); break;
            case G_ICON: if (b) strncpy(b->icon, (char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof b->icon - 1); break;
            case G_DIR: if (b) strncpy(b->dir, (char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof b->dir - 1); break;
            case G_STACK: {
                LONG v = ((struct StringInfo *)gg->SpecialInfo)->LongInt;
                if (b) b->stack = v < 4096 ? 4096 : v > 1024 * 1024 ? 1024 * 1024 : v;
                break;
            }
            case G_TEST:
                if (put_in_place(0)) { test_until = now_seconds() + TEST_SECONDS; status("Testing: back in 15 s unless you choose Use or Save."); }
                redraw = 0;
                break;
            case G_USE: if (put_in_place(0)) { test_until = 0; quit = 1; } redraw = 0; break;
            case G_SAVE: if (put_in_place(1)) { test_until = 0; quit = 1; } redraw = 0; break;
            case G_CANCEL: if (test_until) go_back(); quit = 1; break;
            default: redraw = 0;
            }
            if (redraw && !quit) show();
        }
        if (switch_view && !quit) {
            wx = win->LeftEdge; wy = win->TopEdge;
            ClearMenuStrip(win);
            CloseWindow(win); win = NULL;
            FreeGadgets(glist); glist = NULL;
            FreeMenus(menus); menus = NULL;
            goto reopen;
        }
    }
out:
    if (timer) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (win) { ClearMenuStrip(win); CloseWindow(win); }
    FreeMenus(menus);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

int main(int argc, char **argv)
{
    LONG args[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    /* from Workbench (argc 0) there are no arguments: ReadArgs would read them from
     * libnix's console window, which opens empty and waits */
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM/K,TOOLMANAGER/K,AMIDOCK/K,AMISTART/K,USE/S,SAVE/S,ADVANCED/S,ADD/M", args, NULL) : NULL;
    char *text, err[120];
    int rc = RETURN_OK, first;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Dock"); return RETURN_FAIL; }
    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    /* orig is always what ENV: holds now; FROM only fills the window */
    text = read_file(PREFS_ENV, NULL);
    first = !text && !exists(PREFS_ENVARC);
    if (!text) text = read_file(PREFS_ENVARC, NULL);
    od_parse(&orig, text);
    if (!text) { od_starter(&orig); drop_missing(&orig); }
    if (text) FreeVec(text);
    cur = orig;
    if (args[0]) {
        if (!(text = read_file((const char *)args[0], NULL))) {
            Printf((STRPTR)"Dock: %s can't be read.\n", args[0]);
            rc = RETURN_FAIL;
            goto done;
        }
        od_parse(&cur, text);
        FreeVec(text);
    }
    if (args[1] || args[2] || args[3]) {
        /* TOOLMANAGER, AMIDOCK or AMISTART <file>: its buttons in place of the starter's */
        const char *file = (const char *)(args[1] ? args[1] : args[2] ? args[2] : args[3]);
        od_report r;
        if (first) cur.n = 0;
        if (!take_over(&cur, file, NULL, &r, err, sizeof err)) {
            Printf((STRPTR)"Dock: %s couldn't be taken over: %s.\n", (LONG)file, (LONG)err);
            rc = RETURN_WARN;
            goto done;
        }
        if (!args[4] && !args[5])
            snprintf(status_text, sizeof status_text, "Took over %d buttons. Use, Test or Save to put them in place.", r.added);
    } else if (first && !cur.imported[0]) {
        /* the first start: the first other dock found is taken over, once */
        char file[256], base[160];
        od_report r;
        for (int s = 0; s < SRC_COUNT; s++)
            if (find_source(s, file, sizeof file, base, sizeof base)) {
                static od_dock t;                /* 30 KB: not on a Shell's 4 KB stack */
                od_defaults(&t);
                t.place = cur.place; t.size = cur.size;
                if (take_over(&t, file, base, &r, err, sizeof err)) {
                    cur = t;
                    source = s;
                    snprintf(status_text, sizeof status_text, "Took over %d button%s from %s. Save keeps them.",
                             r.added, r.added == 1 ? "" : "s", src_labels[s]);
                    break;
                }
            }
    }
    if (args[7]) {
        /* ADD <program> ...: a button for each one on this machine and not on the dock yet */
        for (STRPTR *p = (STRPTR *)args[7]; *p; p++)
            if (exists((const char *)*p) && od_ensure(&cur, (const char *)*p) < 0) {
                Printf((STRPTR)"Dock: the dock is full, so %s wasn't added.\n", (LONG)*p);
                rc = RETURN_WARN;
            }
    }
    if (args[4] || args[5]) {                    /* from the Shell: no window */
        if (!write_prefs(&cur, args[5] != 0)) {
            PutStr((STRPTR)"Dock: the settings couldn't be written.\n");
            rc = RETURN_FAIL;
        } else tell_opendock();
        goto done;
    }
    read_view();
    if (args[6]) advanced = 1;
    AslBase = OpenLibrary((STRPTR)"asl.library", 39);
    rc = gui();
    if (AslBase) CloseLibrary(AslBase);
done:
    if (rd) FreeArgs(rd);
    if (IconBase) CloseLibrary(IconBase);
    return rc;
}
