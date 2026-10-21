/* dp_prefs: the fields of screenmode.prefs (SCRM) and overscan.prefs (OSCN) that the Screen mode section
 * edits, read from and written into a chunk's bytes (OpenNDK prefs/screenmode.h, prefs/overscan.h).
 *
 * As pf_prefs does for the Appearance tabs: a put writes the fields it knows and leaves every other byte of
 * the chunk as the file had it (the reserved longs, anything a newer release added after the fields), so
 * settings put back unchanged give the file's own bytes. A chunk that isn't there yet is added, its other
 * bytes zero. Plain C: tested on the host by Display/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef DP_PREFS_H
#define DP_PREFS_H

#include "pf_iff.h"

/* ---- screenmode.prefs: SCRM, struct ScreenModePrefs (28 bytes) ---- */
#define DP_SCRM PF_ID('S', 'C', 'R', 'M')
#define DP_SCRM_LEN 28
#define DP_SIZE_DEFAULT 0xffff            /* Width or Height: the mode's own (STDSCREENWIDTH/HEIGHT) */
#define DP_SMF_AUTOSCROLL 0x0001
/* What OS 3.2.3's IPrefs hands Intuition when there is no screenmode.prefs (IPrefsProbe, 16 October 2026):
 * DisplayID $00008000, Width and Height $FFFF, Depth 2. Read here as "no mode chosen". */
#define DP_NOFILE_ID 0x00008000UL

typedef struct dp_mode {
    uint32_t id;                  /* the DisplayID */
    int width, height;            /* DP_SIZE_DEFAULT: the mode's own */
    int depth;                    /* bit planes: 2 is 4 colours */
    int autoscroll;
} dp_mode;

void dp_mode_defaults(dp_mode *m);                          /* IPrefs' own with no file (above) */
int dp_mode_get(const pf_file *f, dp_mode *out);            /* 1 found; 0 and out the defaults */
int dp_mode_put(pf_file *f, const dp_mode *in);             /* 1, or 0 with no memory or a full file */

/* ---- overscan.prefs: OSCN, struct OverscanPrefs (36 bytes), one chunk for each monitor ---- */
#define DP_OSCN PF_ID('O', 'S', 'C', 'N')
#define DP_OSCN_LEN 36
#define DP_OSCAN_MAGIC 0xFEDCBA89UL
#define DP_MONITOR_MASK 0xffff1000UL     /* graphics/modeid.h's MONITOR_ID_MASK: a DisplayID's monitor */

typedef struct dp_rect { int minx, miny, maxx, maxy; } dp_rect;

typedef struct dp_oscan {
    uint32_t id;                  /* os_DisplayID: a mode of the monitor the chunk is for */
    int hstart, hstop, vstart, vstop;
    int viewx, viewy;             /* os_ViewPos */
    int textw, texth;             /* os_Text: the text overscan's size */
    dp_rect std;                  /* os_Standard: the standard overscan rectangle */
} dp_oscan;

int dp_native(uint32_t id);                                 /* 1 for a chipset monitor's mode (overscan applies) */
int dp_oscan_find(const pf_file *f, uint32_t id);           /* the chunk for id's monitor, or -1 */
int dp_oscan_get(const pf_file *f, uint32_t id, dp_oscan *out);   /* 1 found; 0 and out cleared */
int dp_oscan_put(pf_file *f, const dp_oscan *in);           /* that monitor's chunk (added when missing) */
void dp_oscan_remove(pf_file *f, uint32_t id);              /* none for id's monitor: the display database's own */

#endif /* DP_PREFS_H */
