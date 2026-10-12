/* pf_prefs: the fields of the OS 3.2 preferences files the Appearance tabs edit (pf_prefs.h).
 * The offsets are those of the NDK 3.2 structures, which every file keeps big-endian and unpadded.
 * Plain C: tested on the host by Look/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "pf_prefs.h"

#include <stdlib.h>
#include <string.h>

/* The chunk with that ID whose 16-bit field at `off` is `v` (the type or "which" of fonts, patterns and
 * pointers), or -1. */
static int index_of(const pf_file *f, uint32_t id, int off, int v)
{
    int i;
    for (i = 0; i < f->n; i++)
        if (f->ch[i].id == id && f->ch[i].len >= (uint32_t)off + 2 && pf_get16(f->ch[i].data + off) == (uint16_t)v) return i;
    return -1;
}

/* Where a new chunk of that ID and kind goes: before the first of its ID with a kind after it (by `up`,
 * ascending, or descending), else after the last of its ID, else at the end. */
static int place_for(const pf_file *f, uint32_t id, int off, int v, int up)
{
    int i, last = -1;
    for (i = 0; i < f->n; i++) {
        int k;
        if (f->ch[i].id != id || f->ch[i].len < (uint32_t)off + 2) continue;
        k = pf_get16(f->ch[i].data + off);
        if (up ? k > v : k < v) return i;
        last = i;
    }
    return last >= 0 ? last + 1 : f->n;
}

/* A copy of chunk i's bytes at least `len` long (zeros past its end), for patching; NULL with no memory. */
static uint8_t *working_copy(const pf_file *f, int i, uint32_t len, uint32_t *outlen)
{
    uint32_t have = f->ch[i].len, n = have > len ? have : len;
    uint8_t *p = malloc(n ? n : 1);
    if (!p) return NULL;
    memset(p, 0, n ? n : 1);
    if (have) memcpy(p, f->ch[i].data, have);
    *outlen = n;
    return p;
}

/* A C string field of `size` bytes at d: rewritten (zeros after the name) only when the name differs,
 * so a field whose bytes after the end were not zero stays as it was. */
static void put_name(uint8_t *d, int size, const char *name)
{
    char now[256];
    int k = 0;
    while (k < size && k < (int)sizeof now - 1 && d[k]) { now[k] = (char)d[k]; k++; }
    now[k] = 0;
    if (!strncmp(now, name, (size_t)size - 1) && (int)strlen(name) < size) return;
    memset(d, 0, (size_t)size);
    strncpy((char *)d, name, (size_t)size - 1);
}

static void get_name(const uint8_t *d, int size, char *out, int outsize)
{
    int k = 0;
    while (k < size && k < outsize - 1 && d[k]) { out[k] = (char)d[k]; k++; }
    out[k] = 0;
}

/* Chunk i made `len` bytes long (or added at `at` when i < 0) and its bytes passed to the patch below. */
#define PATCH_BEGIN(f, i, id, at, minlen, d, n)                                        \
    do {                                                                                \
        if ((i) < 0) {                                                                  \
            int at_ = (at);                     /* worked out once, before the insert */ \
            if (!pf_insert((f), at_, (id), NULL, (minlen))) return 0;                   \
            (i) = at_;                                                                  \
        }                                                                               \
        if (!((d) = working_copy((f), (i), (minlen), &(n)))) return 0;                  \
    } while (0)
#define PATCH_END(f, i, d, n)                                                           \
    do {                                                                                \
        int ok_ = pf_set((f), (i), (d), (n));                                           \
        free(d);                                                                        \
        return ok_;                                                                     \
    } while (0)

/* ---- font.prefs ------------------------------------------------------------------------------------- */

int pf_font_get(const pf_file *f, int type, pf_font *out)
{
    int i = index_of(f, PF_FONT, 14, type);
    const uint8_t *d;
    memset(out, 0, sizeof *out);
    out->type = type;
    if (i < 0 || f->ch[i].len < PF_FONT_LEN) return 0;
    d = f->ch[i].data;
    out->front = d[16];
    out->back = d[17];
    out->drawmode = d[18];
    out->special = d[19];
    out->ysize = pf_get16(d + 24);
    out->style = d[26];
    out->flags = d[27];
    get_name(d + 28, 128, out->name, sizeof out->name);
    return 1;
}

int pf_font_put(pf_file *f, const pf_font *in)
{
    int i = index_of(f, PF_FONT, 14, in->type);
    uint8_t *d;
    uint32_t n;
    PATCH_BEGIN(f, i, PF_FONT, place_for(f, PF_FONT, 14, in->type, 1), PF_FONT_LEN, d, n);
    pf_put16(d + 14, (uint16_t)in->type);
    d[16] = (uint8_t)in->front;
    d[17] = (uint8_t)in->back;
    d[18] = (uint8_t)in->drawmode;
    d[19] = (uint8_t)in->special;
    pf_put16(d + 24, (uint16_t)in->ysize);
    d[26] = (uint8_t)in->style;
    d[27] = (uint8_t)in->flags;
    put_name(d + 28, 128, in->name);
    PATCH_END(f, i, d, n);
}

/* ---- palette.prefs ---------------------------------------------------------------------------------- */

void pf_palette_defaults(pf_palette *p)
{
    /* what OS 3.2's Palette writes when there is no file: the thirteen DrawInfo pens of 3.2 (detail, block,
     * text, shine, shadow, fill, fill text, background, highlight text, bar detail, bar block, bar trim, bar
     * contour) and the eight Workbench colours */
    static const uint16_t pens[14] = { 0, 1, 1, 2, 1, 3, 1, 0, 2, 1, 2, 1, 2, PF_PEN_END };
    static const uint16_t rgb[8][3] = {
        { 0xaaaa, 0xaaaa, 0xaaaa }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x6666, 0x8888, 0xbbbb },
        { 0xeeee, 0x4444, 0x4444 }, { 0x5555, 0xdddd, 0x5555 }, { 0x0000, 0x4444, 0xdddd }, { 0xeeee, 0x9999, 0x0000 }
    };
    int k;
    memset(p, 0, sizeof *p);
    memcpy(p->pens4, pens, sizeof pens);
    memcpy(p->pens8, pens, sizeof pens);
    for (k = 0; k < 8; k++) {
        p->col[k].index = (int16_t)k;
        p->col[k].r = rgb[k][0]; p->col[k].g = rgb[k][1]; p->col[k].b = rgb[k][2];
    }
    p->col[8].index = -1;
}

int pf_palette_get(const pf_file *f, pf_palette *out)
{
    int i = pf_find(f, PF_PALT, 0), k;
    const uint8_t *d;
    if (i < 0 || f->ch[i].len < PF_PALT_LEN) { pf_palette_defaults(out); return 0; }
    d = f->ch[i].data;
    for (k = 0; k < PF_PENS; k++) {
        out->pens4[k] = pf_get16(d + 16 + 2 * k);
        out->pens8[k] = pf_get16(d + 80 + 2 * k);
    }
    for (k = 0; k < PF_COLOURS; k++) {
        const uint8_t *c = d + 144 + 8 * k;
        out->col[k].index = (int16_t)pf_get16(c);
        out->col[k].r = pf_get16(c + 2);
        out->col[k].g = pf_get16(c + 4);
        out->col[k].b = pf_get16(c + 6);
    }
    return 1;
}

int pf_palette_put(pf_file *f, const pf_palette *in)
{
    int i = pf_find(f, PF_PALT, 0), k;
    uint8_t *d;
    uint32_t n;
    PATCH_BEGIN(f, i, PF_PALT, f->n, PF_PALT_LEN, d, n);
    for (k = 0; k < PF_PENS; k++) {
        pf_put16(d + 16 + 2 * k, in->pens4[k]);
        pf_put16(d + 80 + 2 * k, in->pens8[k]);
    }
    for (k = 0; k < PF_COLOURS; k++) {
        uint8_t *c = d + 144 + 8 * k;
        pf_put16(c, (uint16_t)in->col[k].index);
        pf_put16(c + 2, in->col[k].r);
        pf_put16(c + 4, in->col[k].g);
        pf_put16(c + 6, in->col[k].b);
    }
    PATCH_END(f, i, d, n);
}

int pf_palette_colour(const pf_palette *p, int index, uint8_t rgb[3])
{
    int k;
    for (k = 0; k < PF_COLOURS && p->col[k].index != -1; k++)
        if (p->col[k].index == index) {
            rgb[0] = (uint8_t)(p->col[k].r >> 8); rgb[1] = (uint8_t)(p->col[k].g >> 8); rgb[2] = (uint8_t)(p->col[k].b >> 8);
            return 1;
        }
    return 0;
}

int pf_palette_set_colour(pf_palette *p, int index, const uint8_t rgb[3])
{
    int k;
    for (k = 0; k < PF_COLOURS && p->col[k].index != -1; k++)
        if (p->col[k].index == index) break;
    if (k == PF_COLOURS || p->col[k].index == -1) {               /* a new one: it and the end after it */
        if (k >= PF_COLOURS - 1) return 0;
        p->col[k + 1].index = -1;
        p->col[k + 1].r = p->col[k + 1].g = p->col[k + 1].b = 0;
    }
    p->col[k].index = (int16_t)index;
    p->col[k].r = (uint16_t)(rgb[0] * 0x101);                     /* 8 bits to 16, as Palette writes them: 0xaa is 0xaaaa */
    p->col[k].g = (uint16_t)(rgb[1] * 0x101);
    p->col[k].b = (uint16_t)(rgb[2] * 0x101);
    return 1;
}

int pf_palette_pen(const uint16_t *pens, int pen)
{
    int k;
    if (pen < 0 || pen >= PF_PENS) return -1;
    for (k = 0; k <= pen; k++) if (pens[k] == PF_PEN_END) return -1;
    return pens[pen];
}

int pf_palette_set_pen(uint16_t *pens, int pen, int colour)
{
    if (pf_palette_pen(pens, pen) < 0 || colour < 0 || colour >= PF_PEN_END) return 0;
    pens[pen] = (uint16_t)colour;
    return 1;
}

/* ---- wbpattern.prefs -------------------------------------------------------------------------------- */

int pf_ptrn_get(const pf_file *f, int which, pf_ptrn *out)
{
    int i = index_of(f, PF_PTRN, 16, which);
    const uint8_t *d;
    uint32_t len;
    memset(out, 0, sizeof *out);
    out->which = which;
    if (i < 0 || (len = f->ch[i].len) < PF_PTRN_HEAD) return 0;
    d = f->ch[i].data;
    out->flags = pf_get16(d + 18);
    out->revision = (int8_t)d[20];
    out->depth = (int8_t)d[21];
    out->datalen = pf_get16(d + 22);
    out->picture = !(out->flags & PF_WBPF_PATTERN);
    if (out->picture) {
        uint32_t n = (uint32_t)out->datalen;
        if (n > len - PF_PTRN_HEAD) n = len - PF_PTRN_HEAD;
        get_name(d + PF_PTRN_HEAD, (int)n, out->name, sizeof out->name);
    }
    return 1;
}

int pf_ptrn_set_picture(pf_file *f, int which, const char *name, int flags)
{
    int i = index_of(f, PF_PTRN, 16, which);
    pf_ptrn now;
    uint32_t nl = (uint32_t)strlen(name), n = PF_PTRN_HEAD + nl;
    uint8_t *d;
    int ok;
    flags &= ~PF_WBPF_PATTERN;
    if (i >= 0 && pf_ptrn_get(f, which, &now) && now.picture && !strcmp(now.name, name))
        return pf_ptrn_set_flags(f, which, flags);            /* the same picture: its bytes as they were */
    if (!(d = calloc(1, n))) return 0;
    if (i >= 0) memcpy(d, f->ch[i].data, PF_PTRN_HEAD);       /* its reserved bytes and revision kept */
    pf_put16(d + 16, (uint16_t)which);
    pf_put16(d + 18, (uint16_t)flags);
    d[21] = 0;                                                /* a picture has no depth of its own */
    pf_put16(d + 22, (uint16_t)nl);                           /* WBPattern writes the name without its 0 */
    memcpy(d + PF_PTRN_HEAD, name, nl);
    if (i >= 0) ok = pf_set(f, i, d, n);
    else ok = pf_insert(f, place_for(f, PF_PTRN, 16, which, 1), PF_PTRN, d, n);
    free(d);
    return ok;
}

int pf_ptrn_set_flags(pf_file *f, int which, int flags)
{
    int i = index_of(f, PF_PTRN, 16, which);
    if (i < 0 || f->ch[i].len < PF_PTRN_HEAD) return 0;
    pf_put16(f->ch[i].data + 18, (uint16_t)flags);
    return 1;
}

void pf_ptrn_remove(pf_file *f, int which)
{
    int i;
    while ((i = index_of(f, PF_PTRN, 16, which)) >= 0) pf_remove(f, i);
}

/* ---- workbench.prefs -------------------------------------------------------------------------------- */

static void wbnc_defaults(pf_wbnc *w)
{
    /* what OS 3.2's Workbench editor starts from with no file */
    memset(w, 0, sizeof *w);
    w->stack = 4096;
    w->restart = 3;
    w->precision = 16;                                        /* PRECISION_ICON */
    w->emboss[0] = -3; w->emboss[1] = -3; w->emboss[2] = 3; w->emboss[3] = 3;
    w->maxname = 25;
    w->newicons = 1;
    w->coloricons = 1;
    w->titledelay = 50;
}

int pf_wbnc_get(const pf_file *f, pf_wbnc *out)
{
    int i = pf_find(f, PF_WBNC, 0), k;
    const uint8_t *d;
    if (i < 0 || f->ch[i].len < PF_WBNC_BASIC) { wbnc_defaults(out); return 0; }
    memset(out, 0, sizeof *out);
    d = f->ch[i].data;
    out->stack = pf_get32(d);
    out->restart = pf_get32(d + 4);
    out->precision = (int32_t)pf_get32(d + 8);
    for (k = 0; k < 4; k++) out->emboss[k] = (int16_t)pf_get16(d + 12 + 2 * k);
    out->borderless = pf_get16(d + 20);
    out->maxname = (int32_t)pf_get32(d + 22);
    out->newicons = pf_get16(d + 26);
    out->coloricons = pf_get16(d + 28);
    if (f->ch[i].len >= PF_WBNC_LEN) {
        out->memtype = pf_get32(d + 30);
        out->lockpens = pf_get16(d + 34);
        out->notitlebar = pf_get16(d + 36);
        out->nogauge = pf_get16(d + 38);
        out->titledelay = pf_get16(d + 40);
        out->copybuf = pf_get32(d + 42);
        out->flags = pf_get32(d + 46);
    }
    return 1;
}

int pf_wbnc_put(pf_file *f, const pf_wbnc *in)
{
    int i = pf_find(f, PF_WBNC, 0), k, at = pf_find(f, PF_PRHD, 0) + 1;
    uint32_t want = PF_WBNC_BASIC, n;
    uint8_t *d;
    /* a 30-byte chunk (before 3.9) grows only when one of the later fields is set */
    if (i < 0 || f->ch[i].len >= PF_WBNC_LEN || in->memtype || in->lockpens || in->notitlebar || in->nogauge ||
        in->titledelay || in->copybuf || in->flags)
        want = PF_WBNC_LEN;
    PATCH_BEGIN(f, i, PF_WBNC, at, want, d, n);
    pf_put32(d, in->stack);
    pf_put32(d + 4, in->restart);
    pf_put32(d + 8, (uint32_t)in->precision);
    for (k = 0; k < 4; k++) pf_put16(d + 12 + 2 * k, (uint16_t)in->emboss[k]);
    pf_put16(d + 20, (uint16_t)in->borderless);
    pf_put32(d + 22, (uint32_t)in->maxname);
    pf_put16(d + 26, (uint16_t)in->newicons);
    pf_put16(d + 28, (uint16_t)in->coloricons);
    if (n >= PF_WBNC_LEN) {
        pf_put32(d + 30, in->memtype);
        pf_put16(d + 34, (uint16_t)in->lockpens);
        pf_put16(d + 36, (uint16_t)in->notitlebar);
        pf_put16(d + 38, (uint16_t)in->nogauge);
        pf_put16(d + 40, in->titledelay);
        pf_put32(d + 42, in->copybuf);
        pf_put32(d + 46, in->flags);
    }
    PATCH_END(f, i, d, n);
}

int pf_wbtf_get(const pf_file *f, char *out, int size)
{
    int i = pf_find(f, PF_WBTF, 0);
    out[0] = 0;
    if (i < 0) return 0;
    get_name(f->ch[i].data, (int)f->ch[i].len, out, size);
    return 1;
}

int pf_wbtf_put(pf_file *f, const char *format)
{
    int i = pf_find(f, PF_WBTF, 0);
    char now[256];
    if (!*format) { if (i >= 0) pf_remove(f, i); return 1; }
    if (i >= 0 && pf_wbtf_get(f, now, sizeof now) && !strcmp(now, format)) return 1;
    if (i >= 0) return pf_set(f, i, format, (uint32_t)strlen(format) + 1);   /* a C string with its 0 */
    i = pf_find(f, PF_WBNC, 0);
    return pf_insert(f, i >= 0 ? i + 1 : f->n, PF_WBTF, format, (uint32_t)strlen(format) + 1);
}

/* ---- asl.prefs -------------------------------------------------------------------------------------- */

int pf_asl_get(const pf_file *f, pf_asl *out)
{
    int i = pf_find(f, PF_ASL, 0);
    const uint8_t *d;
    memset(out, 0, sizeof *out);
    if (i < 0 || f->ch[i].len < PF_ASL_LEN) {
        out->width = 40;                                      /* what OS 3.2's Asl editor starts from with no file */
        out->height = 80;
        return 0;
    }
    d = f->ch[i].data;
    out->sortby = d[16];
    out->sortdrawers = d[17];
    out->sortorder = d[18];
    out->sizepos = d[19];
    out->left = (int16_t)pf_get16(d + 20);
    out->top = (int16_t)pf_get16(d + 22);
    out->width = d[24];
    out->height = d[25];
    return 1;
}

int pf_asl_put(pf_file *f, const pf_asl *in)
{
    int i = pf_find(f, PF_ASL, 0);
    uint8_t *d;
    uint32_t n;
    PATCH_BEGIN(f, i, PF_ASL, f->n, PF_ASL_LEN, d, n);
    d[16] = (uint8_t)in->sortby;
    d[17] = (uint8_t)in->sortdrawers;
    d[18] = (uint8_t)in->sortorder;
    d[19] = (uint8_t)in->sizepos;
    pf_put16(d + 20, (uint16_t)in->left);
    pf_put16(d + 22, (uint16_t)in->top);
    d[24] = (uint8_t)in->width;
    d[25] = (uint8_t)in->height;
    PATCH_END(f, i, d, n);
}

/* ---- pointer.prefs ---------------------------------------------------------------------------------- */

static int pntr_read(const uint8_t *d, uint32_t len, pf_pntr *out)
{
    long need;
    memset(out, 0, sizeof *out);
    if (len < PF_PNTR_HEAD) return 0;
    out->which = pf_get16(d + 16);
    out->size = pf_get16(d + 18);
    out->width = pf_get16(d + 20);
    out->height = pf_get16(d + 22);
    out->depth = pf_get16(d + 24);
    out->ysize = pf_get16(d + 26);
    out->x = (int16_t)pf_get16(d + 28);
    out->y = (int16_t)pf_get16(d + 30);
    if (out->depth < 1 || out->depth > 8 || out->width < 1 || out->width > 256 || out->height < 1 || out->height > 256) return 0;
    out->ncolours = (1 << out->depth) - 1;
    need = PF_PNTR_HEAD + 3L * out->ncolours + (long)out->depth * out->height * ((out->width + 15) / 16) * 2;
    if ((long)len < need) return 0;
    out->rgb = d + PF_PNTR_HEAD;
    out->planes = d + PF_PNTR_HEAD + 3 * out->ncolours;
    return 1;
}

int pf_pntr_get(const pf_file *f, int which, pf_pntr *out)
{
    int i = index_of(f, PF_PNTR, 16, which);
    if (i < 0) { memset(out, 0, sizeof *out); return 0; }
    return pntr_read(f->ch[i].data, f->ch[i].len, out);
}

int pf_pntr_pixel(const pf_pntr *p, int x, int y)
{
    int bpr = ((p->width + 15) / 16) * 2, k, c = 0;
    if (!p->planes || x < 0 || y < 0 || x >= p->width || y >= p->height) return 0;
    for (k = 0; k < p->depth; k++) {
        const uint8_t *row = p->planes + ((long)k * p->height + y) * bpr;
        if (row[x >> 3] & (0x80 >> (x & 7))) c |= 1 << k;
    }
    return c;
}

int pf_pntr_set_hotspot(pf_file *f, int which, int x, int y)
{
    int i = index_of(f, PF_PNTR, 16, which);
    if (i < 0 || f->ch[i].len < PF_PNTR_HEAD) return 0;
    pf_put16(f->ch[i].data + 28, (uint16_t)x);
    pf_put16(f->ch[i].data + 30, (uint16_t)y);
    return 1;
}

int pf_pntr_take(pf_file *f, const pf_file *from, int which)
{
    int j = index_of(from, PF_PNTR, 16, which), i = index_of(f, PF_PNTR, 16, which);
    pf_pntr check;
    if (j < 0 || !pntr_read(from->ch[j].data, from->ch[j].len, &check)) return 0;
    if (i >= 0) return pf_set(f, i, from->ch[j].data, from->ch[j].len);
    /* OS 3.2 keeps them from the last type down to the normal pointer */
    return pf_insert(f, place_for(f, PF_PNTR, 16, which, 0), PF_PNTR, from->ch[j].data, from->ch[j].len);
}

int pf_pntr_take_all(pf_file *f, const pf_file *from)
{
    int at = pf_find(f, PF_PNTR, 0), i, any = 0;
    pf_file keep;
    for (i = 0; i < from->n; i++) if (from->ch[i].id == PF_PNTR) any = 1;
    if (!any) return 0;
    pf_init(&keep);
    if (!pf_copy(&keep, f)) return 0;                          /* f as it was, if memory runs out below */
    if (at < 0) at = f->n;
    while ((i = pf_find(f, PF_PNTR, 0)) >= 0) pf_remove(f, i);
    for (i = 0; i < from->n; i++)
        if (from->ch[i].id == PF_PNTR) {
            if (!pf_insert(f, at++, PF_PNTR, from->ch[i].data, from->ch[i].len)) { pf_copy(f, &keep); pf_free(&keep); return 0; }
        }
    pf_free(&keep);
    return 1;
}
