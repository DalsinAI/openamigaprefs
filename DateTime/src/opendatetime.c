/* OpenDateTime: the date, the time and the region in one OpenPrefs editor. It
 * takes over the OS's Time and Locale editors (OpenUp's RECONCILE puts those in
 * SYS:Prefs/Classic); a double-click on OpenTitle's clock opens it.
 *
 *   OpenDateTime [FROM file] [USE] [SAVE]
 *
 *   - the date (day, month, year) and the time (hours, minutes, seconds), with
 *     Now to read the clock again; set with Use or Save when changed, through
 *     C:Date, and kept in the battery clock with C:SetClock SAVE
 *   - the time zone, as hours from UTC (Locale prefs' GMT offset)
 *   - the country (LOCALE:Countries) and the preferred language
 *     (LOCALE:Languages), as Locale prefs keeps them
 *
 * Save writes ENV:Sys/Locale.prefs and ENVARC:; Use writes ENV: only (IPrefs
 * reads it at once); Cancel changes nothing. Everything in Locale prefs this
 * editor doesn't show is kept as it was.
 *
 * Built into OpenBench (the Team, 11 October 2026: the Open preferences in OpenBench natively) with
 * -DIN_OPENBENCH, as openamigabench desktop/ob_builtin.h says: OpenBench starts it as a process of its
 * own with no arguments, as Workbench starts a tool, so it is the window; Ctrl-C is Cancel, Ctrl-F
 * brings the window to the front; utility.library is opened under this editor's own name.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <utility/date.h>
#include <utility/hooks.h>
#include <prefs/prefhdr.h>
#include <prefs/locale.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char version[] __attribute__((used)) = "$VER: OpenDateTime 0.1.2 (12.10.2026) OpenPrefs, MIT, Copyright (c) 2026 Dalsin Limited";

#ifdef IN_OPENBENCH
/* inside OpenBench, a global UtilityBase could be another built-in's: every UtilityBase below, and in
 * utility.library's calls, is this editor's own */
static struct Library *opendatetime_utility;
#define UtilityBase opendatetime_utility
#else
struct Library *UtilityBase;
#endif

/* 0 when two names are the same, case aside (utility.library's Stricmp: libnix has no stricmp) */
static LONG same_name(const char *a, const char *b)
{
    LONG r = Stricmp((CONST_STRPTR)a, (CONST_STRPTR)b);
    return r;
}

#define LOCALE_ENV "ENV:Sys/Locale.prefs"
#define LOCALE_ENVARC "ENVARC:Sys/Locale.prefs"
#define MAX_NAMES 96

/* ---- Locale prefs ---------------------------------------------------------- */

static struct LocalePrefs lp;
static UBYTE *file_data;                        /* the whole file read, to keep its other chunks */
static LONG file_len;
static int region_saved;                        /* 1 when Locale prefs were read; 0: the defaults below */
static LONG uk_offset_today(void);

static ULONG get32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }
static void put32(UBYTE *p, ULONG v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static UBYTE *read_all(const char *path, LONG *lenp)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    UBYTE *buf = NULL;
    LONG len = 0;
    if (fh) {
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len > 0 && (buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) {
            if (Read(fh, buf, len) != len) { FreeVec(buf); buf = NULL; }
        }
        Close(fh);
    }
    if (lenp) *lenp = buf ? len : 0;
    return buf;
}

/* The data of the first chunk called id in an IFF PREF file, and its length. */
static UBYTE *chunk(UBYTE *d, LONG len, const char *id, LONG *n)
{
    LONG k;
    for (k = 12; d && k + 8 <= len; ) {
        LONG cl = (LONG)get32(d + k + 4);
        if (cl < 0 || k + 8 + cl > len) break;
        if (!memcmp(d + k, id, 4)) { if (n) *n = cl; return d + k + 8; }
        k += 8 + cl + (cl & 1);
    }
    return NULL;
}

/* Locale prefs from path. With none saved (no file, or no LCLE in it), the defaults: the United Kingdom,
 * english, and the United Kingdom's zone today (UTC+1:00 in British Summer Time). locale.library's own
 * defaults (united_states, its GMT offset -300) are not a region anyone chose, so they aren't shown. */
static void load_locale(const char *path)
{
    UBYTE *c;
    LONG n = 0;
    memset(&lp, 0, sizeof lp);
    strcpy(lp.lp_CountryName, "united_kingdom");
    strcpy(lp.lp_PreferredLanguages[0], "english");
    region_saved = 0;
    if (file_data) { FreeVec(file_data); file_data = NULL; }
    file_data = read_all(path, &file_len);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4) && !memcmp(file_data + 8, "PREF", 4) &&
        (c = chunk(file_data, file_len, "LCLE", &n))) {
        memcpy(&lp, c, n < (LONG)sizeof lp ? n : (LONG)sizeof lp);
        region_saved = 1;
    } else lp.lp_GMTOffset = uk_offset_today();
}

static int write_file(const char *path, const UBYTE *data, LONG len)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    LONG got;
    if (!fh) return 0;
    got = Write(fh, (APTR)data, len);
    Close(fh);
    return got == len;
}

/* The file again: its PRHD and other chunks as they were, LCLE with lp. */
static int save_locale(const char *path)
{
    LONG size = 12 + 14 + 8 + sizeof lp + file_len + 16, o = 12, k;
    UBYTE *out = AllocVec(size, MEMF_ANY | MEMF_CLEAR);
    int ok, had_prhd = 0;
    if (!out) return 0;
    memcpy(out, "FORM\0\0\0\0PREF", 12);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4)) {
        for (k = 12; k + 8 <= file_len; ) {
            LONG cl = (LONG)get32(file_data + k + 4), all = 8 + cl + (cl & 1);
            if (cl < 0 || k + all > file_len) break;
            if (memcmp(file_data + k, "LCLE", 4)) {
                if (!memcmp(file_data + k, "PRHD", 4)) had_prhd = 1;
                memcpy(out + o, file_data + k, all);
                o += all;
            }
            k += all;
        }
    }
    if (!had_prhd) {                                   /* a new file: its header first */
        memmove(out + 12 + 14, out + 12, o - 12);
        memcpy(out + 12, "PRHD\0\0\0\6\0\0\0\0\0\0", 14);
        o += 14;
    }
    memcpy(out + o, "LCLE", 4);
    put32(out + o + 4, sizeof lp);
    memcpy(out + o + 8, &lp, sizeof lp);
    o += 8 + sizeof lp + (sizeof lp & 1);
    put32(out + 4, o - 8);
    ok = write_file(path, out, o);
    FreeVec(out);
    return ok;
}

/* A country file's CountryPrefs (its CTRY chunk) into lp. */
static int load_country(const char *name)
{
    char path[160];
    LONG len = 0, n = 0;
    UBYTE *d, *c;
    snprintf(path, sizeof path, "LOCALE:Countries/%s.country", name);
    if (!(d = read_all(path, &len))) return 0;
    c = chunk(d, len, "CTRY", &n);
    if (c) {
        memcpy(&lp.lp_CountryData, c, n < (LONG)sizeof lp.lp_CountryData ? n : (LONG)sizeof lp.lp_CountryData);
        strncpy(lp.lp_CountryName, name, sizeof lp.lp_CountryName - 1);
    }
    FreeVec(d);
    return c != NULL;
}

/* ---- names of countries and languages -------------------------------------- */

static char names_c[MAX_NAMES][32], names_l[MAX_NAMES][30];
static struct Node nodes_c[MAX_NAMES], nodes_l[MAX_NAMES];
static struct List list_c, list_l;
static int count_c, count_l;

static int by_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* The files in dir ending ext, without it, sorted, into names (each width wide). */
static int list_dir(const char *dir, const char *ext, char *names, int width)
{
    BPTR lock = Lock((STRPTR)dir, ACCESS_READ);
    struct FileInfoBlock *fib;
    int n = 0, el = strlen(ext);
    if (!lock) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL))) {
        if (Examine(lock, fib)) {
            while (n < MAX_NAMES && ExNext(lock, fib)) {
                const char *fn = (const char *)fib->fib_FileName;
                int l = strlen(fn);
                if (fib->fib_DirEntryType > 0 || l <= el || l - el >= width) continue;
                if (same_name(fn + l - el, ext)) continue;
                memcpy(names + n * width, fn, l - el);
                names[n * width + l - el] = 0;
                n++;
            }
        }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    qsort(names, n, width, by_name);
    return n;
}

static void make_list(struct List *list, struct Node *nodes, char *names, int width, int n)
{
    int i;
    list->lh_Head = (struct Node *)&list->lh_Tail;      /* NewList, which is amiga.lib's */
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
    for (i = 0; i < n; i++) {
        nodes[i].ln_Name = names + i * width;
        AddTail(list, &nodes[i]);
    }
}

static int index_of(char *names, int width, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++) if (!same_name(names + i * width, name)) return i;
    return -1;
}

/* ---- the clock --------------------------------------------------------------- */

static struct ClockData now_cd, shown_cd;      /* the clock when read, and what the gadgets were given */

static void read_clock(struct ClockData *cd)
{
    struct DateStamp ds;
    ULONG secs;
    DateStamp(&ds);
    secs = ds.ds_Days * 86400UL + ds.ds_Minute * 60UL + ds.ds_Tick / TICKS_PER_SECOND;
    Amiga2Date(secs, cd);
}

/* The United Kingdom's zone on the clock's date, as Locale prefs keeps it (minutes west): -60 (UTC+1:00) in
 * British Summer Time, from the last Sunday of March to the last Sunday of October, else 0. AmigaOS keeps no
 * summer time of its own, so this is only the default shown when no region is saved. */
static LONG uk_offset_today(void)
{
    struct ClockData now, last;
    int last_sunday, after;
    read_clock(&now);
    if (now.month > 3 && now.month < 10) return -60;
    if (now.month < 3 || now.month > 10) return 0;
    memset(&last, 0, sizeof last);                     /* the 31st of March or October, for its weekday */
    last.mday = 31;
    last.month = now.month;
    last.year = now.year;
    last.hour = 12;
    Amiga2Date(Date2Amiga(&last), &last);
    last_sunday = 31 - last.wday;
    after = now.mday > last_sunday || (now.mday == last_sunday && now.hour >= (now.month == 3 ? 1 : 2));
    return now.month == 3 ? (after ? -60 : 0) : (after ? 0 : -60);
}

static const char *const month_names[] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                           "September", "October", "November", "December", NULL };
static const char *const month_dos[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* Sets the system clock with C:Date and keeps it in the battery clock. 1 done. */
static int set_clock(const struct ClockData *cd)
{
    char cmd[96];
    BPTR nil;
    LONG rc;
    if (cd->year < 1978 || cd->year > 2077) return 0;   /* what C:Date's two-digit years reach */
    snprintf(cmd, sizeof cmd, "C:Date %02d-%s-%02d %02d:%02d:%02d", cd->mday, month_dos[cd->month - 1], cd->year % 100,
             cd->hour, cd->min, cd->sec);
    nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    rc = SystemTags((STRPTR)cmd, SYS_Input, (ULONG)nil, SYS_Output, (ULONG)NULL, TAG_DONE);
    if (rc == 0) rc = SystemTags((STRPTR)"C:SetClock SAVE", SYS_Input, (ULONG)nil, SYS_Output, (ULONG)NULL, TAG_DONE);
    if (nil) Close(nil);
    return rc == 0;
}

/* ---- time zones -------------------------------------------------------------- */

#define ZONES 53                                /* UTC-12:00 to UTC+14:00, every half hour */
static char zone_text[ZONES][12];
static const char *zone_labels[ZONES + 1];

static void make_zones(void)
{
    int i;
    for (i = 0; i < ZONES; i++) {
        int m = -720 + i * 30, a = m < 0 ? -m : m;
        snprintf(zone_text[i], sizeof zone_text[i], "UTC%c%d:%02d", m < 0 ? '-' : '+', a / 60, a % 60);
        zone_labels[i] = zone_text[i];
    }
    zone_labels[ZONES] = NULL;
}

/* Locale prefs keeps minutes west of Greenwich: UTC+1 is -60. */
static int zone_of(LONG gmt_offset)
{
    LONG east = -gmt_offset;
    int i = (int)((east + 720) / 30);
    return i < 0 ? 0 : i >= ZONES ? ZONES - 1 : i;
}

/* ---- the lists' rows ------------------------------------------------------------ */

/* GadTools draws a list row's name in JAM2, and on a themed screen OpenLook gives that text a cell of its own
 * colour, so the picked row showed a box beside its bar (0.1); 0.1.1 took the pick's highlight away with the
 * box. Each row is drawn here instead: the whole row in FILLPEN when picked (BACKGROUNDPEN when not), then the
 * name in JAM1 over it, so the bar is one colour from end to end. */
static ULONG row_draw(struct Hook *hook __asm("a0"), struct Node *node __asm("a2"), struct LVDrawMsg *msg __asm("a1"))
{
    struct RastPort *rp;
    UWORD *pens;
    struct Rectangle *b;
    struct TextExtent te;
    LONG len, fit, h;
    int picked;
    (void)hook;
    if (msg->lvdm_MethodID != LV_DRAW) return LVCB_UNKNOWN;
    rp = msg->lvdm_RastPort;
    pens = msg->lvdm_DrawInfo->dri_Pens;
    b = &msg->lvdm_Bounds;
    picked = msg->lvdm_State == LVR_SELECTED || msg->lvdm_State == LVR_SELECTEDDISABLED;
    SetDrMd(rp, JAM1);
    SetAPen(rp, picked ? pens[FILLPEN] : pens[BACKGROUNDPEN]);
    RectFill(rp, b->MinX, b->MinY, b->MaxX, b->MaxY);
    len = node && node->ln_Name ? (LONG)strlen((const char *)node->ln_Name) : 0;
    h = b->MaxY - b->MinY + 1;
    fit = len ? (LONG)TextFit(rp, (STRPTR)node->ln_Name, len, &te, NULL, 1, b->MaxX - b->MinX + 1 - 4, h) : 0;
    if (fit > 0) {
        SetAPen(rp, picked ? pens[FILLTEXTPEN] : pens[TEXTPEN]);
        Move(rp, b->MinX + 2, b->MinY + (h - rp->TxHeight) / 2 + rp->TxBaseline);
        Text(rp, (STRPTR)node->ln_Name, fit);
    }
    return LVCB_OK;
}

static struct Hook row_hook;

/* ---- the window --------------------------------------------------------------- */

enum { G_DAY, G_MONTH, G_YEAR, G_HOUR, G_MIN, G_SEC, G_NOW, G_ZONE, G_COUNTRY, G_LANGUAGE, G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT };
static struct Gadget *gad[G_COUNT];
static struct Window *win;
static char status_text[160];
static int sel_zone, sel_country = -1, sel_language = -1, month_sel;

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

static LONG number(int id)
{
    LONG v = 0;
    if (gad[id]) GT_GetGadgetAttrs(gad[id], win, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
    return v;
}

static void show_clock(void)
{
    read_clock(&now_cd);
    shown_cd = now_cd;
    month_sel = now_cd.month - 1;
    if (!win) return;
    GT_SetGadgetAttrs(gad[G_DAY], win, NULL, GTIN_Number, (ULONG)now_cd.mday, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_MONTH], win, NULL, GTCY_Active, (ULONG)month_sel, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_YEAR], win, NULL, GTIN_Number, (ULONG)now_cd.year, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_HOUR], win, NULL, GTIN_Number, (ULONG)now_cd.hour, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_MIN], win, NULL, GTIN_Number, (ULONG)now_cd.min, TAG_DONE);
    GT_SetGadgetAttrs(gad[G_SEC], win, NULL, GTIN_Number, (ULONG)now_cd.sec, TAG_DONE);
}

/* What Use or Save does: the clock when its fields changed, then Locale prefs. 1 done. */
static int put_in_place(int save)
{
    struct ClockData cd;
    int clock_changed;
    memset(&cd, 0, sizeof cd);
    cd.mday = (UWORD)number(G_DAY);
    cd.month = (UWORD)(month_sel + 1);
    cd.year = (UWORD)number(G_YEAR);
    cd.hour = (UWORD)number(G_HOUR);
    cd.min = (UWORD)number(G_MIN);
    cd.sec = (UWORD)number(G_SEC);
    clock_changed = cd.mday != shown_cd.mday || cd.month != shown_cd.month || cd.year != shown_cd.year ||
                    cd.hour != shown_cd.hour || cd.min != shown_cd.min || cd.sec != shown_cd.sec;
    if (clock_changed) {
        ULONG check = CheckDate(&cd);
        if (!check || cd.hour > 23 || cd.min > 59 || cd.sec > 59) { status("That isn't a date and time: nothing was changed."); return 0; }
        if (!set_clock(&cd)) { status("The clock couldn't be set (C:Date or C:SetClock failed)."); return 0; }
    }
    lp.lp_GMTOffset = -(-720 + sel_zone * 30);
    /* the country's formats: when another country was picked, or when there are none yet (nothing was saved,
     * and the record would otherwise go out with empty date and number formats) */
    if (sel_country >= 0 && (same_name(names_c[sel_country], lp.lp_CountryName) || !lp.lp_CountryData.cp_DateFormat[0]))
        load_country(names_c[sel_country]);
    if (sel_language >= 0 && same_name(names_l[sel_language], lp.lp_PreferredLanguages[0])) {
        int i;
        for (i = 9; i > 0; i--) memcpy(lp.lp_PreferredLanguages[i], lp.lp_PreferredLanguages[i - 1], 30);
        strncpy(lp.lp_PreferredLanguages[0], names_l[sel_language], 29);
        lp.lp_PreferredLanguages[0][29] = 0;
        for (i = 1; i < 10; i++)                       /* the same language once only */
            if (!same_name(lp.lp_PreferredLanguages[i], lp.lp_PreferredLanguages[0])) lp.lp_PreferredLanguages[i][0] = 0;
    }
    if (save && !save_locale(LOCALE_ENVARC)) { status("Couldn't write ENVARC:Sys/Locale.prefs."); return 0; }
    if (!save_locale(LOCALE_ENV)) { status("Couldn't write ENV:Sys/Locale.prefs."); return 0; }
    return 1;
}

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, (STRPTR)"Cancel", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
    { NM_ITEM, (STRPTR)"The clock now", (STRPTR)"N", 0, 0, (APTR)4 },
    { NM_END, 0, 0, 0, 0, 0 }
};

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    struct Menu *menu = NULL;
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, lw, lvh, i, digit, half;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    make_zones();
    sel_zone = zone_of(lp.lp_GMTOffset);
    count_c = list_dir("LOCALE:Countries", ".country", &names_c[0][0], 32);
    count_l = list_dir("LOCALE:Languages", ".language", &names_l[0][0], 30);
    if (count_l < MAX_NAMES && index_of(&names_l[0][0], 30, count_l, "english") < 0) {   /* built into locale.library */
        strcpy(names_l[count_l++], "english");
        qsort(names_l, count_l, 30, by_name);
    }
    make_list(&list_c, nodes_c, &names_c[0][0], 32, count_c);
    make_list(&list_l, nodes_l, &names_l[0][0], 30, count_l);
    sel_country = index_of(&names_c[0][0], 32, count_c, lp.lp_CountryName);
    sel_language = index_of(&names_l[0][0], 30, count_l, lp.lp_PreferredLanguages[0]);
    read_clock(&now_cd);
    shown_cd = now_cd;
    month_sel = now_cd.month - 1;

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    digit = TextLength(&scr->RastPort, (STRPTR)"0000", 4) / 4;
    lw = TextLength(&scr->RastPort, (STRPTR)"Time zone", 9) + 16;     /* the labels' column */
    X = 12 + lw;
    half = 14 * digit + 160;                                          /* each list's width */
    if (half < 200) half = 200;
    W = 12 + 2 * half + 12 + 12;
    lvh = 7 * (fh + 1) + 4;
    snprintf(status_text, sizeof status_text, "%s", region_saved ? "Change what you like, then Use or Save."
             : "No region is saved on this Amiga yet: check the zone, country and language, then Save.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
    row = top;
    {
        int dw = 4 * digit + 16, yw = 6 * digit + 16, mw = TextLength(&scr->RastPort, (STRPTR)"September", 9) + 40;
        G(INTEGER_KIND, G_DAY, X, row, dw, lh, "Date", PLACETEXT_LEFT, GTIN_Number, now_cd.mday, GTIN_MaxChars, 2);
        G(CYCLE_KIND, G_MONTH, X + dw + 6, row, mw, lh, NULL, 0, GTCY_Labels, (ULONG)month_names, GTCY_Active, month_sel);
        G(INTEGER_KIND, G_YEAR, X + dw + 6 + mw + 6, row, yw, lh, NULL, 0, GTIN_Number, now_cd.year, GTIN_MaxChars, 4);
        row += lh + 4;
        G(INTEGER_KIND, G_HOUR, X, row, dw, lh, "Time", PLACETEXT_LEFT, GTIN_Number, now_cd.hour, GTIN_MaxChars, 2);
        G(INTEGER_KIND, G_MIN, X + dw + 6, row, dw, lh, NULL, 0, GTIN_Number, now_cd.min, GTIN_MaxChars, 2);
        G(INTEGER_KIND, G_SEC, X + 2 * (dw + 6), row, dw, lh, NULL, 0, GTIN_Number, now_cd.sec, GTIN_MaxChars, 2);
        G(BUTTON_KIND, G_NOW, X + 3 * (dw + 6), row, TextLength(&scr->RastPort, (STRPTR)"Now", 3) + 24, lh, "_Now", 0,
          GT_Underscore, '_');
        row += lh + 4;
        G(CYCLE_KIND, G_ZONE, X, row, TextLength(&scr->RastPort, (STRPTR)"UTC+00:00", 9) + 40, lh, "Time zone", PLACETEXT_LEFT,
          GTCY_Labels, (ULONG)zone_labels, GTCY_Active, sel_zone);
        row += lh + 10 + fh + 2;
    }
    /* the pick highlighted in the list itself (GTLV_ShowSelected NULL), each row drawn by row_draw */
    row_hook.h_Entry = (ULONG (*)())row_draw;
    row_hook.h_SubEntry = NULL;
    row_hook.h_Data = NULL;
    G(LISTVIEW_KIND, G_COUNTRY, 12, row, half, lvh, "Country", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&list_c, GTLV_ShowSelected, (ULONG)NULL, GTLV_Selected, (ULONG)sel_country,
      GTLV_CallBack, (ULONG)&row_hook, GTLV_Top, (ULONG)(sel_country > 3 ? sel_country - 3 : 0));
    G(LISTVIEW_KIND, G_LANGUAGE, 12 + half + 12, row, half, lvh, "Language", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&list_l, GTLV_ShowSelected, (ULONG)NULL, GTLV_Selected, (ULONG)sel_language,
      GTLV_CallBack, (ULONG)&row_hook, GTLV_Top, (ULONG)(sel_language > 3 ? sel_language - 3 : 0));
    row += lvh + 8;
    G(TEXT_KIND, G_STATUS, 12, row, W - 24, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE);
    row += lh + 6;
    {
        static const char *const names[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 24 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], 12 + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 8;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Date and time", WA_ScreenTitle, (ULONG)"OpenDateTime 0.1.2", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_VANILLAKEY | BUTTONIDCMP |
                                   INTEGERIDCMP | CYCLEIDCMP | LISTVIEWIDCMP | TEXTIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);

    while (!quit) {
        struct IntuiMessage *m;
#ifdef IN_OPENBENCH
        /* inside OpenBench: Ctrl-C (OpenBench is quitting) is Cancel; Ctrl-F (opened again) to the front */
        ULONG got = Wait(1UL << win->UserPort->mp_SigBit | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
        if (got & SIGBREAKF_CTRL_C) { quit = 1; continue; }
        if (got & SIGBREAKF_CTRL_F) { ScreenToFront(win->WScreen); WindowToFront(win); ActivateWindow(win); }
#else
        WaitPort(win->UserPort);
#endif
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) quit = 1;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_VANILLAKEY && (code | 0x20) == 'n') show_clock();
            else if (cls == IDCMP_MENUPICK) {
                while (code != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, code);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: if (put_in_place(1)) quit = 1; break;
                    case 2: if (put_in_place(0)) quit = 1; break;
                    case 3: quit = 1; break;
                    case 4: show_clock(); break;
                    }
                    code = it->NextSelect;
                }
            }
            else if (cls == IDCMP_GADGETUP) {
                switch (gg->GadgetID) {
                case G_MONTH: month_sel = code; break;
                case G_ZONE: sel_zone = code; break;
                case G_COUNTRY: sel_country = code; break;
                case G_LANGUAGE: sel_language = code; break;
                case G_NOW: show_clock(); break;
                case G_SAVE: if (put_in_place(1)) quit = 1; break;
                case G_USE: if (put_in_place(0)) quit = 1; break;
                case G_CANCEL: quit = 1; break;
                }
            }
        }
    }
out:
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

int main(int argc, char **argv)
{
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S", args, NULL) : NULL;
    int rc;
    (void)argv;
    (void)version;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"OpenDateTime"); return RETURN_FAIL; }
    if (!(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39))) { if (rd) FreeArgs(rd); return RETURN_FAIL; }
    load_locale(args[0] ? (const char *)args[0] : LOCALE_ENV);
    if (args[1] || args[2]) {                    /* from the Shell: put a file (FROM) in place, no window */
        if (!lp.lp_CountryData.cp_DateFormat[0]) load_country(lp.lp_CountryName);
        rc = (args[2] && !save_locale(LOCALE_ENVARC)) || !save_locale(LOCALE_ENV) ? RETURN_ERROR : RETURN_OK;
    } else rc = gui();
    if (rd) FreeArgs(rd);
    if (file_data) FreeVec(file_data);
#ifdef IN_OPENBENCH
    file_data = NULL;                            /* OpenBench's data stays: the next start must not free it again */
    file_len = 0;
#endif
    CloseLibrary(UtilityBase);
    return rc;
}
