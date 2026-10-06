/* om_prefs: OpenMenus' settings and taking over MagicMenu's (om_prefs.h).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "om_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const om_colour_keys[OM_C_COUNT] = {
    "background", "text", "selected", "selected.text", "light", "dark", "shadow"
};

static const char *const open_words[] = { "pulldown", "popup", "pointer" };
static const char *const use_words[] = { "hold", "sticky", "click" };
static const char *const bg_words[] = { "solid", "see-through", "image", "see-through-image" };
static const char *const col_words[] = { "theme", "screen", "own" };
static const char *const bar_words[] = { "title", "top", "bottom", "left", "right" };

void om_defaults(om_prefs *p)
{
    static const char *const cols[OM_C_COUNT] = {
        "#f6f7f9", "#121825", "#365fa3", "#ffffff", "#ffffff", "#8a94a6", "#000000"
    };
    memset(p, 0, sizeof *p);
    p->enabled = 1;
    p->open = OM_OPEN_POINTER;
    p->use[OM_PD] = OM_USE_HOLD;
    p->use[OM_PU] = OM_USE_HOLD;
    p->popup_last = 1;
    p->sub_mark = 1;
    p->shadow = 1;
    p->shadow_size = 2;
    p->shadow_strength = 25;
    p->colours = OM_COL_THEME;
    for (int i = 0; i < OM_C_COUNT; i++) strcpy(p->colour[i], cols[i]);
    p->keyboard = 1;
    strcpy(p->key, "ramiga space");
    p->keyboard_ralt = 1;
    p->keep_running = 1;
    p->rightclick = 1;                      /* on: Dale approved the design, 6 Oct 2026 ("build it") */
    p->rightclick_extras = 1;
    p->rightclick_selection = 1;
    p->rightclick_name = 1;
}

/* ---- small helpers ------------------------------------------------------------------- */

static int same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return !*a && !*b;
}

static int pick(const char *w, const char *const *words, int n, int def)
{
    for (int i = 0; i < n; i++) if (same(w, words[i])) return i;
    return def;
}

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* One word of a line, or a "quoted" one, in which \" is a quote and \\ a
 * backslash. NULL at the line's end. */
static const char *word(const char **pp, char *out, int size)
{
    const char *s = *pp;
    int n = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\n' || *s == '\r') { out[0] = 0; *pp = s; return NULL; }
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && *s != '\n' && n < size - 1) {
            if (*s == '\\' && (s[1] == '"' || s[1] == '\\')) s++;
            out[n++] = *s++;
        }
        if (*s == '"') s++;
    }
    else while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && n < size - 1) out[n++] = *s++;
    out[n] = 0;
    *pp = s;
    return out;
}

static int is_colour(const char *s)
{
    if (s[0] != '#' || strlen(s) != 7) return 0;
    for (int i = 1; i < 7; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) return 0;
    return 1;
}

/* ---- OpenMenus' own format ----------------------------------------------------------- */

int om_parse(om_prefs *p, const char *text)
{
    const char *line;
    om_defaults(p);
    if (!text) return 0;
    for (line = text; *line; ) {
        const char *s = line, *next = strchr(line, '\n');
        char k[32], v[256];
        if (word(&s, k, sizeof k) && k[0] != ';' && word(&s, v, sizeof v)) {
            int on = same(v, "on") || same(v, "yes");
            if (same(k, "enabled")) p->enabled = on;
            else if (same(k, "open")) p->open = pick(v, open_words, 3, p->open);
            else if (same(k, "pulldown")) p->use[OM_PD] = pick(v, use_words, 3, p->use[OM_PD]);
            else if (same(k, "popup")) p->use[OM_PU] = pick(v, use_words, 3, p->use[OM_PU]);
            else if (same(k, "delay.pulldown")) p->delay[OM_PD] = clamp(atoi(v), 0, OM_DELAY_MAX);
            else if (same(k, "delay.popup")) p->delay[OM_PU] = clamp(atoi(v), 0, OM_DELAY_MAX);
            else if (same(k, "popup.last")) p->popup_last = on;
            else if (same(k, "submenus.centre")) p->sub_centre = on;
            else if (same(k, "submenus.mark")) p->sub_mark = on;
            else if (same(k, "border")) p->border_double = same(v, "double");
            else if (same(k, "shadow")) p->shadow = on;
            else if (same(k, "shadow.size")) p->shadow_size = clamp(atoi(v), 1, OM_SHADOW_SIZE_MAX);
            else if (same(k, "shadow.strength")) p->shadow_strength = clamp(atoi(v), 1, OM_SHADOW_STRENGTH_MAX);
            else if (same(k, "background")) p->background = pick(v, bg_words, 4, p->background);
            else if (same(k, "background.image")) { strncpy(p->image, v, sizeof p->image - 1); p->image[sizeof p->image - 1] = 0; }
            else if (same(k, "separators")) p->separators_bold = same(v, "bold");
            else if (same(k, "colours")) p->colours = pick(v, col_words, 3, p->colours);
            else if (!strncmp(k, "colour.", 7)) {
                for (int i = 0; i < OM_C_COUNT; i++)
                    if (same(k + 7, om_colour_keys[i]) && is_colour(v)) strcpy(p->colour[i], v);
            }
            else if (same(k, "keyboard")) p->keyboard = on;
            else if (same(k, "keyboard.key")) { strncpy(p->key, v, sizeof p->key - 1); p->key[sizeof p->key - 1] = 0; }
            else if (same(k, "keyboard.ralt")) p->keyboard_ralt = on;
            else if (same(k, "keyboard.top")) p->keyboard_top = on;
            else if (same(k, "programs.keep-running")) p->keep_running = on;
            else if (same(k, "rightclick")) p->rightclick = on;
            else if (same(k, "rightclick.extras")) p->rightclick_extras = on;
            else if (same(k, "rightclick.selection")) p->rightclick_selection = !same(v, "one");
            else if (same(k, "rightclick.name")) p->rightclick_name = on;
            else if (same(k, "bar")) p->bar = pick(v, bar_words, 5, p->bar);
            else if (same(k, "bar.autohide")) p->bar_autohide = on;
            else if (same(k, "imported")) { strncpy(p->imported, v, sizeof p->imported - 1); p->imported[sizeof p->imported - 1] = 0; }
        }
        if (!next) break;
        line = next + 1;
    }
    return 1;
}

/* s with " and \ escaped, for a "quoted" value. */
static const char *quoted(const char *s, char *out, int size)
{
    int n = 0;
    for (; *s && n < size - 2; s++) {
        if (*s == '"' || *s == '\\') out[n++] = '\\';
        out[n++] = *s;
    }
    out[n] = 0;
    return out;
}

#define ADD(...) do { int n_ = snprintf(o, size, __VA_ARGS__); if (n_ < 0 || n_ >= size) return -1; o += n_; size -= n_; } while (0)

int om_write(const om_prefs *p, char *out, int size)
{
    char *o = out, q[520];
    const char *onoff[2] = { "off", "on" };
    ADD("; OpenMenus settings, format 1. OpenPrefs Menus writes this file; OpenMenus reads it.\n");
    ADD("enabled %s\n", onoff[!!p->enabled]);
    ADD("open %s\n", open_words[clamp(p->open, 0, 2)]);
    ADD("pulldown %s\n", use_words[clamp(p->use[OM_PD], 0, 2)]);
    ADD("popup %s\n", use_words[clamp(p->use[OM_PU], 0, 2)]);
    ADD("delay.pulldown %d\n", clamp(p->delay[OM_PD], 0, OM_DELAY_MAX));
    ADD("delay.popup %d\n", clamp(p->delay[OM_PU], 0, OM_DELAY_MAX));
    ADD("popup.last %s\n", onoff[!!p->popup_last]);
    ADD("submenus.centre %s\n", onoff[!!p->sub_centre]);
    ADD("submenus.mark %s\n", onoff[!!p->sub_mark]);
    ADD("border %s\n", p->border_double ? "double" : "single");
    ADD("shadow %s\n", onoff[!!p->shadow]);
    ADD("shadow.size %d\n", clamp(p->shadow_size, 1, OM_SHADOW_SIZE_MAX));
    ADD("shadow.strength %d\n", clamp(p->shadow_strength, 1, OM_SHADOW_STRENGTH_MAX));
    ADD("background %s\n", bg_words[clamp(p->background, 0, 3)]);
    if (p->image[0]) ADD("background.image \"%s\"\n", quoted(p->image, q, sizeof q));
    ADD("separators %s\n", p->separators_bold ? "bold" : "lite");
    ADD("colours %s\n", col_words[clamp(p->colours, 0, 2)]);
    for (int i = 0; i < OM_C_COUNT; i++) ADD("colour.%s %s\n", om_colour_keys[i], p->colour[i]);
    ADD("keyboard %s\n", onoff[!!p->keyboard]);
    ADD("keyboard.key \"%s\"\n", quoted(p->key, q, sizeof q));
    ADD("keyboard.ralt %s\n", onoff[!!p->keyboard_ralt]);
    ADD("keyboard.top %s\n", onoff[!!p->keyboard_top]);
    ADD("programs.keep-running %s\n", onoff[!!p->keep_running]);
    ADD("rightclick %s\n", onoff[!!p->rightclick]);
    ADD("rightclick.extras %s\n", onoff[!!p->rightclick_extras]);
    ADD("rightclick.selection %s\n", p->rightclick_selection ? "all" : "one");
    ADD("rightclick.name %s\n", onoff[!!p->rightclick_name]);
    ADD("bar %s\n", bar_words[clamp(p->bar, 0, 4)]);
    ADD("bar.autohide %s\n", onoff[!!p->bar_autohide]);
    if (p->imported[0]) ADD("imported \"%s\"\n", quoted(p->imported, q, sizeof q));
    return (int)(o - out);
}

/* ---- MagicMenu's file ----------------------------------------------------------------
 *
 * The layout, as MagicMenu 2.x and 3.x write it (text, one setting a line):
 *     MagicMenu/2:
 *     <tab>Name=value
 *     ...
 *     #
 * Values: Yes or No; a number (decimal, or 0x hex); or a "quoted" string in
 * which \\ is a backslash and \ooo an octal byte. A colour is three 32-bit
 * values, Name.R, Name.G and Name.B, the 8-bit level repeated four times.
 * A setting written as 0 in the 3.0 per-menu fields means "never written":
 * the older shared setting decides. */

#define MM_MAX 96

typedef struct mm_file {
    int n;
    char name[MM_MAX][24];
    char value[MM_MAX][260];
} mm_file;

static const char *mm_get(const mm_file *f, const char *name)
{
    for (int i = 0; i < f->n; i++) if (same(f->name[i], name)) return f->value[i];
    return NULL;
}

static long mm_num(const mm_file *f, const char *name, long def)
{
    const char *v = mm_get(f, name);
    if (!v || !*v) return def;
    if (same(v, "yes")) return 1;
    if (same(v, "no")) return 0;
    return strtol(v, NULL, 0);
}

static void mm_text(const mm_file *f, const char *name, char *out, int size)
{
    const char *v = mm_get(f, name);
    int n = 0;
    out[0] = 0;
    if (!v || *v != '"') return;
    for (v++; *v && *v != '"' && n < size - 1; v++) {
        if (*v == '\\' && v[1] == '\\') { out[n++] = '\\'; v++; }
        else if (*v == '\\' && v[1] >= '0' && v[1] <= '3' && v[2] >= '0' && v[2] <= '7' && v[3] >= '0' && v[3] <= '7') {
            out[n++] = (char)(((v[1] - '0') << 6) | ((v[2] - '0') << 3) | (v[3] - '0'));
            v += 3;
        } else out[n++] = *v;
    }
    out[n] = 0;
}

static int mm_colour(const mm_file *f, const char *name, char out[8])
{
    char k[24];
    unsigned long c[3];
    const char *rgb = "RGB";
    for (int i = 0; i < 3; i++) {
        const char *v;
        snprintf(k, sizeof k, "%s.%c", name, rgb[i]);
        if (!(v = mm_get(f, k))) return 0;
        c[i] = strtoul(v, NULL, 0) >> 24;
    }
    snprintf(out, 8, "#%02lx%02lx%02lx", c[0] & 255, c[1] & 255, c[2] & 255);
    return 1;
}

/* A 3.0 per-menu switch (0 unset, 1 off, 2 on), else the shared one. */
static int mm_split(const mm_file *f, const char *split, const char *shared, int def)
{
    long v = mm_num(f, split, 0);
    if (v == 1) return 0;
    if (v == 2) return 1;
    return (int)mm_num(f, shared, def);
}

/* A 3.0 delay (0 unset, 1 to 10 tenths, 255 none), else the old Delayed switch. */
static int mm_delay(const mm_file *f, const char *name)
{
    long v = mm_num(f, name, 0);
    if (v == 255) return 0;
    if (v >= 1 && v <= OM_DELAY_MAX) return (int)v;
    return mm_num(f, "Delayed", 0) ? 1 : 0;
}

static int mm_use(long mode)
{
    /* MagicMenu: 0 hold and release, 1 sticky (opens on move), 2 sticky (opens on click), 3 keyboard */
    return mode == 1 ? OM_USE_STICKY : mode == 2 ? OM_USE_CLICK : OM_USE_HOLD;
}

int om_from_magicmenu(om_prefs *p, const char *text, char *err, int errlen)
{
    static mm_file f;
    const char *line, *s;
    char head[32];
    long v;
    if (!text) { snprintf(err, errlen, "there is no MagicMenu settings file"); return 0; }
    if ((unsigned char)text[0] == 0x01 && (unsigned char)text[1] == 0x31) {
        /* MMPREFS_MAGIC (20041970) as the first long: a 1.x file, the raw structure */
        snprintf(err, errlen, "it is from MagicMenu 1, which kept its settings in another form");
        return 0;
    }
    s = text;
    if (!word(&s, head, sizeof head) || strncmp(head, "MagicMenu/", 10)) {
        snprintf(err, errlen, "it doesn't start the way MagicMenu's settings do");
        return 0;
    }
    f.n = 0;
    line = strchr(text, '\n');
    while (line && *++line && *line != '#' && f.n < MM_MAX) {
        const char *eq, *end = strchr(line, '\n'), *nm = line;
        int ln, vn;
        while (*nm == ' ' || *nm == '\t') nm++;
        eq = strchr(nm, '=');
        if (eq && (!end || eq < end)) {
            ln = (int)(eq - nm);
            vn = end ? (int)(end - eq - 1) : (int)strlen(eq + 1);
            while (vn > 0 && (eq[vn] == '\r' || eq[vn] == ' ')) vn--;
            if (ln > 0 && ln < 24) {
                memcpy(f.name[f.n], nm, ln); f.name[f.n][ln] = 0;
                if (vn > 259) vn = 259;
                memcpy(f.value[f.n], eq + 1, vn); f.value[f.n][vn] = 0;
                f.n++;
            }
        }
        line = end;
    }
    if (!f.n) { snprintf(err, errlen, "it holds no settings"); return 0; }

    p->enabled = (int)mm_num(&f, "Enabled", 1);
    v = mm_num(&f, "MenuType", 2);
    p->open = v == 0 ? OM_OPEN_PULLDOWN : v == 1 ? OM_OPEN_POPUP : OM_OPEN_POINTER;
    p->use[OM_PD] = mm_use(mm_num(&f, "PDMode", 0));
    p->use[OM_PU] = mm_use(mm_num(&f, "PUMode", 0));
    p->delay[OM_PD] = mm_delay(&f, "PDOpenDelay");
    p->delay[OM_PU] = mm_delay(&f, "PUOpenDelay");
    /* KCPUCenter (3.0: PUCenterBox) centres a pop-up on the item last chosen.
     * MagicMenu has no setting for centred submenus: ours stays as it is. */
    p->popup_last = mm_split(&f, "PUCenterBox", "KCPUCenter", p->popup_last);
    p->sub_mark = mm_split(&f, "PDMarkSub", "MarkSub", 1);
    p->border_double = mm_split(&f, "PDDblBorder", "DblBorder", 0);
    p->shadow = mm_split(&f, "PDCastShadows", "CastShadows", 1);
    v = mm_num(&f, "PDShadowDist", 0);
    if (v < 1 || v > OM_SHADOW_SIZE_MAX) v = mm_num(&f, "ShadowDistance", 2);
    p->shadow_size = clamp((int)v, 1, OM_SHADOW_SIZE_MAX);
    v = mm_num(&f, "PDShadowInt", 0);
    if (v < 1 || v > OM_SHADOW_STRENGTH_MAX) v = mm_num(&f, "ShadowIntensity", 25);
    p->shadow_strength = clamp((int)v, 1, OM_SHADOW_STRENGTH_MAX);

    mm_text(&f, "Backfill", p->image, sizeof p->image);
    v = mm_num(&f, "PDBackground", 0);
    if (v >= 1 && v <= 4) p->background = (int)v - 1;     /* MagicMenu: 1 colour, 2 see, 3 image, 4 see + image */
    else if (mm_num(&f, "TransBackfill", 0) && p->image[0]) p->background = OM_BG_SEEIMAGE;
    else if (mm_num(&f, "PDTransparent", 0) || mm_num(&f, "Transparency", 0)) p->background = OM_BG_SEE;
    else p->background = OM_BG_SOLID;
    if ((p->background == OM_BG_IMAGE || p->background == OM_BG_SEEIMAGE) && !p->image[0]) p->background = OM_BG_SOLID;

    v = mm_num(&f, "PDSeparatorStyle", 0);
    p->separators_bold = v == 2 ? 1 : v == 1 ? 0 : mm_num(&f, "SeparatorBarStyle", 0) != 0;

    /* MagicMenu's Multicolor look used its own pens; Standard and Old 3D, the screen's */
    if (mm_num(&f, "PDLook", 2) == 2 && !mm_num(&f, "PreferScreenColours", 0)) {
        static const char *const mm_names[OM_C_COUNT] = { "Background", "Text", "Fill", "HighText", "LightEdge", "DarkEdge", "ShadowCol" };
        p->colours = OM_COL_OWN;
        for (int i = 0; i < OM_C_COUNT; i++) mm_colour(&f, mm_names[i], p->colour[i]);
    } else p->colours = OM_COL_SCREEN;

    p->keyboard = (int)mm_num(&f, "KCEnabled", p->keyboard);
    {
        char k[64];
        mm_text(&f, "KCKeyStr", k, sizeof k);
        if (k[0]) strcpy(p->key, k);
    }
    p->keyboard_ralt = (int)mm_num(&f, "KCAltRCommand", p->keyboard_ralt);
    p->keyboard_top = (int)mm_num(&f, "KCGoTop", p->keyboard_top);
    p->keep_running = (int)mm_num(&f, "NonBlocking", p->keep_running);
    return 1;
}
