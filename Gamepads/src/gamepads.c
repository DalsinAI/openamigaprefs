/* OpenPrefs Gamepads 0.1: game controllers, through OpenInput
 * (openinput.library; design: DESIGN.md, and openamigainput's
 * Design-OpenInput.md section 8).
 *
 *   - the controllers, live: each one's name, maker and whether it is
 *     connected, with what it is, where it comes from and its mapping
 *   - a live test view of the standard layout: every button lit while
 *     pressed (labelled with the pad's own names), the sticks and the
 *     triggers; the raw buttons, axes and hats in the Advanced view; rumble
 *   - the mapping: built in, or the user's own, made with Map... ("press
 *     the bottom face button", and so on, with Skip), in SDL's text format;
 *     the Advanced view shows the line and takes one typed in
 *   - "Let older games see modern pads": OpenInput's ReadJoyPort patch,
 *     opt-in and off by default, with the port and how the pad appears
 *     there (a CD32 pad or a joystick)
 *
 * Save writes ENV: and ENVARC:, Use writes ENV:, Test writes ENV: and
 * stays open, Cancel puts ENV: back as it was.
 *
 *   Gamepads [USE] [SAVE] [ADVANCED]
 *
 * "Gamepads USE" at boot (OpenUp's startup) starts openinput.library, so
 * the ReadJoyPort patch is in when it is switched on.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/openinput.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gp_core.h"

const char version[] __attribute__((used)) = "$VER: Gamepads 0.1 (8.10.2026) OpenPrefs, Dalsin Limited";

struct Library *OpenInputBase;

#define VIEW_ENV "ENV:OpenAmiga/PrefsView"           /* shared by every OpenPrefs editor: simple or advanced */
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"
#define PATCH_ENV "ENV:" OPENINPUT_LOWLEVELPATCH
#define PATCH_ENVARC "ENVARC:" OPENINPUT_LOWLEVELPATCH
#define PORTS_ENV "ENV:" OPENINPUT_PORTS
#define PORTS_ENVARC "ENVARC:" OPENINPUT_PORTS
#define MAPS_ENV "ENV:" OPENINPUT_MAPPINGS
#define MAPS_ENVARC "ENVARC:" OPENINPUT_MAPPINGS
#define MAXPADS 16
#define MAPLEN 512
#define MAXCHANGES 8

/* ---- what is known -------------------------------------------------------------------- */

static gp_settings cur, orig;
static int tested;                                    /* Test wrote ENV: (Cancel puts it back) */
static int advanced;
static int have_lib;                                  /* a working openinput.library (1.2 or later) */

static struct OIControllerInfo pads[MAXPADS];
static int npads;
static struct Node pad_nodes[MAXPADS + 1];
static char pad_text[MAXPADS + 1][112];
static struct List pad_list;

static ULONG sel_id;                                  /* the chosen controller, 0 none */
static struct OIControllerInfo sel;                   /* its info (kept while it is away) */
static int sel_gone;
static APTR handle;                                   /* the editor's handle on it */
static struct OIState st;
static struct OIRawState raw;
static ULONG last_seq = 0xFFFFFFFF;

static struct MsgPort *oiport;
static APTR notify_all, notify_state;

/* mappings changed here: put back on Cancel, kept on Save */
struct Change { UBYTE guid[16]; char was[MAPLEN]; char now[MAPLEN]; int own_was, own_now; };
static struct Change changes[MAXCHANGES];
static int nchanges;

/* the Map... wizard */
static int map_step = -1;                             /* -1: not mapping; else the target being asked for */
static int map_wait_release;
static gp_raw map_rest;
static gp_input map_in[GP_TARGETS];

/* ---- the window ------------------------------------------------------------------------- */

enum {
    G_LIST, G_KIND, G_MAKER, G_FROM, G_MAPPED, G_FEEDS,
    G_RUMBLE, G_RAW, G_OTHER,
    G_MAPCHOICE, G_MAP, G_SKIP, G_MAPLINE, G_MAPTEXT,
    G_PATCH, G_PATCHTEXT, G_PORT, G_AS, G_WHICH, G_PATCHNOTE,
    G_STATUS, G_SAVE, G_USE, G_TEST, G_CANCEL, G_COUNT
};

static const char *mapchoice_labels[] = { "Built in", "Your own", NULL };
static const char *port_labels[] = { "Joystick port (2)", "Mouse port (1)", NULL };
static const char *as_labels[] = { "CD32 pad", "Joystick", NULL };
static STRPTR which_labels[MAXPADS + 3];
static char which_text[MAXPADS + 2][72];
static UBYTE which_guid[MAXPADS + 2][16];
static int nwhich;

static struct Gadget *gad[G_COUNT];
static struct Window *win;
static struct Screen *scr;
static struct DrawInfo *dri;
static char status_text[120], patch_text[80], kind_text[40], maker_text[40], from_text[40], mapped_text[48], feeds_text[60];
static char raw_text[120], other_text[100], map_summary[100], map_line[MAPLEN];
static int box_x, box_y, box_w, box_h;                /* the test view */

#define SET(id, ...) do { if (gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE); } while (0)

/* ---- files ---------------------------------------------------------------------------- */

static LONG read_file(const char *path, char *buf, LONG size)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG n;
    buf[0] = 0;
    if (!fh) return -1;
    n = Read(fh, buf, size - 1);
    Close(fh);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

static int write_file(const char *path, const char *dir, const char *text, LONG n)
{
    BPTR fh, lock;
    if (dir) { if ((lock = Lock((STRPTR)dir, ACCESS_READ))) UnLock(lock); else if ((lock = CreateDir((STRPTR)dir))) UnLock(lock); }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    Write(fh, (APTR)text, n);
    Close(fh);
    return 1;
}

static int read_view(void)
{
    char t[16];
    return read_file(VIEW_ENV, t, sizeof t) > 0 && !strncmp(t, "advanced", 8);
}

static void write_view(int a)
{
    write_file(VIEW_ENV, "ENV:OpenAmiga", a ? "advanced\n" : "simple\n", a ? 9 : 7);
    write_file(VIEW_ENVARC, "ENVARC:OpenAmiga", a ? "advanced\n" : "simple\n", a ? 9 : 7);
}

static void load_settings(gp_settings *s)
{
    char t[512];
    gp_defaults(s);
    if (read_file(PATCH_ENV, t, sizeof t) > 0) s->patch = gp_patch_from_text(t);
    if (read_file(PORTS_ENV, t, sizeof t) > 0) gp_ports_from_text(s, t);
}

static void store_settings(const gp_settings *s, int save)
{
    char t[512];
    int n = gp_ports_to_text(s, t, sizeof t);
    /* the ports first: the library reads both within a second, and should find the port before the switch */
    write_file(PORTS_ENV, "ENV:" OPENINPUT_ENVDIR, t, n);
    write_file(PATCH_ENV, "ENV:" OPENINPUT_ENVDIR, s->patch ? "1\n" : "0\n", 2);
    if (save) {
        write_file(PORTS_ENVARC, "ENVARC:" OPENINPUT_ENVDIR, t, n);
        write_file(PATCH_ENVARC, "ENVARC:" OPENINPUT_ENVDIR, s->patch ? "1\n" : "0\n", 2);
    }
}

/* Is there a line of the user's own for this GUID in mappings.txt? */
static int own_mapping(const UBYTE *guid)
{
    static char t[8192];
    char hex[33];
    const char *l;
    gp_guid_to_hex(guid, hex);
    if (read_file(MAPS_ENV, t, sizeof t) <= 0) return 0;
    for (l = t; *l; ) {
        if (!strncasecmp(l, hex, 32)) return 1;
        while (*l && *l != '\n') l++;
        if (*l) l++;
    }
    return 0;
}

/* mappings.txt without this GUID's line (the built-in mapping again, at the library's next start) */
static void drop_own_mapping(const UBYTE *guid, const char *path, const char *dir)
{
    static char t[8192], out[8192];
    char hex[33];
    const char *l;
    LONG n = 0;
    gp_guid_to_hex(guid, hex);
    if (read_file(path, t, sizeof t) <= 0) return;
    for (l = t; *l; ) {
        const char *e = l;
        while (*e && *e != '\n') e++;
        if (*e) e++;
        if (strncasecmp(l, hex, 32)) { memcpy(out + n, l, e - l); n += e - l; }
        l = e;
    }
    write_file(path, dir, out, n);
}

/* A mapping line in use (and kept, with OISMF_SAVE), as the only line for
 * its GUID in mappings.txt: openinput.library adds the line to the file,
 * so the old ones go first. */
static LONG set_mapping_clean(const char *line, ULONG flags)
{
    UBYTE guid[16];
    if (gp_guid_from_hex(line, guid)) {
        drop_own_mapping(guid, MAPS_ENV, "ENV:" OPENINPUT_ENVDIR);
        if (flags & OISMF_SAVE) drop_own_mapping(guid, MAPS_ENVARC, "ENVARC:" OPENINPUT_ENVDIR);
    }
    return OIN_SetMapping((STRPTR)line, flags);
}

/* The mapping as it was before (a built-in or source's line): in use
 * again, and no line of the user's left in ENV: for it. */
static void set_mapping_builtin(const UBYTE *guid, const char *was)
{
    if (was[0]) OIN_SetMapping((STRPTR)was, OISMF_USE);
    drop_own_mapping(guid, MAPS_ENV, "ENV:" OPENINPUT_ENVDIR);
}

/* ---- the controllers ------------------------------------------------------------------- */

static const char *family_name(ULONG f)
{
    static const char *const names[] = { "Generic", "Xbox", "PlayStation", "Switch", "8BitDo", "CD32", "Amiga" };
    return f < 7 ? names[f] : "Generic";
}

static const char *type_name(ULONG t)
{
    static const char *const names[] = { "Unknown", "Game pad", "Joystick", "CD32 pad", "Arcade stick", "Wheel", "Flight stick" };
    return t < 7 ? names[t] : "Unknown";
}

static const char *source_name(ULONG s)
{
    switch (s) {
    case OISRC_AMIGAPORT: return "An Amiga port";
    case OISRC_POSEIDON: return "USB (Poseidon)";
    case OISRC_AMIGACHROME: return "AmigaChrome";
    case OISRC_PISTORMPORT: return "A1200 port (PiStorm)";
    }
    return "Unknown";
}

static const char *port_name(ULONG p)
{
    return p == 0 ? "the mouse port (1)" : p == 1 ? "the joystick port (2)" : p == 2 ? "adapter port 3" : "adapter port 4";
}

static void close_handle(void)
{
    if (notify_state) { OIN_RemNotify(notify_state); notify_state = NULL; }
    if (handle) { OIN_CloseController(handle); handle = NULL; }
}

static void open_handle(void)
{
    struct TagItem t[3];
    close_handle();
    if (!have_lib || !sel_id || sel_gone) return;
    handle = OIN_OpenControllerA(sel_id, NULL);           /* shared: games keep their port */
    if (oiport) {
        t[0].ti_Tag = OIT_Events; t[0].ti_Data = OIMC_STATE;
        t[1].ti_Tag = OIT_ID; t[1].ti_Data = sel_id;
        t[2].ti_Tag = TAG_DONE;
        notify_state = OIN_AddNotifyA(oiport, t);
    }
    last_seq = 0xFFFFFFFF;
}

static void list_pads(void)
{
    int i, found = -1;
    npads = 0;
    if (have_lib) {
        ULONG n = OIN_ListControllers(pads, MAXPADS, sizeof(pads[0]));
        npads = n > MAXPADS ? MAXPADS : (int)n;
    }
    pad_list.lh_Head = (struct Node *)&pad_list.lh_Tail; pad_list.lh_Tail = NULL; pad_list.lh_TailPred = (struct Node *)&pad_list.lh_Head;
    for (i = 0; i < npads; i++) if (pads[i].oci_ID == sel_id) found = i;
    if (found < 0 && sel_id) {                            /* gone, or back under a new ID */
        for (i = 0; i < npads; i++)
            if (!memcmp(pads[i].oci_GUID, sel.oci_GUID, 16) && !strcmp(pads[i].oci_Name, sel.oci_Name)) { found = i; break; }
        if (found >= 0) { sel_id = pads[found].oci_ID; sel_gone = 0; open_handle(); }
        else if (!sel_gone) { sel_gone = 1; close_handle(); }
    }
    if (found >= 0) { sel = pads[found]; sel_gone = 0; }
    if (!sel_id && npads) { sel = pads[0]; sel_id = sel.oci_ID; sel_gone = 0; open_handle(); }
    for (i = 0; i < npads; i++) {
        snprintf(pad_text[i], sizeof pad_text[i], "%s, %s", pads[i].oci_Name, family_name(pads[i].oci_Family));
        pad_nodes[i].ln_Name = pad_text[i];
        AddTail(&pad_list, &pad_nodes[i]);
    }
    if (sel_id && sel_gone) {                             /* kept in the list, marked, until it comes back */
        snprintf(pad_text[i], sizeof pad_text[i], "%s, %s (not connected)", sel.oci_Name, family_name(sel.oci_Family));
        pad_nodes[i].ln_Name = pad_text[i];
        AddTail(&pad_list, &pad_nodes[i]);
    }
}

static int sel_row(void)
{
    int i;
    for (i = 0; i < npads; i++) if (pads[i].oci_ID == sel_id) return i;
    return sel_id ? npads : -1;
}

/* the "which pad" choices: any modern pad, each modern pad here, and the saved one if it isn't here */
static void list_which(void)
{
    gp_port *p = &cur.port[gp_fed_port(&cur) < 0 ? 1 : gp_fed_port(&cur)];
    int i, have_saved = p->any;
    nwhich = 0;
    strcpy(which_text[nwhich], "Any modern pad"); memset(which_guid[nwhich], 0, 16); nwhich++;
    for (i = 0; i < npads && nwhich < MAXPADS + 1; i++) {
        int k, dup = 0;
        if (pads[i].oci_Source == OISRC_AMIGAPORT) continue;     /* a port never feeds a port */
        for (k = 1; k < nwhich; k++) if (!memcmp(which_guid[k], pads[i].oci_GUID, 16)) dup = 1;
        if (dup) continue;
        snprintf(which_text[nwhich], sizeof which_text[0], "%s", pads[i].oci_Name);
        memcpy(which_guid[nwhich], pads[i].oci_GUID, 16);
        if (!p->any && !memcmp(p->guid, pads[i].oci_GUID, 16)) have_saved = 1;
        nwhich++;
    }
    if (!have_saved) { strcpy(which_text[nwhich], "The saved pad (not here now)"); memcpy(which_guid[nwhich], p->guid, 16); nwhich++; }
    for (i = 0; i < nwhich; i++) which_labels[i] = (STRPTR)which_text[i];
    which_labels[nwhich] = NULL;
}

static int which_index(void)
{
    gp_port *p = &cur.port[gp_fed_port(&cur) < 0 ? 1 : gp_fed_port(&cur)];
    int i;
    if (p->any) return 0;
    for (i = 1; i < nwhich; i++) if (!memcmp(which_guid[i], p->guid, 16)) return i;
    return 0;
}

/* ---- showing it ---------------------------------------------------------------------------- */

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    SET(G_STATUS, GTTX_Text, (ULONG)status_text);
}

static void show_info(void)
{
    struct OILegacyPort lp;
    if (!sel_id) {
        strcpy(kind_text, "-"); strcpy(maker_text, "-"); strcpy(from_text, "-"); strcpy(mapped_text, "-"); strcpy(feeds_text, "-");
    } else {
        snprintf(kind_text, sizeof kind_text, "%s%s", type_name(sel.oci_Type), sel_gone ? ", not connected" : ", connected");
        snprintf(maker_text, sizeof maker_text, "%s", family_name(sel.oci_Family));
        snprintf(from_text, sizeof from_text, "%s", source_name(sel.oci_Source));
        snprintf(mapped_text, sizeof mapped_text, "%s%s", (sel.oci_Flags & OICF_MAPPED) ? "Yes" : "A best guess",
                 own_mapping(sel.oci_GUID) ? ", your own" : "");
        if (sel.oci_Source == OISRC_AMIGAPORT) snprintf(feeds_text, sizeof feeds_text, "It is %s", port_name(sel.oci_LegacyPort));
        else if (sel.oci_Flags & OICF_LEGACYFEED)
            snprintf(feeds_text, sizeof feeds_text, "%s, as a %s", port_name(sel.oci_LegacyPort),
                     sel.oci_LegacyMode == OILEG_CD32 ? "CD32 pad" : sel.oci_LegacyMode == OILEG_MOUSE ? "mouse" :
                     sel.oci_LegacyMode == OILEG_KEYS ? "keys" : "joystick");
        else strcpy(feeds_text, "No Amiga port");
        if ((sel.oci_Flags & OICF_INUSE)) strncat(feeds_text, " (paused: in use)", sizeof feeds_text - strlen(feeds_text) - 1);
    }
    (void)lp;
    SET(G_KIND, GTTX_Text, (ULONG)kind_text);
    SET(G_MAKER, GTTX_Text, (ULONG)maker_text);
    SET(G_FROM, GTTX_Text, (ULONG)from_text);
    SET(G_MAPPED, GTTX_Text, (ULONG)mapped_text);
    SET(G_FEEDS, GTTX_Text, (ULONG)feeds_text);
    SET(G_RUMBLE, GA_Disabled, !handle || !(sel.oci_Flags & OICF_RUMBLE));
    map_line[0] = 0;
    if (have_lib && sel_id) OIN_GetMapping(sel.oci_GUID, (STRPTR)map_line, sizeof map_line);
    gp_mapping_summary(map_line, map_summary, sizeof map_summary);
    if (!map_line[0]) strcpy(map_summary, sel.oci_Source == OISRC_AMIGAPORT || sel.oci_Source == OISRC_AMIGACHROME ?
                             "Mapped by its source" : "None yet: use Map...");
    SET(G_MAPCHOICE, GTCY_Active, sel_id && own_mapping(sel.oci_GUID) ? 1 : 0, GA_Disabled, !sel_id || map_step >= 0);
    SET(G_MAP, GA_Disabled, !handle || map_step >= 0);
    SET(G_SKIP, GA_Disabled, map_step < 0);
    SET(G_MAPTEXT, GTTX_Text, (ULONG)map_summary);
    SET(G_MAPLINE, GTST_String, (ULONG)map_line, GA_Disabled, !sel_id);
}

static void show_patch(void)
{
    int p = gp_fed_port(&cur);
    int as = p >= 0 && cur.port[p].mode == GP_MODE_JOYSTICK;
    if (p < 0) p = 1;
    snprintf(patch_text, sizeof patch_text, "Let older games see modern pads (%s on port %d)", as ? "joystick" : "CD32 pad", p + 1);
    SET(G_PATCH, GTCB_Checked, cur.patch);
    SET(G_PATCHTEXT, GTTX_Text, (ULONG)patch_text);
    SET(G_PORT, GTCY_Active, p == 1 ? 0 : 1, GA_Disabled, !cur.patch);
    SET(G_AS, GTCY_Active, as, GA_Disabled, !cur.patch);
    if (gad[G_WHICH]) { list_which(); SET(G_WHICH, GTCY_Labels, (ULONG)which_labels, GTCY_Active, which_index(), GA_Disabled, !cur.patch); }
}

static void show_list(void)
{
    SET(G_LIST, GTLV_Labels, ~0UL);
    list_pads();
    SET(G_LIST, GTLV_Labels, (ULONG)&pad_list, GTLV_Selected, sel_row(), GTLV_MakeVisible, sel_row() < 0 ? 0 : sel_row());
    show_info();
    if (gad[G_WHICH]) show_patch();
}

/* ---- the test view ------------------------------------------------------------------------------ */

static void box(struct RastPort *rp, int x, int y, int w, int h, int lit, const char *label)
{
    SetAPen(rp, dri->dri_Pens[lit ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, x + 1, y + 1, x + w - 2, y + h - 2);
    SetAPen(rp, dri->dri_Pens[lit ? SHINEPEN : SHADOWPEN]);
    Move(rp, x, y + h - 1); Draw(rp, x, y); Draw(rp, x + w - 1, y);
    SetAPen(rp, dri->dri_Pens[lit ? SHADOWPEN : SHINEPEN]);
    Draw(rp, x + w - 1, y + h - 1); Draw(rp, x, y + h - 1);
    if (label) {
        int n = strlen(label), tw;
        while (n > 0 && (tw = TextLength(rp, (STRPTR)label, n)) > w - 4) n--;
        tw = TextLength(rp, (STRPTR)label, n);
        SetAPen(rp, dri->dri_Pens[lit ? FILLTEXTPEN : TEXTPEN]);
        SetDrMd(rp, JAM1);
        Move(rp, x + (w - tw) / 2, y + (h - rp->TxHeight) / 2 + rp->TxBaseline + 1);
        Text(rp, (STRPTR)label, n);
    }
}

static void bar(struct RastPort *rp, int x, int y, int w, int h, int value, const char *label)
{
    int fill = value <= 0 ? 0 : (int)((long)(w - 2) * value / 32767);
    box(rp, x, y, w, h, 0, NULL);
    if (fill > 0) { SetAPen(rp, dri->dri_Pens[FILLPEN]); RectFill(rp, x + 1, y + 1, x + fill, y + h - 2); }
    if (label) {
        int tw = TextLength(rp, (STRPTR)label, strlen(label));
        SetAPen(rp, dri->dri_Pens[TEXTPEN]); SetDrMd(rp, JAM1);
        Move(rp, x + (w - tw) / 2, y + (h - rp->TxHeight) / 2 + rp->TxBaseline + 1);
        Text(rp, (STRPTR)label, strlen(label));
    }
}

static void stick(struct RastPort *rp, int cx, int cy, int r, int x, int y, int click, const char *label)
{
    int px = cx + (int)((long)x * (r - 3) / 32768), py = cy + (int)((long)y * (r - 3) / 32768);
    SetAPen(rp, dri->dri_Pens[click ? FILLPEN : BACKGROUNDPEN]);
    RectFill(rp, cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1);
    SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
    DrawEllipse(rp, cx, cy, r, r);
    Move(rp, cx - r, cy); Draw(rp, cx + r, cy); Move(rp, cx, cy - r); Draw(rp, cx, cy + r);
    SetAPen(rp, dri->dri_Pens[click ? FILLTEXTPEN : FILLPEN]);
    RectFill(rp, px - 2, py - 2, px + 2, py + 2);
    if (label) {
        int tw = TextLength(rp, (STRPTR)label, strlen(label));
        SetAPen(rp, dri->dri_Pens[TEXTPEN]); SetDrMd(rp, JAM1);
        Move(rp, cx - tw / 2, cy + r + rp->TxBaseline + 3);
        Text(rp, (STRPTR)label, strlen(label));
    }
}

static const char *label_of(ULONG b)
{
    STRPTR s = have_lib ? OIN_ButtonLabel(sel_gone ? 0 : sel_id, b) : NULL;
    return s ? (const char *)s : "?";
}

static void draw_test(void)
{
    struct RastPort *rp;
    int x0, y0, w, h, fh, cw, row1, row2, cx, bw, i;
    ULONG b = (sel_id && !sel_gone) ? st.ois_Buttons : 0;
    if (!win || !box_w) return;
    rp = win->RPort;
    fh = rp->TxHeight;
    x0 = box_x + 2; y0 = box_y + 1; w = box_w - 4; h = box_h - 2;
    SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
    RectFill(rp, x0, y0, x0 + w - 1, y0 + h - 1);
    if (!sel_id) {
        const char *t = have_lib ? "No game controller is connected." : "OpenInput (LIBS:openinput.library 1.2) is not installed.";
        SetAPen(rp, dri->dri_Pens[TEXTPEN]); SetDrMd(rp, JAM1);
        Move(rp, x0 + 8, y0 + h / 2); Text(rp, (STRPTR)t, strlen(t));
        return;
    }
    cw = fh + 4;
    bw = TextLength(rp, (STRPTR)"Triangle", 8) + 8;
    /* the shoulders and triggers */
    row1 = y0 + 3;
    bar(rp, x0 + 6, row1, 60, fh + 4, (sel_id && !sel_gone) ? st.ois_Axes[OIAXIS_TRIGGERLEFT] : 0, "LT");
    box(rp, x0 + 72, row1, bw, fh + 4, b & OIBF(OIB_LEFTSHOULDER), label_of(OIB_LEFTSHOULDER));
    box(rp, x0 + w - 72 - bw, row1, bw, fh + 4, b & OIBF(OIB_RIGHTSHOULDER), label_of(OIB_RIGHTSHOULDER));
    bar(rp, x0 + w - 66, row1, 60, fh + 4, (sel_id && !sel_gone) ? st.ois_Axes[OIAXIS_TRIGGERRIGHT] : 0, "RT");
    /* the d-pad */
    row2 = row1 + fh + 10;
    box(rp, x0 + 8 + cw, row2, cw, cw, b & OIBF(OIB_DPAD_UP), NULL);
    box(rp, x0 + 8, row2 + cw, cw, cw, b & OIBF(OIB_DPAD_LEFT), NULL);
    box(rp, x0 + 8 + 2 * cw, row2 + cw, cw, cw, b & OIBF(OIB_DPAD_RIGHT), NULL);
    box(rp, x0 + 8 + cw, row2 + 2 * cw, cw, cw, b & OIBF(OIB_DPAD_DOWN), NULL);
    /* the left stick */
    stick(rp, x0 + 8 + 3 * cw + 14 + cw + 4, row2 + cw + cw / 2, cw + 4, st.ois_Axes[OIAXIS_LEFTX], st.ois_Axes[OIAXIS_LEFTY],
          b & OIBF(OIB_LEFTSTICK), "L");
    /* Back, Guide, Start */
    {
        int sw = TextLength(rp, (STRPTR)"Options", 7) + 8, total = 3 * sw + 8;
        cx = x0 + (w - total) / 2 - 40;               /* left of the right stick */
        box(rp, cx, row2 + cw / 2, sw, fh + 4, b & OIBF(OIB_BACK), label_of(OIB_BACK));
        box(rp, cx + sw + 4, row2 + cw / 2, sw, fh + 4, b & OIBF(OIB_GUIDE), label_of(OIB_GUIDE));
        box(rp, cx + 2 * sw + 8, row2 + cw / 2, sw, fh + 4, b & OIBF(OIB_START), label_of(OIB_START));
    }
    /* the face buttons: Y on top, X left, B right, A at the bottom */
    cx = x0 + w - 8 - bw - 2;
    box(rp, cx - bw / 2, row2, bw, fh + 4, b & OIBF(OIB_Y), label_of(OIB_Y));
    box(rp, cx - bw - 1, row2 + fh + 5, bw, fh + 4, b & OIBF(OIB_X), label_of(OIB_X));
    box(rp, cx + 1, row2 + fh + 5, bw, fh + 4, b & OIBF(OIB_B), label_of(OIB_B));
    box(rp, cx - bw / 2, row2 + 2 * (fh + 5), bw, fh + 4, b & OIBF(OIB_A), label_of(OIB_A));
    /* the right stick, left of the face buttons */
    stick(rp, cx - bw - 1 - 14 - cw - 4, row2 + 2 * cw + 4, cw + 4, st.ois_Axes[OIAXIS_RIGHTX], st.ois_Axes[OIAXIS_RIGHTY],
          b & OIBF(OIB_RIGHTSTICK), "R");
    /* the rest, in words */
    other_text[0] = 0;
    for (i = OIB_MISC1; i < OIB_COUNT; i++)
        if (b & OIBF(i)) {
            const char *l = label_of(i);
            if (strlen(other_text) + strlen(l) + 3 < sizeof other_text) { if (other_text[0]) strcat(other_text, ", "); strcat(other_text, l); }
        }
    if (sel_gone) strcpy(other_text, "Not connected: it shows again when it comes back.");
    else if (!other_text[0]) strcpy(other_text, map_step >= 0 ? "" : "Press the buttons and move the sticks: they light up here.");
    SET(G_OTHER, GTTX_Text, (ULONG)other_text);
}

static void show_raw(void)
{
    int i, n = 0;
    if (!gad[G_RAW]) return;
    n += snprintf(raw_text + n, sizeof raw_text - n, "Raw: buttons");
    for (i = 0; i < 64 && n < (int)sizeof raw_text - 8; i++)
        if (raw.oir_Buttons[i >> 5] & (1UL << (i & 31))) n += snprintf(raw_text + n, sizeof raw_text - n, " %d", i);
    n += snprintf(raw_text + n, sizeof raw_text - n, "  axes");
    for (i = 0; i < 6 && n < (int)sizeof raw_text - 8; i++) n += snprintf(raw_text + n, sizeof raw_text - n, " %d", (int)raw.oir_Axes[i]);
    if (n < (int)sizeof raw_text - 12) snprintf(raw_text + n, sizeof raw_text - n, "  hat %d", (int)raw.oir_Hats[0]);
    SET(G_RAW, GTTX_Text, (ULONG)raw_text);
}

/* ---- the Map... wizard ----------------------------------------------------------------------------- */

static void raw_now(gp_raw *r)
{
    int i;
    memset(r, 0, sizeof *r);
    r->buttons[0] = raw.oir_Buttons[0]; r->buttons[1] = raw.oir_Buttons[1];
    for (i = 0; i < 16; i++) r->axes[i] = raw.oir_Axes[i];
    for (i = 0; i < 4; i++) r->hats[i] = raw.oir_Hats[i];
}

static void map_ask(void)
{
    char t[120];
    if (map_step < 0) return;
    snprintf(t, sizeof t, "%d of %d: %s, or Skip.", map_step + 1, GP_TARGETS, gp_targets[map_step].ask);
    status(t);
}

static struct Change *change_for(const UBYTE *guid)
{
    int i;
    for (i = 0; i < nchanges; i++) if (!memcmp(changes[i].guid, guid, 16)) return &changes[i];
    if (nchanges == MAXCHANGES) return NULL;
    memset(&changes[nchanges], 0, sizeof changes[0]);
    memcpy(changes[nchanges].guid, guid, 16);
    OIN_GetMapping((UBYTE *)guid, (STRPTR)changes[nchanges].was, MAPLEN);
    changes[nchanges].own_was = changes[nchanges].own_now = own_mapping(guid);
    return &changes[nchanges++];
}

/* a mapping line in use now (ENV:), kept as a change for Save or Cancel */
static int use_mapping(const char *line, int own)
{
    struct Change *c = change_for(sel.oci_GUID);
    if (!c) { status("Too many changes at once: Save or Use first."); return 0; }
    if (OIN_SetMapping((STRPTR)line, OISMF_TEST) != OIERR_OK) { status("That mapping line can't be read."); return 0; }
    if (set_mapping_clean(line, OISMF_USE) != OIERR_OK) { status("OpenInput would not take the mapping."); return 0; }
    strncpy(c->now, line, MAPLEN - 1);
    c->own_now = own;
    return 1;
}

static void map_finish(void)
{
    static char line[MAPLEN];
    int i, any = 0;
    map_step = -1;
    for (i = 0; i < GP_TARGETS; i++) if (map_in[i].kind != GP_IN_NONE) any = 1;
    if (!any) { status("Nothing was mapped."); show_info(); return; }
    if (gp_build_mapping(sel.oci_GUID, sel.oci_Name, map_in, line, sizeof line) < 0) { status("The mapping is too long."); show_info(); return; }
    if (use_mapping(line, 1)) status("Mapped. Try it in the test view; Save keeps it.");
    show_info();
}

static void map_tick(void)
{
    gp_raw now;
    gp_input in;
    char el[16];
    if (map_step < 0) return;
    raw_now(&now);
    in = gp_raw_change(&map_rest, &now, gp_targets[map_step].axis);
    if (map_wait_release) { if (in.kind == GP_IN_NONE) map_wait_release = 0; return; }
    if (in.kind == GP_IN_NONE) return;
    gp_input_text(&in, map_step, el, sizeof el);
    if (!el[0]) return;                                        /* not something this can be: wait */
    map_in[map_step] = in;
    map_wait_release = 1;
    if (++map_step >= GP_TARGETS) map_finish(); else map_ask();
}

/* ---- reading the pad -------------------------------------------------------------------------------- */

static void read_pad(int force)
{
    if (!handle) { if (force) { memset(&st, 0, sizeof st); draw_test(); } return; }
    if (OIN_ReadState(handle, &st, sizeof st) == OIERR_GONE) { show_list(); memset(&st, 0, sizeof st); draw_test(); return; }
    OIN_ReadRaw(handle, &raw, sizeof raw);
    if (force || st.ois_Sequence != last_seq) {
        last_seq = st.ois_Sequence;
        draw_test();
        show_raw();
    }
    map_tick();
}

static void handle_messages(void)
{
    struct OIMessage *m;
    int relist = 0, reread = 0;
    if (!oiport) return;
    while ((m = (struct OIMessage *)GetMsg(oiport))) {
        if (m->oim_Class & (OIMC_ADDED | OIMC_REMOVED | OIMC_MAPPING)) relist = 1;
        if (m->oim_Class & OIMC_STATE) reread = 1;
        ReplyMsg(&m->oim_Message);
    }
    if (relist) show_list();
    if (relist || reread) read_pad(relist);
}

/* ---- putting it in place ---------------------------------------------------------------------------- */

static void keep_mappings(int save)
{
    int i;
    for (i = 0; i < nchanges; i++) {
        struct Change *c = &changes[i];
        if (c->own_now && c->now[0]) set_mapping_clean(c->now, save ? OISMF_SAVE : OISMF_USE);
        else if (!c->own_now && c->own_was) {                 /* back to the built-in one */
            drop_own_mapping(c->guid, MAPS_ENV, "ENV:" OPENINPUT_ENVDIR);
            if (save) drop_own_mapping(c->guid, MAPS_ENVARC, "ENVARC:" OPENINPUT_ENVDIR);
        }
    }
}

static void put_back_mappings(void)
{
    int i;
    for (i = 0; i < nchanges; i++) {
        struct Change *c = &changes[i];
        if (!strcmp(c->was, c->now) && c->own_was == c->own_now) continue;
        if (c->own_was) set_mapping_clean(c->was, OISMF_USE);
        else set_mapping_builtin(c->guid, c->was);
    }
}

/* the library reads the settings once a second: wait until the patch is
 * as set, up to 3 s. 1: it is; 2: AmigaChrome's cores feed that port at
 * the chips (they answer for it first); 0: not yet. */
static int wait_for_patch(int on)
{
    int i, p = gp_fed_port(&cur);
    struct OILegacyPort lp;
    if (!have_lib || p < 0) return 1;
    for (i = 0; i < 15; i++) {
        memset(&lp, 0, sizeof lp);
        if (OIN_GetLegacyPort(p, &lp, sizeof lp) == OIERR_OK) {
            if (lp.olp_Where == OILPW_CORE) return 2;
            if ((lp.olp_Where == OILPW_LOWLEVEL) == (on != 0)) return 1;
        }
        Delay(10);
    }
    return 0;
}

static void put_in_place(int save)
{
    store_settings(&cur, save);
    keep_mappings(save);
}

static void put_back(void)
{
    if (tested) store_settings(&orig, 0);
    put_back_mappings();
}

/* ---- the window -------------------------------------------------------------------------------------- */

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

static void set_port(int port, int as)
{
    gp_port keep = cur.port[gp_fed_port(&cur) < 0 ? 1 : gp_fed_port(&cur)];
    cur.port[0].mode = cur.port[1].mode = GP_MODE_NONE;
    cur.port[port] = keep;
    cur.port[port].mode = as ? GP_MODE_JOYSTICK : GP_MODE_CD32;
    cur.port[port ^ 1].any = 1; memset(cur.port[port ^ 1].guid, 0, 16);
}

/* RETURN_OK or RETURN_FAIL, or 99 when the view changed (open again) */
static int gui_once(void)
{
    APTR vi;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    int fh, top, row, quit = 0, rc = RETURN_OK, W = 560, L = 10, X = 96, lh, i, listw = 270, ix;
    struct Menu *menu = NULL;
    ULONG sigs;
    if (!(scr = LockPubScreen(NULL))) return RETURN_FAIL;
    vi = GetVisualInfoA(scr, NULL);
    dri = GetScreenDrawInfo(scr);
    if (!vi || !dri) { rc = RETURN_FAIL; goto out; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 6;
    list_pads();

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))

    /* the controllers, and the chosen one's details */
    row = top + fh + 4;
    G(LISTVIEW_KIND, G_LIST, L, row, listw, 5 * (fh + 1) + 4, "Game controllers", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&pad_list, GTLV_ShowSelected, 0, GTLV_Selected, sel_row(), GTLV_ReadOnly, FALSE);
    ix = L + listw + 10 + 70;
    G(TEXT_KIND, G_KIND, ix, row, W - ix - 10, fh + 2, "Kind", PLACETEXT_LEFT, GTTX_Text, (ULONG)kind_text);
    G(TEXT_KIND, G_MAKER, ix, row + (fh + 3), W - ix - 10, fh + 2, "Maker", PLACETEXT_LEFT, GTTX_Text, (ULONG)maker_text);
    G(TEXT_KIND, G_FROM, ix, row + 2 * (fh + 3), W - ix - 10, fh + 2, "From", PLACETEXT_LEFT, GTTX_Text, (ULONG)from_text);
    G(TEXT_KIND, G_MAPPED, ix, row + 3 * (fh + 3), W - ix - 10, fh + 2, "Mapped", PLACETEXT_LEFT, GTTX_Text, (ULONG)mapped_text);
    G(TEXT_KIND, G_FEEDS, ix, row + 4 * (fh + 3), W - ix - 10, fh + 2, "Feeds", PLACETEXT_LEFT, GTTX_Text, (ULONG)feeds_text);
    row += 5 * (fh + 1) + 4 + 8;

    /* the test view: drawn by draw_test() */
    row += fh + 2;
    box_x = L; box_y = row; box_w = W - 20; box_h = 3 * (fh + 4) + 3 * fh + 18 + 6;
    row += box_h + 2;
    G(TEXT_KIND, G_OTHER, L, row + 2, W - 20 - 96, fh + 2, NULL, 0, GTTX_Text, (ULONG)other_text);
    G(BUTTON_KIND, G_RUMBLE, W - 10 - 90, row, 90, lh - 2, "Rumble", 0, GA_Disabled, TRUE); row += lh;
    if (advanced) { G(TEXT_KIND, G_RAW, L, row, W - 20, fh + 2, NULL, 0, GTTX_Text, (ULONG)raw_text); row += fh + 4; }
    row += 4;

    /* the mapping */
    G(CYCLE_KIND, G_MAPCHOICE, X, row, 120, lh, "Mapping", PLACETEXT_LEFT, GTCY_Labels, (ULONG)mapchoice_labels);
    G(BUTTON_KIND, G_MAP, X + 126, row, 80, lh, "Map...", 0, GA_Disabled, FALSE);
    G(BUTTON_KIND, G_SKIP, X + 210, row, 60, lh, "Skip", 0, GA_Disabled, TRUE);
    G(TEXT_KIND, G_MAPTEXT, X + 276, row, W - X - 276 - 10, lh, NULL, 0, GTTX_Text, (ULONG)map_summary); row += lh + 2;
    if (advanced) { G(STRING_KIND, G_MAPLINE, X, row, W - X - 10, lh, "Line", PLACETEXT_LEFT, GTST_MaxChars, MAPLEN - 1); row += lh + 2; }
    row += 6;

    /* older games */
    G(CHECKBOX_KIND, G_PATCH, L, row, 26, lh - 2, NULL, 0, GTCB_Scaled, TRUE);
    G(TEXT_KIND, G_PATCHTEXT, L + 30, row, W - L - 40, lh - 2, NULL, 0, GTTX_Text, (ULONG)patch_text); row += lh;
    G(TEXT_KIND, G_PATCHNOTE, L + 30, row, W - L - 40, fh + 2, NULL, 0,
      GTTX_Text, (ULONG)"Patches lowlevel.library's ReadJoyPort, for games that read the port through it. Off by default.");
    row += fh + 6;
    G(CYCLE_KIND, G_PORT, X, row, 170, lh, "Port", PLACETEXT_LEFT, GTCY_Labels, (ULONG)port_labels);
    G(CYCLE_KIND, G_AS, X + 230, row, 110, lh, "As a", PLACETEXT_LEFT, GTCY_Labels, (ULONG)as_labels); row += lh + 2;
    if (advanced) {
        list_which();
        G(CYCLE_KIND, G_WHICH, X, row, W - X - 10, lh, "Which pad", PLACETEXT_LEFT, GTCY_Labels, (ULONG)which_labels); row += lh + 2;
    }
    row += 6;

    G(TEXT_KIND, G_STATUS, L, row, W - 20, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + 6;
    {
        static const char *const names[4] = { "Save", "Use", "Test", "Cancel" };
        static const int ids[4] = { G_SAVE, G_USE, G_TEST, G_CANCEL };
        int bw = (W - 20 - 3 * 10) / 4;
        for (i = 0; i < 4; i++) G(BUTTON_KIND, ids[i], L + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 6;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    menus[6].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Gamepads", WA_ScreenTitle, (ULONG)"OpenPrefs Gamepads 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, 40, WA_Top, scr->BarHeight + 4, WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_INTUITICKS | LISTVIEWIDCMP | BUTTONIDCMP |
                                   CYCLEIDCMP | STRINGIDCMP | CHECKBOXIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    /* the test view's frame and title */
    {
        struct RastPort *rp = win->RPort;
        const char *t = "Test";
        DrawBevelBox(rp, box_x, box_y, box_w, box_h, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, TRUE, TAG_DONE);
        SetAPen(rp, dri->dri_Pens[TEXTPEN]); SetDrMd(rp, JAM1);
        Move(rp, box_x, box_y - 3 - fh + rp->TxBaseline); Text(rp, (STRPTR)t, strlen(t));
    }
    show_info();
    show_patch();
    read_pad(1);
    draw_test();
    show_raw();
    if (map_step >= 0) map_ask(); else status(status_text);

    sigs = 1UL << win->UserPort->mp_SigBit | (oiport ? 1UL << oiport->mp_SigBit : 0);
    while (!quit) {
        struct IntuiMessage *m;
        Wait(sigs);
        handle_messages();
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { put_back(); quit = 1; }
            else if (cls == IDCMP_MENUPICK) {
                UWORD mc = code;
                while (mc != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, mc);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: put_in_place(1); quit = 1; break;
                    case 2: put_in_place(0); quit = 1; break;
                    case 3: put_back(); quit = 1; break;
                    case 4: advanced = (it->Flags & CHECKED) != 0; write_view(advanced); rc = 99; quit = 1; break;
                    }
                    mc = it->NextSelect;
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); draw_test(); }
            else if (cls == IDCMP_INTUITICKS) {
                static int ticks;
                read_pad(0);                                    /* in case a notification was missed */
                if (++ticks >= 10) { ticks = 0; if (!oiport) show_list(); }
            }
            else if ((cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN) && gg) {
                switch (gg->GadgetID) {
                case G_LIST:
                    if (code < npads) { sel = pads[code]; sel_id = sel.oci_ID; sel_gone = 0; open_handle(); }
                    map_step = -1;
                    show_info(); read_pad(1);
                    break;
                case G_RUMBLE:
                    if (handle) status(OIN_Rumble(handle, 0xC000, 0x8000, 400) == OIERR_OK ? "Rumble: did it shake?" : "This pad can't rumble.");
                    break;
                case G_MAPCHOICE:
                    if (code == 0) {                            /* built in */
                        struct Change *c = change_for(sel.oci_GUID);
                        if (c && c->own_was && !c->was[0]) status("The built-in mapping comes back at the next start.");
                        else if (c) {
                            if (c->own_was) status("The built-in mapping comes back at the next start; Save to keep it so.");
                            else { set_mapping_builtin(c->guid, c->was); status("The built-in mapping."); }
                            strcpy(c->now, c->was); c->own_now = 0;
                        }
                    } else if (!own_mapping(sel.oci_GUID)) status("Use Map... to make your own.");
                    show_info();
                    break;
                case G_MAP:
                    if (!handle) break;
                    raw_now(&map_rest);
                    memset(map_in, 0, sizeof map_in);
                    map_step = 0; map_wait_release = 1;
                    show_info(); map_ask();
                    break;
                case G_SKIP:
                    if (map_step >= 0) { map_in[map_step].kind = GP_IN_NONE; map_wait_release = 1; if (++map_step >= GP_TARGETS) map_finish(); else map_ask(); }
                    break;
                case G_MAPLINE: {
                    const char *t = (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer;
                    if (!gp_mapping_ok(t)) status("That isn't a mapping line: GUID,name,a:b0,...");
                    else if (strncasecmp(t, map_line, 32)) status("That line is for another pad (its GUID differs).");
                    else if (use_mapping(t, 1)) status("Your line is in use. Save keeps it.");
                    show_info();
                    break;
                }
                case G_PATCH:
                    cur.patch = (gg->Flags & GFLG_SELECTED) != 0;
                    status(cur.patch ? "On: older games will see the pad. Test tries it now, Save keeps it." : "Off: nothing is patched.");
                    show_patch();
                    break;
                case G_PORT: set_port(code == 0 ? 1 : 0, cur.port[gp_fed_port(&cur) < 0 ? 1 : gp_fed_port(&cur)].mode == GP_MODE_JOYSTICK); show_patch(); break;
                case G_AS: set_port(gp_fed_port(&cur) < 0 ? 1 : gp_fed_port(&cur), code == 1); show_patch(); break;
                case G_WHICH: {
                    gp_port *p = &cur.port[gp_fed_port(&cur) < 0 ? 1 : gp_fed_port(&cur)];
                    if (code < nwhich) { p->any = code == 0; memcpy(p->guid, which_guid[code], 16); }
                    show_patch();
                    break;
                }
                case G_TEST:
                    store_settings(&cur, 0); tested = 1;
                    switch (wait_for_patch(cur.patch)) {
                    case 1: status(cur.patch ? "Testing: older games see the pad now." : "Testing: the patch is out."); break;
                    case 2: status("AmigaChrome feeds this port already, for every game (Cradle's Game pads)."); break;
                    default: status("OpenInput hasn't changed the patch yet (is the library 1.2?)."); break;
                    }
                    show_list();
                    break;
                case G_USE: put_in_place(0); quit = 1; break;
                case G_SAVE: put_in_place(1); quit = 1; break;
                case G_CANCEL: put_back(); quit = 1; break;
                }
            }
        }
    }
out:
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    if (vi) FreeVisualInfo(vi);
    if (dri) { FreeScreenDrawInfo(scr, dri); dri = NULL; }
    UnlockPubScreen(NULL, scr);
    return rc;
}

int main(int argc, char **argv)
{
    LONG args[3] = { 0, 0, 0 };
    /* from Workbench (argc 0) there are no arguments: ReadArgs would read them from
     * libnix's console window, which opens empty and waits */
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"USE/S,SAVE/S,ADVANCED/S", args, NULL) : NULL;
    int rc = RETURN_OK;
    (void)argv;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Gamepads"); return RETURN_FAIL; }
    if ((OpenInputBase = OpenLibrary((STRPTR)OPENINPUT_NAME, OPENINPUT_VERSION)))
        have_lib = OpenInputBase->lib_Version > 1 || OpenInputBase->lib_Revision >= OPENINPUT_REVISION_CORE;
    load_settings(&cur);
    orig = cur;
    if (args[0] || args[1]) {                    /* from the Shell or at boot: no window */
        put_in_place(args[1] != 0);
        if (have_lib && cur.patch && !wait_for_patch(1)) PutStr((STRPTR)"Gamepads: OpenInput didn't put its ReadJoyPort patch in.\n");
    } else {
        advanced = args[2] ? 1 : read_view();
        if (have_lib && (oiport = CreateMsgPort())) {
            struct TagItem t[2] = { { OIT_Events, OIMC_ADDED | OIMC_REMOVED | OIMC_MAPPING }, { TAG_DONE, 0 } };
            notify_all = OIN_AddNotifyA(oiport, t);
        }
        if (!OpenInputBase) strcpy(status_text, "OpenInput isn't installed (LIBS:openinput.library): pads can't be listed.");
        else if (!have_lib) strcpy(status_text, "This openinput.library is the 1.1 skeleton: it lists no pads (1.2 does).");
        else {
            int i, ac = 0;
            list_pads();
            for (i = 0; i < npads; i++) if (pads[i].oci_Source == OISRC_AMIGACHROME) ac = 1;
            strcpy(status_text, ac ? "On AmigaChrome every game already sees the pads (Cradle's Game pads)."
                                   : "Pick a controller to test it.");
        }
        while ((rc = gui_once()) == 99) ;
        close_handle();
        if (notify_all) OIN_RemNotify(notify_all);
        if (oiport) DeleteMsgPort(oiport);
    }
    if (OpenInputBase) CloseLibrary(OpenInputBase);
    if (rd) FreeArgs(rd);
    return rc;
}
