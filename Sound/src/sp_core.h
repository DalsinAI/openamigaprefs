/* sp_core: the Sound editor's settings, and the OS files it keeps them in.
 *
 *   ENV:OpenAmiga/Sound    our own lines: the levels, who mixes AHI, the
 *                          menu bar speaker (format 1, "key value")
 *   ENV:Sys/sound.prefs    the beep, as the OS's Sound prefs writes it
 *   ENV:Sys/ahi.prefs      AHI's units and global settings, as AHI's own
 *                          editor writes it
 *
 * Plain C with no Amiga calls, so the same code is tested on the host
 * (tests/test_sp_core.c) and runs on the Amiga, in the Sound editor and in
 * OpenSpeaker alike. The OS files are kept byte for byte where we change
 * nothing: chunks we don't know go back as they came.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef SP_CORE_H
#define SP_CORE_H

#include <stdint.h>

/* ACAHI's modes (DEVS:AudioModes/ACAUDIO) */
#define SP_MODE_HOSTMIX 0x00AC0001UL     /* the PC mixes AHI's channels */
#define SP_MODE_HIFI    0x00AC0002UL     /* AHI's HiFi mixer on the 68k */
#define SP_MODE_16BIT   0x00AC0003UL     /* AHI's 16-bit mixer on the 68k */
#define SP_IS_ACAHI_MODE(m) (((m) & 0xFFFF0000UL) == 0x00AC0000UL)

#define SP_UNITS 5                       /* AHI's units 0-3, then its "music unit" (AHI_NO_UNIT, 255) */
#define SP_MUSIC_UNIT 255
#define SP_AHI_MAX 4096                  /* an ahi.prefs we read or write */
#define SP_SOUND_PREFS_SIZE 318          /* FORM PREF with PRHD and SOND */

enum { SP_MIX_PC = 0, SP_MIX_AMIGA = 1 };
enum { SP_BEEP_SOUND = 0, SP_BEEP_FLASH = 1, SP_BEEP_BOTH = 2, SP_BEEP_NONE = 3 };

typedef struct sp_unit {
    uint32_t mode;                       /* AHI audio mode ID; 0 none set */
    uint32_t freq;                       /* Hz */
    int channels;
    uint32_t out_volume;                 /* Fixed, 0x10000 = 0 dB */
    uint32_t monitor, gain, input, output;   /* kept as read */
} sp_unit;

typedef struct sp_settings {
    /* ENV:OpenAmiga/Sound */
    int volume, paula, ahi;              /* percent, 0-100 */
    int muted;
    int mix;                             /* SP_MIX_*: the AmigaChrome mode for AHI's units */
    int speaker;                         /* the speaker on the menu bar */
    int wheel;                           /* percent a wheel step */

    /* ENV:Sys/sound.prefs */
    int beep;                            /* SP_BEEP_* */
    int beep_sample;                     /* 0 the beep, 1 a sound file */
    char sample[256];
    int beep_volume;                     /* 0-64 */
    int beep_period;                     /* Paula's period: 124 (high) to 1000 (low) */
    int beep_duration;                   /* fiftieths of a second, 1-100 */

    /* ENV:Sys/ahi.prefs */
    sp_unit unit[SP_UNITS];
    int clip;                            /* clip the mix instead of scaling it */
    int anticlick;                       /* ms, 0 off */
    int maxcpu;                          /* percent */
    int have_ahi_prefs;                  /* an ahi.prefs was read */
} sp_settings;

void sp_defaults(sp_settings *s);

/* ENV:OpenAmiga/Sound. Unknown lines are skipped. 1, or 0 when text is NULL. */
int sp_parse(sp_settings *s, const char *text);
/* Its text into out; the length, or -1 when it doesn't fit. */
int sp_write(const sp_settings *s, char *out, int size);

/* sound.prefs: 1 when it was one (the beep's fields read), else 0. */
int sp_read_sound_prefs(sp_settings *s, const uint8_t *data, long len);
/* Writes SP_SOUND_PREFS_SIZE bytes. */
long sp_write_sound_prefs(const sp_settings *s, uint8_t *out);

/* ahi.prefs: 1 when it was one (units and globals read), else 0. */
int sp_read_ahi_prefs(sp_settings *s, const uint8_t *data, long len);
/* A new ahi.prefs into out (at most size bytes): old's chunks we don't
 * change are kept as they were (old may be NULL). The length, or -1. */
long sp_write_ahi_prefs(const sp_settings *s, const uint8_t *old, long oldlen, uint8_t *out, long size);

/* Who mixes, for the board's units: every unit on an AmigaChrome mode moves
 * to Host mix (SP_MIX_PC) or HiFi (SP_MIX_AMIGA); with none on one yet
 * (a first start), all of them do. */
void sp_apply_mix(sp_settings *s);
/* SP_MIX_PC when a unit is on Host mix, else SP_MIX_AMIGA. */
int sp_mix_of(const sp_settings *s);

/* The board's LEVELS register and back (vendor protocol: acahi.h). */
uint32_t sp_levels_word(const sp_settings *s);
void sp_from_levels_word(sp_settings *s, uint32_t w);

/* Without the board (a real Amiga): the volume is AHI's output volume on
 * every unit, muted as 0. */
void sp_volume_to_units(sp_settings *s);

#endif
