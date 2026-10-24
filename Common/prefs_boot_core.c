/* OpenIntuition: prefs_boot_core.h's readers. Copyright (c) 2026 AmigaChrome Team. MIT. */
#include "prefs_boot_core.h"

static int pbc_clamp(long v, long lo, long hi) { return (int)(v < lo ? lo : v > hi ? hi : v); }

static int pbc_blank(char c) { return c == ' ' || c == '\t'; }
static int pbc_eol(char c) { return c == '\n' || c == '\r' || c == 0; }

/* a number as atoi and sscanf's %ld read it: blanks, a sign, digits; 0
 * digits: 0 and *ok 0. Kept within a long's reach. */
static long pbc_num(const char **pp, const char *end, int *ok)
{
    const char *p = *pp;
    long v = 0;
    int neg = 0, any = 0;
    while (p < end && pbc_blank(*p)) p++;
    if (p < end && (*p == '-' || *p == '+')) neg = *p++ == '-';
    while (p < end && *p >= '0' && *p <= '9') {
        if (v < 100000000L) v = v * 10 + (*p - '0');
        p++;
        any = 1;
    }
    *pp = p;
    if (ok) *ok = any;
    return neg ? -v : v;
}

/* the word at p (after blanks) into out, up to a blank or the line's end; its length */
static int pbc_word(const char **pp, const char *end, char *out, int size)
{
    const char *p = *pp;
    int n = 0;
    while (p < end && pbc_blank(*p)) p++;
    while (p < end && !pbc_blank(*p) && !pbc_eol(*p)) {
        if (n < size - 1) out[n] = *p;
        n++;
        p++;
    }
    out[n < size ? n : size - 1] = 0;
    *pp = p;
    return n;
}

static int pbc_is(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* ---- ENV:OpenAmiga/Sound ---- */

pbc_u32 pbc_sound_levels(const char *text, pbc_u32 len)
{
    const char *p = text, *end = text ? text + len : text;
    int volume = 100, paula = 100, ahi = 100, muted = 0;
    while (p && p < end) {
        char key[32], val[32];
        const char *q = p;
        while (q < end && pbc_blank(*q)) q++;
        if (q < end && *q != ';' && pbc_word(&q, end, key, sizeof key) && pbc_word(&q, end, val, sizeof val)) {
            const char *v = val;
            long n = pbc_num(&v, val + 31, 0);
            if (pbc_is(key, "volume")) volume = pbc_clamp(n, 0, 100);
            else if (pbc_is(key, "paula")) paula = pbc_clamp(n, 0, 100);
            else if (pbc_is(key, "ahi")) ahi = pbc_clamp(n, 0, 100);
            else if (pbc_is(key, "mute")) {
                if (pbc_is(val, "on") || pbc_is(val, "yes") || pbc_is(val, "1")) muted = 1;
                else if (pbc_is(val, "off") || pbc_is(val, "no") || pbc_is(val, "0")) muted = 0;
            }
        }
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    return (pbc_u32)volume | (pbc_u32)paula << 8 | (pbc_u32)ahi << 16 | (muted ? 1u << 24 : 0);
}

/* ---- ENV:OpenAmiga/Stack ---- */

static pbc_u32 pbc_kb(long kb)
{
    kb = (kb + 63) / 64 * 64;                   /* whole 64 KB, as the runtime rounds */
    return (pbc_u32)(kb < 64 ? 64 : kb > 65536 ? 65536 : kb);
}

static int pbc_same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (x != y) return 0;
    }
    return *a == *b;
}

static void pbc_set(struct pbc_stack *s, const char *name, int n, long kb)
{
    int i, k;
    if (!n || n >= PBC_NAMELEN) return;
    for (k = 0; k < n; k++)
        if (name[k] == ':' || name[k] == '/') return;
    for (i = 0; i < s->nprogs && !pbc_same(s->prog[i].name, name); i++) {}
    if (i == s->nprogs) {
        if (s->nprogs == PBC_MAXPROGS) return;
        s->nprogs++;
        for (k = 0; k <= n; k++) s->prog[i].name[k] = name[k];
    }
    s->prog[i].kb = pbc_kb(kb);
}

static int pbc_starts(const char *p, const char *end, const char *w)
{
    while (*w && p < end && *p == *w) p++, w++;
    return !*w;
}

void pbc_stack_parse(struct pbc_stack *s, const char *text, pbc_u32 len)
{
    const char *p = text, *end = text ? text + len : text;
    s->cap_kb = PBC_CAP_DEFAULT_KB;
    s->nprogs = 0;
    while (p && p < end) {
        const char *q = p;
        int ok;
        if (pbc_starts(q, end, "CAP")) {
            long kb;
            q += 3;
            kb = pbc_num(&q, end, &ok);
            if (ok) s->cap_kb = pbc_kb(kb);
        } else if (pbc_starts(q, end, "PROGRAM")) {
            char name[64];
            int n;
            q += 7;
            n = pbc_word(&q, end, name, sizeof name);
            if (n > 0 && n < (int)sizeof name) {
                long kb = pbc_num(&q, end, &ok);
                if (ok && kb > 0) pbc_set(s, name, n, kb);
            }
        }
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
}

/* ---- ENV:OpenInput/LowLevelPatch ---- */

int pbc_lowlevel_on(const char *text, pbc_u32 len)
{
    return text && len > 0 && text[0] == '1';
}
