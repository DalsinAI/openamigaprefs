/* barlogo.h: what the start of the Workbench screen's title holds, shared by OpenTitle and OpenBench (the Team,
 * 11 October 2026).
 *
 * The room for the logo. OpenTitle draws the logo in a window of its own at the bar's left edge: the bar's height
 * less 2, aspect correct (its picture is 64 x 44), 2 pixels in, in a window 4 wider than the logo. The title
 * leaves room for it by starting with spaces of the bar's own font: enough that the text starts BARLOGO_GAP pixels
 * past the logo's window, and never fewer than BARLOGO_MIN_SPACES (OpenTitle tells a title that has room for the
 * logo by those first spaces, as it did when the room was always 9 spaces). A title that starts at the bar's left
 * edge is another program's, and OpenTitle hides the logo for it.
 *
 * What the cores are. The CPU cores board (Dalsin $DA15, product 7, the ACSV block with CLASS 2) gives its cores
 * in UNITS ($24) and the instruction sets they run in ISAS ($28): bit 0 m68k (always: every core runs 68k code),
 * bit 1 x86-64, bit 2 ARM64 (docs/architecture/CORES_BOARD.md in AmigaChrome, section 0a). The title reads
 * "CPU = 1 (m68k) + 8 (m68k)", or "+ 8 (m68k, x86-64)" when the host also runs native jobs on them (the Team:
 * "the screen menu should indicate what the cores are, + 8 (m68k) x86 or whatever").
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef BARLOGO_H
#define BARLOGO_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#define BARLOGO_PIC_W 64                /* the logo's picture (OpenTitle's logo_big.h) */
#define BARLOGO_PIC_H 44
#define BARLOGO_GAP 6                   /* between the logo's window and the title's first letter */
#define BARLOGO_MIN_SPACES 9
#define BARLOGO_MAX_SPACES 40

/* The logo's height for a bar of bar_h pixels, and its width in *w. */
static inline WORD barlogo_size(WORD bar_h, WORD *w)
{
    WORD h = bar_h - 2;
    if (h > BARLOGO_PIC_H) h = BARLOGO_PIC_H;
    if (h < 8) h = 8;
    *w = (WORD)((h * BARLOGO_PIC_W + BARLOGO_PIC_H / 2) / BARLOGO_PIC_H);
    return h;
}

/* The width of the logo's window in the bar. */
static inline WORD barlogo_window_w(WORD bar_h)
{
    WORD w;
    barlogo_size(bar_h, &w);
    return w + 4;
}

/* How many spaces of the bar's font the title starts with on screen s. */
static inline int barlogo_spaces(struct Screen *s)
{
    struct RastPort rp;
    WORD sp, need;
    int n;
    if (!s || !s->RastPort.Font) return BARLOGO_MIN_SPACES;
    InitRastPort(&rp);
    SetFont(&rp, s->RastPort.Font);
    sp = TextLength(&rp, (STRPTR)" ", 1);
    if (sp < 1) sp = 1;
    need = barlogo_window_w(s->BarHeight) + BARLOGO_GAP - s->BarHBorder;
    n = (need + sp - 1) / sp;
    if (n < BARLOGO_MIN_SPACES) n = BARLOGO_MIN_SPACES;
    if (n > BARLOGO_MAX_SPACES) n = BARLOGO_MAX_SPACES;
    return n;
}

/* The spaces themselves, in buf (at least BARLOGO_MAX_SPACES + 1 bytes). */
static inline char *barlogo_pad(struct Screen *s, char *buf)
{
    int i, n = barlogo_spaces(s);
    for (i = 0; i < n; i++) buf[i] = ' ';
    buf[n] = 0;
    return buf;
}

/* "m68k", "m68k, x86-64", "m68k, arm64" or "m68k, x86-64, arm64" for the board's ISAS register. */
static inline const char *barlogo_isas(ULONG isas)
{
    static const char *const names[4] = { "m68k", "m68k, x86-64", "m68k, arm64", "m68k, x86-64, arm64" };
    return names[(isas >> 1) & 3];
}

#endif
