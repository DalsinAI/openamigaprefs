/* pr_prefs: the paper settings the Printing section edits, in printer.prefs (PTXT) and printergfx.prefs (PGFX), the
 * files printer.device reads for unit 0 (OpenNDK prefs/printertxt.h and prefs/printergfx.h, NDK 3.2's layouts).
 *
 *   printer.prefs, PTXT:    pt_PaperSize, pt_PaperLength (lines a page), pt_LeftMargin, pt_RightMargin (columns)
 *   printergfx.prefs, PGFX: pg_Aspect (a picture across the paper or turned), pg_PrintDensity (1 to 7)
 *
 * As ic_prefs does for IControl: a put writes the fields it knows and leaves every other byte of the chunk as the
 * file had it (the driver's name, the port, the pitch, spacing and quality, the shading, the reserved longs), and
 * every other chunk (PUNT, PDEV, any the section doesn't know), so settings put back unchanged give the file's own
 * bytes. A file with no PTXT or PGFX gets one as printer.device takes a missing one (its no-file state).
 *
 * With no file at all, pr_new_txt and pr_new_gfx make one: with OpenPrint, the bytes openamigaup's OpenPrinter part
 * writes (tools/build_pack.py printer_prefs and printergfx_prefs: OpenPrint's driver on oapspool.device unit 0, A4,
 * 66 lines, margins 5 to 75, density 2); without it, printer.device's no-file state (amigachrome-guest
 * devices/openprinter.device, opr_default_prefs: the generic driver on the parallel port), so a save changes
 * nothing but the fields the page shows. Plain C: tested on the host by Printing/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef PR_PREFS_H
#define PR_PREFS_H

#include "pf_iff.h"

#define PR_PTXT PF_ID('P', 'T', 'X', 'T')
#define PR_PUNT PF_ID('P', 'U', 'N', 'T')
#define PR_PDEV PF_ID('P', 'D', 'E', 'V')
#define PR_PGFX PF_ID('P', 'G', 'F', 'X')
#define PR_PTXT_LEN 64                    /* struct PrinterTxtPrefs, big-endian, 68k alignment */
#define PR_PUNT_LEN 56                    /* struct PrinterUnitPrefs */
#define PR_PDEV_LEN 52                    /* struct PrinterDeviceUnitPrefs */
#define PR_PGFX_LEN 38                    /* struct PrinterGfxPrefs */

/* pt_PaperSize (prefs/printertxt.h) */
enum { PR_US_LETTER, PR_US_LEGAL, PR_N_TRACTOR, PR_W_TRACTOR, PR_CUSTOM, PR_A0, PR_A1, PR_A2, PR_A3, PR_A4, PR_A5,
       PR_A6, PR_A7, PR_A8, PR_PAPERS };

/* pg_Aspect (prefs/printergfx.h) */
#define PR_HORIZONTAL 0
#define PR_VERTICAL 1

#define PR_LINES_MAX 999
#define PR_COLUMN_MAX 999
#define PR_DENSITY_MAX 7

typedef struct pr_prefs {
    int paper;                    /* PR_ paper size */
    int lines;                    /* lines a page, 1 to PR_LINES_MAX */
    int left, right;              /* the margins, in columns: 1 <= left <= right <= PR_COLUMN_MAX */
    int aspect;                   /* PR_HORIZONTAL or PR_VERTICAL */
    int density;                  /* 1 to PR_DENSITY_MAX */
} pr_prefs;

/* the paper sizes in the order the page lists them (A4 first), and each one's name */
extern const unsigned char pr_paper_order[PR_PAPERS];
const char *pr_paper_name(int paper);                       /* "A4", "US Letter"...; "A4" for an unknown one */
int pr_paper_named(const char *word);                       /* A4, LETTER, LEGAL... in either case; -1 for none */
int pr_paper_openprint(int paper);                          /* 1 when OpenPrint's driver makes a page that size */
int pr_aspect_named(const char *word);                      /* HORIZONTAL or VERTICAL, in either case; -1 for none */
int pr_density_dpi(int density);                            /* OpenPrint's driver's dots to the inch at a density */

void pr_fix(pr_prefs *p);                                   /* every field in its range */
int pr_get(const pf_file *txt, const pf_file *gfx, pr_prefs *out);   /* 1 PTXT found, 2 PGFX found (both 3) */
int pr_put(pf_file *txt, pf_file *gfx, const pr_prefs *in);          /* 1, or 0 with no memory or a full file */

int pr_new_txt(pf_file *f, int openprint);                  /* a new printer.prefs (above); 1, or 0 with no memory */
int pr_new_gfx(pf_file *f, int openprint);                  /* a new printergfx.prefs */

/* Where printer.prefs sends what is printed, in words: "OpenPrint's driver, oapspool.device unit 0" */
void pr_route(const pf_file *txt, char *buf, int size);

#endif /* PR_PREFS_H */
