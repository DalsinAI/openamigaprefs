/* od_dock: OpenDock's settings, and taking over other docks' (od_dock.h).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "od_dock.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *const place_words[] = { "bottom", "top", "left", "right" };
static const char *const size_words[] = { "small", "medium", "large" };
static const char *const kind_words[] = { "wb", "cli", "arexx", "separator" };

int od_cell(int size) { return size == OD_LARGE ? 72 : size == OD_MEDIUM ? 56 : 40; }

void od_defaults(od_dock *d)
{
    memset(d, 0, sizeof *d);
    d->place = OD_BOTTOM;
    d->size = OD_MEDIUM;
    d->running = 1;
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
    return *a == *b;
}

static int ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), t = strlen(tail);
    return n >= t && same(s + n - t, tail);
}

static int pick(const char *w, const char *const *words, int n, int def)
{
    for (int i = 0; i < n; i++) if (same(w, words[i])) return i;
    return def;
}

static void copy(char *to, const char *from, int size)
{
    int n = 0;
    if (from) while (from[n] && n < size - 1) { to[n] = from[n]; n++; }
    to[n] = 0;
}

/* The last part of a path: "SYS:Utilities/MultiView" gives "MultiView". */
static const char *file_part(const char *p)
{
    const char *s = strrchr(p, '/');
    if (!s) s = strrchr(p, ':');
    return s ? s + 1 : p;
}

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
        while (*s && *s != '"' && *s != '\n') {
            if (*s == '\\' && (s[1] == '"' || s[1] == '\\')) s++;
            if (n < size - 1) out[n++] = *s;
            s++;
        }
        if (*s == '"') s++;
    } else
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') { if (n < size - 1) out[n++] = *s; s++; }
    out[n] = 0;
    *pp = s;
    return out;
}

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

static od_button *add_button(od_dock *d, od_report *r)
{
    od_button *b;
    if (d->n >= OD_MAX) { if (r) r->full++; return NULL; }
    b = &d->b[d->n++];
    memset(b, 0, sizeof *b);
    b->stack = 4096;
    if (r) r->added++;
    return b;
}

static void add_separator(od_dock *d)
{
    /* never two together, and never first */
    if (d->n && d->b[d->n - 1].kind != OD_SEPARATOR && d->n < OD_MAX) {
        memset(&d->b[d->n], 0, sizeof d->b[0]);
        d->b[d->n++].kind = OD_SEPARATOR;
    }
}

static void end_separator(od_dock *d)
{
    if (d->n && d->b[d->n - 1].kind == OD_SEPARATOR) d->n--;
}

static od_button *add_program(od_dock *d, od_report *r, int kind, const char *label, const char *command)
{
    od_button *b = add_button(d, r);
    if (!b) return NULL;
    b->kind = kind;
    copy(b->command, command, sizeof b->command);
    copy(b->label, label && *label ? label : file_part(command), sizeof b->label);
    return b;
}

void od_starter(od_dock *d)
{
    d->n = 0;
    add_program(d, NULL, OD_WB, "Shell", "SYS:System/Shell");
    add_program(d, NULL, OD_WB, "Prefs", "SYS:Prefs");
    add_program(d, NULL, OD_WB, "MultiView", "SYS:Utilities/MultiView");
    add_program(d, NULL, OD_WB, "Look", "SYS:Prefs/Look");
}

/* ---- OpenDock's own format ------------------------------------------------------------ */

int od_parse(od_dock *d, const char *text)
{
    const char *line;
    od_defaults(d);
    if (!text) return 0;
    for (line = text; *line; ) {
        const char *s = line, *next = strchr(line, '\n');
        char k[32], v[256];
        if (word(&s, k, sizeof k) && k[0] != ';') {
            if (same(k, "separator")) { if (d->n < OD_MAX) { memset(&d->b[d->n], 0, sizeof d->b[0]); d->b[d->n++].kind = OD_SEPARATOR; } }
            else if (same(k, "button") && word(&s, v, sizeof v)) {
                /* button <wb|cli|arexx> "<command>" [label "..."] [icon "..."] [dir "..."] [stack n] */
                int kind = pick(v, kind_words, 3, -1);
                od_button *b;
                if (kind >= 0 && word(&s, v, sizeof v) && (b = add_button(d, NULL))) {
                    b->kind = kind;
                    copy(b->command, v, sizeof b->command);
                    copy(b->label, file_part(v), sizeof b->label);
                    while (word(&s, k, sizeof k) && word(&s, v, sizeof v)) {
                        if (same(k, "label")) copy(b->label, v, sizeof b->label);
                        else if (same(k, "icon")) copy(b->icon, v, sizeof b->icon);
                        else if (same(k, "dir")) copy(b->dir, v, sizeof b->dir);
                        else if (same(k, "stack")) { b->stack = atol(v); if (b->stack < 4096) b->stack = 4096; }
                    }
                }
            } else if (word(&s, v, sizeof v)) {
                int on = same(v, "on") || same(v, "yes");
                if (same(k, "place")) d->place = pick(v, place_words, 4, d->place);
                else if (same(k, "size")) d->size = pick(v, size_words, 3, d->size);
                else if (same(k, "labels")) d->labels = on;
                else if (same(k, "running")) d->running = on;
                else if (same(k, "magnify")) d->magnify = on;
                else if (same(k, "imported")) copy(d->imported, v, sizeof d->imported);
            }
        }
        if (!next) break;
        line = next + 1;
    }
    return 1;
}

#define ADD(...) do { int n_ = snprintf(o, size, __VA_ARGS__); if (n_ < 0 || n_ >= size) return -1; o += n_; size -= n_; } while (0)

int od_write(const od_dock *d, char *out, int size)
{
    char *o = out, q[520];
    const char *onoff[2] = { "off", "on" };
    ADD("; OpenDock settings, format 1. OpenPrefs Dock writes this file; OpenDock reads it.\n");
    ADD("place %s\n", place_words[d->place >= 0 && d->place < 4 ? d->place : 0]);
    ADD("size %s\n", size_words[d->size >= 0 && d->size < 3 ? d->size : 1]);
    ADD("labels %s\n", onoff[!!d->labels]);
    ADD("running %s\n", onoff[!!d->running]);
    ADD("magnify %s\n", onoff[!!d->magnify]);
    if (d->imported[0]) ADD("imported \"%s\"\n", quoted(d->imported, q, sizeof q));
    for (int i = 0; i < d->n; i++) {
        const od_button *b = &d->b[i];
        if (b->kind == OD_SEPARATOR) { ADD("separator\n"); continue; }
        ADD("button %s \"%s\"", kind_words[b->kind >= 0 && b->kind < 3 ? b->kind : 0], quoted(b->command, q, sizeof q));
        ADD(" label \"%s\"", quoted(b->label, q, sizeof q));
        if (b->icon[0]) ADD(" icon \"%s\"", quoted(b->icon, q, sizeof q));
        if (b->dir[0]) ADD(" dir \"%s\"", quoted(b->dir, q, sizeof q));
        if (b->stack > 4096) ADD(" stack %ld", b->stack);
        ADD("\n");
    }
    return (int)(o - out);
}

/* ---- which file is which ------------------------------------------------------------- */

int od_sniff(const unsigned char *data, long len)
{
    long i = 0;
    if (len >= 12 && !memcmp(data, "FORM", 4) && !memcmp(data + 8, "PREF", 4)) return 1;
    while (i < len && (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r' || data[i] == 0xef || data[i] == 0xbb || data[i] == 0xbf)) i++;
    if (len - i >= 5 && !memcmp(data + i, "<?xml", 5)) return 2;
    for (i = 0; i + 6 < len; i++)
        if (!memcmp(data + i, "NEWDIR", 6) && (i == 0 || data[i - 1] == '\n')) return 3;
    return 0;
}

/* ---- ToolManager 2 ----------------------------------------------------------------------
 *
 * ENV(ARC):ToolManager.prefs, as ToolManager 2.0 to 2.2 write it: IFF FORM PREF,
 * a PRHD chunk (version 0), then one chunk for each object. Each chunk is a
 * big-endian header whose first long says which strings follow (bit 0 the
 * first), then those strings, each ending in a NUL.
 *   TMEX, a program: header 20 bytes (strings, flags, delay, stack, UWORD
 *         type 0 CLI / 1 WB / 2 ARexx / 3 dock / 4 hotkey / 5 network,
 *         priority); strings: name, command, current drawer, hotkey,
 *         output, path, public screen.
 *   TMIM, an image: header 4 bytes; strings: name, file.
 *   TMDO, a dock: header 28 bytes; strings: name, hotkey, public screen,
 *         title, font; then its tools, each a flags byte (bit 7 one more,
 *         bit 0 a program's name follows, bit 1 an image's, bit 2 a sound's),
 *         ending with a byte without bit 7.
 * Docks name their programs and images; the first object of a name counts.
 * ToolManager 3 keeps another form, which is refused with a plain reason. */

#define TM_MAX 96

static struct { char name[48], command[256], dir[128]; int type; long stack; } tm_exec[TM_MAX];
static struct { char name[48], file[160]; } tm_image[TM_MAX];
static int tm_nexec, tm_nimage;

static unsigned long be32(const unsigned char *p) { return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3]; }

/* The next NUL-ended string in [*pp, end), into out; 0 when it runs past the end. */
static int tm_string(const unsigned char **pp, const unsigned char *end, char *out, int size)
{
    const unsigned char *p = *pp;
    int n = 0;
    while (p < end && *p) { if (n < size - 1) out[n++] = (char)*p; p++; }
    out[n] = 0;
    if (p >= end) return 0;
    *pp = p + 1;
    return 1;
}

/* The strings a header's first long asks for, bits 0 to count-1, into the slots given (NULL: read and drop). */
static int tm_strings(const unsigned char **pp, const unsigned char *end, unsigned long bits, int count, char **slots, const int *sizes)
{
    char drop[256];
    for (int i = 0; i < count; i++)
        if (bits & (1UL << i)) {
            if (!tm_string(pp, end, slots[i] ? slots[i] : drop, slots[i] ? sizes[i] : (int)sizeof drop)) return 0;
        } else if (slots[i]) slots[i][0] = 0;
    return 1;
}

static int tm_find_exec(const char *name)
{
    for (int i = 0; i < tm_nexec; i++) if (!strcmp(tm_exec[i].name, name)) return i;
    return -1;
}

static const char *tm_find_image(const char *name)
{
    for (int i = 0; i < tm_nimage; i++) if (!strcmp(tm_image[i].name, name)) return tm_image[i].file;
    return NULL;
}

int od_from_toolmanager(od_dock *d, const unsigned char *data, long len, od_report *r, char *err, int errlen)
{
    const unsigned char *p, *end;
    int pass, docks = 0, first_object = 1;
    od_report dummy;
    if (!r) r = &dummy;
    memset(r, 0, sizeof *r);
    if (!data || len < 12 || memcmp(data, "FORM", 4) || memcmp(data + 8, "PREF", 4)) {
        snprintf(err, errlen, "it isn't ToolManager's settings file");
        return 0;
    }
    if (be32(data + 4) + 8 < (unsigned long)len) len = (long)be32(data + 4) + 8;
    end = data + len;
    tm_nexec = tm_nimage = 0;
    /* two passes: the programs and images first, then the docks that name them */
    for (pass = 0; pass < 2; pass++)
        for (p = data + 12; p + 8 <= end; ) {
            const unsigned char *c = p + 8, *ce;
            unsigned long size = be32(p + 4);
            if (size > (unsigned long)(end - c)) break;
            ce = c + size;
            if (pass == 0 && !memcmp(p, "PRHD", 4)) {
                if (size < 1 || c[0] != 0) { snprintf(err, errlen, "it is from a ToolManager this doesn't know (settings version %d)", size ? c[0] : -1); return 0; }
            } else if (pass == 0 && first_object && memcmp(p, "PRHD", 4)) {
                first_object = 0;
                if (memcmp(p, "TM", 2) || !strchr("EISMIDA", p[2])) {
                    snprintf(err, errlen, "it is from ToolManager 3, which keeps its settings in another form");
                    return 0;
                }
            }
            if (pass == 0 && !memcmp(p, "TMEX", 4) && size >= 20 && tm_nexec < TM_MAX) {
                const unsigned char *s = c + 20;
                char *slots[7] = { tm_exec[tm_nexec].name, tm_exec[tm_nexec].command, tm_exec[tm_nexec].dir, NULL, NULL, NULL, NULL };
                static const int sizes[7] = { 48, 256, 128, 0, 0, 0, 0 };
                if (tm_strings(&s, ce, be32(c), 7, slots, sizes)) {
                    tm_exec[tm_nexec].stack = (long)be32(c + 12);
                    tm_exec[tm_nexec].type = c[16] << 8 | c[17];
                    tm_nexec++;
                }
            } else if (pass == 0 && !memcmp(p, "TMIM", 4) && size >= 4 && tm_nimage < TM_MAX) {
                const unsigned char *s = c + 4;
                char *slots[2] = { tm_image[tm_nimage].name, tm_image[tm_nimage].file };
                static const int sizes[2] = { 48, 160 };
                if (tm_strings(&s, ce, be32(c), 2, slots, sizes)) tm_nimage++;
            } else if (pass == 1 && !memcmp(p, "TMDO", 4) && size >= 28) {
                const unsigned char *s = c + 28;
                char *slots[5] = { NULL, NULL, NULL, NULL, NULL };
                static const int sizes[5] = { 0, 0, 0, 0, 0 };
                if (tm_strings(&s, ce, be32(c), 5, slots, sizes)) {
                    if (docks++) add_separator(d);
                    while (s < ce && (*s & 0x80)) {
                        int f = *s++;
                        char exec[48] = "", image[48] = "", sound[48];
                        if ((f & 1) && !tm_string(&s, ce, exec, sizeof exec)) break;
                        if ((f & 2) && !tm_string(&s, ce, image, sizeof image)) break;
                        if ((f & 4) && !tm_string(&s, ce, sound, sizeof sound)) break;
                        {
                            int e = exec[0] ? tm_find_exec(exec) : -1;
                            const char *img = image[0] ? tm_find_image(image) : NULL;
                            od_button *b;
                            int t = e >= 0 ? tm_exec[e].type : -1;
                            if (t < 0 || t > 2 || !tm_exec[e].command[0]) { r->skipped++; continue; }
                            if (!(b = add_program(d, r, t == 0 ? OD_CLI : t == 1 ? OD_WB : OD_AREXX, tm_exec[e].name, tm_exec[e].command))) continue;
                            if (t == 0) {
                                /* "[]" is where dropped icons go: none from the dock */
                                char *h;
                                copy(b->dir, tm_exec[e].dir, sizeof b->dir);
                                while ((h = strstr(b->command, "[]"))) {
                                    int cut = h > b->command && h[-1] == ' ' ? 1 : 0;
                                    memmove(h - cut, h + 2, strlen(h + 2) + 1);
                                }
                            }
                            if (tm_exec[e].stack > 4096 && tm_exec[e].stack < 1024 * 1024) b->stack = tm_exec[e].stack;
                            if (img && img[0]) {
                                /* an icon (with or without .info); an IFF picture stays out: OpenDock draws icons */
                                copy(b->icon, img, sizeof b->icon);
                                if (ends_with(b->icon, ".info")) b->icon[strlen(b->icon) - 5] = 0;
                                else if (ends_with(b->icon, ".iff") || ends_with(b->icon, ".ilbm") || ends_with(b->icon, ".anim") || ends_with(b->icon, ".brush"))
                                    b->icon[0] = 0;
                            }
                        }
                    }
                }
            }
            p = ce + (size & 1);
        }
    end_separator(d);
    if (!docks) { snprintf(err, errlen, "it has no docks"); return 0; }
    if (!r->added) { snprintf(err, errlen, "its docks have no programs to start"); return 0; }
    return 1;
}

/* ---- AmiDock (AmigaOS 4) ----------------------------------------------------------------
 *
 * ENV(ARC):Sys/AmiDock.amiga.com.xml, PrefsObjects XML: <dict> holds
 * <key>, value pairs; values are <string>, <integer>, <bool>, <array> or
 * <dict>. The root's Docks is an array of docks; each has Categories, an
 * array whose entries have Icons, an array of { Name, FileName }. A
 * FileName ending ".docky" is a plug-in: Separator.docky becomes a
 * separator, the others (SubDock, clocks) are left out, and each dock's
 * buttons follow the last's after a separator. AmiDock starts its programs
 * as Workbench does. AmigaOS 3.9's AmiDock keeps another form, refused. */

typedef struct ax { const char *p; char key[32]; } ax;

/* The next tag: its name into tag ("dict", "/dict", ...); 0 at the end. */
static int ax_tag(ax *x, char *tag, int size)
{
    const char *s = x->p;
    int n = 0;
    for (;;) {
        while (*s && *s != '<') s++;
        if (!*s) { x->p = s; return 0; }
        if (s[1] == '?' || s[1] == '!') { while (*s && *s != '>') s++; continue; }
        break;
    }
    s++;
    while (*s && *s != '>' && *s != ' ' && n < size - 1) tag[n++] = *s++;
    tag[n] = 0;
    while (*s && *s != '>') s++;
    if (*s) s++;
    x->p = s;
    return 1;
}

/* Text up to the next tag, decoded (entities, UTF-8 to Latin-1), then past its closing tag. */
static void ax_text(ax *x, char *out, int size)
{
    const char *s = x->p;
    int n = 0;
    char tag[16];
    while (*s && *s != '<') {
        unsigned c = (unsigned char)*s;
        if (c == '&') {
            static const struct { const char *e; char c; } ent[] = { { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' } };
            unsigned i;
            for (i = 0; i < 5; i++) if (!strncmp(s, ent[i].e, strlen(ent[i].e))) break;
            if (i < 5) { c = (unsigned char)ent[i].c; s += strlen(ent[i].e); }
            else if (s[1] == '#') {
                /* &#233; or &#xe9; */
                const char *q = s + 2;
                int hex = *q == 'x' || *q == 'X';
                c = (unsigned)strtol(q + hex, (char **)&q, hex ? 16 : 10);
                if (*q == ';') q++;
                if (c > 255) c = '?';
                s = q;
            } else s++;
        } else if ((c & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) { c = (c & 0x1f) << 6 | (s[1] & 0x3f); s += 2; if (c > 255) c = '?'; }
        else if (c >= 0x80) { s++; while ((*s & 0xc0) == 0x80) s++; c = '?'; }
        else s++;
        if (n < size - 1) out[n++] = (char)c;
    }
    out[n] = 0;
    x->p = s;
    ax_tag(x, tag, sizeof tag);
}

/* Past a value whose opening tag was just read. */
static void ax_skip(ax *x, const char *tag)
{
    char t[16], drop[8];
    int depth = 1;
    if (strcmp(tag, "dict") && strcmp(tag, "array")) {
        if (tag[strlen(tag) - 1] != '/') ax_text(x, drop, sizeof drop);
        return;
    }
    while (depth && ax_tag(x, t, sizeof t)) {
        if (!strcmp(t, "dict") || !strcmp(t, "array")) depth++;
        else if (!strcmp(t, "/dict") || !strcmp(t, "/array")) depth--;
    }
}

static void ax_value(ax *x, const char *tag, const char *key, od_dock *d, od_report *r, int *docks);

/* An Icons entry: one button. */
static void ax_icon(ax *x, od_dock *d, od_report *r)
{
    char t[16], key[32] = "", name[48] = "", file[256] = "";
    while (ax_tag(x, t, sizeof t) && strcmp(t, "/dict")) {
        if (!strcmp(t, "key")) ax_text(x, key, sizeof key);
        else if (!strcmp(t, "string") && !strcmp(key, "Name")) ax_text(x, name, sizeof name);
        else if (!strcmp(t, "string") && !strcmp(key, "FileName")) ax_text(x, file, sizeof file);
        else ax_skip(x, t);
    }
    if (!file[0]) { r->skipped++; return; }
    if (ends_with(file, ".docky")) {
        if (ends_with(file, "Separator.docky")) add_separator(d);
        else r->skipped++;
        return;
    }
    add_program(d, r, OD_WB, name, file);
}

static void ax_value(ax *x, const char *tag, const char *key, od_dock *d, od_report *r, int *docks)
{
    char t[16], k[32] = "";
    if (!strcmp(tag, "dict")) {
        while (ax_tag(x, t, sizeof t) && strcmp(t, "/dict")) {
            if (!strcmp(t, "key")) ax_text(x, k, sizeof k);
            else ax_value(x, t, k, d, r, docks);
        }
    } else if (!strcmp(tag, "array")) {
        while (ax_tag(x, t, sizeof t) && strcmp(t, "/array")) {
            if (!strcmp(key, "Docks") && !strcmp(t, "dict")) { if ((*docks)++) add_separator(d); ax_value(x, t, "", d, r, docks); }
            else if (!strcmp(key, "Icons") && !strcmp(t, "dict")) ax_icon(x, d, r);
            else if (!strcmp(key, "Categories") && !strcmp(t, "dict")) ax_value(x, t, "", d, r, docks);
            else ax_skip(x, t);
        }
    } else ax_skip(x, tag);
}

int od_from_amidock(od_dock *d, const char *text, od_report *r, char *err, int errlen)
{
    ax x;
    char t[16];
    int docks = 0;
    od_report dummy;
    if (!r) r = &dummy;
    memset(r, 0, sizeof *r);
    if (!text || od_sniff((const unsigned char *)text, (long)strlen(text)) != 2) {
        snprintf(err, errlen, "it isn't AmiDock's settings from AmigaOS 4 (AmigaOS 3.9's AmiDock keeps them in another form)");
        return 0;
    }
    x.p = text;
    while (ax_tag(&x, t, sizeof t) && strcmp(t, "dict")) ;
    if (strcmp(t, "dict")) { snprintf(err, errlen, "it holds no settings"); return 0; }
    {
        if (d->n) add_separator(d);
        ax_value(&x, "dict", "", d, r, &docks);
    }
    end_separator(d);
    if (!docks) { snprintf(err, errlen, "it has no docks"); return 0; }
    if (!r->added) { snprintf(err, errlen, "its docks have no programs to start"); return 0; }
    return 1;
}

/* ---- AmiStart ----------------------------------------------------------------------------
 *
 * AmiStart's sm.prefs (the PREFS tooltype says where): text, one setting a
 * line, "KEYWORD KEY="value" FLAG ...". Menus are NEWDIR NAME="<name>" ...
 * ITEM ... ENDDIR blocks; TASKBAR is the bar's own buttons, MAIN the start
 * menu. An ITEM starts a program with FILE= (EXECMODE 0 as Workbench does,
 * 1 from a Shell, with ARG= the arguments in hex), or opens a submenu
 * (DIR=), a drawer (SYSDIR) or a module (EXTERNAL), which are left out.
 * ICON= is an icon without .info, relative to AmiStart's drawer when it has
 * no volume. The taskbar's buttons are taken; with none, the start menu's
 * programs. */

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void as_item(const char *s, const char *base, od_dock *d, od_report *r)
{
    char tok[300], name[48] = "", icon[160] = "", file[256] = "", arg[128] = "";
    int mode = 0, other = 0, have_name = 0;
    long stack = 4096;
    while (word(&s, tok, sizeof tok)) {
        char *eq = strchr(tok, '=');
        const char *v;
        char val[256];
        if (!eq) {
            if (same(tok, "SYSDIR") || same(tok, "EXTERNAL") || same(tok, "QUIT")) other = 1;
            continue;
        }
        *eq = 0;
        v = eq + 1;
        /* KEY="value" arrives as one word: the quotes are inside it */
        if (*v == '"') {
            int n = 0;
            v++;
            while (*v && *v != '"' && n < (int)sizeof val - 1) val[n++] = *v++;
            val[n] = 0;
            if (*v != '"') {
                /* a value with spaces: the rest of it is in the next words */
                while (*s && *s != '"' && *s != '\n' && n < (int)sizeof val - 1) val[n++] = *s++;
                if (*s == '"') s++;
                val[n] = 0;
            }
        } else copy(val, v, sizeof val);
        /* NAME comes twice on a SYSDIR item: the first is the label */
        if (same(tok, "NAME") && !have_name) { copy(name, val, sizeof name); have_name = 1; }
        else if (same(tok, "ICON")) copy(icon, val, sizeof icon);
        else if (same(tok, "FILE")) copy(file, val, sizeof file);
        else if (same(tok, "ARG")) {
            int n = 0;
            for (const char *h = val; h[0] && h[1] && n < (int)sizeof arg - 1; h += 2) {
                int a = hexval(h[0]), b = hexval(h[1]);
                if (a < 0 || b < 0) break;
                arg[n++] = (char)(a << 4 | b);
            }
            arg[n] = 0;
        } else if (same(tok, "EXECMODE")) mode = atoi(val);
        else if (same(tok, "STACK")) stack = atol(val);
        else if (same(tok, "DIR")) other = 1;
    }
    if (other || !file[0] || mode > 1) { r->skipped++; return; }
    {
        char cmd[400];
        od_button *b;
        if (mode == 1 && arg[0]) snprintf(cmd, sizeof cmd, "%s %s", file, arg);
        else copy(cmd, file, sizeof cmd);
        if (!(b = add_program(d, r, mode == 1 ? OD_CLI : OD_WB, name, cmd))) return;
        if (stack > 4096 && stack < 1024 * 1024) b->stack = stack;
        if (icon[0] && strcmp(icon, file)) {
            if (!strchr(icon, ':') && base && base[0]) {
                size_t bl = strlen(base);
                snprintf(b->icon, sizeof b->icon, "%s%s%s", base, base[bl - 1] == ':' || base[bl - 1] == '/' ? "" : "/", icon);
            } else copy(b->icon, icon, sizeof b->icon);
        }
    }
}

/* The ITEM lines of one NEWDIR block; the number of them. */
static int as_block(const char *text, const char *want, const char *base, od_dock *d, od_report *r)
{
    const char *line = text;
    int in = 0, items = 0;
    char head[40];
    while (*line) {
        const char *s = line, *next = strchr(line, '\n');
        char tok[300];
        if (word(&s, tok, sizeof tok)) {
            if (same(tok, "NEWDIR")) {
                snprintf(head, sizeof head, "NAME=\"%s\"", want);
                in = word(&s, tok, sizeof tok) && same(tok, head);
            } else if (same(tok, "ENDDIR")) in = 0;
            else if (in && same(tok, "ITEM")) { as_item(s, base, d, r); items++; }
        }
        if (!next) break;
        line = next + 1;
    }
    return items;
}

int od_from_amistart(od_dock *d, const char *text, const char *base, od_report *r, char *err, int errlen)
{
    int before = d->n;
    od_report dummy;
    if (!r) r = &dummy;
    memset(r, 0, sizeof *r);
    if (!text || od_sniff((const unsigned char *)text, (long)strlen(text)) != 3) {
        snprintf(err, errlen, "it isn't AmiStart's settings file");
        return 0;
    }
    if (d->n) add_separator(d);
    as_block(text, "TASKBAR", base, d, r);
    if (!r->added) as_block(text, "MAIN", base, d, r);
    end_separator(d);
    if (!r->added) {
        d->n = before;
        snprintf(err, errlen, "its taskbar and start menu have no programs to start");
        return 0;
    }
    return 1;
}
