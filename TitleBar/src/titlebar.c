/* OpenPrefs Title bar: the editor for OpenTitle (the logo, free memory,
 * clock, network icons and the cog with the settings editors on the
 * Workbench screen's title bar, and the black border around the screen).
 *
 *   TitleBar [FROM file] [USE] [SAVE]
 *
 * Save writes ENV:OpenPrefs/TitleBar and ENVARC:; Use writes ENV: only;
 * Cancel leaves both. Either way OpenTitle is told (Ctrl-F to the task of its
 * public port) or, when something is switched on and it isn't running,
 * started from SYS:C.
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tb_prefs.h"

static const char version[] __attribute__((used)) = "$VER: TitleBar 0.2 (8.10.2026) OpenPrefs, MIT, Copyright (c) 2026 Dalsin Limited";

static tb_prefs cur;

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

static int running(void)
{
    int r;
    Forbid();
    r = FindPort((STRPTR)TB_PORT) != NULL;
    Permit();
    return r;
}

static void tell(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)TB_PORT)) && p->mp_SigTask) Signal((struct Task *)p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
    if (!p && (cur.logo || cur.memory || cur.clock || cur.network || cur.cog || cur.border_black)) {
        BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
        /* asynchronous: both handles are the new process's, closed when it ends */
        if (!in || !out || SystemTags((STRPTR)TB_TOOL, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, SYS_UserShell, TRUE,
                                      NP_StackSize, 16384, TAG_DONE) == -1) {
            if (in) Close(in);
            if (out) Close(out);
        }
    }
}

static int put_in_place(int save)
{
    char text[256];
    int ok;
    tb_text(&cur, text, sizeof text);
    ok = write_file(TB_ENV, text);
    if (save) ok = write_file(TB_ENVARC, text) && ok;
    tell();
    return ok;
}

/* ---- the window ---------------------------------------------------------------------- */

enum { G_LOGO, G_MEMORY, G_CLOCK, G_DATE, G_NET, G_COG, G_BORDER, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };
static struct Gadget *gad[G_COUNT];
static struct Window *win;
static char status_text[120];

#define SET(id, ...) do { if (gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE); } while (0)

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static void show(void)
{
    SET(G_LOGO, GTCB_Checked, cur.logo);
    SET(G_MEMORY, GTCB_Checked, cur.memory);
    SET(G_CLOCK, GTCB_Checked, cur.clock);
    SET(G_DATE, GTCB_Checked, cur.date, GA_Disabled, !cur.clock);
    SET(G_NET, GTCB_Checked, cur.network);
    SET(G_COG, GTCB_Checked, cur.cog);
    SET(G_BORDER, GTCB_Checked, cur.border_black);
}

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    int fh, lh, row, quit = 0, rc = RETURN_OK, W = 400, X = 260, i;
    struct Menu *menu = NULL;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    strcpy(status_text, running() ? "OpenTitle is running." : "OpenTitle isn't running: Use or Save starts it.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))

    row = scr->WBorTop + fh + 1 + 8;
    G(CHECKBOX_KIND, G_LOGO, X, row, 26, lh, "AmigaChrome logo at the left", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 4;
    G(CHECKBOX_KIND, G_MEMORY, X, row, 26, lh, "Free chip and fast memory", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 4;
    G(CHECKBOX_KIND, G_CLOCK, X, row, 26, lh, "Clock at the right", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 4;
    G(CHECKBOX_KIND, G_DATE, X, row, 26, lh, "Day and date with the clock", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 4;
    G(CHECKBOX_KIND, G_NET, X, row, 26, lh, "Network icons (LAN, Wi-Fi)", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 4;
    G(CHECKBOX_KIND, G_COG, X, row, 26, lh, "Settings cog at the right end", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 10;
    G(CHECKBOX_KIND, G_BORDER, X, row, 26, lh, "Black border around the screen", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 10;
    G(TEXT_KIND, G_STATUS, 10, row, W - 20, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + 6;
    {
        static const char *const names[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 20 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], 10 + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Title bar", WA_ScreenTitle, (ULONG)"OpenPrefs Title bar 0.2", WA_PubScreen, (ULONG)scr,
                         WA_Left, 60, WA_Top, scr->BarHeight + 20, WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | CHECKBOXIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    show();

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) quit = 1;
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: put_in_place(1); quit = 1; break;
                    case 2: put_in_place(0); quit = 1; break;
                    case 3: quit = 1; break;
                    }
                    code = it->NextSelect;
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_GADGETUP) {
                int sel = (gg->Flags & GFLG_SELECTED) != 0;
                switch (gg->GadgetID) {
                case G_LOGO: cur.logo = sel; break;
                case G_MEMORY: cur.memory = sel; break;
                case G_CLOCK: cur.clock = sel; show(); break;
                case G_DATE: cur.date = sel; break;
                case G_NET: cur.network = sel; break;
                case G_COG: cur.cog = sel; break;
                case G_BORDER: cur.border_black = sel; break;
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

int main(int argc, char **argv)
{
    LONG args[3] = { 0, 0, 0 };
    /* from Workbench (argc 0) there are no arguments: ReadArgs would read them from
     * libnix's console window, which opens empty and waits */
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S", args, NULL) : NULL;
    char *text;
    int rc;
    (void)version;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"TitleBar"); return RETURN_FAIL; }
    text = read_file(args[0] ? (const char *)args[0] : TB_ENV);
    if (!text && !args[0]) text = read_file(TB_ENVARC);
    tb_parse(&cur, text);
    if (text) FreeVec(text);
    if (args[1] || args[2]) {                /* from the Shell: no window */
        rc = put_in_place(args[2] != 0) ? RETURN_OK : RETURN_ERROR;
        if (rd) FreeArgs(rd);
        return rc;
    }
    if (rd) FreeArgs(rd);
    return gui();
}
