/* OpenSerial: the serial port's settings in one OpenPrefs editor. It takes over
 * the OS's Serial editor (OpenUp's RECONCILE puts that in SYS:Prefs/Classic).
 *
 *   OpenSerial [FROM file] [USE] [SAVE]
 *
 *   - the baud rate, the buffer sizes, handshaking (none, XON/XOFF, RTS/CTS),
 *     parity, bits per character and stop bits, and whether the port is shared,
 *     as serial.prefs keeps them (prefs/serial.h)
 *
 * Save writes ENV:Sys/serial.prefs and ENVARC:; Use writes ENV: only (IPrefs
 * reads it at once); Cancel changes nothing. Everything else in the file is
 * kept as it was.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <prefs/prefhdr.h>
#include <prefs/serial.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char version[] __attribute__((used)) = "$VER: OpenSerial 0.1 (10.10.2026) OpenPrefs, MIT, Copyright (c) 2026 Dalsin Limited";

#define SERIAL_ENV "ENV:Sys/serial.prefs"
#define SERIAL_ENVARC "ENVARC:Sys/serial.prefs"

/* ---- serial.prefs ------------------------------------------------------------- */

static struct SerialPrefs sp;
static UBYTE *file_data;                        /* the whole file read, to keep its other chunks */
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

static void load_serial(const char *path)
{
    UBYTE *c;
    LONG n = 0;
    memset(&sp, 0, sizeof sp);
    sp.sp_BaudRate = 19200;                     /* the OS's defaults */
    sp.sp_InputBuffer = 512;
    sp.sp_OutputBuffer = 512;
    sp.sp_InputHandshake = HSHAKE_XON;
    sp.sp_OutputHandshake = HSHAKE_XON;
    sp.sp_BitsPerChar = BITS_8;
    sp.sp_Parity = PARITY_NONE;
    sp.sp_StopBits = SBITS_1;
    if (file_data) { FreeVec(file_data); file_data = NULL; }
    file_data = read_all(path, &file_len);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4) && !memcmp(file_data + 8, "PREF", 4) &&
        (c = chunk(file_data, file_len, "SERL", &n)))
        memcpy(&sp, c, n < (LONG)sizeof sp ? n : (LONG)sizeof sp);
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

/* The file again: its PRHD and other chunks as they were, SERL with sp. */
static int save_serial(const char *path)
{
    LONG size = 12 + 14 + 8 + sizeof sp + file_len + 16, o = 12, k;
    UBYTE *out = AllocVec(size, MEMF_ANY | MEMF_CLEAR);
    int ok, had_prhd = 0;
    if (!out) return 0;
    memcpy(out, "FORM\0\0\0\0PREF", 12);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4)) {
        for (k = 12; k + 8 <= file_len; ) {
            LONG cl = (LONG)get32(file_data + k + 4), all = 8 + cl + (cl & 1);
            if (cl < 0 || k + all > file_len) break;
            if (memcmp(file_data + k, "SERL", 4)) {
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
    memcpy(out + o, "SERL", 4);
    put32(out + o + 4, sizeof sp);
    memcpy(out + o + 8, &sp, sizeof sp);
    o += 8 + sizeof sp + (sizeof sp & 1);
    put32(out + 4, o - 8);
    ok = write_file(path, out, o);
    FreeVec(out);
    return ok;
}

/* ---- the window ------------------------------------------------------------------ */

enum { G_BAUD = 1, G_INBUF, G_OUTBUF, G_HANDSHAKE, G_PARITY, G_BITS, G_STOP, G_SHARED, G_7WIRE, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };

static const ULONG bauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 31250, 38400, 57600, 115200 };
static const char *const baud_labels[] = { "300", "1200", "2400", "4800", "9600", "19200", "31250", "38400", "57600", "115200", NULL };
#define BAUDS (int)(sizeof bauds / sizeof bauds[0])
static const char *const hs_labels[] = { "XON/XOFF", "RTS/CTS", "None", NULL };          /* in HSHAKE_* order */
static const UBYTE hs_values[] = { HSHAKE_XON, HSHAKE_RTS, HSHAKE_NONE };
static const char *const parity_labels[] = { "None", "Even", "Odd", "Mark", "Space", NULL };   /* PARITY_* order */
static const char *const bits_labels[] = { "7", "8", NULL };                                 /* BITS_7, BITS_8 */
static const char *const stop_labels[] = { "1", "2", NULL };                                 /* SBITS_1, SBITS_2 */

static struct Window *win;
static struct Gadget *gad[G_COUNT];
static char status_text[80];
static int sel_baud, sel_hs, sel_parity, sel_bits, sel_stop, shared, wire7;

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

static int baud_index(ULONG baud)
{
    int i, best = 5;                             /* 19200 when the rate is not in the list */
    for (i = 0; i < BAUDS; i++) if (bauds[i] == baud) best = i;
    return best;
}

static int hs_index(UBYTE hs)
{
    int i;
    for (i = 0; i < 3; i++) if (hs_values[i] == hs) return i;
    return 0;
}

/* What Use or Save does: the gadgets into sp, then the file. 1 done. */
static int put_in_place(int save)
{
    LONG in = number(G_INBUF), out = number(G_OUTBUF);
    if (in < 64) in = 64;
    if (in > 65536) in = 65536;
    if (out < 64) out = 64;
    if (out > 65536) out = 65536;
    sp.sp_BaudRate = bauds[sel_baud];
    sp.sp_InputBuffer = in;
    sp.sp_OutputBuffer = out;
    sp.sp_InputHandshake = sp.sp_OutputHandshake = hs_values[sel_hs];
    sp.sp_Parity = sel_parity;
    sp.sp_BitsPerChar = sel_bits;
    sp.sp_StopBits = sel_stop;
    sp.sp_SerialFlags = (shared ? SERIALFLAG_SHARED : 0) | (wire7 ? SERIALFLAG_7WIRE : 0);
    if (save && !save_serial(SERIAL_ENVARC)) { status("Couldn't write ENVARC:Sys/serial.prefs."); return 0; }
    if (!save_serial(SERIAL_ENV)) { status("Couldn't write ENV:Sys/serial.prefs."); return 0; }
    return 1;
}

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, (STRPTR)"Cancel", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_END, 0, 0, 0, 0, 0 }
};

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    struct Menu *menu = NULL;
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, lw, cw, nw, i;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    sel_baud = baud_index(sp.sp_BaudRate);
    sel_hs = hs_index(sp.sp_InputHandshake);
    sel_parity = sp.sp_Parity <= PARITY_SPACE ? sp.sp_Parity : PARITY_NONE;
    sel_bits = sp.sp_BitsPerChar == BITS_7 ? 0 : 1;
    sel_stop = sp.sp_StopBits == SBITS_2 ? 1 : 0;
    shared = (sp.sp_SerialFlags & SERIALFLAG_SHARED) != 0;
    wire7 = (sp.sp_SerialFlags & SERIALFLAG_7WIRE) != 0;

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    lw = TextLength(&scr->RastPort, (STRPTR)"Bits per character", 18) + 16;   /* the labels' column */
    X = 12 + lw;
    cw = TextLength(&scr->RastPort, (STRPTR)"XON/XOFF", 8) + 40;             /* a cycle gadget */
    nw = TextLength(&scr->RastPort, (STRPTR)"000000", 6) + 16;               /* a number */
    W = X + cw + 12;
    if (W < 300) W = 300;
    snprintf(status_text, sizeof status_text, "%s", "Change what you like, then Use or Save.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
    row = top;
    G(CYCLE_KIND, G_BAUD, X, row, cw, lh, "Baud rate", PLACETEXT_LEFT, GTCY_Labels, (ULONG)baud_labels, GTCY_Active, sel_baud);
    row += lh + 4;
    G(INTEGER_KIND, G_INBUF, X, row, nw, lh, "Input buffer", PLACETEXT_LEFT, GTIN_Number, sp.sp_InputBuffer, GTIN_MaxChars, 6);
    row += lh + 4;
    G(INTEGER_KIND, G_OUTBUF, X, row, nw, lh, "Output buffer", PLACETEXT_LEFT, GTIN_Number, sp.sp_OutputBuffer, GTIN_MaxChars, 6);
    row += lh + 10;
    G(CYCLE_KIND, G_HANDSHAKE, X, row, cw, lh, "Handshaking", PLACETEXT_LEFT, GTCY_Labels, (ULONG)hs_labels, GTCY_Active, sel_hs);
    row += lh + 4;
    G(CYCLE_KIND, G_PARITY, X, row, cw, lh, "Parity", PLACETEXT_LEFT, GTCY_Labels, (ULONG)parity_labels, GTCY_Active, sel_parity);
    row += lh + 4;
    G(CYCLE_KIND, G_BITS, X, row, cw, lh, "Bits per character", PLACETEXT_LEFT, GTCY_Labels, (ULONG)bits_labels, GTCY_Active, sel_bits);
    row += lh + 4;
    G(CYCLE_KIND, G_STOP, X, row, cw, lh, "Stop bits", PLACETEXT_LEFT, GTCY_Labels, (ULONG)stop_labels, GTCY_Active, sel_stop);
    row += lh + 10;
    G(CHECKBOX_KIND, G_SHARED, X, row, 26, lh, "Shared port", PLACETEXT_LEFT, GTCB_Checked, shared, GTCB_Scaled, TRUE);
    row += lh + 4;
    G(CHECKBOX_KIND, G_7WIRE, X, row, 26, lh, "7-wire cable", PLACETEXT_LEFT, GTCB_Checked, wire7, GTCB_Scaled, TRUE);
    row += lh + 10;
    G(TEXT_KIND, G_STATUS, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE);
    row += lh + 6;
    {
        static const char *const names[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 24 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], 12 + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Serial", WA_ScreenTitle, (ULONG)"OpenSerial 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | BUTTONIDCMP | INTEGERIDCMP |
                                   CYCLEIDCMP | CHECKBOXIDCMP | TEXTIDCMP,
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
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: if (put_in_place(1)) quit = 1; break;
                    case 2: if (put_in_place(0)) quit = 1; break;
                    case 3: quit = 1; break;
                    }
                    code = it->NextSelect;
                }
            }
            else if (cls == IDCMP_GADGETUP) {
                switch (gg->GadgetID) {
                case G_BAUD: sel_baud = code; break;
                case G_HANDSHAKE: sel_hs = code; break;
                case G_PARITY: sel_parity = code; break;
                case G_BITS: sel_bits = code; break;
                case G_STOP: sel_stop = code; break;
                case G_SHARED: shared = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_7WIRE: wire7 = (gg->Flags & GFLG_SELECTED) != 0; break;
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
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"OpenSerial"); return RETURN_FAIL; }
    load_serial(args[0] ? (const char *)args[0] : SERIAL_ENV);
    if (args[1] || args[2]) {
        rc = (args[2] && !save_serial(SERIAL_ENVARC)) || !save_serial(SERIAL_ENV) ? RETURN_ERROR : RETURN_OK;
    } else rc = gui();
    if (rd) FreeArgs(rd);
    if (file_data) FreeVec(file_data);
    return rc;
}
