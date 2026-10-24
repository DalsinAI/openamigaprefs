/* The boot's Sound reader (Common/prefs_boot_core.c, pbc_sound_levels) against the Sound editor's own
 * (Sound/src/sp_core.c: sp_parse, then sp_levels_word) on the same files: the board must get the same LEVELS
 * word from intuition at boot as from the editor's Use. Prints PASS or the files that differ.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <stdio.h>
#include <string.h>
#include "prefs_boot_core.h"
#include "sp_core.h"

static const char *const files[] = {
    "; OpenPrefs Sound, format 1\nvolume 80\npaula 50\nahi 70\nmute off\nmix pc\nspeaker on\nwheel 5\n",
    "", "mute on\n", "mute yes", "mute 1\n", "mute on\nmute off\n", "mute on\nmute maybe\n",
    "volume 150\npaula -5\nahi 100\n", "volume 42abc\npaula x\n", "  volume\t 33\r\npaula 44\r\n",
    "; volume 1\n  ; paula 2\nvolume\nbass 9\nahi 12\n", "volume 10\nvolume 20\n", "Volume 10\n", "ahi 5",
    "volume 0\npaula 0\nahi 0\nmute on\n", "volume 100\npaula 100\nahi 100\n", "volume 7 extra words\n",
};

int main(void)
{
    unsigned i, bad = 0;
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        sp_settings s;
        unsigned long a, b;
        sp_defaults(&s);
        sp_parse(&s, files[i]);
        a = sp_levels_word(&s);
        b = pbc_sound_levels(files[i], (pbc_u32)strlen(files[i]));
        if (a != b) { printf("FAIL file %u: the editor %08lx, the boot %08lx\n", i, a, b); bad++; }
    }
    printf("%u files: %s\n", i, bad ? "FAIL" : "PASS");
    return bad != 0;
}
