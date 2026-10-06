/* sp_core: see sp_core.h. MIT, Copyright (c) 2026 Dalsin Limited. */
#include "sp_core.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SOND_SIZE 284                    /* struct SoundPrefs (prefs/sound.h) */
#define AHIG_SIZE 22                     /* struct AHIGlobalPrefs (devices/ahi.h, AHI 6) */
#define AHIU_SIZE 32                     /* struct AHIUnitPrefs */
#define FIXED_ONE 0x10000UL

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

void sp_defaults(sp_settings *s)
{
    memset(s, 0, sizeof *s);
    s->volume = s->paula = s->ahi = 100;
    s->mix = SP_MIX_PC;
    s->speaker = 1;
    s->wheel = 5;
    s->beep = SP_BEEP_FLASH;             /* the OS's own default: the screen flashes */
    s->beep_volume = 64;
    s->beep_period = 428;
    s->beep_duration = 10;
    s->clip = 0;
    s->anticlick = 0;
    s->maxcpu = 90;
}

/* ---- ENV:OpenAmiga/Sound ------------------------------------------------------------- */

static const char *word(const char **pp, char *out, int size)
{
    const char *s = *pp;
    int n = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\n' || *s == '\r' || *s == ';') { out[0] = 0; *pp = s; return NULL; }
    while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' && n < size - 1) out[n++] = *s++;
    out[n] = 0;
    *pp = s;
    return out;
}

static int onoff(const char *w, int def)
{
    if (!strcmp(w, "on") || !strcmp(w, "yes") || !strcmp(w, "1")) return 1;
    if (!strcmp(w, "off") || !strcmp(w, "no") || !strcmp(w, "0")) return 0;
    return def;
}

int sp_parse(sp_settings *s, const char *text)
{
    const char *p = text;
    if (!text) return 0;
    while (*p) {
        char key[32], val[32];
        if (word(&p, key, sizeof key) && word(&p, val, sizeof val)) {
            int n = atoi(val);
            if (!strcmp(key, "volume")) s->volume = clamp(n, 0, 100);
            else if (!strcmp(key, "paula")) s->paula = clamp(n, 0, 100);
            else if (!strcmp(key, "ahi")) s->ahi = clamp(n, 0, 100);
            else if (!strcmp(key, "mute")) s->muted = onoff(val, s->muted);
            else if (!strcmp(key, "mix")) s->mix = !strcmp(val, "amiga") ? SP_MIX_AMIGA : !strcmp(val, "pc") ? SP_MIX_PC : s->mix;
            else if (!strcmp(key, "speaker")) s->speaker = onoff(val, s->speaker);
            else if (!strcmp(key, "wheel")) s->wheel = clamp(n, 1, 25);
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 1;
}

int sp_write(const sp_settings *s, char *out, int size)
{
    int n = snprintf(out, (size_t)size,
                     "; OpenPrefs Sound, format 1\n"
                     "volume %d\npaula %d\nahi %d\nmute %s\nmix %s\nspeaker %s\nwheel %d\n",
                     s->volume, s->paula, s->ahi, s->muted ? "on" : "off", s->mix == SP_MIX_AMIGA ? "amiga" : "pc",
                     s->speaker ? "on" : "off", s->wheel);
    return n < 0 || n >= size ? -1 : n;
}

/* ---- IFF ---------------------------------------------------------------------------------- */

/* The chunks of a FORM PREF: calls each with its ID, data and length. 1, or 0 when it isn't one. */
typedef void (*chunk_fn)(void *ctx, uint32_t id, const uint8_t *data, uint32_t len);

static int each_chunk(const uint8_t *d, long len, chunk_fn fn, void *ctx)
{
    long at, end;
    if (!d || len < 12 || get32(d) != 0x464F524DUL || get32(d + 8) != 0x50524546UL) return 0;   /* FORM, PREF */
    end = 8 + (long)get32(d + 4);
    if (end > len) end = len;
    for (at = 12; at + 8 <= end; ) {
        uint32_t id = get32(d + at), n = get32(d + at + 4);
        if ((long)n > end - at - 8) break;
        fn(ctx, id, d + at + 8, n);
        at += 8 + (long)n + (long)(n & 1);
    }
    return 1;
}

static uint8_t *chunk(uint8_t *p, const char *id, const uint8_t *data, uint32_t n)
{
    memcpy(p, id, 4);
    put32(p + 4, n);
    memcpy(p + 8, data, n);
    if (n & 1) p[8 + n] = 0;
    return p + 8 + n + (n & 1);
}

/* ---- sound.prefs -------------------------------------------------------------------------- */

static void sond_chunk(void *ctx, uint32_t id, const uint8_t *d, uint32_t n)
{
    sp_settings *s = ctx;
    int flash, sound;
    if (id != 0x534F4E44UL || n < 28) return;                      /* SOND */
    flash = get16(d + 16) != 0;
    sound = get16(d + 18) != 0;
    s->beep = flash && sound ? SP_BEEP_BOTH : flash ? SP_BEEP_FLASH : sound ? SP_BEEP_SOUND : SP_BEEP_NONE;
    s->beep_sample = get16(d + 20) == 1;
    s->beep_volume = clamp(get16(d + 22), 0, 64);
    s->beep_period = clamp(get16(d + 24), 124, 1000);
    s->beep_duration = clamp(get16(d + 26), 1, 100);
    if (n >= SOND_SIZE) { memcpy(s->sample, d + 28, 255); s->sample[255] = 0; }
    s->sample[sizeof s->sample - 1] = 0;
}

int sp_read_sound_prefs(sp_settings *s, const uint8_t *data, long len)
{
    return each_chunk(data, len, sond_chunk, s);
}

long sp_write_sound_prefs(const sp_settings *s, uint8_t *out)
{
    uint8_t prhd[6] = { 0 }, sond[SOND_SIZE];
    uint8_t *p = out + 12;
    memset(sond, 0, sizeof sond);
    put16(sond + 16, s->beep == SP_BEEP_FLASH || s->beep == SP_BEEP_BOTH);
    put16(sond + 18, s->beep == SP_BEEP_SOUND || s->beep == SP_BEEP_BOTH);
    put16(sond + 20, s->beep_sample ? 1 : 0);
    put16(sond + 22, (uint16_t)clamp(s->beep_volume, 0, 64));
    put16(sond + 24, (uint16_t)clamp(s->beep_period, 124, 1000));
    put16(sond + 26, (uint16_t)clamp(s->beep_duration, 1, 100));
    { size_t k = strlen(s->sample); memcpy(sond + 28, s->sample, k > 255 ? 255 : k); }   /* NUL-filled already */
    p = chunk(p, "PRHD", prhd, sizeof prhd);
    p = chunk(p, "SOND", sond, sizeof sond);
    memcpy(out, "FORM", 4);
    put32(out + 4, (uint32_t)(p - out - 8));
    memcpy(out + 8, "PREF", 4);
    return (long)(p - out);
}

/* ---- ahi.prefs ---------------------------------------------------------------------------- */

static int unit_slot(int unit) { return unit >= 0 && unit < 4 ? unit : unit == SP_MUSIC_UNIT ? 4 : -1; }
static int slot_unit(int slot) { return slot < 4 ? slot : SP_MUSIC_UNIT; }

static void ahi_chunk(void *ctx, uint32_t id, const uint8_t *d, uint32_t n)
{
    sp_settings *s = ctx;
    if (id == 0x41484947UL && n >= 20) {                           /* AHIG */
        s->maxcpu = clamp((int)((get32(d + 8) * 100 + FIXED_ONE / 2) / FIXED_ONE), 10, 100);
        s->clip = get16(d + 12) != 0;
        s->anticlick = clamp((int)((get32(d + 16) * 1000 + FIXED_ONE / 2) / FIXED_ONE), 0, 100);
        s->have_ahi_prefs = 1;
    } else if (id == 0x41484955UL && n >= AHIU_SIZE) {              /* AHIU */
        int slot = unit_slot(d[0]);
        if (slot < 0) return;
        s->unit[slot].channels = get16(d + 2);
        s->unit[slot].mode = get32(d + 4);
        s->unit[slot].freq = get32(d + 8);
        s->unit[slot].monitor = get32(d + 12);
        s->unit[slot].gain = get32(d + 16);
        s->unit[slot].out_volume = get32(d + 20);
        s->unit[slot].input = get32(d + 24);
        s->unit[slot].output = get32(d + 28);
        s->have_ahi_prefs = 1;
    }
}

int sp_read_ahi_prefs(sp_settings *s, const uint8_t *data, long len)
{
    return each_chunk(data, len, ahi_chunk, s);
}

static void put_ahig(const sp_settings *s, uint8_t *d)
{
    put32(d + 8, (uint32_t)clamp(s->maxcpu, 10, 100) * FIXED_ONE / 100);
    put16(d + 12, s->clip ? 1 : 0);
    put32(d + 16, (uint32_t)clamp(s->anticlick, 0, 100) * FIXED_ONE / 1000);
}

static void put_ahiu(const sp_unit *u, int unit, uint8_t *d)
{
    d[0] = (uint8_t)unit;
    d[1] = 0;
    put16(d + 2, (uint16_t)(u->channels ? u->channels : 8));
    put32(d + 4, u->mode);
    put32(d + 8, u->freq ? u->freq : 44100);
    put32(d + 12, u->monitor);
    put32(d + 16, u->gain);
    put32(d + 20, u->out_volume);
    put32(d + 24, u->input);
    put32(d + 28, u->output);
}

struct rewrite {
    const sp_settings *s;
    uint8_t *p, *end;
    int done[SP_UNITS], ahig, overflow;
};

static void copy_chunk(void *ctx, uint32_t id, const uint8_t *d, uint32_t n)
{
    struct rewrite *r = ctx;
    uint8_t idb[5], buf[256];
    put32(idb, id); idb[4] = 0;
    if (r->overflow) return;
    if ((long)(8 + n + (n & 1)) > r->end - r->p) { r->overflow = 1; return; }
    if (id == 0x41484947UL && n >= 20 && n <= sizeof buf) {
        memcpy(buf, d, n); put_ahig(r->s, buf); d = buf; r->ahig = 1;
    } else if (id == 0x41484955UL && n >= AHIU_SIZE && n <= sizeof buf) {
        int slot = unit_slot(d[0]);
        if (slot >= 0 && r->s->unit[slot].mode && !r->done[slot]) {
            memcpy(buf, d, n); put_ahiu(&r->s->unit[slot], d[0], buf); d = buf; r->done[slot] = 1;
        } else if (slot >= 0 && r->done[slot]) return;             /* a second chunk for a unit: the first one counts */
    }
    r->p = chunk(r->p, (const char *)idb, d, n);
}

long sp_write_ahi_prefs(const sp_settings *s, const uint8_t *old, long oldlen, uint8_t *out, long size)
{
    struct rewrite r;
    uint8_t buf[AHIU_SIZE > AHIG_SIZE ? AHIU_SIZE : AHIG_SIZE];
    memset(&r, 0, sizeof r);
    if (size < 12 + 14 + 8 + AHIG_SIZE + SP_UNITS * (8 + AHIU_SIZE)) return -1;
    r.s = s; r.p = out + 12; r.end = out + size;
    if (!each_chunk(old, oldlen, copy_chunk, &r)) {
        uint8_t prhd[6] = { 0 };
        r.p = chunk(out + 12, "PRHD", prhd, sizeof prhd);
    }
    if (r.overflow) return -1;
    if (!r.ahig) {
        memset(buf, 0, sizeof buf);
        put_ahig(s, buf);
        if (r.end - r.p < 8 + AHIG_SIZE) return -1;
        r.p = chunk(r.p, "AHIG", buf, AHIG_SIZE);
    }
    for (int slot = 0; slot < SP_UNITS; slot++) {
        if (r.done[slot] || !s->unit[slot].mode) continue;
        if (r.end - r.p < 8 + AHIU_SIZE) return -1;
        put_ahiu(&s->unit[slot], slot_unit(slot), buf);
        r.p = chunk(r.p, "AHIU", buf, AHIU_SIZE);
    }
    memcpy(out, "FORM", 4);
    put32(out + 4, (uint32_t)(r.p - out - 8));
    memcpy(out + 8, "PREF", 4);
    return (long)(r.p - out);
}

/* ---- the mix, the levels ------------------------------------------------------------------ */

void sp_apply_mix(sp_settings *s)
{
    uint32_t mode = s->mix == SP_MIX_AMIGA ? SP_MODE_HIFI : SP_MODE_HOSTMIX;
    int any = 0;
    for (int i = 0; i < SP_UNITS; i++) if (SP_IS_ACAHI_MODE(s->unit[i].mode)) any = 1;
    for (int i = 0; i < SP_UNITS; i++) {
        sp_unit *u = &s->unit[i];
        if (any && !SP_IS_ACAHI_MODE(u->mode)) continue;            /* a unit on another card stays there */
        if (!SP_IS_ACAHI_MODE(u->mode)) {                           /* a first start: the unit is new */
            u->gain = FIXED_ONE;
            u->out_volume = FIXED_ONE;
        }
        u->mode = mode;
        if (mode == SP_MODE_HOSTMIX) { u->freq = 44100; if (!u->channels) u->channels = 16; }
        else { if (!u->freq) u->freq = 44100; if (!u->channels) u->channels = 8; }
    }
}

int sp_mix_of(const sp_settings *s)
{
    for (int i = 0; i < SP_UNITS; i++) if (s->unit[i].mode == SP_MODE_HOSTMIX) return SP_MIX_PC;
    for (int i = 0; i < SP_UNITS; i++) if (SP_IS_ACAHI_MODE(s->unit[i].mode)) return SP_MIX_AMIGA;
    return s->mix;
}

uint32_t sp_levels_word(const sp_settings *s)
{
    return (uint32_t)clamp(s->volume, 0, 100) | (uint32_t)clamp(s->paula, 0, 100) << 8 | (uint32_t)clamp(s->ahi, 0, 100) << 16
           | (s->muted ? 1UL << 24 : 0);
}

void sp_from_levels_word(sp_settings *s, uint32_t w)
{
    s->volume = clamp((int)(w & 0xff), 0, 100);
    s->paula = clamp((int)(w >> 8 & 0xff), 0, 100);
    s->ahi = clamp((int)(w >> 16 & 0xff), 0, 100);
    s->muted = (w >> 24 & 1) != 0;
}

void sp_volume_to_units(sp_settings *s)
{
    uint32_t v = s->muted ? 0 : (uint32_t)clamp(s->volume, 0, 100) * FIXED_ONE / 100;
    for (int i = 0; i < SP_UNITS; i++) if (s->unit[i].mode) s->unit[i].out_volume = v;
}
