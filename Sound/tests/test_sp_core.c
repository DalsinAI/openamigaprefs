/* Host tests for the Sound editor's settings and files (sp_core.c).
 *   tests/run.sh
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "sp_core.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fails++; } } while (0)

static uint8_t *load(const char *dir, const char *name, long *len)
{
    char path[512];
    FILE *f;
    uint8_t *p;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = fopen(path, "rb"))) return NULL;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)*len);
    if (fread(p, 1, (size_t)*len, f) != (size_t)*len) { free(p); p = NULL; }
    fclose(f);
    return p;
}

static void text_round_trip(void)
{
    sp_settings s, t;
    char buf[512];
    sp_defaults(&s);
    CHECK(s.volume == 100 && s.paula == 100 && s.ahi == 100 && !s.muted && s.speaker && s.mix == SP_MIX_PC, "defaults");
    s.volume = 70; s.paula = 90; s.ahi = 85; s.muted = 1; s.mix = SP_MIX_AMIGA; s.speaker = 0; s.wheel = 10;
    CHECK(sp_write(&s, buf, sizeof buf) > 0, "writes");
    sp_defaults(&t);
    sp_parse(&t, buf);
    CHECK(t.volume == 70 && t.paula == 90 && t.ahi == 85 && t.muted && t.mix == SP_MIX_AMIGA && !t.speaker && t.wheel == 10, "round trip:\n%s", buf);
    sp_defaults(&t);
    sp_parse(&t, "volume 250\nfuture.key 3\n; a comment\nahi -4\nmute maybe\n");
    CHECK(t.volume == 100 && t.ahi == 0 && !t.muted, "clamped, unknown lines skipped");
    CHECK(sp_write(&s, buf, 20) == -1, "too small");
}

static void sound_prefs(void)
{
    sp_settings s, t;
    uint8_t out[SP_SOUND_PREFS_SIZE];
    sp_defaults(&s);
    s.beep = SP_BEEP_BOTH; s.beep_sample = 1; strcpy(s.sample, "SYS:Sounds/Chime"); s.beep_volume = 40; s.beep_period = 300; s.beep_duration = 20;
    CHECK(sp_write_sound_prefs(&s, out) == SP_SOUND_PREFS_SIZE, "size");
    CHECK(!memcmp(out, "FORM", 4) && !memcmp(out + 8, "PREF", 4) && !memcmp(out + 12, "PRHD", 4) && !memcmp(out + 26, "SOND", 4), "layout");
    sp_defaults(&t);
    CHECK(sp_read_sound_prefs(&t, out, sizeof out), "reads");
    CHECK(t.beep == SP_BEEP_BOTH && t.beep_sample && !strcmp(t.sample, "SYS:Sounds/Chime") && t.beep_volume == 40 && t.beep_period == 300
          && t.beep_duration == 20, "round trip");
    s.beep = SP_BEEP_NONE; sp_write_sound_prefs(&s, out); sp_read_sound_prefs(&t, out, sizeof out);
    CHECK(t.beep == SP_BEEP_NONE, "nothing");
    CHECK(!sp_read_sound_prefs(&t, (const uint8_t *)"FORMxxxxILBM", 12), "not prefs");
}

static void ahi_prefs(const char *dir)
{
    sp_settings s, t;
    long len = 0, n;
    uint8_t *old = load(dir, "sample-ahi.prefs", &len), out[SP_AHI_MAX];
    CHECK(old != NULL, "sample-ahi.prefs (from mkahiprefs.py)");
    if (!old) return;
    sp_defaults(&s);
    CHECK(sp_read_ahi_prefs(&s, old, len) && s.have_ahi_prefs, "reads");
    CHECK(s.unit[0].mode == SP_MODE_HOSTMIX && s.unit[4].mode == SP_MODE_HOSTMIX && s.unit[0].freq == 44100 && s.unit[0].out_volume == 0x10000,
          "units: %08lx", (unsigned long)s.unit[0].mode);
    CHECK(s.maxcpu == 90 && !s.clip && s.anticlick == 0, "globals: cpu %d", s.maxcpu);
    CHECK(sp_mix_of(&s) == SP_MIX_PC, "Host mix is mixing on the PC");

    n = sp_write_ahi_prefs(&s, old, len, out, sizeof out);
    CHECK(n == len && !memcmp(out, old, (size_t)len), "unchanged settings write the same bytes (%ld, %ld)", n, len);

    s.mix = SP_MIX_AMIGA; sp_apply_mix(&s);
    s.volume = 50; sp_volume_to_units(&s);
    s.clip = 1; s.maxcpu = 50; s.anticlick = 20;
    n = sp_write_ahi_prefs(&s, old, len, out, sizeof out);
    CHECK(n == len, "same chunks: %ld", n);
    sp_defaults(&t);
    sp_read_ahi_prefs(&t, out, n);
    CHECK(t.unit[2].mode == SP_MODE_HIFI && t.unit[4].mode == SP_MODE_HIFI && sp_mix_of(&t) == SP_MIX_AMIGA, "HiFi now");
    CHECK(t.unit[1].out_volume == 0x8000, "half the volume: %08lx", (unsigned long)t.unit[1].out_volume);
    CHECK(t.clip && t.maxcpu == 50 && t.anticlick == 20, "globals changed: %d %d %d", t.clip, t.maxcpu, t.anticlick);
    s.muted = 1; sp_volume_to_units(&s);
    CHECK(s.unit[0].out_volume == 0, "muted: 0");

    /* a unit on another card stays there */
    sp_defaults(&s);
    sp_read_ahi_prefs(&s, old, len);
    s.unit[3].mode = 0x00020004UL;
    s.mix = SP_MIX_AMIGA; sp_apply_mix(&s);
    CHECK(s.unit[3].mode == 0x00020004UL && s.unit[0].mode == SP_MODE_HIFI, "another card's unit kept");

    /* a first start: no ahi.prefs */
    sp_defaults(&s);
    sp_apply_mix(&s);
    n = sp_write_ahi_prefs(&s, NULL, 0, out, sizeof out);
    sp_defaults(&t);
    CHECK(n > 0 && sp_read_ahi_prefs(&t, out, n), "a new file");
    CHECK(t.unit[0].mode == SP_MODE_HOSTMIX && t.unit[4].mode == SP_MODE_HOSTMIX && t.unit[0].channels == 16 && t.unit[0].out_volume == 0x10000,
          "every unit on Host mix: %08lx %d", (unsigned long)t.unit[0].mode, t.unit[0].channels);
    CHECK(sp_write_ahi_prefs(&s, NULL, 0, out, 40) == -1, "too small");
    free(old);
}

static void levels(void)
{
    sp_settings s;
    sp_defaults(&s);
    CHECK(sp_levels_word(&s) == 0x00646464UL, "ACAHI_LEVELS_DEFAULT");
    s.volume = 70; s.paula = 100; s.ahi = 85; s.muted = 1;
    CHECK(sp_levels_word(&s) == (70 | 100 << 8 | 85 << 16 | 1UL << 24), "packed");
    sp_defaults(&s);
    sp_from_levels_word(&s, 20 | 30 << 8 | 40 << 16 | 1UL << 24 | 1UL << 31);
    CHECK(s.volume == 20 && s.paula == 30 && s.ahi == 40 && s.muted, "unpacked");
}

int main(int argc, char **argv)
{
    text_round_trip();
    sound_prefs();
    ahi_prefs(argc > 1 ? argv[1] : ".");
    levels();
    if (!fails) printf("sp_core: ok\n");
    return fails ? 1 : 0;
}
