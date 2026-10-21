/* dp_prefs: screenmode.prefs and overscan.prefs, the fields the Screen mode section edits (dp_prefs.h).
 * The offsets are those of OpenNDK's prefs/screenmode.h and prefs/overscan.h, big-endian and unpadded.
 * Plain C: tested on the host by Display/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "dp_prefs.h"

#include <stdlib.h>
#include <string.h>

/* Chunk i's bytes as a copy at least `len` long (zeros past its end), or a new zeroed one when i is -1 */
static uint8_t *working_copy(const pf_file *f, int i, uint32_t len, uint32_t *outlen)
{
    uint32_t have = i >= 0 ? f->ch[i].len : 0, n = have > len ? have : len;
    uint8_t *p = malloc(n);
    if (!p) return NULL;
    memset(p, 0, n);
    if (have) memcpy(p, f->ch[i].data, have);
    *outlen = n;
    return p;
}

/* p (n bytes) into chunk i, or a new chunk of that ID at the end; p freed */
static int store(pf_file *f, int i, uint32_t id, uint8_t *p, uint32_t n)
{
    int ok = i >= 0 ? pf_set(f, i, p, n) : pf_add(f, id, p, n);
    free(p);
    return ok;
}

/* ---- screenmode.prefs ------------------------------------------------------------------------------- */

void dp_mode_defaults(dp_mode *m)
{
    m->id = DP_NOFILE_ID;
    m->width = m->height = DP_SIZE_DEFAULT;
    m->depth = 2;
    m->autoscroll = 0;
}

int dp_mode_get(const pf_file *f, dp_mode *out)
{
    int i = pf_find(f, DP_SCRM, 0);
    const uint8_t *d;
    dp_mode_defaults(out);
    if (i < 0 || f->ch[i].len < DP_SCRM_LEN) return 0;
    d = f->ch[i].data;
    out->id = pf_get32(d + 16);
    out->width = pf_get16(d + 20);
    out->height = pf_get16(d + 22);
    out->depth = pf_get16(d + 24);
    out->autoscroll = (pf_get16(d + 26) & DP_SMF_AUTOSCROLL) != 0;
    return 1;
}

int dp_mode_put(pf_file *f, const dp_mode *in)
{
    int i = pf_find(f, DP_SCRM, 0);
    uint32_t n;
    uint8_t *p = working_copy(f, i, DP_SCRM_LEN, &n);
    if (!p) return 0;
    pf_put32(p + 16, in->id);
    pf_put16(p + 20, (uint16_t)in->width);
    pf_put16(p + 22, (uint16_t)in->height);
    pf_put16(p + 24, (uint16_t)in->depth);
    pf_put16(p + 26, (uint16_t)((pf_get16(p + 26) & ~DP_SMF_AUTOSCROLL) | (in->autoscroll ? DP_SMF_AUTOSCROLL : 0)));
    return store(f, i, DP_SCRM, p, n);
}

/* ---- overscan.prefs --------------------------------------------------------------------------------- */

/* The chipset's monitors are graphics/modeid.h's: the default monitor (0), NTSC, PAL, VGA, A2024, Proto,
 * Euro72, Euro36, Super72, DblNTSC and DblPAL, $00001000 to $000A1000 in the monitor part. An RTG board's
 * modes are outside that range and have no overscan to set. */
int dp_native(uint32_t id)
{
    return id != 0xffffffffUL && (id & DP_MONITOR_MASK) <= 0x000a1000UL;
}

int dp_oscan_find(const pf_file *f, uint32_t id)
{
    int i;
    for (i = 0; i < f->n; i++)
        if (f->ch[i].id == DP_OSCN && f->ch[i].len >= DP_OSCN_LEN &&
            (pf_get32(f->ch[i].data + 16) & DP_MONITOR_MASK) == (id & DP_MONITOR_MASK)) return i;
    return -1;
}

int dp_oscan_get(const pf_file *f, uint32_t id, dp_oscan *out)
{
    int i = dp_oscan_find(f, id);
    const uint8_t *d;
    memset(out, 0, sizeof *out);
    out->id = id;
    if (i < 0) return 0;
    d = f->ch[i].data;
    out->hstart = pf_get16(d + 8);
    out->hstop = pf_get16(d + 10);
    out->vstart = pf_get16(d + 12);
    out->vstop = pf_get16(d + 14);
    out->id = pf_get32(d + 16);
    out->viewx = (int16_t)pf_get16(d + 20);
    out->viewy = (int16_t)pf_get16(d + 22);
    out->textw = (int16_t)pf_get16(d + 24);
    out->texth = (int16_t)pf_get16(d + 26);
    out->std.minx = (int16_t)pf_get16(d + 28);
    out->std.miny = (int16_t)pf_get16(d + 30);
    out->std.maxx = (int16_t)pf_get16(d + 32);
    out->std.maxy = (int16_t)pf_get16(d + 34);
    return 1;
}

int dp_oscan_put(pf_file *f, const dp_oscan *in)
{
    int i = dp_oscan_find(f, in->id);
    uint32_t n;
    uint8_t *p = working_copy(f, i, DP_OSCN_LEN, &n);
    if (!p) return 0;
    pf_put32(p + 4, DP_OSCAN_MAGIC);
    pf_put16(p + 8, (uint16_t)in->hstart);
    pf_put16(p + 10, (uint16_t)in->hstop);
    pf_put16(p + 12, (uint16_t)in->vstart);
    pf_put16(p + 14, (uint16_t)in->vstop);
    pf_put32(p + 16, in->id);
    pf_put16(p + 20, (uint16_t)in->viewx);
    pf_put16(p + 22, (uint16_t)in->viewy);
    pf_put16(p + 24, (uint16_t)in->textw);
    pf_put16(p + 26, (uint16_t)in->texth);
    pf_put16(p + 28, (uint16_t)in->std.minx);
    pf_put16(p + 30, (uint16_t)in->std.miny);
    pf_put16(p + 32, (uint16_t)in->std.maxx);
    pf_put16(p + 34, (uint16_t)in->std.maxy);
    return store(f, i, DP_OSCN, p, n);
}

void dp_oscan_remove(pf_file *f, uint32_t id)
{
    int i;
    while ((i = dp_oscan_find(f, id)) >= 0) pf_remove(f, i);
}
