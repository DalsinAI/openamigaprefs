/* pf_iff: an OS preferences file (IFF FORM PREF) as the list of its chunks (pf_iff.h).
 * Plain C: tested on the host by Look/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "pf_iff.h"

#include <stdlib.h>
#include <string.h>

uint16_t pf_get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t pf_get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
void pf_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void pf_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

void pf_init(pf_file *f) { memset(f, 0, sizeof *f); }

void pf_free(pf_file *f)
{
    int i;
    for (i = 0; i < f->n; i++) free(f->ch[i].data);
    pf_init(f);
}

/* len bytes copied from data (zeros when data is NULL); NULL with no memory */
static uint8_t *dup_bytes(const void *data, uint32_t len)
{
    uint8_t *p;
    if (!len) return NULL;
    if (!(p = malloc(len))) return NULL;
    if (data) memcpy(p, data, len); else memset(p, 0, len);
    return p;
}

int pf_insert(pf_file *f, int at, uint32_t id, const void *data, uint32_t len)
{
    uint8_t *p = NULL;
    if (f->n >= PF_MAX_CHUNKS || at < 0 || at > f->n || len > (uint32_t)PF_MAX_BYTES) return 0;
    if (len && !(p = dup_bytes(data, len))) return 0;
    memmove(&f->ch[at + 1], &f->ch[at], (size_t)(f->n - at) * sizeof f->ch[0]);
    f->ch[at].id = id;
    f->ch[at].len = len;
    f->ch[at].data = p;
    f->n++;
    return 1;
}

int pf_add(pf_file *f, uint32_t id, const void *data, uint32_t len) { return pf_insert(f, f->n, id, data, len); }

int pf_new(pf_file *f)
{
    static const uint8_t prhd[6] = { 0, 0, 0, 0, 0, 0 };     /* version 0, type 0, flags 0, as the OS editors write it */
    pf_free(f);
    return pf_add(f, PF_PRHD, prhd, sizeof prhd);
}

int pf_set(pf_file *f, int i, const void *data, uint32_t len)
{
    uint8_t *p = NULL;
    if (i < 0 || i >= f->n || len > (uint32_t)PF_MAX_BYTES) return 0;
    if (len && !(p = dup_bytes(data, len))) return 0;
    free(f->ch[i].data);
    f->ch[i].data = p;
    f->ch[i].len = len;
    return 1;
}

void pf_remove(pf_file *f, int i)
{
    if (i < 0 || i >= f->n) return;
    free(f->ch[i].data);
    memmove(&f->ch[i], &f->ch[i + 1], (size_t)(f->n - i - 1) * sizeof f->ch[0]);
    f->n--;
    memset(&f->ch[f->n], 0, sizeof f->ch[0]);
}

int pf_find(const pf_file *f, uint32_t id, int nth)
{
    int i;
    for (i = 0; i < f->n; i++)
        if (f->ch[i].id == id && nth-- == 0) return i;
    return -1;
}

int pf_copy(pf_file *dst, const pf_file *src)
{
    int i;
    pf_free(dst);
    for (i = 0; i < src->n; i++)
        if (!pf_add(dst, src->ch[i].id, src->ch[i].data, src->ch[i].len)) { pf_free(dst); return 0; }
    return 1;
}

int pf_same(const pf_file *a, const pf_file *b)
{
    int i;
    if (a->n != b->n) return 0;
    for (i = 0; i < a->n; i++)
        if (a->ch[i].id != b->ch[i].id || a->ch[i].len != b->ch[i].len ||
            (a->ch[i].len && memcmp(a->ch[i].data, b->ch[i].data, a->ch[i].len)))
            return 0;
    return 1;
}

/* an IFF ID: four printable characters, the first not a space */
static int good_id(uint32_t id)
{
    int k;
    if ((id >> 24) == ' ') return 0;
    for (k = 0; k < 4; k++) {
        unsigned c = (id >> (24 - 8 * k)) & 0xff;
        if (c < 0x20 || c > 0x7e) return 0;
    }
    return 1;
}

int pf_parse(pf_file *f, const uint8_t *buf, long len)
{
    long end, i;
    uint32_t size;
    pf_free(f);
    if (!buf || len < 12 || pf_get32(buf) != PF_FORM || pf_get32(buf + 8) != PF_PREF) return 0;
    size = pf_get32(buf + 4);
    if (size < 4 || size > (uint32_t)PF_MAX_BYTES || (long)size + 8 > len) return 0;
    end = 8 + (long)size;
    for (i = 12; i < end; ) {
        uint32_t id, n;
        if (i + 8 > end) { pf_free(f); return 0; }
        id = pf_get32(buf + i);
        n = pf_get32(buf + i + 4);
        if (!good_id(id) || id == PF_FORM || id == PF_ID('L', 'I', 'S', 'T') || id == PF_ID('C', 'A', 'T', ' ') ||
            id == PF_ID('P', 'R', 'O', 'P') || n > (uint32_t)(end - i - 8)) {
            pf_free(f);
            return 0;
        }
        if (!pf_add(f, id, buf + i + 8, n)) { pf_free(f); return 0; }
        i += 8 + (long)n + (long)(n & 1);
    }
    return 1;
}

long pf_size(const pf_file *f)
{
    long s = 12;
    int i;
    for (i = 0; i < f->n; i++) s += 8 + (long)f->ch[i].len + (long)(f->ch[i].len & 1);
    return s;
}

long pf_write(const pf_file *f, uint8_t *out, long size)
{
    long need = pf_size(f), at = 12;
    int i;
    if (!out || size < need) return -1;
    pf_put32(out, PF_FORM);
    pf_put32(out + 4, (uint32_t)(need - 8));
    pf_put32(out + 8, PF_PREF);
    for (i = 0; i < f->n; i++) {
        const pf_chunk *c = &f->ch[i];
        pf_put32(out + at, c->id);
        pf_put32(out + at + 4, c->len);
        if (c->len) memcpy(out + at + 8, c->data, c->len);
        at += 8 + (long)c->len;
        if (c->len & 1) out[at++] = 0;                         /* IFF's pad byte */
    }
    return at;
}
