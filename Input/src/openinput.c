/* OpenInput: the mouse, the keyboard and the keymap in one OpenPrefs editor,
 * with the gamepads under it. It takes over the OS's Input editor (OpenUp's
 * RECONCILE puts that in SYS:Prefs/Classic).
 *
 *   OpenInput [FROM file] [USE] [SAVE]
 *
 *   - the mouse: its speed, acceleration, the buttons swapped, the double-click
 *     time
 *   - the keyboard: the key repeat delay and rate, in milliseconds
 *   - the keymap, from DEVS:Keymaps
 *   - Gamepads... opens the Gamepads editor (SYS:Prefs/Gamepads), so pads live
 *     under Input (the user, 10 October 2026)
 *
 * Save writes ENV:Sys/input.prefs and ENVARC:; Use writes ENV: only (IPrefs
 * reads it at once); Cancel changes nothing. Everything else in the file is
 * kept as it was.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <prefs/prefhdr.h>
#include <prefs/input.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char version[] __attribute__((used)) = "$VER: OpenInput 0.1 (10.10.2026) OpenPrefs, MIT, Copyright (c) 2026 Dalsin Limited";

struct Library *UtilityBase;

#define INPUT_ENV "ENV:Sys/input.prefs"
#define INPUT_ENVARC "ENVARC:Sys/input.prefs"
#define GAMEPADS "SYS:Prefs/Gamepads"
#define MAX_NAMES 64

/* ---- input.prefs ------------------------------------------------------------- */

static struct InputPrefs ip;
static UBYTE *file_data;
static LONG file_len;

static ULONG get32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }
static void put32(UBYTE *p, ULONG v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static UBYTE *read_all(const char *path, LONG *lenp)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG len = 0;
    if (fh) {
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len > 0 && (buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) {
            if (Read(fh, buf, len) != len) { FreeVec(buf); buf = NULL; }
        }
        Close(fh);
    }
    if (lenp) *lenp = buf ? len : 0;
    return buf;
}

static UBYTE *chunk(UBYTE *d, LONG len, const char *id, LONG *n)
{
    LONG k;
    for (k = 12; d && k + 8 <= len; ) {
        LONG cl = (LONG)get32(d + k + 4);
        if (cl < 0 || k + 8 + cl > len) break;
        if (!memcmp(d + k, id, 4)) { if (n) *n = cl; return d + k + 8; }
        k += 8 + cl + (cl & 1);
    }
    return NULL;
}

static void load_input(const char *path)
{
    UBYTE *c;
    LONG n = 0;
    memset(&ip, 0, sizeof ip);
    ip.ip_PointerTicks = 1;                      /* the OS's defaults: fast, no acceleration */
    ip.ip_DoubleClick.tv_secs = 0; ip.ip_DoubleClick.tv_micro = 500000;
    ip.ip_KeyRptDelay.tv_secs = 0; ip.ip_KeyRptDelay.tv_micro = 500000;
    ip.ip_KeyRptSpeed.tv_secs = 0; ip.ip_KeyRptSpeed.tv_micro = 40000;
    strcpy(ip.ip_Keymap, "usa1");
    if (file_data) { FreeVec(file_data); file_data = NULL; }
    file_data = read_all(path, &file_len);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4) && !memcmp(file_data + 8, "PREF", 4) &&
        (c = chunk(file_data, file_len, "ICTL", &n)))
        memcpy(&ip, c, n < (LONG)sizeof ip ? n : (LONG)sizeof ip);
}

static int write_file(const char *path, const UBYTE *data, LONG len)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    LONG got;
    if (!fh) return 0;
    got = Write(fh, (APTR)data, len);
    Close(fh);
    return got == len;
}

static int save_input(const char *path)
{
    LONG size = 12 + 14 + 8 + sizeof ip + file_len + 16, o = 12, k;
    UBYTE *out = AllocVec(size, MEMF_ANY | MEMF_CLEAR);
    int ok, had_prhd = 0;
    if (!out) return 0;
    memcpy(out, "FORM\0\0\0\0PREF", 12);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4)) {
        for (k = 12; k + 8 <= file_len; ) {
            LONG cl = (LONG)get32(file_data + k + 4), all = 8 + cl + (cl & 1);
            if (cl < 0 || k + all > file_len) break;
            if (memcmp(file_data + k, "ICTL", 4)) {
                if (!memcmp(file_data + k, "PRHD", 4)) had_prhd = 1;
                memcpy(out + o, file_data + k, all);
                o += all;
            }
            k += all;
        }
    }
    if (!had_prhd) {
        memmove(out + 12 + 14, out + 12, o - 12);
        memcpy(out + 12, "PRHD\0\0\0\6\0\0\0\0\0\0", 14);
        o += 14;
    }
    memcpy(out + o, "ICTL", 4);
    put32(out + o + 4, sizeof ip);
    memcpy(out + o + 8, &ip, sizeof ip);
    o += 8 + sizeof ip + (sizeof ip & 1);
    put32(out + 4, o - 8);
    ok = write_file(path, out, o);
    FreeVec(out);
    return ok;
}

/* ---- the keymaps ------------------------------------------------------------- */

static char names[MAX_NAMES][64];
static struct Node nodes[MAX_NAMES];
static struct List list;
static int count;

static int by_name(const void *a, const void *b) { return Stricmp((CONST_STRPTR)a, (CONST_STRPTR)b); }

static int list_keymaps(void)
{
    BPTR lock = Lock((STRPTR)"DEVS:Keymaps", ACCESS_READ);
    struct FileInfoBlock *fib;
    int n = 0;
    if (!lock) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (Examine(lock, fib)) {
            while (n < MAX_NAMES && ExNext(lock, fib)) {
                const char *fn = (const char *)fib->fib_FileName;
                if (fib->fib_DirEntryType > 0 || strlen(fn) >= 64 || !strcmp(fn, ".") ) continue;
                if (strlen(fn) > 5 && !Stricmp((CONST_STRPTR)fn + strlen(fn) - 5, (CONST_STRPTR)".info")) continue;
                strcpy(names[n++], fn);
            }
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    qsort(names, n, 64, by_name);
    return n;
}

static void make_list(void)
{
    int i;
    list.lh_Head = (struct Node *)&list.lh_Tail;
    list.lh_Tail = NULL;
    list.lh_TailPred = (struct Node *)&list.lh_Head;
    for (i = 0; i < count; i++) {
        nodes[i].ln_Name = names[i];
        AddTail(&list, &nodes[i]);
    }
}

static int index_of(const char *name)
{
    int i;
    for (i = 0; i < count; i++) if (!Stricmp((CONST_STRPTR)names[i], (CONST_STRPTR)name)) return i;
    return -1;
}

/* ---- the window ----------------------------------------------------------------- */

enum { G_SPEED = 1, G_ACCEL, G_SWAP, G_DBL, G_DELAY, G_RATE, G_KEYMAP, G_PADS, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };

static const char *const speed_labels[] = { "Slow", "Medium", "Fast", NULL };
static const UWORD speed_ticks[] = { 4, 2, 1 };

static struct Window *win;
static struct Gadget *gad[G_COUNT];
static char status_text[80];
static int sel_speed, accel, swap, sel_keymap;

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

static LONG number(int id)
{
    LONG v = 0;
    if (gad[id]) GT_GetGadgetAttrs(gad[id], win, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
    return v;
}

static LONG ms_of(const struct timeval *tv) { return tv->tv_secs * 1000 + tv->tv_micro / 1000; }

static void set_ms(struct timeval *tv, LONG ms)
{
    if (ms < 20) ms = 20;
    if (ms > 5000) ms = 5000;
    tv->tv_secs = ms / 1000;
    tv->tv_micro = (ms % 1000) * 1000;
}

static int put_in_place(int save)
{
    ip.ip_PointerTicks = speed_ticks[sel_speed];
    ip.ip_MouseAccel = accel ? 1 : 0;
    ip.ip_SwitchMouseButtons = swap ? 1 : 0;
    set_ms(&ip.ip_DoubleClick, number(G_DBL));
    set_ms(&ip.ip_KeyRptDelay, number(G_DELAY));
    set_ms(&ip.ip_KeyRptSpeed, number(G_RATE));
    if (sel_keymap >= 0 && sel_keymap < count) {
        strncpy(ip.ip_Keymap, names[sel_keymap], sizeof ip.ip_Keymap - 1);
        ip.ip_Keymap[sizeof ip.ip_Keymap - 1] = 0;
        strncpy(ip.ip_KeymapName, names[sel_keymap], sizeof ip.ip_KeymapName - 1);
        ip.ip_KeymapName[sizeof ip.ip_KeymapName - 1] = 0;
    }
    if (save && !save_input(INPUT_ENVARC)) { status("Couldn't write ENVARC:Sys/input.prefs."); return 0; }
    if (!save_input(INPUT_ENV)) { status("Couldn't write ENV:Sys/input.prefs."); return 0; }
    return 1;
}

/* The Gamepads editor, started on its own; this window stays. */
static void gamepads(void)
{
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)"\"" GAMEPADS "\"", SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE,
                                  SYS_UserShell, TRUE, NP_StackSize, 16384, TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
        status("Couldn't start SYS:Prefs/Gamepads.");
    }
}

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, (STRPTR)"Cancel", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Gamepads...", (STRPTR)"G", 0, 0, (APTR)4 },
    { NM_END, 0, 0, 0, 0, 0 }
};

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    struct Menu *menu = NULL;
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, lw, cw, nw, lvh, i, keymap_x, keymap_w;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    count = list_keymaps();
    make_list();
    sel_keymap = index_of(ip.ip_KeymapName[0] ? ip.ip_KeymapName : ip.ip_Keymap);
    sel_speed = ip.ip_PointerTicks >= 4 ? 0 : ip.ip_PointerTicks >= 2 ? 1 : 2;
    accel = ip.ip_MouseAccel != 0;
    swap = ip.ip_SwitchMouseButtons != 0;

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    lw = TextLength(&scr->RastPort, (STRPTR)"Double-click (ms)", 17) + 16;
    X = 12 + lw;
    cw = TextLength(&scr->RastPort, (STRPTR)"Medium", 6) + 40;
    nw = TextLength(&scr->RastPort, (STRPTR)"00000", 5) + 16;
    keymap_w = TextLength(&scr->RastPort, (STRPTR)"swiss_german_long_name", 22) + 40;
    if (keymap_w < 180) keymap_w = 180;
    keymap_x = X + cw + 24;
    W = keymap_x + keymap_w + 12;
    lvh = 7 * (fh + 1) + 4;
    snprintf(status_text, sizeof status_text, "%s", "Change what you like, then Use or Save.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
    row = top + fh + 4;
    G(LISTVIEW_KIND, G_KEYMAP, keymap_x, row, keymap_w, lvh, "Keymap", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&list, GTLV_Selected, (ULONG)sel_keymap, GTLV_Top, (ULONG)(sel_keymap > 3 ? sel_keymap - 3 : 0));
    G(CYCLE_KIND, G_SPEED, X, row, cw, lh, "Mouse speed", PLACETEXT_LEFT, GTCY_Labels, (ULONG)speed_labels, GTCY_Active, sel_speed);
    row += lh + 4;
    G(CHECKBOX_KIND, G_ACCEL, X, row, 26, lh, "Acceleration", PLACETEXT_LEFT, GTCB_Checked, accel, GTCB_Scaled, TRUE);
    row += lh + 4;
    G(CHECKBOX_KIND, G_SWAP, X, row, 26, lh, "Swap buttons", PLACETEXT_LEFT, GTCB_Checked, swap, GTCB_Scaled, TRUE);
    row += lh + 4;
    G(INTEGER_KIND, G_DBL, X, row, nw, lh, "Double-click (ms)", PLACETEXT_LEFT, GTIN_Number, ms_of(&ip.ip_DoubleClick), GTIN_MaxChars, 5);
    row += lh + 10;
    G(INTEGER_KIND, G_DELAY, X, row, nw, lh, "Repeat delay (ms)", PLACETEXT_LEFT, GTIN_Number, ms_of(&ip.ip_KeyRptDelay), GTIN_MaxChars, 5);
    row += lh + 4;
    G(INTEGER_KIND, G_RATE, X, row, nw, lh, "Repeat rate (ms)", PLACETEXT_LEFT, GTIN_Number, ms_of(&ip.ip_KeyRptSpeed), GTIN_MaxChars, 5);
    row += lh + 10;
    G(BUTTON_KIND, G_PADS, X, row, TextLength(&scr->RastPort, (STRPTR)"Gamepads...", 11) + 24, lh, "_Gamepads...", 0, GT_Underscore, '_');
    row += lh + 4;
    if (top + fh + 4 + lvh + 8 > row) row = top + fh + 4 + lvh + 8;
    G(TEXT_KIND, G_STATUS, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE);
    row += lh + 6;
    {
        static const char *const bnames[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 24 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], 12 + i * (bw + 10), row, bw, lh, bnames[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Input", WA_ScreenTitle, (ULONG)"OpenInput 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_VANILLAKEY | BUTTONIDCMP |
                                   INTEGERIDCMP | CYCLEIDCMP | CHECKBOXIDCMP | LISTVIEWIDCMP | TEXTIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) quit = 1;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_VANILLAKEY && (code | 0x20) == 'g') gamepads();
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: if (put_in_place(1)) quit = 1; break;
                    case 2: if (put_in_place(0)) quit = 1; break;
                    case 3: quit = 1; break;
                    case 4: gamepads(); break;
                    }
                    code = it->NextSelect;
                }
            }
            else if (cls == IDCMP_GADGETUP) {
                switch (gg->GadgetID) {
                case G_SPEED: sel_speed = code; break;
                case G_ACCEL: accel = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_SWAP: swap = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_KEYMAP: sel_keymap = code; break;
                case G_PADS: gamepads(); break;
                case G_SAVE: if (put_in_place(1)) quit = 1; break;
                case G_USE: if (put_in_place(0)) quit = 1; break;
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
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S", args, NULL) : NULL;
    int rc;
    (void)argv;
    (void)version;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"OpenInput"); return RETURN_FAIL; }
    if (!(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39))) { if (rd) FreeArgs(rd); return RETURN_FAIL; }
    load_input(args[0] ? (const char *)args[0] : INPUT_ENV);
    if (args[1] || args[2]) {
        rc = (args[2] && !save_input(INPUT_ENVARC)) || !save_input(INPUT_ENV) ? RETURN_ERROR : RETURN_OK;
    } else rc = gui();
    if (rd) FreeArgs(rd);
    if (file_data) FreeVec(file_data);
    CloseLibrary(UtilityBase);
    return rc;
}
