/* OpenSystem: the Team's System preferences. Its first page is Stack: the
 * adaptive stack's default cap and each program's own cap (amigachrome
 * docs/design/Design-Adaptive-Stack.md; decided 13 October 2026: a Stack page in
 * the System prefs owns the cap, one voice).
 *
 *   OpenSystem [APPLY] [INFO] [CAP kb] [PROGRAM name] [PCAP kb] [USE] [SAVE]
 *
 *   - APPLY: hand ENV:OpenAmiga/Stack to the runtime (for User-startup)
 *   - INFO: what the runtime does now: the mode, the cap, the programs on
 *     growing stacks and the stack alerts since boot
 *   - CAP, PROGRAM with PCAP (0 removes it), with USE or SAVE: change the
 *     settings from the Shell
 *   - no arguments: the window
 *
 * The settings are a short text file, ENV:OpenAmiga/Stack (Use) and ENVARC:
 * (Save): "CAP <kb>", then "PROGRAM <name> <kb>" lines. On AmigaChrome they go
 * to the runtime through the memory controller's stack registers (AmigaChrome Team
 * product 12 or 14, at $900 on the board). A program's name is its command's
 * file part, as the Shell runs it ("StackGrow"), case aside.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/expansion.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef LT_ONE
#include "looktabs.h"                       /* a page of OpenPrefs (LT_OP): its frame and sections */
#define LT_REFRESH(w) lt_draw((w), -1)
#else
#define LT_REFRESH(w) ((void)0)
#define LT_DIRTY 1
#define LT_DX 0
#define lt_note_to(f) ((void)0)
#endif

static const char version[] __attribute__((used)) = "$VER: OpenSystem 0.1 (13.10.2026) OpenPrefs, MIT, Copyright (c) 2026 AmigaChrome Team";

static struct Library *opensystem_expansion;
#define ExpansionBase opensystem_expansion

#define STACK_ENV "ENV:OpenAmiga/Stack"
#define STACK_ENVARC "ENVARC:OpenAmiga/Stack"
#define AMIGACHROME_MANUFACTURER 0xDA15
#define LOCAL_PRODUCT 12             /* AC090's memory controller */
#define AAPLUS_PRODUCT 14            /* the AA+ board's controller: the same registers */
#define STK_REGS 0x900
#define STK_SIGNATURE 0x53544B31UL   /* "STK1" */
#define CAP_MIN_KB 64
#define CAP_MAX_KB 65536
#define CAP_DEFAULT_KB 1024
#define MAX_PROGS 32                 /* the runtime keeps 32 */
#define NAME_LEN 32                  /* the runtime's name register: 31 characters */

/* ---- the settings ------------------------------------------------------------------ */

struct prog { char name[NAME_LEN]; LONG kb; };
static LONG cap_kb = CAP_DEFAULT_KB;
static struct prog progs[MAX_PROGS];
static int nprogs;

static LONG clamp_kb(LONG kb)
{
    kb = (kb + 63) / 64 * 64;                    /* whole 64 KB chunks, as the runtime rounds */
    return kb < CAP_MIN_KB ? CAP_MIN_KB : kb > CAP_MAX_KB ? CAP_MAX_KB : kb;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

static int prog_index(const char *name)
{
    int i;
    for (i = 0; i < nprogs; i++) if (same_name(progs[i].name, name)) return i;
    return -1;
}

/* A program's cap: kb 0 removes it. 0 when the list is full or the name is no good. */
static int set_prog(const char *name, LONG kb)
{
    int i = prog_index(name);
    if (!*name || strlen(name) >= NAME_LEN || strchr(name, ' ') || strchr(name, ':') || strchr(name, '/')) return 0;
    if (!kb) {
        if (i >= 0) progs[i] = progs[--nprogs];
        return 1;
    }
    if (i < 0) { if (nprogs == MAX_PROGS) return 0; i = nprogs++; strcpy(progs[i].name, name); }
    progs[i].kb = clamp_kb(kb);
    return 1;
}

/* Read a settings file; with none, the defaults. 1 if there was one. */
static int load_settings(const char *path)
{
    char line[96], name[64];
    long kb;
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    cap_kb = CAP_DEFAULT_KB;
    nprogs = 0;
    if (!f) return 0;
    while (FGets(f, (STRPTR)line, sizeof line)) {
        if (sscanf(line, "CAP %ld", &kb) == 1) cap_kb = clamp_kb(kb);
        else if (sscanf(line, "PROGRAM %63s %ld", name, &kb) == 2 && kb > 0) set_prog(name, kb);
    }
    Close(f);
    return 1;
}

/* Write the settings to path, in dir (made if it is not there). */
static int save_settings(const char *dir, const char *path)
{
    char line[96];
    int i, ok;
    BPTR f, l = Lock((STRPTR)dir, SHARED_LOCK);
    if (!l) l = CreateDir((STRPTR)dir);
    if (l) UnLock(l);
    if (!(f = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    snprintf(line, sizeof line, "CAP %ld\n", (long)cap_kb);
    ok = FPuts(f, (STRPTR)line) == 0;
    for (i = 0; ok && i < nprogs; i++) {
        snprintf(line, sizeof line, "PROGRAM %s %ld\n", progs[i].name, (long)progs[i].kb);
        ok = FPuts(f, (STRPTR)line) == 0;
    }
    Close(f);
    return ok;
}

/* ---- the runtime: the controller's stack registers --------------------------------- */

/* Longs: 0 "STK1"; 4 mode (0 off, 1 watch, 2 grow); 8 the default cap in bytes;
 * $C the chunk; $10, $14 the stack range; $18 command (1 set the program at
 * $20 to the cap at $1C, 0 removing it; 2 clear every program's cap), read: the
 * programs with caps; $1C a cap; $20-$3F a name; $40 programs on growing stacks
 * now; $44 stack alerts since boot. */
static volatile ULONG *stack_regs(void)
{
    struct ConfigDev *cd = NULL;
    volatile ULONG *r;
    if (!ExpansionBase) return NULL;
    if (!(cd = FindConfigDev(NULL, AMIGACHROME_MANUFACTURER, LOCAL_PRODUCT)))
        cd = FindConfigDev(NULL, AMIGACHROME_MANUFACTURER, AAPLUS_PRODUCT);
    if (!cd || !cd->cd_BoardAddr) return NULL;
    r = (volatile ULONG *)((UBYTE *)cd->cd_BoardAddr + STK_REGS);
    return r[0] == STK_SIGNATURE ? r : NULL;
}

/* Hand the settings to the runtime. 0 when there is no runtime that takes them. */
static int apply_settings(void)
{
    volatile ULONG *r = stack_regs();
    int i, k;
    if (!r) return 0;
    r[2] = (ULONG)cap_kb << 10;
    r[6] = 2;                                    /* every program's cap goes, then this list's are set */
    for (i = 0; i < nprogs; i++) {
        char name[NAME_LEN];
        memset(name, 0, sizeof name);
        strncpy(name, progs[i].name, NAME_LEN - 1);
        for (k = 0; k < NAME_LEN / 4; k++)
            r[8 + k] = (ULONG)(UBYTE)name[4 * k] << 24 | (ULONG)(UBYTE)name[4 * k + 1] << 16 |
                       (ULONG)(UBYTE)name[4 * k + 2] << 8 | (UBYTE)name[4 * k + 3];
        r[7] = (ULONG)progs[i].kb << 10;
        r[6] = 1;
    }
    return 1;
}

static const char *mode_name(ULONG mode)
{
    return mode == 2 ? "grow" : mode == 1 ? "watch only" : "off";
}

/* One line on what the runtime does now, for the window and INFO. */
static void runtime_line(char *out, int n)
{
    volatile ULONG *r = stack_regs();
    if (!r) { snprintf(out, n, "No AmigaChrome stack runtime: the settings are kept for when there is."); return; }
    snprintf(out, n, "Runtime: %s; cap %lu KB; %lu on growing stacks now; %lu stack alerts since boot",
             mode_name(r[1]), (unsigned long)(r[2] >> 10), (unsigned long)r[16], (unsigned long)r[17]);
}

static int info(void)
{
    char line[160];
    volatile ULONG *r = stack_regs();
    int i;
    runtime_line(line, sizeof line);
    Printf((STRPTR)"%s\n", (ULONG)line);
    if (r) Printf((STRPTR)"Stack range $%08lx, %lu MB; chunk %lu KB; %lu programs with caps of their own\n",
                  r[4], r[5] >> 20, r[3] >> 10, r[6]);
    Printf((STRPTR)"Saved for Use (" STACK_ENV "): cap %ld KB\n", cap_kb);
    for (i = 0; i < nprogs; i++) Printf((STRPTR)"  %s: %ld KB\n", (ULONG)progs[i].name, progs[i].kb);
    return RETURN_OK;
}

/* ---- the window: the Stack page ---------------------------------------------------- */

enum { G_RUNTIME, G_CAP, G_LIST, G_NAME, G_PCAP, G_SET, G_REMOVE, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };
static struct Gadget *gad[G_COUNT];
static struct Window *win;
static struct List prog_list;
static struct Node prog_nodes[MAX_PROGS];
static char prog_text[MAX_PROGS][NAME_LEN + 16];
static char runtime_text[160], status_text[120];
static int sel = -1;

static void make_prog_list(void)
{
    int i;
    prog_list.lh_Head = (struct Node *)&prog_list.lh_Tail;     /* an empty list (amiga.lib's NewList) */
    prog_list.lh_Tail = NULL;
    prog_list.lh_TailPred = (struct Node *)&prog_list.lh_Head;
    for (i = 0; i < nprogs; i++) {
        snprintf(prog_text[i], sizeof prog_text[i], "%-24s %6ld KB", progs[i].name, (long)progs[i].kb);
        memset(&prog_nodes[i], 0, sizeof prog_nodes[i]);
        prog_nodes[i].ln_Name = prog_text[i];
        AddTail(&prog_list, &prog_nodes[i]);
    }
}

static void show_status(const char *s)
{
    snprintf(status_text, sizeof status_text, "%s", s);
    if (win) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

static void show_list(void)
{
    if (win) GT_SetGadgetAttrs(gad[G_LIST], win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
    make_prog_list();
    if (win) GT_SetGadgetAttrs(gad[G_LIST], win, NULL, GTLV_Labels, (ULONG)&prog_list,
                               GTLV_Selected, sel >= 0 ? (ULONG)sel : ~0UL, TAG_DONE);
}

static LONG int_of(int id)
{
    return ((struct StringInfo *)gad[id]->SpecialInfo)->LongInt;
}

static const char *str_of(int id)
{
    return (const char *)((struct StringInfo *)gad[id]->SpecialInfo)->Buffer;
}

/* The default cap from its gadget, rounded, shown back. */
static void take_cap(void)
{
    cap_kb = clamp_kb(int_of(G_CAP));
    GT_SetGadgetAttrs(gad[G_CAP], win, NULL, GTIN_Number, cap_kb, TAG_DONE);
}

static void set_from_fields(void)
{
    char name[NAME_LEN + 8];
    LONG kb = int_of(G_PCAP);
    snprintf(name, sizeof name, "%s", str_of(G_NAME));
    if (!name[0]) { show_status("Type the program's name (its command, as the Shell runs it)."); return; }
    if (kb <= 0) { show_status("Give the program a cap in KB (64 to 65536)."); return; }
    if (!set_prog(name, kb)) { show_status("No room for that name: 31 characters, no spaces, colons or slashes; 32 programs."); return; }
    sel = prog_index(name);
    show_list();
    show_status("Set. Use or Save hands it to the runtime.");
}

static void remove_selected(void)
{
    if (sel < 0 || sel >= nprogs) { show_status("Pick a program in the list first."); return; }
    set_prog(progs[sel].name, 0);
    sel = -1;
    show_list();
    show_status("Removed: that program gets the default cap.");
}

/* Use (save 0) or Save (1): the file(s), then the runtime. 1 to close the window. */
static int put_in_place(int save)
{
    take_cap();
    if ((save && !save_settings("ENVARC:OpenAmiga", STACK_ENVARC)) || !save_settings("ENV:OpenAmiga", STACK_ENV)) {
        show_status("Could not write the settings file.");
        return 0;
    }
    apply_settings();
    return 1;
}

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, lw, lvh, i, digit;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    make_prog_list();
    runtime_line(runtime_text, sizeof runtime_text);
    snprintf(status_text, sizeof status_text, "%s", "A stack grows in 64 KB chunks up to its cap. Past it, the program alone stops.");

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    digit = TextLength(&scr->RastPort, (STRPTR)"0000", 4) / 4;
    lw = TextLength(&scr->RastPort, (STRPTR)"Default cap (KB)", 16) + 16;
    X = 12 + lw;
    W = TextLength(&scr->RastPort, (STRPTR)runtime_text, strlen(runtime_text)) + 40;
    if (W < 520) W = 520;
    if (W > scr->Width - 40) W = scr->Width - 40;
    lvh = 6 * (fh + 1) + 4;

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#ifdef LT_ONE
    lt_make(NULL, NULL, 10, top, W - 24, lh, LT_SYSTEM);     /* the frame's sections (OpenPrefs) */
    top += lt_space(scr->Font, W - 24, lh);
#endif
    row = top;
    G(TEXT_KIND, G_RUNTIME, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)runtime_text, GTTX_Border, TRUE, GTTX_Clipped, TRUE);
    row += lh + 8;
    G(INTEGER_KIND, G_CAP, X, row, 8 * digit + 16, lh, "Default cap (KB)", PLACETEXT_LEFT, GTIN_Number, cap_kb, GTIN_MaxChars, 5);
    row += lh + 8 + fh + 4;
    G(LISTVIEW_KIND, G_LIST, 12, row, W - 24, lvh, "Programs with a cap of their own", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&prog_list, GTLV_ShowSelected, (ULONG)NULL, GTLV_Selected, ~0UL);
    row += lvh + 4;
    {
        int nw = W - 24 - (8 * digit + 16) - 2 * (TextLength(&scr->RastPort, (STRPTR)"Remove", 6) + 24) - 3 * 6;
        int bw = TextLength(&scr->RastPort, (STRPTR)"Remove", 6) + 24;
        G(STRING_KIND, G_NAME, 12, row, nw, lh, NULL, 0, GTST_MaxChars, NAME_LEN - 1);
        G(INTEGER_KIND, G_PCAP, 12 + nw + 6, row, 8 * digit + 16, lh, NULL, 0, GTIN_Number, CAP_DEFAULT_KB, GTIN_MaxChars, 5);
        G(BUTTON_KIND, G_SET, 12 + nw + 6 + 8 * digit + 16 + 6, row, bw, lh, "Set", 0, GA_Disabled, FALSE);
        G(BUTTON_KIND, G_REMOVE, W - 12 - bw, row, bw, lh, "Remove", 0, GA_Disabled, FALSE);
    }
    row += lh + 8;
    G(TEXT_KIND, G_STATUS, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE, GTTX_Clipped, TRUE);
    row += lh + 6;
    {
        static const char *const names[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 24 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], 12 + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
#ifdef LT_ONE
    win = lt_open(scr, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, scr->BarHeight + 20, W, row - scr->WBorTop - fh - 1, glist, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | INTEGERIDCMP | STRINGIDCMP | LISTVIEWIDCMP | TEXTIDCMP, "OpenSystem 0.1");
#else
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"System: Stack", WA_ScreenTitle, (ULONG)"OpenSystem 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | INTEGERIDCMP | STRINGIDCMP |
                                   LISTVIEWIDCMP | TEXTIDCMP,
                         TAG_DONE);
#endif
    if (!win) { rc = RETURN_FAIL; goto out; }
    GT_RefreshWindow(win, NULL);
    lt_note_to(show_status);

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) quit = 1;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); LT_REFRESH(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_GADGETUP) {
                switch (gg->GadgetID) {
                case G_CAP: take_cap(); break;
                case G_LIST:
                    sel = code < nprogs ? code : -1;
                    if (sel >= 0) {
                        GT_SetGadgetAttrs(gad[G_NAME], win, NULL, GTST_String, (ULONG)progs[sel].name, TAG_DONE);
                        GT_SetGadgetAttrs(gad[G_PCAP], win, NULL, GTIN_Number, progs[sel].kb, TAG_DONE);
                    }
                    break;
                case G_PCAP:
                case G_SET: set_from_fields(); break;
                case G_REMOVE: remove_selected(); break;
#ifdef LT_ONE
                default: {                          /* another section: the page's changes kept, as Use, when it has any */
                    int to = lt_hit(gg, code);
                    if (to >= 0) { if (LT_DIRTY) put_in_place(0); lt_go(win, to); quit = 1; }
                    break;
                }
#endif
                case G_SAVE: if (put_in_place(1)) quit = 1; break;
                case G_USE: if (put_in_place(0)) quit = 1; break;
                case G_CANCEL: quit = 1; break;
                }
            }
        }
    }
out:
#ifdef LT_ONE
    if (win) { lt_close(win, glist); win = NULL; }
#else
    if (win) { CloseWindow(win); win = NULL; }
#endif
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

/* ---- the Shell --------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    LONG args[7] = { 0, 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"APPLY/S,INFO/S,CAP/K/N,PROGRAM/K,PCAP/K/N,USE/S,SAVE/S", args, NULL) : NULL;
    int rc = RETURN_OK;
    (void)argv;
    (void)version;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"OpenSystem"); return RETURN_FAIL; }
    opensystem_expansion = OpenLibrary((STRPTR)"expansion.library", 37);
    load_settings(STACK_ENV);
    if (args[0]) {                               /* APPLY: ENV: to the runtime */
        if (!apply_settings()) { PutStr((STRPTR)"OpenSystem: no AmigaChrome stack runtime here; nothing to apply\n"); rc = RETURN_WARN; }
    } else if (args[1]) rc = info();
    else if (args[2] || args[3] || args[5] || args[6]) {
        if (args[2]) cap_kb = clamp_kb(*(LONG *)args[2]);
        if (args[3] && !set_prog((const char *)args[3], args[4] ? *(LONG *)args[4] : 0)) {
            PutStr((STRPTR)"OpenSystem: no room for that program (31 characters, no spaces, colons or slashes; 32 programs)\n");
            rc = RETURN_ERROR;
        } else if ((args[6] && !save_settings("ENVARC:OpenAmiga", STACK_ENVARC)) || !save_settings("ENV:OpenAmiga", STACK_ENV)) {
            PutStr((STRPTR)"OpenSystem: could not write the settings file\n");
            rc = RETURN_ERROR;
        } else apply_settings();
    }
#ifndef OP_APPLY
    else rc = gui();                         /* OpenPrefs's C:OpenPrefs (OP_APPLY): APPLY only, no window */
#endif
    if (rd) FreeArgs(rd);
    if (opensystem_expansion) CloseLibrary(opensystem_expansion);
    return rc;
}
