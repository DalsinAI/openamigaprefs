/* ic_prefs: the fields of icontrol.prefs (ICTL, OpenNDK prefs/icontrol.h) that the IControl section edits.
 *
 * As pf_prefs does for the Appearance tabs: a put writes the fields it knows and leaves every other byte of
 * the chunk as the file had it (the reserved longs, OS 3.2's version and hover fields after the first 28
 * bytes, flag bits this section has no switch for), so settings put back unchanged give the file's own
 * bytes. With no file, a 28-byte chunk (the size every release reads) is added. Plain C: tested on the host
 * by IControl/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef IC_PREFS_H
#define IC_PREFS_H

#include "pf_iff.h"

#define IC_ICTL PF_ID('I', 'C', 'T', 'L')
#define IC_ICTL_LEN 28                    /* TimeOut, MetaDrag, Flags and the four keys after 4 reserved longs */

/* ic_Flags' bits (prefs/icontrol.h) */
#define IC_COERCE_COLORS  0x00000001UL    /* a screen keeps its colours when shown in another mode */
#define IC_COERCE_LACE    0x00000002UL    /* avoid flicker: a non-interlaced mode where one will do */
#define IC_STRGAD_FILTER  0x00000004UL    /* text gadgets take no control characters */
#define IC_MENUSNAP       0x00000008UL    /* a menu opened by the keyboard goes to the screen's corner */
#define IC_MODEPROMOTE    0x00000010UL    /* old programs' modes promoted to the monitor's better ones */
#define IC_CORRECT_RATIO  0x00000020UL
#define IC_OFFSCRNWIN     0x00008000UL    /* windows may be dragged off the screen */

/* the qualifiers that drag a screen with the mouse (devices/inputevent.h) */
#define IC_Q_LSHIFT   0x0001
#define IC_Q_CONTROL  0x0008
#define IC_Q_LALT     0x0010
#define IC_Q_LCOMMAND 0x0040

typedef struct ic_prefs {
    int timeout;                  /* ic_TimeOut, in ticks (50 a second) */
    int metadrag;                 /* IC_Q_ bits */
    uint32_t flags;               /* IC_ bits, and any others the file has */
    char keys[4];                 /* Amiga-key shortcuts: Workbench to front, front screen to back, OK, Cancel */
} ic_prefs;

void ic_defaults(ic_prefs *p);
int ic_get(const pf_file *f, ic_prefs *out);                /* 1 found; 0 and out the defaults */
int ic_put(pf_file *f, const ic_prefs *in);                 /* 1, or 0 with no memory or a full file */

#endif /* IC_PREFS_H */
