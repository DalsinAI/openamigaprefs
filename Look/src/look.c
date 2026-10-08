/* OpenPrefs Look 0.1: the editor for OpenLook (the OpenGadTools look on
 * Workbench and every GadTools program). A GadTools prefs editor, as the
 * OS's own: Save, Use, Test (put back after 15 seconds unless kept) and
 * Cancel. What it sets lives where OpenLook already keeps it,
 * ENV:OpenGadTools/Look (ENVARC: when saved); OpenLook draws.
 *
 *   - the theme, from SYS:Prefs/Presets/Themes, with a preview
 *   - light, dark, or automatic by the clock
 *   - an accent colour of the user's over the theme's
 *   - Lite: shadows, rounding and gradients left out; automatic on a real
 *     68040 or 68060 without OpenGPU
 *   - which screens are themed (Workbench, other public screens, game
 *     screens) and programs never touched
 *   - icon labels (Font prefs' Workbench icon text: plain, fields, shadow, outline)
 *   - desktop profiles (SYS:Prefs/Presets/OpenPrefs/<name>.profile), and
 *     "choose a profile by machine at start"
 *   - focus: ClickToFront and AutoPoint, on at start or not, from one place
 *
 * Holding Shift while OpenLook starts gives the OS's own look (safe start).
 *
 *   Look [FROM file] [USE] [SAVE]
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/dosasl.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <prefs/prefhdr.h>
#include <prefs/font.h>
#include <libraries/iffparse.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/iffparse.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "ogt_theme.h"

const char version[] __attribute__((used)) = "$VER: Look 0.2 (5.10.2026) OpenPrefs, Dalsin Limited";

#define PREFS_ENV "ENV:OpenGadTools/Look"
#define PREFS_ENVARC "ENVARC:OpenGadTools/Look"
#define THEME_DIR "SYS:Prefs/Presets/Themes"
#define PROFILE_DIR "SYS:Prefs/Presets/OpenPrefs"
#define FONT_ENV "ENV:Sys/font.prefs"
#define FONT_ENVARC "ENVARC:Sys/font.prefs"
#define CTF_TOOL "SYS:Tools/Commodities/ClickToFront"
#define AP_TOOL "SYS:Tools/Commodities/AutoPoint"
#define WBSTARTUP "SYS:WBStartup/"
#define TEST_SECONDS 15
#define VIEW_ENV "ENV:OpenAmiga/PrefsView"           /* shared by every OpenPrefs editor: simple or advanced */
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"

/* ---- the settings --------------------------------------------------------------------- */

#define MAX_THEMES 32
#define MAX_PROFILES 16

struct look {
    char theme[48];
    int mode;                     /* 0 light, 1 dark, 2 automatic */
    int dark_from, dark_to;
    int own_accent;
    char accent[16];
    int lite;                     /* 0 automatic, 1 on, 2 off */
    int lite_shadows, lite_rounding, lite_gradients;   /* left out when Lite is on */
    int wb_themed, pub_themed, custom_themed;
    char never[160];              /* program names, comma between */
    int profile_auto;
    char desktop[112];            /* kept as read: the editor has no gadget for it yet */
};

static struct look cur, orig;
static char theme_names[MAX_THEMES][48];
static int nthemes;
static char profile_names[MAX_PROFILES][48];
static int nprofiles;

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
    if ((slash = strrchr(dir, '/'))) {
        *slash = 0;
        if ((lock = Lock((STRPTR)dir, ACCESS_READ))) UnLock(lock);
        else if ((lock = CreateDir((STRPTR)dir))) UnLock(lock);
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    Write(fh, (APTR)data, len);
    Close(fh);
    return 1;
}

static char *word(char **p, char *out, int size)
{
    char *s = *p;
    int n = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\n' || *s == '\r') { out[0] = 0; *p = s; return NULL; }
    if (*s == '"') { s++; while (*s && *s != '"' && *s != '\n' && n < size - 1) out[n++] = *s++; if (*s == '"') s++; }
    else while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && n < size - 1) out[n++] = *s++;
    out[n] = 0;
    *p = s;
    return out;
}

static void defaults(struct look *l)
{
    memset(l, 0, sizeof *l);
    strcpy(l->theme, "Open");
    l->dark_from = 19; l->dark_to = 7;
    strcpy(l->accent, "#365fa3");
    l->lite_shadows = l->lite_rounding = l->lite_gradients = 1;
    l->wb_themed = l->pub_themed = 1;
}

/* The prefs text (OpenLook 0.4's format) into the settings. */
static void parse_look(struct look *l, const char *text)
{
    char *t, *line, *p, w[112], v[112];
    defaults(l);
    if (!text || !(t = AllocVec(strlen(text) + 1, MEMF_ANY))) return;
    strcpy(t, text);
    for (line = t; *line; ) {
        char *next = strchr(line, '\n');
        p = line;
        if (line == t && word(&p, w, sizeof w) && w[0] != ';' && strcmp(w, "target") && strcmp(w, "name")) {
            strncpy(l->theme, w, sizeof l->theme - 1);
            if (word(&p, v, sizeof v)) l->mode = !strcmp(v, "dark") ? 1 : !strcmp(v, "auto") ? 2 : 0;
        } else if (line != t || p != line) {
            p = line;
            if (word(&p, w, sizeof w) && w[0] != ';') {
                if (!strcmp(w, "mode") && word(&p, v, sizeof v)) {
                    if (!strcmp(v, "auto")) l->mode = 2;
                    if (word(&p, v, sizeof v)) l->dark_from = atoi(v) % 24;
                    if (word(&p, v, sizeof v)) l->dark_to = atoi(v) % 24;
                } else if (!strcmp(w, "theme") && word(&p, v, sizeof v)) strncpy(l->theme, v, sizeof l->theme - 1);
                else if (!strcmp(w, "accent") && word(&p, v, sizeof v)) { l->own_accent = 1; strncpy(l->accent, v, sizeof l->accent - 1); }
                else if (!strcmp(w, "desktop")) { while (*p == ' ') p++; strncpy(l->desktop, p, sizeof l->desktop - 1); { char *e = strchr(l->desktop, '\n'); if (e) *e = 0; } }
                else if (!strcmp(w, "lite") && word(&p, v, sizeof v)) l->lite = !strcmp(v, "on") ? 1 : !strcmp(v, "off") ? 2 : 0;
                else if (!strncmp(w, "lite.", 5) && word(&p, v, sizeof v)) {
                    int on = !strcmp(v, "on");
                    if (!strcmp(w + 5, "shadows")) l->lite_shadows = on;
                    else if (!strcmp(w + 5, "rounding")) l->lite_rounding = on;
                    else if (!strcmp(w + 5, "gradients")) l->lite_gradients = on;
                } else if (!strcmp(w, "screen") && word(&p, v, sizeof v)) {
                    char r[16];
                    int themed = word(&p, r, sizeof r) && !strcmp(r, "themed");
                    if (!strcmp(v, "workbench")) l->wb_themed = themed;
                    else if (!strcmp(v, "public")) l->pub_themed = themed;
                    else if (!strcmp(v, "custom")) l->custom_themed = themed;
                } else if (!strcmp(w, "never") && word(&p, v, sizeof v)) {
                    if (l->never[0]) strncat(l->never, ", ", sizeof l->never - strlen(l->never) - 1);
                    strncat(l->never, v, sizeof l->never - strlen(l->never) - 1);
                } else if (!strcmp(w, "profile") && word(&p, v, sizeof v)) l->profile_auto = !strcmp(v, "auto");
            }
        }
        if (!next) break;
        line = next + 1;
    }
    FreeVec(t);
}

static void make_look_text(const struct look *l, char *out, int size)
{
    static const char *modes[3] = { "light", "dark", "auto" };
    char *o = out;
    int n;
    n = snprintf(o, size, "%s %s\n; OpenPrefs Look 0.1 wrote the lines below; OpenLook 0.4 reads them\n", l->theme, modes[l->mode]);
    o += n; size -= n;
    if (l->mode == 2) { n = snprintf(o, size, "mode auto %d %d\n", l->dark_from, l->dark_to); o += n; size -= n; }
    if (l->own_accent) { n = snprintf(o, size, "accent %s\n", l->accent); o += n; size -= n; }
    if (l->desktop[0]) { n = snprintf(o, size, "desktop %s\n", l->desktop); o += n; size -= n; }
    n = snprintf(o, size, "lite %s\nlite.shadows %s\nlite.rounding %s\nlite.gradients %s\n",
                 l->lite == 1 ? "on" : l->lite == 2 ? "off" : "auto",
                 l->lite_shadows ? "on" : "off", l->lite_rounding ? "on" : "off", l->lite_gradients ? "on" : "off");
    o += n; size -= n;
    n = snprintf(o, size, "screen workbench %s\nscreen public %s\nscreen custom %s\n",
                 l->wb_themed ? "themed" : "classic", l->pub_themed ? "themed" : "classic", l->custom_themed ? "themed" : "classic");
    o += n; size -= n;
    {
        const char *s = l->never;
        while (*s && size > 40) {
            char name[40];
            int k = 0;
            while (*s == ' ' || *s == ',') s++;
            while (*s && *s != ',' && k < 39) name[k++] = *s++;
            while (k && name[k - 1] == ' ') k--;
            name[k] = 0;
            if (k) { n = snprintf(o, size, "never \"%s\"\n", name); o += n; size -= n; }
        }
    }
    if (l->profile_auto) snprintf(o, size, "profile auto\n");
}

/* ---- OpenLook: telling it ---------------------------------------------------------------- */

static int tell_openlook(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)"OpenLook"))) Signal(p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
    return p != NULL;
}

static void write_look(const struct look *l, int save)
{
    static char text[2048];
    make_look_text(l, text, sizeof text);
    write_file(PREFS_ENV, text, strlen(text));
    if (save) write_file(PREFS_ENVARC, text, strlen(text));
}

/* ---- icon labels: Font prefs' Workbench icon text -------------------------------------- */

/* 0 plain, 1 fields (a box behind the label), 2 shadow, 3 outline */
static int label_style(const UBYTE *chunk)
{
    const struct FontPrefs *fp = (const struct FontPrefs *)chunk;
    if (fp->fp_SpecialDrawMode == 1) return 2;
    if (fp->fp_SpecialDrawMode == 2) return 3;
    return fp->fp_DrawMode == JAM2 ? 1 : 0;
}

/* The FONT chunk of type FP_WBFONT in a font.prefs file, or NULL. */
static UBYTE *wbfont_chunk(UBYTE *data, LONG len)
{
    LONG i = 12;
    while (i + 8 <= len) {
        ULONG id = *(ULONG *)(data + i), n = *(ULONG *)(data + i + 4);
        if (id == ID_FONT && n >= sizeof(struct FontPrefs) && ((struct FontPrefs *)(data + i + 8))->fp_Type == FP_WBFONT)
            return data + i + 8;
        i += 8 + ((n + 1) & ~1UL);
    }
    return NULL;
}

static int read_label_style(void)
{
    LONG len = 0;
    UBYTE *d = (UBYTE *)read_file(FONT_ENV, &len), *c;
    int s = 0;
    if (d && (c = wbfont_chunk(d, len))) s = label_style(c);
    if (d) FreeVec(d);
    return s;
}

static void write_label_style(int style, int save)
{
    const char *paths[2] = { FONT_ENV, FONT_ENVARC };
    for (int k = 0; k < (save ? 2 : 1); k++) {
        LONG len = 0;
        UBYTE *d = (UBYTE *)read_file(paths[k], &len), *c;
        if (d && (c = wbfont_chunk(d, len))) {
            struct FontPrefs *fp = (struct FontPrefs *)c;
            fp->fp_DrawMode = style == 1 ? JAM2 : JAM1;
            fp->fp_SpecialDrawMode = style == 2 ? 1 : style == 3 ? 2 : 0;
            write_file(paths[k], d, len);
        }
        if (d) FreeVec(d);
    }
}

/* ---- focus: ClickToFront and AutoPoint ----------------------------------------------------- */

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

static const char *base_name(const char *path) { const char *s = strrchr(path, '/'); return s ? s + 1 : strchr(path, ':') ? strchr(path, ':') + 1 : path; }

static int at_start(const char *tool)
{
    char p[96];
    snprintf(p, sizeof p, WBSTARTUP "%s", base_name(tool));
    return exists(p);
}

static int running(const char *tool)
{
    struct Task *t;
    Forbid();
    t = FindTask((STRPTR)base_name(tool));
    Permit();
    return t != NULL;
}

/* A command with NIL: for its input and output: from Workbench, Input() is libnix's
 * console window, which a command reading or writing it would open. */
static void run_quiet(const char *cmd)
{
    BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (in && out) SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, TAG_DONE);
    if (in) Close(in);                     /* a command that isn't asynchronous leaves them to us */
    if (out) Close(out);
}

/* On at start: the tool and its icon in WBStartup; and running now, or stopped. */
static void set_focus_tool(const char *tool, int on, const char *qualifier)
{
    char dst[96], cmd[200];
    snprintf(dst, sizeof dst, WBSTARTUP "%s", base_name(tool));
    if (on && !at_start(tool) && exists(tool)) {
        snprintf(cmd, sizeof cmd, "Copy \"%s\" \"%s\" CLONE QUIET >NIL:", tool, dst);
        run_quiet(cmd);
        snprintf(cmd, sizeof cmd, "Copy \"%s.info\" \"%s.info\" CLONE QUIET >NIL:", tool, dst);
        run_quiet(cmd);
    }
    if (!on && at_start(tool)) {
        DeleteFile((STRPTR)dst);
        snprintf(cmd, sizeof cmd, "%s.info", dst);
        DeleteFile((STRPTR)cmd);
    }
    if (qualifier && on) {                    /* ClickToFront's QUALIFIER tooltype, in the WBStartup copy */
        struct DiskObject *dob = GetDiskObject((STRPTR)dst);
        if (dob) {
            static char q[40];
            static char *tt[8];
            int n = 0;
            char **old = (char **)dob->do_ToolTypes;
            snprintf(q, sizeof q, "QUALIFIER=%s", qualifier);
            tt[n++] = q;
            for (int i = 0; old && old[i] && n < 7; i++) if (strncmp(old[i], "QUALIFIER=", 10)) tt[n++] = old[i];
            tt[n] = NULL;
            dob->do_ToolTypes = (STRPTR *)tt;
            PutDiskObject((STRPTR)dst, dob);
            dob->do_ToolTypes = (STRPTR *)old;
            FreeDiskObject(dob);
        }
    }
    if (on && !running(tool) && exists(tool)) {
        snprintf(cmd, sizeof cmd, "Run >NIL: \"%s\"%s%s", on && at_start(tool) ? dst : tool,
                 qualifier ? " QUALIFIER=" : "", qualifier ? qualifier : "");
        run_quiet(cmd);
    }
    if (!on && running(tool)) {
        struct Task *t;
        Forbid();
        if ((t = FindTask((STRPTR)base_name(tool)))) Signal(t, SIGBREAKF_CTRL_C);
        Permit();
    }
}

/* ---- the lists -------------------------------------------------------------------------------- */

static void list_dir(const char *dir, const char *ext, char names[][48], int max, int *count)
{
    BPTR lock = Lock((STRPTR)dir, ACCESS_READ);
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    int n = 0;
    size_t el = strlen(ext);
    if (lock && fib && Examine(lock, fib))
        while (ExNext(lock, fib) && n < max) {
            size_t l = strlen((char *)fib->fib_FileName);
            if (fib->fib_DirEntryType < 0 && l > el && !strcmp((char *)fib->fib_FileName + l - el, ext) && l - el < 48) {
                memcpy(names[n], fib->fib_FileName, l - el);
                names[n][l - el] = 0;
                n++;
            }
        }
    if (fib) FreeDosObject(DOS_FIB, fib);
    if (lock) UnLock(lock);
    /* sorted, for the list */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[48];
            strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t);
        }
    *count = n;
}

/* ---- the preview ---------------------------------------------------------------------------------- */

static ogt_theme preview_theme;
static int preview_loaded;

static void load_preview(const struct look *l)
{
    char path[160], err[120];
    char *text;
    if (preview_loaded) { ogt_theme_free(&preview_theme); preview_loaded = 0; }
    snprintf(path, sizeof path, THEME_DIR "/%s.theme", l->theme);
    if (!(text = read_file(path, NULL))) return;
    if (ogt_theme_parse(&preview_theme, text, err, sizeof err)) {
        preview_loaded = 1;
        if (l->lite == 1) ogt_theme_set_lite(&preview_theme, (l->lite_shadows ? OGT_LITE_SHADOWS : 0) |
                                             (l->lite_rounding ? OGT_LITE_ROUNDING : 0) | (l->lite_gradients ? OGT_LITE_GRADIENTS : 0));
        if (l->own_accent) {
            ogt_rgb a;
            if (ogt_parse_colour(l->accent, &a)) ogt_theme_set_accent(&preview_theme, a);
        }
        if (l->desktop[0]) ogt_theme_override(&preview_theme, "desktop", l->desktop);
    }
    FreeVec(text);
}

static LONG pens[64];
static int npens;

static void free_pens(struct Screen *scr)
{
    for (int i = 0; i < npens; i++) ReleasePen(scr->ViewPort.ColorMap, pens[i]);
    npens = 0;
}

static void fill(struct RastPort *rp, struct Screen *scr, ogt_rgb c, int x, int y, int w, int h)
{
    LONG pen;
    if (w <= 0 || h <= 0) return;
    pen = ObtainBestPen(scr->ViewPort.ColorMap, (ULONG)c.r * 0x01010101UL, (ULONG)c.g * 0x01010101UL, (ULONG)c.b * 0x01010101UL,
                        OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    if (pen < 0) return;
    if (npens < 64) pens[npens++] = pen; else ReleasePen(scr->ViewPort.ColorMap, pen);
    SetAPen(rp, (UBYTE)pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

static int key_colour(const char *key, int mode, ogt_rgb *c)
{
    return preview_loaded && ogt_theme_colour(&preview_theme, mode, key, c);
}

/* A gradient key, a band a row at a time (or in steps on a palette screen). */
static void band(struct RastPort *rp, struct Screen *scr, const char *key, int mode, int x, int y, int w, int h, ogt_rgb def)
{
    ogt_grad g;
    if (!preview_loaded || !ogt_theme_grad(&preview_theme, mode, key, &g)) { fill(rp, scr, def, x, y, w, h); return; }
    for (int i = 0; i < h; i += 2) fill(rp, scr, ogt_grad_at(&g, h > 1 ? i * 100 / (h - 1) : 0), x, y + i, w, i + 2 <= h ? 2 : 1);
}

static void draw_preview(struct Window *win, int x, int y, int w, int h, const struct look *l)
{
    struct RastPort *rp = win->RPort;
    struct Screen *scr = win->WScreen;
    int mode = l->mode == 1 ? OGT_DARK : OGT_LIGHT, th = 12;
    ogt_rgb c, white = { 255, 255, 255 }, grey = { 160, 160, 160 }, black = { 0, 0, 0 };
    free_pens(scr);
    band(rp, scr, "desktop", mode, x, y, w, h, grey);
    if (!preview_loaded) return;
    if (preview_theme.passthrough) {
        SetAPen(rp, 1);
        Move(rp, x + 8, y + h / 2);
        Text(rp, (STRPTR)"The OS's own look", 17);
        return;
    }
    /* an inactive window behind, an active one in front */
    for (int k = 0; k < 2; k++) {
        int wx = x + 10 + k * 26, wy = y + 8 + k * 18, ww = w - 50, wh = h - 34;
        const char *tk = k ? "title.active" : "title.inactive";
        fill(rp, scr, key_colour(k ? "frame.active" : "frame.inactive", mode, &c) ? c : black, wx, wy, ww, wh);
        band(rp, scr, tk, mode, wx + 1, wy + 1, ww - 2, th, grey);
        fill(rp, scr, key_colour("window", mode, &c) ? c : white, wx + 1, wy + 1 + th, ww - 2, wh - th - 2);
        if (key_colour(k ? "title.active.text" : "title.inactive.text", mode, &c)) {
            fill(rp, scr, c, wx + 6, wy + 5, 40, 3);         /* the title, as a stroke */
        }
        if (k) {
            /* a button, a string, an accent mark */
            band(rp, scr, "button", mode, wx + 8, wy + th + 10, 60, 14, white);
            if (key_colour("button.border", mode, &c)) {
                fill(rp, scr, c, wx + 8, wy + th + 10, 60, 1); fill(rp, scr, c, wx + 8, wy + th + 23, 60, 1);
                fill(rp, scr, c, wx + 8, wy + th + 10, 1, 14); fill(rp, scr, c, wx + 67, wy + th + 10, 1, 14);
            }
            fill(rp, scr, key_colour("string", mode, &c) ? c : white, wx + 76, wy + th + 10, ww - 90, 14);
            if (key_colour("accent", mode, &c)) fill(rp, scr, c, wx + 8, wy + th + 32, 14, 14);
            band(rp, scr, "fill", mode, wx + 30, wy + th + 34, ww - 44, 10, grey);
        }
    }
}

/* ---- the window ---------------------------------------------------------------------------------- */

enum {
    G_THEMES, G_MODE, G_FROM, G_TO, G_OWNACCENT, G_ACCENT, G_LITE, G_LSHADOW, G_LROUND, G_LGRAD,
    G_WB, G_PUB, G_CUSTOM, G_NEVER, G_LABELS, G_PROFILE, G_APPLYPROFILE, G_PROFILEAUTO,
    G_CTF, G_CTFQUAL, G_AP, G_STATUS, G_SAVE, G_USE, G_TEST, G_CANCEL, G_COUNT
};

static const char *mode_labels[] = { "Light", "Dark", "By the clock", NULL };
static const char *lite_labels[] = { "Automatic", "On", "Off", NULL };
static const char *themed_labels[] = { "Themed", "Classic", NULL };
static const char *label_labels[] = { "Plain", "Fields", "Shadow", "Outline", NULL };
static const char *qual_labels[] = { "Any click", "Left Alt", "Left Amiga", "Ctrl", NULL };
static const char *qual_values[] = { "NONE", "LALT", "LCOMMAND", "CONTROL" };
static STRPTR profile_labels[MAX_PROFILES + 2];
static struct List theme_list;
static struct Node theme_nodes[MAX_THEMES];
static char status_text[120];

static void build_theme_list(void)
{
    theme_list.lh_Head = (struct Node *)&theme_list.lh_Tail; theme_list.lh_Tail = NULL; theme_list.lh_TailPred = (struct Node *)&theme_list.lh_Head;
    for (int i = 0; i < nthemes; i++) {
        theme_nodes[i].ln_Name = theme_names[i];
        AddTail(&theme_list, &theme_nodes[i]);
    }
}

static int theme_index(const char *name)
{
    for (int i = 0; i < nthemes; i++) if (!strcmp(theme_names[i], name)) return i;
    return -1;
}

static struct Gadget *gad[G_COUNT];
static struct Window *win;
static int px, py, pw, ph;               /* the preview's box */
static int label_now, label_orig;
static int ctf_on, ctf_qual, ap_on, ctf_orig, ap_orig;

#define SET(id, ...) do { if (gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE); } while (0)
static int advanced;                     /* the view: Simple shows theme, mode and profiles; Advanced every setting */

static int read_view(void)
{
    char *t = read_file(VIEW_ENV, NULL);
    int a = t && !strncmp(t, "advanced", 8);
    if (t) FreeVec(t);
    return a;
}

static void write_view(int a)
{
    write_file(VIEW_ENV, a ? "advanced\n" : "simple\n", a ? 9 : 7);
    write_file(VIEW_ENVARC, a ? "advanced\n" : "simple\n", a ? 9 : 7);
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

static void show(const struct look *l)
{
    SET(G_THEMES, GTLV_Selected, theme_index(l->theme), GTLV_MakeVisible, theme_index(l->theme) < 0 ? 0 : theme_index(l->theme));
    SET(G_MODE, GTCY_Active, l->mode);
    SET(G_FROM, GTIN_Number, l->dark_from, GA_Disabled, l->mode != 2);
    SET(G_TO, GTIN_Number, l->dark_to, GA_Disabled, l->mode != 2);
    SET(G_OWNACCENT, GTCB_Checked, l->own_accent);
    SET(G_ACCENT, GTST_String, (ULONG)l->accent, GA_Disabled, !l->own_accent);
    SET(G_LITE, GTCY_Active, l->lite);
    SET(G_LSHADOW, GTCB_Checked, l->lite_shadows, GA_Disabled, l->lite != 1);
    SET(G_LROUND, GTCB_Checked, l->lite_rounding, GA_Disabled, l->lite != 1);
    SET(G_LGRAD, GTCB_Checked, l->lite_gradients, GA_Disabled, l->lite != 1);
    SET(G_WB, GTCY_Active, !l->wb_themed);
    SET(G_PUB, GTCY_Active, !l->pub_themed);
    SET(G_CUSTOM, GTCY_Active, !l->custom_themed);
    SET(G_NEVER, GTST_String, (ULONG)l->never);
    SET(G_PROFILEAUTO, GTCB_Checked, l->profile_auto);
    load_preview(l);
    draw_preview(win, px, py, pw, ph, l);
}

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    SET(G_STATUS, GTTX_Text, (ULONG)status_text);
}

/* the profile's settings into the editor (not yet used) */
static void apply_profile(int i)
{
    char path[160];
    char *text;
    snprintf(path, sizeof path, PROFILE_DIR "/%s.profile", profile_names[i]);
    if (!(text = read_file(path, NULL))) { status("That profile can't be read."); return; }
    {
        int keep_auto = cur.profile_auto;
        parse_look(&cur, text);
        cur.profile_auto = keep_auto;
    }
    FreeVec(text);
    show(&cur);
    snprintf(status_text, sizeof status_text, "%s: Use, Test or Save to put it in place.", profile_names[i]);
    status(status_text);
}

static ULONG test_until;                 /* seconds since 1978 when a Test goes back, or 0 */

static ULONG now_seconds(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (ULONG)ds.ds_Days * 86400 + (ULONG)ds.ds_Minute * 60 + (ULONG)ds.ds_Tick / TICKS_PER_SECOND;
}

static void put_in_place(int save)
{
    write_look(&cur, save);
    write_label_style(label_now, save);
    tell_openlook();
}

static void go_back(void)
{
    write_look(&orig, 0);
    write_label_style(label_orig, 0);
    tell_openlook();
}

static void focus_now(void)
{
    if (ctf_on != ctf_orig || ctf_on) set_focus_tool(CTF_TOOL, ctf_on, qual_values[ctf_qual]);
    if (ap_on != ap_orig) set_focus_tool(AP_TOOL, ap_on, NULL);
    ctf_orig = ctf_on; ap_orig = ap_on;
}

/* RETURN_OK or RETURN_FAIL, or 99 when the view changed (open again) */
static int gui_once(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    int fh, top, row, quit = 0, rc = RETURN_OK, W = 620, L = 10, R = 300, i, lh;
    struct MsgPort *tport = NULL;
    struct timerequest *tr = NULL;
    int timer = 0;
    struct Menu *menu = NULL;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 6;
    build_theme_list();
    for (i = 0; i < nprofiles; i++) profile_labels[i] = (STRPTR)profile_names[i];
    if (!nprofiles) profile_labels[nprofiles++] = (STRPTR)"(no profiles)";
    profile_labels[nprofiles] = NULL;
    strcpy(status_text, cur.profile_auto ? "By machine is on: OpenLook uses the profile that fits this machine."
                                         : "Choose a theme. Test shows it for 15 seconds.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))

    /* the left column: themes and the preview */
    row = top + fh + 4;
    G(LISTVIEW_KIND, G_THEMES, L, row, 270, 6 * (fh + 1) + 4, "Theme", PLACETEXT_ABOVE, GTLV_Labels, (ULONG)&theme_list, GTLV_ShowSelected, NULL);
    px = L; py = row + 6 * (fh + 1) + 10; pw = 270; ph = 120;

    /* the right column */
    row = top;
    G(CYCLE_KIND, G_MODE, R + 110, row, 200, lh, "Mode", PLACETEXT_LEFT, GTCY_Labels, (ULONG)mode_labels); row += lh + 4;
    G(INTEGER_KIND, G_FROM, R + 110, row, 50, lh, "Dark from", PLACETEXT_LEFT, GTIN_MaxChars, 2);
    G(INTEGER_KIND, G_TO, R + 220, row, 50, lh, "to", PLACETEXT_LEFT, GTIN_MaxChars, 2); row += lh + 8;
    if (advanced) {
    G(CHECKBOX_KIND, G_OWNACCENT, R + 110, row, 26, lh, "Own accent", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    G(STRING_KIND, G_ACCENT, R + 150, row, 90, lh, NULL, 0, GTST_MaxChars, 7); row += lh + 8;
    G(CYCLE_KIND, G_LITE, R + 110, row, 200, lh, "Lite", PLACETEXT_LEFT, GTCY_Labels, (ULONG)lite_labels); row += lh + 4;
    G(CHECKBOX_KIND, G_LSHADOW, R + 110, row, 26, lh, "No shadows", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
    G(CHECKBOX_KIND, G_LROUND, R + 230, row, 26, lh, "Square", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 2;
    G(CHECKBOX_KIND, G_LGRAD, R + 110, row, 26, lh, "Flat", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 8;
    G(CYCLE_KIND, G_WB, R + 110, row, 200, lh, "Workbench", PLACETEXT_LEFT, GTCY_Labels, (ULONG)themed_labels); row += lh + 4;
    G(CYCLE_KIND, G_PUB, R + 110, row, 200, lh, "Other screens", PLACETEXT_LEFT, GTCY_Labels, (ULONG)themed_labels); row += lh + 4;
    G(CYCLE_KIND, G_CUSTOM, R + 110, row, 200, lh, "Game screens", PLACETEXT_LEFT, GTCY_Labels, (ULONG)themed_labels); row += lh + 4;
    G(STRING_KIND, G_NEVER, R + 110, row, 200, lh, "Never touch", PLACETEXT_LEFT, GTST_MaxChars, 150); row += lh + 8;
    G(CYCLE_KIND, G_LABELS, R + 110, row, 200, lh, "Icon labels", PLACETEXT_LEFT, GTCY_Labels, (ULONG)label_labels); row += lh + 8;
    }
    G(CYCLE_KIND, G_PROFILE, R + 110, row, 130, lh, "Profile", PLACETEXT_LEFT, GTCY_Labels, (ULONG)profile_labels);
    G(BUTTON_KIND, G_APPLYPROFILE, R + 244, row, 66, lh, "Load", 0, GA_Disabled, profile_labels[0][0] == '('); row += lh + 2;
    G(CHECKBOX_KIND, G_PROFILEAUTO, R + 110, row, 26, lh, "By machine", PLACETEXT_LEFT, GTCB_Scaled, TRUE); row += lh + 8;
    if (advanced) {
    G(CHECKBOX_KIND, G_CTF, R + 110, row, 26, lh, "Click to front", PLACETEXT_LEFT, GTCB_Scaled, TRUE, GA_Disabled, !exists(CTF_TOOL));
    G(CYCLE_KIND, G_CTFQUAL, R + 150, row, 160, lh, NULL, 0, GTCY_Labels, (ULONG)qual_labels, GA_Disabled, !exists(CTF_TOOL)); row += lh + 4;
    G(CHECKBOX_KIND, G_AP, R + 110, row, 26, lh, "Focus follows", PLACETEXT_LEFT, GTCB_Scaled, TRUE, GA_Disabled, !exists(AP_TOOL)); row += lh + 10;

    }
    if (py + ph + 10 > row) row = py + ph + 10;
    G(TEXT_KIND, G_STATUS, L, row, W - 20, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + 6;
    {
        static const char *const names[4] = { "Save", "Use", "Test", "Cancel" };
        static const int ids[4] = { G_SAVE, G_USE, G_TEST, G_CANCEL };
        int bw = (W - 20 - 3 * 10) / 4;
        for (i = 0; i < 4; i++) G(BUTTON_KIND, ids[i], L + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    menus[6].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Look", WA_ScreenTitle, (ULONG)"OpenPrefs Look 0.2", WA_PubScreen, (ULONG)scr,
                         WA_Left, 40, WA_Top, scr->BarHeight + 10, WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | LISTVIEWIDCMP | BUTTONIDCMP | CYCLEIDCMP | STRINGIDCMP |
                                   CHECKBOXIDCMP | INTEGERIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    show(&cur);
    SET(G_LABELS, GTCY_Active, label_now);
    SET(G_CTF, GTCB_Checked, ctf_on);
    SET(G_AP, GTCB_Checked, ap_on);
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);

    while (!quit) {
        struct IntuiMessage *m;
        ULONG tsig = timer ? 1UL << tport->mp_SigBit : 0;
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
                status("The test is over: the look is back as it was.");
            } else {
                snprintf(status_text, sizeof status_text, "Testing: back in %lu s unless you choose Use or Save.",
                         (unsigned long)(test_until - now_seconds()));
                status(status_text);
            }
        }
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { if (test_until) go_back(); quit = 1; }
            else if (cls == IDCMP_MENUPICK) {
                UWORD mc = code;
                while (mc != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, mc);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: put_in_place(1); focus_now(); test_until = 0; quit = 1; break;
                    case 2: put_in_place(0); focus_now(); test_until = 0; quit = 1; break;
                    case 3: if (test_until) go_back(); quit = 1; break;
                    case 4: advanced = (it->Flags & CHECKED) != 0; write_view(advanced); rc = 99; quit = 1; break;
                    }
                    mc = it->NextSelect;
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); draw_preview(win, px, py, pw, ph, &cur); }
            else if (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN) {
                int redraw = 1;
                switch (gg->GadgetID) {
                case G_THEMES: if (code < nthemes) strcpy(cur.theme, theme_names[code]); break;
                case G_MODE: cur.mode = code; break;
                case G_FROM: cur.dark_from = ((struct StringInfo *)gg->SpecialInfo)->LongInt % 24; break;
                case G_TO: cur.dark_to = ((struct StringInfo *)gg->SpecialInfo)->LongInt % 24; break;
                case G_OWNACCENT: cur.own_accent = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_ACCENT: {
                    ogt_rgb a;
                    const char *s = (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer;
                    if (ogt_parse_colour(s, &a)) strncpy(cur.accent, s, sizeof cur.accent - 1);
                    else status("An accent is #rrggbb, for example #365fa3.");
                    break;
                }
                case G_LITE: cur.lite = code; break;
                case G_LSHADOW: cur.lite_shadows = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_LROUND: cur.lite_rounding = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_LGRAD: cur.lite_gradients = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_WB: cur.wb_themed = code == 0; break;
                case G_PUB: cur.pub_themed = code == 0; break;
                case G_CUSTOM: cur.custom_themed = code == 0; break;
                case G_NEVER: strncpy(cur.never, (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof cur.never - 1); redraw = 0; break;
                case G_LABELS: label_now = code; redraw = 0; break;
                case G_PROFILE: redraw = 0; break;
                case G_APPLYPROFILE: {
                    ULONG at = 0;
                    GT_GetGadgetAttrs(gad[G_PROFILE], win, NULL, GTCY_Active, (ULONG)&at, TAG_DONE);
                    if ((int)at < nprofiles) apply_profile((int)at);
                    redraw = 0;
                    break;
                }
                case G_PROFILEAUTO: cur.profile_auto = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
                case G_CTF: ctf_on = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
                case G_CTFQUAL: ctf_qual = code; redraw = 0; break;
                case G_AP: ap_on = (gg->Flags & GFLG_SELECTED) != 0; redraw = 0; break;
                case G_TEST:
                    put_in_place(0);
                    test_until = now_seconds() + TEST_SECONDS;
                    status("Testing: back in 15 s unless you choose Use or Save.");
                    redraw = 0;
                    break;
                case G_USE: put_in_place(0); focus_now(); test_until = 0; quit = 1; break;
                case G_SAVE: put_in_place(1); focus_now(); test_until = 0; quit = 1; break;
                case G_CANCEL: if (test_until) go_back(); quit = 1; break;
                }
                if (redraw && !quit) show(&cur);
            }
        }
    }
out:
    if (timer) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (win) { free_pens(win->WScreen); ClearMenuStrip(win); CloseWindow(win); win = NULL; }
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

static int gui(void)
{
    int r;
    while ((r = gui_once()) == 99) ;
    return r;
}

int main(int argc, char **argv)
{
    LONG args[4] = { 0, 0, 0, 0 };
    /* from Workbench (argc 0) there are no arguments: ReadArgs would read them from
     * libnix's console window, which opens empty and waits */
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S,ADVANCED/S", args, NULL) : NULL;
    char *text;
    int rc;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Look"); return RETURN_FAIL; }
    text = read_file(args[0] ? (const char *)args[0] : PREFS_ENV, NULL);
    parse_look(&cur, text);
    if (text) FreeVec(text);
    orig = cur;
    label_now = label_orig = read_label_style();
    ctf_on = ctf_orig = at_start(CTF_TOOL) || running(CTF_TOOL);
    ap_on = ap_orig = at_start(AP_TOOL) || running(AP_TOOL);
    if (args[1] || args[2]) {                /* from the Shell: no window */
        put_in_place(args[2] != 0);
        if (rd) FreeArgs(rd);
        return RETURN_OK;
    }
    advanced = args[3] ? 1 : read_view();
    if (rd) FreeArgs(rd);
    list_dir(THEME_DIR, ".theme", theme_names, MAX_THEMES, &nthemes);
    list_dir(PROFILE_DIR, ".profile", profile_names, MAX_PROFILES, &nprofiles);
    rc = gui();
    if (preview_loaded) ogt_theme_free(&preview_theme);
    return rc;
}
