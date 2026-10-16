/* pf_prefs: the fields of the OS 3.2 preferences files the Appearance tabs edit, read from and written
 * into a chunk's bytes (NDK 3.2 prefs/font.h, palette.h, wbpattern.h, workbench.h, asl.h, pointer.h).
 *
 * A put changes only the bytes of the fields whose value differs, so reserved bytes, bytes after a name's
 * end and anything a newer release added after the fields stay as the file had them, and settings put
 * back unchanged give the file's own bytes. A chunk that isn't there yet is added in the order the OS
 * editors write them, its other bytes zero. Plain C: tested on the host by Look/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef PF_PREFS_H
#define PF_PREFS_H

#include "pf_iff.h"

/* ---- font.prefs: FONT, struct FontPrefs (156 bytes) ---- */
#define PF_FONT PF_ID('F', 'O', 'N', 'T')
#define PF_FONT_LEN 156
enum { PF_WBFONT = 0, PF_SYSFONT = 1, PF_SCREENFONT = 2 };

typedef struct pf_font {
    int type;                     /* PF_WBFONT (icons), PF_SYSFONT (fixed width, text), PF_SCREENFONT */
    int front, back;              /* pens */
    int drawmode, special;        /* JAM1 0 or JAM2 1; special 0 none, 1 shadow, 2 outline (OS 3.2) */
    int ysize, style, flags;      /* the TextAttr's */
    char name[128];               /* "topaz.font" */
} pf_font;

int pf_font_get(const pf_file *f, int type, pf_font *out);  /* 1 found; 0 and out cleared (type set) */
int pf_font_put(pf_file *f, const pf_font *in);             /* 1, or 0 with no memory */

/* ---- palette.prefs: PALT, struct PalettePrefs (400 bytes) ---- */
#define PF_PALT PF_ID('P', 'A', 'L', 'T')
#define PF_PALT_LEN 400
#define PF_PENS 32
#define PF_COLOURS 32
#define PF_PEN_END 0xffff         /* ends a pen list */

typedef struct pf_palette {
    uint16_t pens4[PF_PENS];      /* DrawInfo pens for a screen of 4 colours, PF_PEN_END after the last */
    uint16_t pens8[PF_PENS];      /* and of 8 or more */
    struct { int16_t index; uint16_t r, g, b; } col[PF_COLOURS];   /* index -1 after the last */
} pf_palette;

int pf_palette_get(const pf_file *f, pf_palette *out);      /* 1 found; 0 and out the OS's defaults */
int pf_palette_put(pf_file *f, const pf_palette *in);
void pf_palette_defaults(pf_palette *p);                     /* what Palette writes with no file to start from */
int pf_palette_colour(const pf_palette *p, int index, uint8_t rgb[3]);   /* 1 when the list has that colour */
int pf_palette_set_colour(pf_palette *p, int index, const uint8_t rgb[3]); /* 1, or 0 when the list is full */
int pf_palette_pen(const uint16_t *pens, int pen);          /* the colour of DrawInfo pen `pen`, or -1 */
int pf_palette_set_pen(uint16_t *pens, int pen, int colour);  /* 1, or 0 when the list is shorter */

/* ---- wbpattern.prefs: PTRN, struct WBPatternPrefs (24 bytes) and its data ---- */
#define PF_PTRN PF_ID('P', 'T', 'R', 'N')
#define PF_PTRN_HEAD 24
enum { PF_WBP_ROOT = 0, PF_WBP_DRAWER = 1, PF_WBP_SCREEN = 2 };
#define PF_WBPF_PATTERN 0x0001
#define PF_WBPF_NOREMAP 0x0010
#define PF_WBPF_DITHER_MASK 0x0300
#define PF_WBPF_PRECISION_MASK 0x0c00
#define PF_WBPF_PLACEMENT_MASK 0x3000

typedef struct pf_ptrn {
    int which, flags, revision, depth, datalen;
    int picture;                  /* 1: the data names a picture (no PF_WBPF_PATTERN); 0: a drawn pattern */
    char name[256];               /* the picture's path, when picture */
} pf_ptrn;

int pf_ptrn_get(const pf_file *f, int which, pf_ptrn *out); /* 1 found */
int pf_ptrn_set_picture(pf_file *f, int which, const char *name, int flags);   /* a picture, as WBPattern writes it */
int pf_ptrn_set_flags(pf_file *f, int which, int flags);    /* 1, or 0 when there is no such chunk */
void pf_ptrn_remove(pf_file *f, int which);                 /* none: the OS's own backdrop */

/* ---- workbench.prefs: WBNC, struct WorkbenchExtendedPrefs (30 or 50 bytes); WBTF, the title's format ---- */
#define PF_WBNC PF_ID('W', 'B', 'N', 'C')
#define PF_WBTF PF_ID('W', 'B', 'T', 'F')
#define PF_WBHD PF_ID('W', 'B', 'H', 'D')
#define PF_WBNC_BASIC 30
#define PF_WBNC_LEN 50
#define PF_WBF_DRAWERPOSMASK 0x00000003UL
#define PF_WBF_BOUNDTEXTVIEW 0x00000080UL
#define PF_WBF_OLDDATESFIRST 0x00010000UL

typedef struct pf_wbnc {
    uint32_t stack, restart;      /* default stack in bytes; type-ahead restart in seconds */
    int32_t precision;            /* the icons' colour precision: PRECISION_EXACT -1, IMAGE 0, ICON 16, GUI 32 */
    int16_t emboss[4];            /* the icon frame: min x, min y, max x, max y */
    int borderless;
    int32_t maxname;              /* icon label length */
    int newicons, coloricons;
    uint32_t memtype;             /* the rest from 3.9's extension (0 when the chunk is 30 bytes) */
    int lockpens, notitlebar, nogauge;
    uint16_t titledelay;          /* ms */
    uint32_t copybuf, flags;
} pf_wbnc;

int pf_wbnc_get(const pf_file *f, pf_wbnc *out);            /* 1 found; 0 and out the OS's defaults */
int pf_wbnc_put(pf_file *f, const pf_wbnc *in);
int pf_wbtf_get(const pf_file *f, char *out, int size);     /* 1 when there is a title format */
int pf_wbtf_put(pf_file *f, const char *format);           /* "" takes it out (the OS's own title) */

/* The icon frame as OS 3.2's Workbench editor offers it ("Border Size"): 0 none (borderless, the frame
 * -1 -1 1 1), 1 small (-1 -1 1 1), 2 medium (-2 -2 2 2), 3 large (-3 -3 3 3); -1 another frame. */
int pf_wbnc_frame(const pf_wbnc *w);
void pf_wbnc_set_frame(pf_wbnc *w, int size);              /* 0 to 3, the fields that editor writes */

/* WBHD: the devices Workbench hides, a chunk each with the name, its colon and a 0 ("DF0:"), after WBNC */
int pf_wbhd_get(const pf_file *f, int nth, char *out, int size);   /* 1 and the nth hidden device */
int pf_wbhd_is(const pf_file *f, const char *name);        /* 1 when that device is hidden (either case) */
/* The hidden devices: n names. When they are the ones the file has, in any order and either case, the file
 * is left as it is; else every WBHD goes and these are written in this order where the first was (after
 * WBNC when there was none), as the editor writes them. 1, or 0 with no memory. */
int pf_wbhd_set(pf_file *f, const char *const *names, int n);

/* ---- asl.prefs: "ASL ", struct AslPrefs (26 bytes) ---- */
#define PF_ASL PF_ID('A', 'S', 'L', ' ')
#define PF_ASL_LEN 26
typedef struct pf_asl {
    int sortby, sortdrawers, sortorder;     /* ASLFRSORTBY_, ASLFRSORTDRAWERS_, ASLFRSORTORDER_ */
    int sizepos;                  /* ASLPOS_ (bits 0-3), ASLSIZE_ (bits 4-5), ASLOPTION_ASLOverrides (bit 6) */
    int left, top;                /* the offset, for a place relative to a corner */
    int width, height;            /* per cent, for a relative size */
} pf_asl;

int pf_asl_get(const pf_file *f, pf_asl *out);              /* 1 found; 0 and out zero (the OS's defaults) */
int pf_asl_put(pf_file *f, const pf_asl *in);

/* ---- pointer.prefs: PNTR, struct PointerPrefs (32 bytes), its colours and its planes ---- */
#define PF_PNTR PF_ID('P', 'N', 'T', 'R')
#define PF_PNTR_HEAD 32
typedef struct pf_pntr {
    int which;                    /* POINTERTYPE_: 0 normal, 1 busy, ... (OS 3.2 keeps 41) */
    int size, width, height, depth, ysize;
    int x, y;                     /* the hot spot, as an offset (negative) */
    int ncolours;                 /* (1 << depth) - 1 */
    const uint8_t *rgb;           /* ncolours x 3 bytes, in the chunk */
    const uint8_t *planes;        /* depth planes, each height rows of ((width + 15) / 16) words, in the chunk */
} pf_pntr;

int pf_pntr_get(const pf_file *f, int which, pf_pntr *out); /* 1 found and whole */
int pf_pntr_pixel(const pf_pntr *p, int x, int y);          /* the colour (0 clear) at x, y */
int pf_pntr_set_hotspot(pf_file *f, int which, int x, int y);
int pf_pntr_take(pf_file *f, const pf_file *from, int which);   /* that pointer from another file (a set) */
int pf_pntr_take_all(pf_file *f, const pf_file *from);       /* every pointer from another file */
/* Drawing, as OS 3.2's Pointer editor writes it: a pixel's bits in each plane of that pointer (colour 0 clear);
 * a colour (1 to 3) in every pointer of the file, which share them; every pixel of one pointer clear. */
int pf_pntr_set_pixel(pf_file *f, int which, int x, int y, int colour);   /* 1, or 0 (no such pointer or pixel) */
int pf_pntr_set_colour(pf_file *f, int colour, const uint8_t rgb[3]);    /* 1 when a pointer has that colour */
int pf_pntr_clear(pf_file *f, int which);

#endif /* PF_PREFS_H */
