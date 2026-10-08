/* gp_core: the Gamepads editor's settings and text formats (gp_core.h).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gp_core.h"

const gp_target gp_targets[GP_TARGETS] = {
    { "a", "Press the bottom face button", 0 },
    { "b", "Press the right face button", 0 },
    { "x", "Press the left face button", 0 },
    { "y", "Press the top face button", 0 },
    { "back", "Press Back (Select, Share, Minus)", 0 },
    { "guide", "Press Guide (Home, the logo button)", 0 },
    { "start", "Press Start (Options, Plus, Play)", 0 },
    { "leftstick", "Click the left stick in", 0 },
    { "rightstick", "Click the right stick in", 0 },
    { "leftshoulder", "Press the left shoulder button", 0 },
    { "rightshoulder", "Press the right shoulder button", 0 },
    { "dpup", "Press the d-pad up", 0 },
    { "dpdown", "Press the d-pad down", 0 },
    { "dpleft", "Press the d-pad left", 0 },
    { "dpright", "Press the d-pad right", 0 },
    { "leftx", "Move the left stick right", 1 },
    { "lefty", "Move the left stick down", 1 },
    { "rightx", "Move the right stick right", 1 },
    { "righty", "Move the right stick down", 1 },
    { "lefttrigger", "Pull the left trigger", 1 },
    { "righttrigger", "Pull the right trigger", 1 },
};

#define T_FIRST_STICK 15
#define T_FIRST_TRIGGER 19

void gp_defaults(gp_settings *s)
{
    memset(s, 0, sizeof *s);
    s->patch = 0;
    s->port[0].mode = GP_MODE_NONE; s->port[0].any = 1;
    s->port[1].mode = GP_MODE_CD32; s->port[1].any = 1;
}

int gp_patch_from_text(const char *t)
{
    while (t && (*t == ' ' || *t == '\t')) t++;
    return t && t[0] == '1';
}

static const char *skip_space(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }
static const char *skip_word(const char *s) { while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') s++; return s; }
static int word_is(const char *s, const char *w)
{
    size_t n = strlen(w);
    return !strncmp(s, w, n) && (!s[n] || s[n] == ' ' || s[n] == '\t' || s[n] == '\n' || s[n] == '\r');
}

void gp_ports_from_text(gp_settings *s, const char *t)
{
    s->port[0].mode = GP_MODE_NONE; s->port[0].any = 1; memset(s->port[0].guid, 0, 16);
    s->port[1].mode = GP_MODE_CD32; s->port[1].any = 1; memset(s->port[1].guid, 0, 16);
    while (t && *t) {
        const char *l = skip_space(t);
        if (word_is(l, "port")) {
            l = skip_space(skip_word(l));
            if ((l[0] == '0' || l[0] == '1') && (l[1] == ' ' || l[1] == '\t')) {
                gp_port *p = &s->port[l[0] - '0'];
                l = skip_space(l + 1);
                p->mode = word_is(l, "cd32") ? GP_MODE_CD32 : word_is(l, "joystick") ? GP_MODE_JOYSTICK : GP_MODE_NONE;
                l = skip_space(skip_word(l));
                p->any = !gp_guid_from_hex(l, p->guid);
                if (p->any) memset(p->guid, 0, 16);
            }
        }
        while (*t && *t != '\n') t++;
        if (*t) t++;
    }
}

int gp_ports_to_text(const gp_settings *s, char *out, int size)
{
    static const char *const modes[3] = { "none", "joystick", "cd32" };
    int n = snprintf(out, size, "# OpenInput: which pad feeds which Amiga port through the ReadJoyPort patch\n");
    for (int i = 1; i >= 0; i--) {
        char g[33];
        const gp_port *p = &s->port[i];
        int m = p->mode >= 0 && p->mode <= 2 ? p->mode : 0;
        if (p->any) strcpy(g, "any"); else gp_guid_to_hex(p->guid, g);
        if (n < size) n += snprintf(out + n, size - n, "port %d %s %s\n", i, modes[m], g);
    }
    return n < size ? n : size - 1;
}

int gp_fed_port(const gp_settings *s)
{
    if (s->port[1].mode != GP_MODE_NONE) return 1;
    if (s->port[0].mode != GP_MODE_NONE) return 0;
    return -1;
}

void gp_guid_to_hex(const uint8_t *g, char *out33)
{
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) { out33[2 * i] = hx[g[i] >> 4]; out33[2 * i + 1] = hx[g[i] & 15]; }
    out33[32] = 0;
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int gp_guid_from_hex(const char *s, uint8_t *g)
{
    uint8_t t[16];
    for (int i = 0; i < 16; i++) {
        int h = hexv(s[2 * i]), l = h < 0 ? -1 : hexv(s[2 * i + 1]);
        if (h < 0 || l < 0) return 0;
        t[i] = (uint8_t)(h << 4 | l);
    }
    memcpy(g, t, 16);
    return 1;
}

void gp_input_text(const gp_input *in, int t, char *out, int size)
{
    out[0] = 0;
    if (t < 0 || t >= GP_TARGETS) return;
    switch (in->kind) {
    case GP_IN_BUTTON:
        if (t >= T_FIRST_STICK && t < T_FIRST_TRIGGER) return;     /* a stick needs an axis */
        snprintf(out, size, "b%d", in->index);
        break;
    case GP_IN_HAT:
        if (t >= T_FIRST_STICK) return;                            /* sticks and triggers need an axis or a button */
        snprintf(out, size, "h%d.%d", in->index, in->value);
        break;
    case GP_IN_AXIS:
        if (t >= T_FIRST_TRIGGER)                                  /* a trigger: the whole axis when it rests at one end */
            snprintf(out, size, in->rest < -16000 ? "a%d" : in->value < 0 ? "-a%d" : "+a%d", in->index);
        else if (t >= T_FIRST_STICK)                               /* a stick, asked right or down */
            snprintf(out, size, in->value > 0 ? "a%d" : "a%d~", in->index);
        else                                                       /* a button on half an axis (a d-pad as axes) */
            snprintf(out, size, in->value < 0 ? "-a%d" : "+a%d", in->index);
        break;
    }
}

int gp_build_mapping(const uint8_t *guid, const char *name, const gp_input in[GP_TARGETS], char *out, int size)
{
    char g[33], el[16];
    int n;
    gp_guid_to_hex(guid, g);
    n = snprintf(out, size, "%s,", g);
    for (const char *c = name; *c && n < size - 1; c++) if (*c != ',') out[n++] = *c;   /* a comma would end the name */
    if (n >= size - 1) return -1;
    out[n++] = ','; out[n] = 0;
    for (int t = 0; t < GP_TARGETS; t++) {
        gp_input_text(&in[t], t, el, sizeof el);
        if (!el[0]) continue;
        n += snprintf(out + n, size - n, "%s:%s,", gp_targets[t].key, el);
        if (n >= size) return -1;
    }
    n += snprintf(out + n, size - n, "platform:AmigaOS 3,");
    return n < size ? n : -1;
}

int gp_mapping_ok(const char *line)
{
    uint8_t g[16];
    const char *c, *name;
    int pairs = 0;
    if (!line || !gp_guid_from_hex(line, g) || line[32] != ',') return 0;
    name = line + 33;
    if (!(c = strchr(name, ',')) || c == name) return 0;
    for (c++; *c; ) {
        const char *e = strchr(c, ',');
        const char *colon = strchr(c, ':');
        size_t len = e ? (size_t)(e - c) : strlen(c);
        if (len) {
            if (!colon || colon >= c + len || colon == c || colon == c + len - 1) return 0;
            if (strncmp(c, "platform:", 9)) pairs++;
        }
        if (!e) break;
        c = e + 1;
    }
    return pairs > 0;
}

void gp_mapping_summary(const char *line, char *out, int size)
{
    static const struct { const char *key, *name; } names[] = {
        { "a", "A" }, { "b", "B" }, { "x", "X" }, { "y", "Y" }, { "start", "Start" }, { "back", "Back" },
        { "leftshoulder", "LB" }, { "rightshoulder", "RB" }, { "leftx", "Stick" }, { "lefttrigger", "LT" },
    };
    const char *c;
    int n = 0, count = 0;
    out[0] = 0;
    if (!gp_mapping_ok(line)) { snprintf(out, size, "(none)"); return; }
    c = strchr(line + 33, ',') + 1;
    while (*c) {
        const char *e = strchr(c, ',');
        size_t len = e ? (size_t)(e - c) : strlen(c);
        const char *colon = memchr(c, ':', len);
        if (colon && strncmp(c, "platform:", 9)) {
            count++;
            for (size_t k = 0; k < sizeof names / sizeof names[0]; k++)
                if ((size_t)(colon - c) == strlen(names[k].key) && !strncmp(c, names[k].key, colon - c) && n < size - 24)
                    n += snprintf(out + n, size - n, "%s%s %.*s", n ? ", " : "", names[k].name, (int)(len - (colon - c) - 1), colon + 1);
        }
        if (!e) break;
        c = e + 1;
    }
    if (n < size - 20) snprintf(out + n, size - n, "%s(%d inputs)", n ? "  " : "", count);
}

gp_input gp_raw_change(const gp_raw *rest, const gp_raw *now, int axis_first)
{
    gp_input in = { GP_IN_NONE, 0, 0, 0 };
    int best = -1, bestd = GP_AXIS_MOVE;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == (axis_first ? 1 : 0)) {
            for (int b = 0; b < 64; b++) {
                uint32_t m = 1u << (b & 31);
                if ((now->buttons[b >> 5] & m) && !(rest->buttons[b >> 5] & m)) { in.kind = GP_IN_BUTTON; in.index = b; return in; }
            }
        } else {
            for (int a = 0; a < 16; a++) {
                int d = abs((int)now->axes[a] - (int)rest->axes[a]);
                if (d > bestd) { bestd = d; best = a; }
            }
            if (best >= 0) {
                in.kind = GP_IN_AXIS; in.index = best;
                in.value = now->axes[best] > rest->axes[best] ? 1 : -1;
                in.rest = rest->axes[best];
                return in;
            }
        }
    }
    for (int h = 0; h < 4; h++) {
        int bits = now->hats[h] & ~rest->hats[h];
        static const int order[4] = { 1, 2, 4, 8 };
        for (int k = 0; k < 4; k++)
            if (bits & order[k]) { in.kind = GP_IN_HAT; in.index = h; in.value = order[k]; return in; }
    }
    return in;
}
