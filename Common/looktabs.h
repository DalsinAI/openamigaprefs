/* looktabs.h: Look, Windows, Title bar and Menus as one editor with tabs (the user, 10 October 2026:
 * "combine windows and look into a single, tabbed interface under OpenLook ... title bar and menus
 * should live under here too").
 *
 * Each of the four editors shows the same row of tabs at its top; its own tab is the sunken one. Choosing
 * another tab keeps what was changed (as Use does), opens that editor where this window is, and closes
 * this one, so the four read as one window with four pages. Each stays its own program, so each still
 * opens alone from the cog, the menu or Workbench.
 *
 * The place passes in ENV:OpenPrefs/LookTab ("left top"), read once and deleted by the editor that opens.
 * Gadget IDs from LT_ID up are the tabs: an editor's own gadgets stay below.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef LOOKTABS_H
#define LOOKTABS_H

#include <exec/types.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/dos.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <string.h>

enum { LT_LOOK, LT_WINDOWS, LT_TITLEBAR, LT_MENUS, LT_N };
#define LT_ID 900
#define LT_IS(id) ((id) >= LT_ID && (id) < LT_ID + LT_N)
#define LT_PLACE "ENV:OpenPrefs/LookTab"

static const char *const lt_label[LT_N] = { "Look", "Windows", "Title bar", "Menus" };
static const char *const lt_path[LT_N] = { "SYS:Prefs/Look", "SYS:Prefs/Windows", "SYS:Prefs/TitleBar", "SYS:Prefs/Menus" };

/* The row of tabs, x..x+w at y, lh high: the others are buttons, this editor's own (me) a sunken box
 * with its name in the middle. Returns the last gadget (NULL when one could not be made). */
static struct Gadget *lt_make(struct Gadget *g, struct NewGadget *ng, int x, int y, int w, int lh, int me)
{
    int i, gap = 4, tw = (w - (LT_N - 1) * gap) / LT_N;
    for (i = 0; i < LT_N && g; i++) {
        ng->ng_LeftEdge = x + i * (tw + gap);
        ng->ng_TopEdge = y;
        ng->ng_Width = i == LT_N - 1 ? w - i * (tw + gap) : tw;
        ng->ng_Height = lh;
        ng->ng_GadgetID = LT_ID + i;
        if (i == me) {
            ng->ng_GadgetText = NULL;
            ng->ng_Flags = 0;
            g = CreateGadget(TEXT_KIND, g, ng, GTTX_Text, (ULONG)lt_label[i], GTTX_Border, TRUE, GTTX_Justification, GTJ_CENTER,
                             TAG_DONE);
        } else {
            ng->ng_GadgetText = (STRPTR)lt_label[i];
            ng->ng_Flags = PLACETEXT_IN;
            g = CreateGadget(BUTTON_KIND, g, ng, TAG_DONE);
        }
    }
    return g;
}

/* Where the editor before put its window, if it handed over to this one: 1 and left, top set. */
static int lt_place(int *left, int *top)
{
    BPTR f = Open((STRPTR)LT_PLACE, MODE_OLDFILE);
    char line[32];
    int l, t, ok = 0;
    if (!f) return 0;
    if (FGets(f, (STRPTR)line, sizeof line - 1) && sscanf(line, "%d %d", &l, &t) == 2 && l >= 0 && t >= 0) {
        *left = l; *top = t; ok = 1;
    }
    Close(f);
    DeleteFile((STRPTR)LT_PLACE);
    return ok;
}

/* Hands over to the editor of tab `to`, at this window's place. The caller then closes its window. */
static void lt_go(struct Window *win, int to)
{
    BPTR f, in, out;
    char cmd[64];
    if (to < 0 || to >= LT_N) return;
    if (win) {
        BPTR dir = CreateDir((STRPTR)"ENV:OpenPrefs");
        if (dir) UnLock(dir);
        if ((f = Open((STRPTR)LT_PLACE, MODE_NEWFILE))) {
            char line[32];
            snprintf(line, sizeof line, "%d %d\n", (int)win->LeftEdge, (int)win->TopEdge);
            Write(f, line, strlen(line));
            Close(f);
        }
    }
    snprintf(cmd, sizeof cmd, "\"%s\"", lt_path[to]);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, SYS_UserShell, TRUE,
                                  NP_StackSize, 16384, TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
    }
}

#endif
