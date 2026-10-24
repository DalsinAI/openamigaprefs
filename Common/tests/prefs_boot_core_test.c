/* Common/prefs_boot_core.c: the vectors (a copy of amigachrome-guest's
 * tests/m68k/prefs_boot_core_test.c, for its libraries/openintuition copy).
 * Each case is a file as the editor writes it, or one it might meet, and
 * what the boot must apply. Prints "ok"/"FAIL" lines, then PASS or FAIL.
 * Copyright (c) 2026 AmigaChrome Team. MIT. */
#include <stdio.h>
#include <string.h>
#include "prefs_boot_core.h"

static int fails;

static void check(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static pbc_u32 lv(int v, int p, int a, int m) { return (pbc_u32)v | (pbc_u32)p << 8 | (pbc_u32)a << 16 | (m ? 1u << 24 : 0); }

static pbc_u32 snd(const char *t) { return pbc_sound_levels(t, (pbc_u32)strlen(t)); }

static void stk(struct pbc_stack *s, const char *t) { pbc_stack_parse(s, t, (pbc_u32)strlen(t)); }

int main(void)
{
    struct pbc_stack s;
    int i;
    char big[2048];

    /* ---- ENV:OpenAmiga/Sound ---- */
    check("Sound: the editor's own file",
          snd("; OpenPrefs Sound, format 1\nvolume 80\npaula 50\nahi 70\nmute off\nmix pc\nspeaker on\nwheel 5\n")
          == lv(80, 50, 70, 0));
    check("Sound: no file: 100, 100, 100, unmuted", pbc_sound_levels(0, 0) == lv(100, 100, 100, 0));
    check("Sound: an empty file", snd("") == lv(100, 100, 100, 0));
    check("Sound: muted (on, yes, 1)", snd("mute on\n") == lv(100, 100, 100, 1) && snd("mute yes") == lv(100, 100, 100, 1)
          && snd("mute 1\n") == lv(100, 100, 100, 1));
    check("Sound: mute off after on; a word it doesn't know keeps it", snd("mute on\nmute off\n") == lv(100, 100, 100, 0)
          && snd("mute on\nmute maybe\n") == lv(100, 100, 100, 1));
    check("Sound: clamped to 0-100", snd("volume 150\npaula -5\nahi 100\n") == lv(100, 0, 100, 0));
    check("Sound: a number as atoi reads it", snd("volume 42abc\npaula x\n") == lv(42, 0, 100, 0));
    check("Sound: CR LF lines, blanks and tabs", snd("  volume\t 33\r\npaula 44\r\n") == lv(33, 44, 100, 0));
    check("Sound: a comment line, a key with no value, unknown keys",
          snd("; volume 1\n  ; paula 2\nvolume\nbass 9\nahi 12\n") == lv(100, 100, 12, 0));
    check("Sound: the last line counts", snd("volume 10\nvolume 20\n") == lv(20, 100, 100, 0));
    check("Sound: keys are lower case", snd("Volume 10\n") == lv(100, 100, 100, 0));
    check("Sound: no newline at the end", snd("ahi 5") == lv(100, 100, 5, 0));
    check("Sound: only the length given is read", pbc_sound_levels("volume 77\nvolume 11\n", 10) == lv(77, 100, 100, 0));

    /* ---- ENV:OpenAmiga/Stack ---- */
    stk(&s, "CAP 2048\nPROGRAM OpenMonitor 256\nPROGRAM Doom 4096\n");
    check("Stack: the editor's own file", s.cap_kb == 2048 && s.nprogs == 2 && !strcmp(s.prog[0].name, "OpenMonitor")
          && s.prog[0].kb == 256 && !strcmp(s.prog[1].name, "Doom") && s.prog[1].kb == 4096);
    pbc_stack_parse(&s, 0, 0);
    check("Stack: no file: CAP 1024, no programs", s.cap_kb == 1024 && s.nprogs == 0);
    stk(&s, "CAP 100\n");
    check("Stack: rounded up to whole 64 KB", s.cap_kb == 128);
    stk(&s, "CAP 10\n");
    check("Stack: at least 64", s.cap_kb == 64);
    stk(&s, "CAP 999999\n");
    check("Stack: at most 65536", s.cap_kb == 65536);
    stk(&s, "CAP -5\n");
    check("Stack: a negative CAP is 64", s.cap_kb == 64);
    stk(&s, "CAP x\n CAP 512\nCAPX 512\n");
    check("Stack: CAP with no number, CAP after a blank, CAPX: not read", s.cap_kb == 1024);
    stk(&s, "CAP256\n");
    check("Stack: CAP256 reads as sscanf reads it", s.cap_kb == 256);
    stk(&s, "PROGRAM a 0\nPROGRAM b -64\nPROGRAM c\nPROGRAM d x\n");
    check("Stack: a program's kb must be above 0", s.nprogs == 0);
    stk(&s, "PROGRAM Work:x 64\nPROGRAM a/b 64\nPROGRAM abcdefghijabcdefghijabcdefghij12 64\n"
             "PROGRAM abcdefghijabcdefghijabcdefghij1 100\n");
    check("Stack: names with ':' or '/', or of 32 characters, are not taken; 31 are",
          s.nprogs == 1 && !strcmp(s.prog[0].name, "abcdefghijabcdefghijabcdefghij1") && s.prog[0].kb == 128);
    stk(&s, "PROGRAM Doom 128\nPROGRAM DOOM 256\n");
    check("Stack: a name's case is ignored; the last line counts, the first spelling kept",
          s.nprogs == 1 && !strcmp(s.prog[0].name, "Doom") && s.prog[0].kb == 256);
    big[0] = 0;
    for (i = 0; i < 40; i++) {
        char line[40];
        sprintf(line, "PROGRAM p%d %d\n", i, 64 * (i + 1));
        strcat(big, line);
    }
    strcat(big, "PROGRAM p3 9\n");
    stk(&s, big);
    check("Stack: at most 32 programs; a known one still changes", s.nprogs == 32 && !strcmp(s.prog[31].name, "p31")
          && s.prog[3].kb == 64);
    stk(&s, "CAP 512\r\nPROGRAM\tx\t192\r\n");
    check("Stack: CR LF and tabs", s.cap_kb == 512 && s.nprogs == 1 && s.prog[0].kb == 192);

    /* ---- ENV:OpenInput/LowLevelPatch ---- */
    check("LowLevelPatch: \"1\" is on", pbc_lowlevel_on("1", 1) && pbc_lowlevel_on("1\n", 2));
    check("LowLevelPatch: \"0\", empty, none: off", !pbc_lowlevel_on("0", 1) && !pbc_lowlevel_on("", 0) && !pbc_lowlevel_on(0, 0)
          && !pbc_lowlevel_on(" 1", 2));

    puts(fails ? "FAIL" : "PASS");
    return fails != 0;
}
