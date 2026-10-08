/* gp_core: the Gamepads editor's settings and the text formats it keeps
 * them in, with no Amiga calls, so the same code is tested on the host
 * (tests/test_gp_core.c) and runs in the editor.
 *
 *   ENV:OpenInput/LowLevelPatch  "1": openinput.library patches
 *                                lowlevel.library's ReadJoyPort, so older
 *                                games see a modern pad; anything else, or
 *                                no file: nothing is patched (the default)
 *   ENV:OpenInput/ports.prefs    which pad feeds which Amiga port through
 *                                that patch: "port N none|joystick|cd32
 *                                any|GUID" a line per port (openinput.library
 *                                reads the same lines)
 *   ENV:OpenInput/mappings.txt   the user's own mappings, in SDL's
 *                                GameControllerDB format; written by
 *                                openinput.library (OIN_SetMapping)
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef GP_CORE_H
#define GP_CORE_H

#include <stdint.h>

/* OILEG_* from libraries/openinput.h, which the host tests don't include */
enum { GP_MODE_NONE = 0, GP_MODE_JOYSTICK = 1, GP_MODE_CD32 = 2 };

typedef struct gp_port {
    int mode;                    /* GP_MODE_* */
    int any;                     /* the first modern pad, not one GUID */
    uint8_t guid[16];
} gp_port;

typedef struct gp_settings {
    int patch;                   /* LowLevelPatch is "1" */
    gp_port port[2];             /* 0 the mouse port ("port 1" on the case), 1 the joystick port ("port 2") */
} gp_settings;

/* The defaults: patch off; port 1 (the joystick port) a CD32 pad from any pad, port 0 none. */
void gp_defaults(gp_settings *s);
/* LowLevelPatch's text: "1" is on. */
int gp_patch_from_text(const char *t);
/* ports.prefs's text into s->port; lines it doesn't know are skipped. */
void gp_ports_from_text(gp_settings *s, const char *t);
/* ports.prefs as text, into out (size bytes); its length. */
int gp_ports_to_text(const gp_settings *s, char *out, int size);
/* Which port feeds the patch, for the switch's words: the port that has a
 * mode, the joystick port first; -1 for none. */
int gp_fed_port(const gp_settings *s);

/* ---- GUIDs ---------------------------------------------------------------- */
void gp_guid_to_hex(const uint8_t *g, char *out33);
int gp_guid_from_hex(const char *s, uint8_t *g);          /* 1 when 32 hex digits were read */

/* ---- mappings in SDL's text format ----------------------------------------
 * "GUID,name,a:b0,b:b1,...,platform:AmigaOS 3," */

#define GP_TARGETS 21            /* what the Map... wizard asks for, in its order */
typedef struct gp_target {
    const char *key;             /* SDL's name: "a", "leftx", "dpup" */
    const char *ask;             /* the words: "Press the bottom face button" */
    int axis;                    /* 1: a stick or trigger (an axis, or a button for a digital trigger) */
} gp_target;
extern const gp_target gp_targets[GP_TARGETS];

/* One input, as the wizard saw it: a button, an axis moved one way, or a hat's direction. */
enum { GP_IN_NONE = 0, GP_IN_BUTTON, GP_IN_AXIS, GP_IN_HAT };
typedef struct gp_input {
    int kind;                    /* GP_IN_* */
    int index;                   /* button, axis or hat number */
    int value;                   /* GP_IN_AXIS: -1 or +1, the way it moved; GP_IN_HAT: the hat bit */
    int rest;                    /* GP_IN_AXIS: where it rested (a trigger resting at -32768 uses the whole axis) */
} gp_input;

/* The SDL element for an input asked for by target t: "b3", "h0.4",
 * "a2" (a stick moved right or down as asked), "a2~" (it moved the other
 * way), "+a4" (a trigger resting at 0), "a4" (a trigger resting at
 * -32768), "+a0" or "-a0" (a button target on half an axis). Empty when
 * the input can't serve the target (a stick from a button). */
void gp_input_text(const gp_input *in, int t, char *out, int size);

/* A mapping line from the wizard's answers (NONE for skipped); its length,
 * or -1 if it didn't fit. */
int gp_build_mapping(const uint8_t *guid, const char *name, const gp_input in[GP_TARGETS], char *out, int size);

/* Is it a mapping line the library can take (a GUID, a name, at least one
 * "key:element")? 1 or 0. */
int gp_mapping_ok(const char *line);

/* A short reading of a mapping line for the window: "A b0, B b1, ... (12 inputs)". */
void gp_mapping_summary(const char *line, char *out, int size);

/* The physical difference between raw readings that counts as a move. */
#define GP_AXIS_MOVE 16000

/* What changed between a resting raw state and now: the first button
 * pressed, else the axis moved furthest past GP_AXIS_MOVE (the axis first
 * when axis_first: a stick or a trigger is asked for, and a pad may report
 * the same press as a button too), else a hat. */
typedef struct gp_raw {
    uint32_t buttons[2];
    int16_t axes[16];
    uint8_t hats[4];
} gp_raw;
gp_input gp_raw_change(const gp_raw *rest, const gp_raw *now, int axis_first);

#endif
