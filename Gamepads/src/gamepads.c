/* OpenPrefs Gamepads 0.2: game controllers, through OpenInput
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
#include "gp_pad.h"
#include "gp_draw.h"

const char version[] __attribute__((used)) = "$VER: Gamepads 0.2 (9.10.2026) OpenPrefs, Dalsin Limited";

struct Library *OpenInputBase;

#define VIEW_ENV "ENV:OpenAmiga/PrefsView"           /* shared by every OpenPrefs editor: simple or advanced */
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"
#define PATCH_ENV "ENV:" OPENINPUT_LOWLEVELPATCH
#define PATCH_ENVARC "ENVARC:" OPENINPUT_LOWLEVELPATCH
#define PORTS_ENV "ENV:" OPENINPUT_PORTS
#define PORTS_ENVARC "ENVARC:" OPENINPUT_PORTS
#define MOUSEPORT_ENV "ENV:OpenInput/MousePort"
#define MOUSEPORT_ENVARC "ENVARC:OpenInput/MousePort"
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
    G_MAPCHOICE, G_MAP, G_SKIP, G_MAPLINE,
    G_PATCH, G_PATCHTEXT, G_PORT, G_AS, G_WHICH, G_PATCHNOTE, G_MOUSESTICK,
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
static char raw_text[120], other_text[100], map_line[MAPLEN];
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
    s->mouse_stick = read_file(MOUSEPORT_ENV, t, sizeof t) > 0 && t[0] == 'j';
}

static void store_settings(const gp_settings *s, int save)
{
    char t[512];
    int n = gp_ports_to_text(s, t, sizeof t);
    /* the ports first: the library reads both within a second, and should find the port before the switch */
    write_file(PORTS_ENV, "ENV:" OPENINPUT_ENVDIR, t, n);
    write_file(PATCH_ENV, "ENV:" OPENINPUT_ENVDIR, s->patch ? "1\n" : "0\n", 2);
    write_file(MOUSEPORT_ENV, "ENV:" OPENINPUT_ENVDIR, s->mouse_stick ? "joystick\n" : "mouse\n", s->mouse_stick ? 9 : 6);
    if (save) {
        write_file(MOUSEPORT_ENVARC, "ENVARC:" OPENINPUT_ENVDIR, s->mouse_stick ? "joystick\n" : "mouse\n", s->mouse_stick ? 9 : 6);
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

/* Skip is in the window only while Map... runs (it is last in the gadget list, so taking it out leaves the rest linked) */
static int skip_shown;
static void show_skip(int on)
{
    struct Gadget *k = gad[G_SKIP];
    if (!win || !k || on == skip_shown) return;
    if (on) { AddGList(win, k, ~0, 1, NULL); RefreshGList(k, win, NULL, 1); }
    else { RemoveGList(win, k, 1); EraseRect(win->RPort, k->LeftEdge, k->TopEdge, k->LeftEdge + k->Width - 1, k->TopEdge + k->Height - 1); }
    skip_shown = on;
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
    SET(G_MAPCHOICE, GTCY_Active, sel_id && own_mapping(sel.oci_GUID) ? 1 : 0, GA_Disabled, !sel_id || map_step >= 0);
    SET(G_MAP, GA_Disabled, !handle || map_step >= 0);
    show_skip(map_step >= 0);
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

/* ---- the test view (gp_draw.c draws it) ------------------------------------------------------ */

static struct gpv_view *view;
static int view_dirty = 1;                            /* draw it all at the next draw_test */
static LONG dead_zone = -1;                           /* ENV:OpenInput/DeadZone, read once */

static const char *label_of(ULONG b)
{
    STRPTR s = have_lib ? OIN_ButtonLabel(sel_gone ? 0 : sel_id, b) : NULL;
    return s ? (const char *)s : "?";
}

static int pad_kind(void)
{
    if (sel.oci_Type == OITYPE_CD32PAD || sel.oci_Family == OIFAM_CD32) return GP_PAD_CD32;
    if (sel.oci_Type == OITYPE_JOYSTICK || sel.oci_Family == OIFAM_AMIGA) return GP_PAD_JOYSTICK;
    return GP_PAD_MODERN;
}

/* The dead zone the sticks are drawn with: OpenInput's (ENV:OpenInput/DeadZone),
 * or a tenth of the travel, the rest zone a game usually takes, when none is set. */
static int stick_dead_zone(void)
{
    if (dead_zone < 0) {
        char t[16];
        LONG v = 0, i;
        if (read_file("ENV:OpenInput/DeadZone", t, sizeof t) > 0)
            for (i = 0; t[i] >= '0' && t[i] <= '9'; i++) v = v * 10 + (t[i] - '0');
        dead_zone = v > 0 ? (v > 32767 ? 32767 : v) : 3277;
    }
    return (int)dead_zone;
}

static void draw_test(int force)
{
    gpv_state s;
    int i, fam = sel.oci_Family, x0 = box_x + 2, y0 = box_y + 1, w = box_w - 4, h = box_h - 2;
    struct RastPort *rp;
    if (!win || !box_w) return;
    rp = win->RPort;
    if (!sel_id || !view) {
        const char *t = have_lib ? "No game controller is connected." : "OpenInput (LIBS:openinput.library 1.2) is not installed.";
        SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]); RectFill(rp, x0, y0, x0 + w - 1, y0 + h - 1);
        SetAPen(rp, dri->dri_Pens[TEXTPEN]); SetDrMd(rp, JAM1);
        Move(rp, x0 + (w - TextLength(rp, (STRPTR)t, strlen(t))) / 2, y0 + h / 2); Text(rp, (STRPTR)t, strlen(t));
        view_dirty = 1;
        return;
    }
    memset(&s, 0, sizeof s);
    s.kind = pad_kind();
    s.family = fam;
    s.connected = !sel_gone;
    s.buttons = st.ois_Buttons;
    for (i = 0; i < 6; i++) s.axes[i] = st.ois_Axes[i];
    s.deadzone = stick_dead_zone();
    for (i = 0; i < 4; i++) s.face[i] = label_of(OIB_A + i);
    if (s.kind == GP_PAD_JOYSTICK) { s.face[0] = "1"; s.face[1] = "2"; }
    if (s.kind == GP_PAD_CD32) {
        s.shoulder[0] = label_of(OIB_LEFTSHOULDER); s.shoulder[1] = label_of(OIB_RIGHTSHOULDER);
        s.pill[0] = label_of(OIB_START);
    } else if (fam == OIFAM_PLAYSTATION) {
        s.shoulder[0] = "L1"; s.shoulder[1] = "R1"; s.trigger[0] = "L2"; s.trigger[1] = "R2"; s.pill[0] = "Share"; s.pill[1] = "Options";
    } else if (fam == OIFAM_SWITCH || fam == OIFAM_8BITDO) {
        s.shoulder[0] = "L"; s.shoulder[1] = "R"; s.trigger[0] = "ZL"; s.trigger[1] = "ZR"; s.pill[0] = "-"; s.pill[1] = "+";
    } else {
        s.shoulder[0] = "LB"; s.shoulder[1] = "RB"; s.trigger[0] = "LT"; s.trigger[1] = "RT";
        s.pill[0] = fam == OIFAM_XBOX ? "View" : "Back"; s.pill[1] = fam == OIFAM_XBOX ? "Menu" : "Start";
    }
    gpv_draw(view, rp, x0, y0, w, h, &s, force || view_dirty);
    view_dirty = 0;
    /* what is pressed, in words: every button by the pad's own name */
    other_text[0] = 0;
    if (sel_gone) strcpy(other_text, "Not connected: it shows again when it comes back.");
    else {
        for (i = 0; i < OIB_COUNT; i++)
            if (st.ois_Buttons & OIBF(i)) {
                const char *l = label_of(i);
                if (strlen(other_text) + strlen(l) + 12 < sizeof other_text) { strcat(other_text, other_text[0] ? ", " : "Pressed: "); strcat(other_text, l); }
            }
        if (!other_text[0] && map_step < 0) strcpy(other_text, "Press the buttons and move the sticks: they light up above.");
    }
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
    if (!handle) { if (force) { memset(&st, 0, sizeof st); draw_test(0); } return; }
    if (OIN_ReadState(handle, &st, sizeof st) == OIERR_GONE) { show_list(); memset(&st, 0, sizeof st); draw_test(0); return; }
    OIN_ReadRaw(handle, &raw, sizeof raw);
    if (force || st.ois_Sequence != last_seq) {
        last_seq = st.ois_Sequence;
        draw_test(0);
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
    int fh, top, row, quit = 0, rc = RETURN_OK, W = 560, L = 10, X = 96, lh, i, listw = 270, ix, pass, boxh, skip_x = 0, skip_y = 0;
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
    boxh = 12 * fh + 70;
    list_pads();

    /* twice, if need be: the test view as big as the font asks, but the window no taller than the screen */
    for (pass = 0; pass < 2; pass++) {
    glist = NULL;
    memset(gad, 0, sizeof gad);
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
    box_x = L; box_y = row; box_w = W - 20; box_h = boxh;
    row += box_h + 2;
    G(TEXT_KIND, G_OTHER, L, row + 2, W - 20 - 96, fh + 2, NULL, 0, GTTX_Text, (ULONG)other_text);
    G(BUTTON_KIND, G_RUMBLE, W - 10 - 90, row, 90, lh - 2, "Rumble", 0, GA_Disabled, TRUE); row += lh;
    if (advanced) { G(TEXT_KIND, G_RAW, L, row, W - 20, fh + 2, NULL, 0, GTTX_Text, (ULONG)raw_text); row += fh + 4; }
    row += 4;

    /* the mapping */
    G(CYCLE_KIND, G_MAPCHOICE, X, row, 120, lh, "Mapping", PLACETEXT_LEFT, GTCY_Labels, (ULONG)mapchoice_labels);
    G(BUTTON_KIND, G_MAP, X + 126, row, 80, lh, "Map...", 0, GA_Disabled, FALSE);
    skip_x = X + 210; skip_y = row;
    row += lh + 2;
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
        G(CHECKBOX_KIND, G_MOUSESTICK, L, row + 4, 26, lh - 2, "A joystick is in the mouse port (1), not the mouse", PLACETEXT_RIGHT,
          GTCB_Scaled, TRUE, GTCB_Checked, cur.mouse_stick); row += lh + 4;
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
    /* Skip, last in the list: shown only while Map... runs (show_skip) */
    G(BUTTON_KIND, G_SKIP, skip_x, skip_y, 60, lh, "Skip", 0, GA_Disabled, FALSE);
    if (!g || pass) break;
    {   int room = scr->Height - (scr->BarHeight + 4);              /* the window's top edge is there */
        int need = row + scr->WBorBottom;                           /* the window's whole height */
        if (need <= room || boxh <= 96) break;
        boxh -= need - room;
        if (boxh < 96) boxh = 96;
        FreeGadgets(glist);
    }
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    menus[6].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Gamepads", WA_ScreenTitle, (ULONG)"OpenPrefs Gamepads 0.2", WA_PubScreen, (ULONG)scr,
                         WA_Left, 40, WA_Top, scr->BarHeight + 4, WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_INTUITICKS | LISTVIEWIDCMP | BUTTONIDCMP |
                                   CYCLEIDCMP | STRINGIDCMP | CHECKBOXIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    skip_shown = 1;
    show_skip(map_step >= 0);
    if (!(view = gpv_open(scr, dri))) { rc = RETURN_FAIL; goto out; }
    view_dirty = 1;
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
    draw_test(1);
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
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); draw_test(1); }
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
                case G_MOUSESTICK:
                    cur.mouse_stick = (gg->Flags & GFLG_SELECTED) != 0;
                    status(cur.mouse_stick ? "The mouse port is listed as a joystick (Test or Use)." : "The mouse port is the mouse.");
                    break;
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
    if (!skip_shown && gad[G_SKIP]) FreeGadgets(gad[G_SKIP]);   /* out of the list: freed on its own */
    skip_shown = 1;
    FreeGadgets(glist);
    gpv_close(view); view = NULL;
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
            strcpy(status_text, ac ? "On AmigaChrome, Cradle's Game pads can give every game the pads."
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
