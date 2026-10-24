/* pr_prefs: printer.prefs and printergfx.prefs, the fields the Printing section edits (pr_prefs.h). The offsets are
 * those of OpenNDK's struct PrinterTxtPrefs, PrinterUnitPrefs, PrinterDeviceUnitPrefs and PrinterGfxPrefs,
 * big-endian with the 68k's two-byte alignment (amigachrome-guest's printer_core.cpp asserts the same sizes).
 * Plain C: tested on the host by Printing/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "pr_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PTXT */
#define T_DRIVER 16                       /* pt_Driver[30] */
#define T_DRIVER_LEN 30
#define T_PORT 46
#define T_TYPE 48
#define T_SIZE 50
#define T_LENGTH 52
#define T_PITCH 54
#define T_SPACING 56
#define T_LEFT 58
#define T_RIGHT 60
#define T_QUALITY 62
/* PGFX */
#define G_ASPECT 16
#define G_SHADE 18
#define G_THRESHOLD 22
#define G_DENSITY 30
#define G_MAXWIDTH 32
#define G_MAXHEIGHT 34
/* PUNT and PDEV */
#define U_UNIT 16
#define U_NAME 24                         /* pu_DeviceName[32], without ".device" */
#define D_UNIT 16
#define D_NAME 20                         /* pd_UnitName[32] */
#define NAME_LEN 32

const unsigned char pr_paper_order[PR_PAPERS] = { PR_A4, PR_US_LETTER, PR_US_LEGAL, PR_A3, PR_A5, PR_A0, PR_A1, PR_A2,
                                                  PR_A6, PR_A7, PR_A8, PR_N_TRACTOR, PR_W_TRACTOR, PR_CUSTOM };

static const char *const paper_names[PR_PAPERS] = { "US Letter", "US Legal", "Narrow tractor", "Wide tractor", "Custom",
                                                    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8" };
/* the Shell's words for them (OpenPrefs PRINTING PAPER letter) */
static const char *const paper_words[PR_PAPERS] = { "LETTER", "LEGAL", "NARROW", "WIDE", "CUSTOM",
                                                    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8" };

const char *pr_paper_name(int paper) { return paper >= 0 && paper < PR_PAPERS ? paper_names[paper] : paper_names[PR_A4]; }

static int same_word(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (x != y) return 0;
    }
    return !*a && !*b;
}

int pr_paper_named(const char *word)
{
    int i;
    if (!word) return -1;
    for (i = 0; i < PR_PAPERS; i++)
        if (same_word(word, paper_words[i]) || same_word(word, paper_names[i])) return i;
    if (same_word(word, "USLETTER")) return PR_US_LETTER;
    if (same_word(word, "USLEGAL")) return PR_US_LEGAL;
    return -1;
}

/* openamigaprint's src/driver/openprint_driver.c (geometry): A3, A4, A5, US Letter and US Legal; any other is A4 */
int pr_paper_openprint(int paper)
{
    return paper == PR_A3 || paper == PR_A4 || paper == PR_A5 || paper == PR_US_LETTER || paper == PR_US_LEGAL;
}

int pr_aspect_named(const char *word)
{
    if (!word) return -1;
    if (same_word(word, "HORIZONTAL")) return PR_HORIZONTAL;
    if (same_word(word, "VERTICAL")) return PR_VERTICAL;
    return -1;
}

/* openprint_driver.c (set_density): 72, 100, 150, 200, 300, 600 and 1200 dots to the inch */
int pr_density_dpi(int density)
{
    static const short dpi[PR_DENSITY_MAX] = { 72, 100, 150, 200, 300, 600, 1200 };
    return density >= 1 && density <= PR_DENSITY_MAX ? dpi[density - 1] : dpi[0];
}

void pr_fix(pr_prefs *p)
{
    if (p->paper < 0 || p->paper >= PR_PAPERS) p->paper = PR_A4;
    if (p->lines < 1) p->lines = 1;
    if (p->lines > PR_LINES_MAX) p->lines = PR_LINES_MAX;
    if (p->left < 1) p->left = 1;
    if (p->left > PR_COLUMN_MAX) p->left = PR_COLUMN_MAX;
    if (p->right > PR_COLUMN_MAX) p->right = PR_COLUMN_MAX;
    if (p->right < p->left) p->right = p->left;
    p->aspect = p->aspect == PR_VERTICAL ? PR_VERTICAL : PR_HORIZONTAL;
    if (p->density < 1) p->density = 1;
    if (p->density > PR_DENSITY_MAX) p->density = PR_DENSITY_MAX;
}

static void put_name(uint8_t *at, int size, const char *name)
{
    int n = (int)strlen(name);
    memset(at, 0, (size_t)size);
    memcpy(at, name, (size_t)(n < size ? n : size));
}

/* A chunk's bytes when there is no file: OpenPrinter's (openprint) or printer.device's own no-file state */
static void txt_chunk(uint8_t *b, int openprint)
{
    memset(b, 0, PR_PTXT_LEN);
    put_name(b + T_DRIVER, T_DRIVER_LEN, openprint ? "OpenPrint" : "generic");
    if (openprint) {
        pf_put16(b + T_TYPE, 1);                                 /* single sheets */
        pf_put16(b + T_SIZE, PR_A4);
        pf_put16(b + T_QUALITY, 1);                              /* letter quality */
    }
    pf_put16(b + T_LENGTH, 66);
    pf_put16(b + T_LEFT, 5);
    pf_put16(b + T_RIGHT, 75);
}

static void gfx_chunk(uint8_t *b, int openprint)
{
    memset(b, 0, PR_PGFX_LEN);
    pf_put16(b + G_THRESHOLD, 7);
    if (openprint) {
        pf_put16(b + G_SHADE, 2);                                /* colour */
        b[G_DENSITY] = 2;                                        /* OpenPrint's driver: 100 dots to the inch */
    } else {
        b[G_DENSITY] = 1;
        pf_put16(b + G_MAXWIDTH, 80);
        pf_put16(b + G_MAXHEIGHT, 110);
    }
}

int pr_new_txt(pf_file *f, int openprint)
{
    uint8_t t[PR_PTXT_LEN], u[PR_PUNT_LEN], d[PR_PDEV_LEN];
    txt_chunk(t, openprint);
    if (!pf_new(f) || !pf_add(f, PR_PTXT, t, sizeof t)) { pf_free(f); return 0; }
    if (!openprint) return 1;                    /* no PUNT: the port pt_Port names; no PDEV: unit 0's own name */
    memset(u, 0, sizeof u);                      /* unit 0, no flags */
    put_name(u + U_NAME, NAME_LEN, "oapspool");  /* oapspool.device, OpenPrint's spooler */
    memset(d, 0, sizeof d);
    put_name(d + D_NAME, NAME_LEN, "OpenPrint");
    if (!pf_add(f, PR_PUNT, u, sizeof u) || !pf_add(f, PR_PDEV, d, sizeof d)) { pf_free(f); return 0; }
    return 1;
}

int pr_new_gfx(pf_file *f, int openprint)
{
    uint8_t g[PR_PGFX_LEN];
    gfx_chunk(g, openprint);
    if (!pf_new(f) || !pf_add(f, PR_PGFX, g, sizeof g)) { pf_free(f); return 0; }
    return 1;
}

int pr_get(const pf_file *txt, const pf_file *gfx, pr_prefs *out)
{
    uint8_t t[PR_PTXT_LEN], g[PR_PGFX_LEN];
    int i = txt ? pf_find(txt, PR_PTXT, 0) : -1, j = gfx ? pf_find(gfx, PR_PGFX, 0) : -1, found = 0;
    /* no chunk: printer.device's no-file state; a short one: the rest zero, as printer.device reads it */
    txt_chunk(t, 0);
    gfx_chunk(g, 0);
    if (i >= 0) {
        memset(t, 0, sizeof t);
        memcpy(t, txt->ch[i].data, txt->ch[i].len < sizeof t ? txt->ch[i].len : sizeof t);
        found |= 1;
    }
    if (j >= 0) {
        memset(g, 0, sizeof g);
        memcpy(g, gfx->ch[j].data, gfx->ch[j].len < sizeof g ? gfx->ch[j].len : sizeof g);
        found |= 2;
    }
    out->paper = pf_get16(t + T_SIZE);
    out->lines = pf_get16(t + T_LENGTH);
    out->left = pf_get16(t + T_LEFT);
    out->right = pf_get16(t + T_RIGHT);
    out->aspect = pf_get16(g + G_ASPECT);
    out->density = g[G_DENSITY];
    pr_fix(out);
    return found;
}

/* Chunk id in f with the fields set() sets, every other byte as the file had it. A short chunk grows to len with
 * zeros (as printer.device reads it), a longer one keeps its length; with none, one is added from base. */
static int put_chunk(pf_file *f, uint32_t id, const uint8_t *base, uint32_t len, const pr_prefs *in,
                     void (*set)(uint8_t *, const pr_prefs *))
{
    int i = pf_find(f, id, 0), ok;
    uint32_t have = i >= 0 ? f->ch[i].len : 0, n = have > len ? have : len;
    uint8_t *b = malloc(n);
    if (!b) return 0;
    if (i >= 0) {
        memset(b, 0, n);
        if (have) memcpy(b, f->ch[i].data, have);
    } else memcpy(b, base, len);
    set(b, in);
    ok = i >= 0 ? pf_set(f, i, b, n) : pf_add(f, id, b, n);
    free(b);
    return ok;
}

static void set_txt(uint8_t *b, const pr_prefs *in)
{
    pf_put16(b + T_SIZE, (uint16_t)in->paper);
    pf_put16(b + T_LENGTH, (uint16_t)in->lines);
    pf_put16(b + T_LEFT, (uint16_t)in->left);
    pf_put16(b + T_RIGHT, (uint16_t)in->right);
}

static void set_gfx(uint8_t *b, const pr_prefs *in)
{
    pf_put16(b + G_ASPECT, (uint16_t)in->aspect);
    b[G_DENSITY] = (uint8_t)in->density;
}

int pr_put(pf_file *txt, pf_file *gfx, const pr_prefs *in)
{
    uint8_t t[PR_PTXT_LEN], g[PR_PGFX_LEN];
    pr_prefs p = *in;
    pr_fix(&p);
    txt_chunk(t, 0);
    gfx_chunk(g, 0);
    if (txt && !put_chunk(txt, PR_PTXT, t, sizeof t, &p, set_txt)) return 0;
    if (gfx && !put_chunk(gfx, PR_PGFX, g, sizeof g, &p, set_gfx)) return 0;
    return 1;
}

/* a NUL-ended copy of a name field of at most size bytes */
static void name_of(char *out, const uint8_t *at, int size)
{
    int n = 0;
    while (n < size && at[n]) { out[n] = (char)at[n]; n++; }
    out[n] = 0;
}

void pr_route(const pf_file *txt, char *buf, int size)
{
    char driver[T_DRIVER_LEN + 1], device[NAME_LEN + 8];
    long unit = 0;
    int i = pf_find(txt, PR_PTXT, 0), j = pf_find(txt, PR_PUNT, 0), serial = 0;
    strcpy(driver, "generic");                   /* printer.device's, with no PTXT or no name in it */
    if (i >= 0 && txt->ch[i].len > T_DRIVER && txt->ch[i].data[T_DRIVER]) {
        int n = (int)txt->ch[i].len - T_DRIVER;
        name_of(driver, txt->ch[i].data + T_DRIVER, n < T_DRIVER_LEN ? n : T_DRIVER_LEN);
    }
    if (i >= 0 && txt->ch[i].len > T_PORT) serial = txt->ch[i].data[T_PORT] == 1;
    strcpy(device, serial ? "serial.device" : "parallel.device");
    if (j >= 0 && txt->ch[j].len >= U_UNIT + 4) unit = (long)(int32_t)pf_get32(txt->ch[j].data + U_UNIT);
    if (j >= 0 && txt->ch[j].len > U_NAME && txt->ch[j].data[U_NAME]) {
        int n = (int)txt->ch[j].len - U_NAME, k;
        name_of(device, txt->ch[j].data + U_NAME, n < NAME_LEN ? n : NAME_LEN);
        k = (int)strlen(device);
        if (k < 7 || strcmp(device + k - 7, ".device")) strcat(device, ".device");   /* kept without it, as the editor did */
    }
    snprintf(buf, (size_t)size, "%s driver, %s unit %ld", driver, device, unit);
}
