/* OpenDateTime: the date, the time and the region in one OpenPrefs editor. It
 * takes over the OS's Time and Locale editors (OpenUp's RECONCILE puts those in
 * SYS:Prefs/Classic); a double-click on OpenTitle's clock opens it.
 *
 *   OpenDateTime [FROM file] [USE] [SAVE] [ZONE place] [KEEP] [INFO]
 *
 *   - the date (day, month, year) and the time (hours, minutes, seconds), with
 *     Now to read the clock again; set with Use or Save when changed, through
 *     C:Date, and kept in the battery clock with C:SetClock SAVE
 *   - the time zone as a place (Europe/London), not an offset: Locale prefs'
 *     GMT offset follows the place's summer time. On AmigaChrome the place and
 *     its offset are the host's; on a real Amiga, the place's rules (zones.h,
 *     from the tz database) say when summer time starts and ends
 *   - the country (LOCALE:Countries) and the preferred language
 *     (LOCALE:Languages), as Locale prefs keeps them
 *
 * Save writes ENV:Sys/Locale.prefs and ENVARC:; Use writes ENV: only (IPrefs
 * reads it at once); Cancel changes nothing. Everything in Locale prefs this
 * editor doesn't show is kept as it was. The place goes in a chunk of the Team's
 * own beside LCLE (ZONE), which locale.library doesn't read.
 *
 * From the Shell: ZONE picks the place (with USE or SAVE); KEEP is the background
 * part, which keeps the GMT offset (and on a real Amiga the clock) right across
 * the changes; INFO says what the place, the offset and the next change are.
 *
 * Built into OpenBench (the Team, 11 October 2026: the Open preferences in OpenBench natively) with
 * -DIN_OPENBENCH, as openamigabench desktop/ob_builtin.h says: OpenBench starts it as a process of its
 * own with no arguments, as Workbench starts a tool, so it is the window; Ctrl-C is Cancel, Ctrl-F
 * brings the window to the front; utility.library is opened under this editor's own name.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/configvars.h>
#include <libraries/locale.h>
#include <resources/battclock.h>
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
#include <proto/expansion.h>
#include <proto/locale.h>
#include <proto/timer.h>
#include <proto/battclock.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char version[] __attribute__((used)) = "$VER: OpenDateTime 0.2.2 (13.10.2026) OpenPrefs, MIT, Copyright (c) 2026 AmigaChrome Team";

#ifdef IN_OPENBENCH
/* inside OpenBench, a global UtilityBase could be another built-in's: every UtilityBase below, and in
 * utility.library's calls, is this editor's own */
static struct Library *opendatetime_utility;
#define UtilityBase opendatetime_utility
#else
struct Library *UtilityBase;
#endif
/* the other libraries and resources this editor calls, under its own names in either build */
static struct Library *opendatetime_expansion, *opendatetime_locale, *opendatetime_battclock;
static struct Device *opendatetime_timer;
#define ExpansionBase opendatetime_expansion
#define LocaleBase opendatetime_locale
#define BattClockBase opendatetime_battclock
#define TimerBase opendatetime_timer

/* 0 when two names are the same, case aside (utility.library's Stricmp: libnix has no stricmp) */
static LONG same_name(const char *a, const char *b)
{
    LONG r = Stricmp((CONST_STRPTR)a, (CONST_STRPTR)b);
    return r;
}

#define LOCALE_ENV "ENV:Sys/Locale.prefs"
#define LOCALE_ENVARC "ENVARC:Sys/Locale.prefs"
#define MAX_NAMES 96

/* ---- places ------------------------------------------------------------------------ */

#include "tzrule.c"
#include "zones.h"
#ifdef LT_ONE
#include "looktabs.h"                       /* a page of OpenPrefs (LT_OP): its frame and sections */
#define LT_REFRESH(w) lt_draw((w), -1)
#else
#define LT_REFRESH(w) ((void)0)
#define LT_DIRTY 1
#define LT_DX 0
#define lt_note_to(f) ((void)0)
#endif

#define AMIGA_EPOCH 252460800L                  /* 1 January 1978, UTC, in seconds from 1970 */
#define DEFAULT_PLACE "Europe/London"           /* shown when no region is saved, as 0.1.2's United Kingdom */

/* The place in Locale prefs: the Team's ZONE chunk, beside LCLE. locale.library and IPrefs read LCLE and
 * leave other chunks be; the OS's own Locale editor drops it (the place is then none, and the offset stays
 * as that editor saved it). */
struct ZonePrefs {
    char zp_Place[40];                          /* "Europe/London"; "" none picked */
    char zp_Rule[56];                           /* its POSIX rule when saved, for a place this table lacks */
    ULONG zp_Flags;
};
#define ZPF_HOST 1                              /* the place and offset are the host's (AmigaChrome) */

static int place_index(const char *name)
{
    int i;
    if (!name || !*name) return -1;
    for (i = 0; i < ZONE_COUNT; i++) if (!strcmp(zone_table[i].name, name)) return i;
    return -1;
}

/* A place's rules: the table's, else the rule saved with it. 1 when there are some. */
static int place_zone(const char *name, const char *rule, struct tz_zone *z)
{
    int i = place_index(name);
    memset(z, 0, sizeof *z);
    if (i >= 0) {
        if (!tz_parse(zone_rules[zone_table[i].rule], &z->rule)) return 0;
        z->changes = zone_table[i].changes ? &zone_changes[zone_table[i].first] : NULL;
        z->count = zone_table[i].changes;
        return 1;
    }
    return rule && *rule && tz_parse(rule, &z->rule);
}

static const char *place_rule(const char *name)
{
    int i = place_index(name);
    return i >= 0 ? zone_rules[zone_table[i].rule] : "";
}

/* The system clock (local time) in seconds from 1970, as tzrule.c counts. */
static tz_t clock_local(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (tz_t)AMIGA_EPOCH + (tz_t)ds.ds_Days * 86400 + ds.ds_Minute * 60L + ds.ds_Tick / TICKS_PER_SECOND;
}

/* "UTC+1:00" from seconds east. */
static void offset_text(char *out, int size, long east)
{
    long a = east < 0 ? -east : east;
    snprintf(out, size, "UTC%c%ld:%02ld", east < 0 ? '-' : '+', a / 3600, a / 60 % 60);
}

/* ---- the host's clock (AmigaChrome) -------------------------------------------------- */

/* accontrol.device (amigachrome-guest): ACCMD_BOARD asks the host; the status (query 7) starts with
 * "clock": {"zone": "Europe/London", "offset": 60, "dst": true, "abbr": "BST", "utc": ...}. */
#define ACCONTROL_NAME "accontrol.device"
#define ACCMD_BOARD (CMD_NONSTD + 2)
#define ACC_QUERY_STATUS 7
#define ANSWER_SIZE 4096

struct host_clock {
    char zone[40];
    long east;                                  /* seconds east of UTC */
    int dst;
    char abbr[12];
};

/* A machine with AmigaChrome's ACBridge board (AmigaChrome Team, or 2011 before AmigaChrome Team): its battery clock runs on
 * the host's local time, so it is right at start, and moves with the host's when the offset changes. */
static int on_amigachrome(void)
{
    int yes = 0;
    if ((ExpansionBase = OpenLibrary((STRPTR)"expansion.library", 37))) {
        yes = FindConfigDev(NULL, 0xDA15, 1) != NULL || FindConfigDev(NULL, 2011, 1) != NULL;
        CloseLibrary(ExpansionBase);
        ExpansionBase = NULL;
    }
    return yes;
}

/* The text after key within [s, end), or NULL. */
static const char *json_at(const char *s, const char *end, const char *key)
{
    const char *p = strstr(s, key);
    return p && p + strlen(key) <= end ? p + strlen(key) : NULL;
}

static void json_text(const char *p, const char *end, char *out, int size)
{
    int n = 0;
    while (p && p < end && *p != '"' && n < size - 1) out[n++] = *p++;
    out[n] = 0;
}

static int parse_clock(const char *s, struct host_clock *hc)
{
    const char *c = strstr(s, "\"clock\":{"), *end, *p;
    memset(hc, 0, sizeof *hc);
    if (!c) return 0;
    c += 9;
    if (!(end = strchr(c, '}'))) return 0;
    if (!(p = json_at(c, end, "\"offset\":"))) return 0;
    hc->east = strtol(p, NULL, 10) * 60;
    json_text(json_at(c, end, "\"zone\":\""), end, hc->zone, sizeof hc->zone);
    json_text(json_at(c, end, "\"abbr\":\""), end, hc->abbr, sizeof hc->abbr);
    p = json_at(c, end, "\"dst\":");
    hc->dst = p && *p == 't';
    return 1;
}

/* The host's place and offset. 1 when the host said. */
static int ask_host(struct host_clock *hc)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *io;
    char *buf;
    int ok = 0;
    memset(hc, 0, sizeof *hc);
    if (!port) return 0;
    if ((io = (struct IOStdReq *)CreateIORequest(port, sizeof *io))) {
        if (!OpenDevice((STRPTR)ACCONTROL_NAME, 0, (struct IORequest *)io, 0)) {
            if ((buf = AllocVec(ANSWER_SIZE, MEMF_ANY | MEMF_CLEAR))) {
                io->io_Command = ACCMD_BOARD;
                io->io_Offset = ACC_QUERY_STATUS;
                io->io_Data = buf;
                io->io_Length = ANSWER_SIZE - 1;
                if (!DoIO((struct IORequest *)io)) {
                    buf[io->io_Actual < ANSWER_SIZE - 1 ? io->io_Actual : ANSWER_SIZE - 1] = 0;
                    ok = parse_clock(buf, hc);
                }
                FreeVec(buf);
            }
            CloseDevice((struct IORequest *)io);
        }
        DeleteIORequest((struct IORequest *)io);
    }
    DeleteMsgPort(port);
    return ok;
}

/* ---- Locale prefs ---------------------------------------------------------- */

static struct LocalePrefs lp;
static struct ZonePrefs zp;
static UBYTE *file_data;                        /* the whole file read, to keep its other chunks */
static LONG file_len;
static int region_saved;                        /* 1 when Locale prefs were read; 0: the defaults below */

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

/* The offset a place has now, as Locale prefs keeps it (minutes west): for the defaults. */
static LONG place_west_now(const char *name)
{
    struct tz_zone z;
    if (!place_zone(name, NULL, &z)) return 0;
    return -tz_zone_offset_for_local(&z, clock_local(), NULL) / 60;
}

/* Locale prefs from path. With none saved (no file, or no LCLE in it), the defaults: the United Kingdom,
 * english, and Europe/London with its offset today (UTC+1:00 in British Summer Time). locale.library's own
 * defaults (united_states, its GMT offset -300) are not a region anyone chose, so they aren't shown. */
static void load_locale(const char *path)
{
    UBYTE *c;
    LONG n = 0;
    memset(&lp, 0, sizeof lp);
    memset(&zp, 0, sizeof zp);
    strcpy(lp.lp_CountryName, "united_kingdom");
    strcpy(lp.lp_PreferredLanguages[0], "english");
    region_saved = 0;
    if (file_data) { FreeVec(file_data); file_data = NULL; }
    file_data = read_all(path, &file_len);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4) && !memcmp(file_data + 8, "PREF", 4) &&
        (c = chunk(file_data, file_len, "LCLE", &n))) {
        memcpy(&lp, c, n < (LONG)sizeof lp ? n : (LONG)sizeof lp);
        region_saved = 1;
        if ((c = chunk(file_data, file_len, "ZONE", &n))) {
            memcpy(&zp, c, n < (LONG)sizeof zp ? n : (LONG)sizeof zp);
            zp.zp_Place[sizeof zp.zp_Place - 1] = 0;
            zp.zp_Rule[sizeof zp.zp_Rule - 1] = 0;
        }
    } else {
        strcpy(zp.zp_Place, DEFAULT_PLACE);
        strcpy(zp.zp_Rule, place_rule(DEFAULT_PLACE));
        lp.lp_GMTOffset = place_west_now(DEFAULT_PLACE);
    }
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

/* The file again: its PRHD and other chunks as they were, LCLE with lp, and ZONE with zp when it says anything. */
static int save_locale(const char *path)
{
    LONG size = 12 + 14 + 8 + sizeof lp + 8 + sizeof zp + file_len + 16, o = 12, k;
    UBYTE *out = AllocVec(size, MEMF_ANY | MEMF_CLEAR);
    int ok, had_prhd = 0;
    if (!out) return 0;
    memcpy(out, "FORM\0\0\0\0PREF", 12);
    if (file_data && file_len >= 12 && !memcmp(file_data, "FORM", 4)) {
        for (k = 12; k + 8 <= file_len; ) {
            LONG cl = (LONG)get32(file_data + k + 4), all = 8 + cl + (cl & 1);
            if (cl < 0 || k + all > file_len) break;
            if (memcmp(file_data + k, "LCLE", 4) && memcmp(file_data + k, "ZONE", 4)) {
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
    if (zp.zp_Place[0] || zp.zp_Rule[0] || zp.zp_Flags) {
        memcpy(out + o, "ZONE", 4);
        put32(out + o + 4, sizeof zp);
        memcpy(out + o + 8, &zp, sizeof zp);
        o += 8 + sizeof zp + (sizeof zp & 1);
    }
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

static void new_list(struct List *list)
{
    list->lh_Head = (struct Node *)&list->lh_Tail;      /* NewList, which is amiga.lib's */
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
}

static void make_list(struct List *list, struct Node *nodes, char *names, int width, int n)
{
    int i;
    new_list(list);
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

static const char *const month_names[] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                           "September", "October", "November", "December", NULL };
static const char *const month_dos[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/* Sets the system clock with C:Date and keeps it in the battery clock. 1 done. */
static int set_clock(const struct ClockData *cd)
{
    char cmd[96];
    BPTR nil, nilout;
    LONG rc;
    if (cd->year < 1978 || cd->year > 2077) return 0;   /* what C:Date's two-digit years reach */
    snprintf(cmd, sizeof cmd, "C:Date %02d-%s-%02d %02d:%02d:%02d", cd->mday, month_dos[cd->month - 1], cd->year % 100,
             cd->hour, cd->min, cd->sec);
    /* A synchronous call is given both handles, never a NULL output (the Team's decision of 12 October,
     * as OpenDock and OpenMenus do): a NIL: input and a NIL: output, opened and closed here. */
    nil = Open((STRPTR)"NIL:", MODE_OLDFILE);
    nilout = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!nil || !nilout) {
        if (nil) Close(nil);
        if (nilout) Close(nilout);
        return 0;
    }
    rc = SystemTags((STRPTR)cmd, SYS_Input, (ULONG)nil, SYS_Output, (ULONG)nilout, TAG_DONE);
    if (rc == 0) rc = SystemTags((STRPTR)"C:SetClock SAVE", SYS_Input, (ULONG)nil, SYS_Output, (ULONG)nilout, TAG_DONE);
    Close(nilout);
    Close(nil);
    return rc == 0;
}

/* "25-Oct-2026 02:00" from a local time. */
static void when_text(char *out, int size, tz_t local)
{
    int y, mo, d, h, mi, s;
    tz_civil(local, &y, &mo, &d, &h, &mi, &s);
    snprintf(out, size, "%d-%s-%d %02d:%02d", d, month_dos[mo - 1], y, h, mi);
}

/* The system clock's seconds (from 1978) through timer.device; *battery the battery clock's, or 0 with none.
 * With move, the system clock goes on move seconds first, and with keep_battery the battery clock with it. */
static int clock_times(LONG move, int keep_battery, ULONG *system, ULONG *battery)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *tr;
    int ok = 0;
    if (system) *system = 0;
    if (battery) *battery = 0;
    if (!port) return 0;
    if ((tr = (struct timerequest *)CreateIORequest(port, sizeof *tr))) {
        if (!OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0)) {
            struct timeval tv;
            TimerBase = tr->tr_node.io_Device;
            GetSysTime(&tv);
            ok = 1;
            if (move) {
                tv.tv_secs += move;
                tr->tr_node.io_Command = TR_SETSYSTIME;
                tr->tr_time.tv_secs = tv.tv_secs;
                tr->tr_time.tv_micro = tv.tv_micro;
                ok = !DoIO((struct IORequest *)tr);
            }
            if (system) *system = tv.tv_secs;
            if ((BattClockBase = OpenResource((STRPTR)BATTCLOCKNAME))) {
                if (ok && move && keep_battery) WriteBattClock(tv.tv_secs);
                if (battery) *battery = ReadBattClock();
            }
            CloseDevice((struct IORequest *)tr);
            TimerBase = NULL;
        }
        DeleteIORequest((struct IORequest *)tr);
    }
    DeleteMsgPort(port);
    return ok;
}

/* ---- the background part (KEEP) ----------------------------------------------------- */

/* One voice: Locale prefs own the zone. This part changes nothing but Locale prefs' GMT offset (and on
 * AmigaChrome the place, which is the host's), and the clock when the offset moves under it:
 *
 * - On AmigaChrome the battery clock runs on the host's local time, so the clock is right at start and
 *   the host says the offset. When it changes (summer time, or the host's place), the battery clock has
 *   moved with the host and the system clock hasn't: it is set on by the whole quarter hours between them.
 * - On a real Amiga the clock keeps local time on the offset Locale prefs saved; the place's rules say
 *   what the offset is now (tz_keep_step). When they differ, the offset is saved and the clock moved by the
 *   difference, the battery clock with it. Off over a change, the Amiga catches up when this starts.
 *
 * Each pair moves together or not at all: the system clock only once ENV: has the new offset, the battery
 * clock only once ENVARC: has it, so a write that fails (IPrefs holding the file) is tried again next time
 * and never moves a clock twice. */

#define KEEPER_PORT "OpenDateTime.keeper"

/* Puts west and the place in one Locale prefs file, if it has a region (or, with create, the editor's
 * defaults). 1 when the file has them now. */
static int keep_file(const char *path, LONG west, const char *place, const char *rule, ULONG flags, int create)
{
    load_locale(path);
    if (!region_saved) {
        if (!create) return 0;
        load_country(lp.lp_CountryName);              /* the editor's defaults, with their formats */
    } else if (lp.lp_GMTOffset == west && !strcmp(zp.zp_Place, place) && !strcmp(zp.zp_Rule, rule) && zp.zp_Flags == flags)
        return 1;
    lp.lp_GMTOffset = west;
    memset(&zp, 0, sizeof zp);
    strncpy(zp.zp_Place, place, sizeof zp.zp_Place - 1);
    strncpy(zp.zp_Rule, rule, sizeof zp.zp_Rule - 1);
    zp.zp_Flags = flags;
    return save_locale(path);
}

struct keeper {
    int chrome;                                 /* on AmigaChrome */
    int first;                                  /* the first look since starting */
    char said[160];                             /* what the last look did */
};

/* One look. Returns the seconds to the next. */
static LONG keep_once(struct keeper *k)
{
    struct host_clock hc;
    struct tz_zone z;
    char place[40], rule[56], a[16], b[16];
    ULONG flags, sys = 0, bat = 0;
    LONG saved_west, want_west, move = 0, wait = 3600;
    int host = k->chrome && ask_host(&hc), archive;
    tz_t local = clock_local(), utc, when;
    long east_after, want;

    load_locale(LOCALE_ENV);
    saved_west = lp.lp_GMTOffset;
    strncpy(place, zp.zp_Place, sizeof place - 1);
    place[sizeof place - 1] = 0;
    strncpy(rule, zp.zp_Rule, sizeof rule - 1);
    rule[sizeof rule - 1] = 0;
    flags = zp.zp_Flags;
    if (!region_saved && !host) { strcpy(k->said, "no region is saved: nothing to keep"); return 3600; }
    if (host) {                                        /* AmigaChrome: the host says */
        strncpy(place, hc.zone, sizeof place - 1);
        strncpy(rule, place_rule(hc.zone), sizeof rule - 1);
        flags = ZPF_HOST;
        want = hc.east;
        wait = 60;
    } else {
        if (!place_zone(place, rule, &z)) { strcpy(k->said, "no place is saved: the offset stays as saved"); return 3600; }
        if (k->chrome) {                               /* the clock is the host's, right: its offset by the rules */
            want = tz_zone_offset_for_local(&z, local, NULL);
            utc = local - want;
        } else {                                       /* a real Amiga: the clock is on the saved offset */
            utc = local + saved_west * 60L;
            move = tz_keep_step(&z, local, -saved_west * 60L, &want);
        }
        if (tz_zone_next_change(&z, utc, &when, &east_after)) {
            tz_t left = when - utc + 2;
            wait = left < 1 ? 1 : left > 3600 ? 3600 : (LONG)left;
        }
    }
    want_west = -(LONG)(want / 60);
    if (k->chrome && (k->first || want_west != saved_west)) {
        if (clock_times(0, 0, &sys, &bat) && bat) {    /* the battery clock moved with the host's: by whole quarter hours */
            LONG diff = (LONG)(bat - sys), q = (diff >= 0 ? diff + 450 : diff - 450) / 900 * 900;
            if (q && diff - q <= 60 && q - diff <= 60) move = q;
        } else if (!k->first) move = (saved_west - want_west) * 60L;
    }
    /* ENV:, then the system clock; ENVARC: (when it has the same place), then the battery clock */
    if (!keep_file(LOCALE_ENV, want_west, place, rule, flags, host)) {
        snprintf(k->said, sizeof k->said, "couldn't write %s: trying again in a minute", LOCALE_ENV);
        return 60;
    }
    load_locale(LOCALE_ENVARC);
    archive = region_saved && (host || !strcmp(zp.zp_Place, place)) && keep_file(LOCALE_ENVARC, want_west, place, rule, flags, 0);
    if (move) clock_times(move, !k->chrome && archive, NULL, NULL);
    offset_text(a, sizeof a, want);
    offset_text(b, sizeof b, -saved_west * 60L);
    if (move)
        snprintf(k->said, sizeof k->said, "%s %s: the clock moved %ld minutes (the offset was %s)", place[0] ? place : "the host", a,
                 (long)move / 60, b);
    else if (want_west != saved_west)
        snprintf(k->said, sizeof k->said, "%s %s: the offset was %s", place[0] ? place : "the host", a, b);
    else
        snprintf(k->said, sizeof k->said, "%s %s, as saved", place[0] ? place : "the host", a);
    k->first = 0;
    return wait;
}

static int keeper(void)
{
    struct keeper k;
    struct MsgPort *kp, *tport = NULL;
    struct timerequest *tr = NULL;
    int quit = 0, rc = RETURN_FAIL;
    memset(&k, 0, sizeof k);
    k.first = 1;
    Forbid();
    if (FindPort((STRPTR)KEEPER_PORT)) { Permit(); PutStr((STRPTR)"OpenDateTime KEEP is running already\n"); return RETURN_WARN; }
    if ((kp = CreateMsgPort())) {
        kp->mp_Node.ln_Name = (char *)KEEPER_PORT;
        kp->mp_Node.ln_Pri = -128;
        AddPort(kp);
    }
    Permit();
    if (!kp) return RETURN_FAIL;
    k.chrome = on_amigachrome();
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)) &&
        !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0)) {
        char last[160];
        last[0] = 0;
        rc = RETURN_OK;
        while (!quit) {
            ULONG got;
            LONG wait = keep_once(&k);
            if (strcmp(k.said, last)) {                /* what changed, to the output it was started with */
                char at[40];
                when_text(at, sizeof at, clock_local());
                PutStr((STRPTR)at);
                PutStr((STRPTR)" ");
                PutStr((STRPTR)k.said);
                PutStr((STRPTR)"\n");
                Flush(Output());
                strcpy(last, k.said);
            }
            tr->tr_node.io_Command = TR_ADDREQUEST;
            tr->tr_time.tv_secs = wait;
            tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
            got = Wait(1UL << tport->mp_SigBit | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_E);
            if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
            WaitIO((struct IORequest *)tr);
            SetSignal(0, 1UL << tport->mp_SigBit);     /* the aborted request's reply leaves this set: clear it, or every Wait returns at once */
            if (got & SIGBREAKF_CTRL_C) quit = 1;     /* Ctrl-E: the editor saved a place, look again now */
        }
        CloseDevice((struct IORequest *)tr);
    }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    RemPort(kp);
    DeleteMsgPort(kp);
    return rc;
}

/* The editor saved: the background part, if it runs, looks again now. */
static void tell_keeper(void)
{
    struct MsgPort *kp;
    Forbid();
    if ((kp = FindPort((STRPTR)KEEPER_PORT)) && kp->mp_SigTask) Signal((struct Task *)kp->mp_SigTask, SIGBREAKF_CTRL_E);
    Permit();
}

/* ---- INFO ------------------------------------------------------------------------- */

static void say(const char *label, const char *text)
{
    PutStr((STRPTR)label);
    PutStr((STRPTR)text);
    PutStr((STRPTR)"\n");
}

static int info(void)
{
    struct host_clock hc;
    struct tz_zone z;
    char line[160], a[16];
    int chrome = on_amigachrome(), host = chrome && ask_host(&hc), dst = 0;
    tz_t local = clock_local(), when;
    long east, after;
    load_locale(LOCALE_ENV);
    snprintf(line, sizeof line, "%s%s", zp.zp_Place[0] ? zp.zp_Place : region_saved ? "none (an offset only)" : "none saved",
             zp.zp_Flags & ZPF_HOST ? " (the host's)" : "");
    say("Place:        ", line);
    offset_text(a, sizeof a, -lp.lp_GMTOffset * 60L);
    snprintf(line, sizeof line, "%s (GMT offset %ld)%s", a, (long)lp.lp_GMTOffset, region_saved ? "" : ", the editor's default");
    say("Locale prefs: ", line);
    if ((LocaleBase = OpenLibrary((STRPTR)"locale.library", 38))) {
        struct Locale *loc = OpenLocale(NULL);
        if (loc) {
            offset_text(a, sizeof a, -loc->loc_GMTOffset * 60L);
            snprintf(line, sizeof line, "%s (GMT offset %ld)", a, (long)loc->loc_GMTOffset);
            say("In use:       ", line);
            CloseLocale(loc);
        }
        CloseLibrary(LocaleBase);
        LocaleBase = NULL;
    }
    if (host) {
        offset_text(a, sizeof a, hc.east);
        snprintf(line, sizeof line, "%s, %s (%s)%s", hc.zone[0] ? hc.zone : "no place named", a, hc.abbr, hc.dst ? ", summer time" : "");
        say("Host:         ", line);
    } else say("Host:         ", chrome ? "AmigaChrome, but accontrol.device didn't answer" : "none (a real Amiga)");
    when_text(line, sizeof line, local);
    say("Clock:        ", line);
    if (place_zone(zp.zp_Place, zp.zp_Rule, &z)) {
        east = tz_zone_offset_at(&z, local + lp.lp_GMTOffset * 60L, &dst);
        offset_text(a, sizeof a, east);
        snprintf(line, sizeof line, "%s%s, by the place's rules", a, dst ? " (summer time)" : "");
        say("Now:          ", line);
        if (tz_zone_next_change(&z, local + lp.lp_GMTOffset * 60L, &when, &after)) {
            char w[40];
            when_text(w, sizeof w, when + east);
            offset_text(a, sizeof a, after);
            snprintf(line, sizeof line, "%s, to %s", w, a);
            say("Next change:  ", line);
        } else say("Next change:  ", "none: no summer time");
    }
    {
        struct MsgPort *kp;
        Forbid();
        kp = FindPort((STRPTR)KEEPER_PORT);
        Permit();
        say("KEEP:         ", kp ? "running" : "not running");
    }
    return RETURN_OK;
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

/* ---- the places: a region, then the place in it ---------------------------------- */

#define MAX_REGIONS 16
static char region_text[MAX_REGIONS][12];
static const char *region_labels[MAX_REGIONS + 1];
static int region_first[MAX_REGIONS], region_count[MAX_REGIONS], regions;
static char city_text[ZONE_COUNT][32];         /* each place without its region, '_' as a space */
static struct Node city_nodes[ZONE_COUNT];
static struct List city_list;

/* The table is in order of name, so each region's places are together. */
static void make_regions(void)
{
    int i, k;
    regions = 0;
    for (i = 0; i < ZONE_COUNT; i++) {
        const char *n = zone_table[i].name, *slash = strchr(n, '/'), *city = slash ? slash + 1 : n;
        int len = slash ? (int)(slash - n) : (int)strlen(n);
        if (!regions || (int)strlen(region_text[regions - 1]) != len || strncmp(region_text[regions - 1], n, len)) {
            if (regions == MAX_REGIONS) break;
            snprintf(region_text[regions], sizeof region_text[0], "%.*s", len, n);
            region_first[regions] = i;
            region_count[regions] = 0;
            regions++;
        }
        region_count[regions - 1]++;
        for (k = 0; city[k] && k < 31; k++) city_text[i][k] = city[k] == '_' ? ' ' : city[k];
        city_text[i][k] = 0;
    }
    for (i = 0; i < regions; i++) region_labels[i] = region_text[i];
    region_labels[regions] = NULL;
}

static int region_of(int place)
{
    int r;
    for (r = 0; r < regions; r++) if (place >= region_first[r] && place < region_first[r] + region_count[r]) return r;
    return 0;
}

static void fill_cities(int r)
{
    int i;
    new_list(&city_list);
    for (i = 0; i < region_count[r]; i++) {
        city_nodes[region_first[r] + i].ln_Name = city_text[region_first[r] + i];
        AddTail(&city_list, &city_nodes[region_first[r] + i]);
    }
}

/* ---- the window --------------------------------------------------------------- */

enum { G_DAY, G_MONTH, G_YEAR, G_HOUR, G_MIN, G_SEC, G_NOW, G_ZONETEXT, G_REGION, G_PLACE, G_COUNTRY, G_LANGUAGE, G_STATUS,
       G_SAVE, G_USE, G_CANCEL, G_COUNT };
static struct Gadget *gad[G_COUNT];
static struct Window *win;
static char status_text[160], zone_line[120];
static int sel_place = -1, sel_region, sel_country = -1, sel_language = -1, month_sel;
static int host_ok;                             /* AmigaChrome's host said its place: the place is the host's */
static struct host_clock gui_hc;

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    if (win && gad[G_STATUS]) GT_SetGadgetAttrs(gad[G_STATUS], win, NULL, GTTX_Text, (ULONG)status_text, TAG_DONE);
}

/* The zone's line: the place, its offset now, and until when. */
static void show_zone(void)
{
    char a[16], w[40];
    struct tz_zone z;
    tz_t local = clock_local(), when;
    long east, after;
    int dst;
    if (host_ok) {
        offset_text(a, sizeof a, gui_hc.east);
        snprintf(zone_line, sizeof zone_line, "%s, the host's: %s%s%s%s", gui_hc.zone[0] ? gui_hc.zone : "AmigaChrome", a,
                 gui_hc.abbr[0] ? " " : "", gui_hc.abbr, gui_hc.dst ? ", summer time" : "");
    } else if (sel_place >= 0 && place_zone(zone_table[sel_place].name, NULL, &z)) {
        east = tz_zone_offset_for_local(&z, local, &dst);
        offset_text(a, sizeof a, east);
        if (tz_zone_next_change(&z, local - east, &when, &after)) {
            when_text(w, sizeof w, when + east);
            snprintf(zone_line, sizeof zone_line, "%s: %s%s until %s", zone_table[sel_place].name, a, dst ? ", summer time," : "", w);
        } else snprintf(zone_line, sizeof zone_line, "%s: %s all year", zone_table[sel_place].name, a);
    } else {
        offset_text(a, sizeof a, -lp.lp_GMTOffset * 60L);
        snprintf(zone_line, sizeof zone_line, "%s as saved, with no place: pick one below", a);
    }
    if (win && gad[G_ZONETEXT]) GT_SetGadgetAttrs(gad[G_ZONETEXT], win, NULL, GTTX_Text, (ULONG)zone_line, TAG_DONE);
}

static void show_region(int r)
{
    int pick = sel_place >= region_first[r] && sel_place < region_first[r] + region_count[r] ? sel_place - region_first[r] : -1;
    sel_region = r;
    if (win) GT_SetGadgetAttrs(gad[G_PLACE], win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
    fill_cities(r);
    if (win) GT_SetGadgetAttrs(gad[G_PLACE], win, NULL, GTLV_Labels, (ULONG)&city_list, GTLV_Selected, pick >= 0 ? (ULONG)pick : ~0UL,
                               GTLV_Top, (ULONG)(pick > 3 ? pick - 3 : 0), TAG_DONE);
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
    show_zone();
}

/* The place into Locale prefs (lp, zp), with its offset at local: the host's on AmigaChrome. With no place
 * picked, the offset stays as it was saved. */
static void put_place(const char *name, tz_t local)
{
    struct tz_zone z;
    if (host_ok) {
        lp.lp_GMTOffset = -(LONG)(gui_hc.east / 60);
        name = gui_hc.zone;
    } else if (name && place_zone(name, NULL, &z)) {
        lp.lp_GMTOffset = -(LONG)(tz_zone_offset_for_local(&z, local, NULL) / 60);
    } else return;
    memset(&zp, 0, sizeof zp);
    strncpy(zp.zp_Place, name, sizeof zp.zp_Place - 1);
    strncpy(zp.zp_Rule, place_rule(name), sizeof zp.zp_Rule - 1);
    zp.zp_Flags = host_ok ? ZPF_HOST : 0;
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
    put_place(sel_place >= 0 ? zone_table[sel_place].name : NULL, clock_local());
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
    tell_keeper();
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
    int fh, lh, top, row, quit = 0, rc = RETURN_OK, W, X, lw, lvh, i, digit, third, pick;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    host_ok = on_amigachrome() && ask_host(&gui_hc);
    make_regions();
    sel_place = place_index(host_ok ? gui_hc.zone : zp.zp_Place);
    sel_region = region_of(sel_place >= 0 ? sel_place : place_index(DEFAULT_PLACE));
    fill_cities(sel_region);
    pick = sel_place >= 0 ? sel_place - region_first[sel_region] : -1;
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
    show_zone();

    fh = scr->Font->ta_YSize;
    lh = fh + 6;
    top = scr->WBorTop + fh + 1 + 8;
    digit = TextLength(&scr->RastPort, (STRPTR)"0000", 4) / 4;
    lw = TextLength(&scr->RastPort, (STRPTR)"Time zone", 9) + 16;     /* the labels' column */
    X = 12 + lw;
    third = 20 * digit + 16;                                          /* each list's width */
    if (third < 176) third = 176;
    W = 12 + 3 * third + 2 * 12 + 12;
    lvh = 7 * (fh + 1) + 4;
    snprintf(status_text, sizeof status_text, "%s",
             !region_saved ? "No region is saved on this Amiga yet: check the zone, country and language, then Save."
             : host_ok ? "The time zone is the host's. Change what you like, then Use or Save."
             : sel_place < 0 ? "Pick the place you are in: summer time then changes by itself."
             : "Change what you like, then Use or Save.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#ifdef LT_ONE
    lt_make(NULL, NULL, 10, top, W - 24, lh, LT_DATETIME);     /* the frame's sections (OpenPrefs) */
    top += lt_space(scr->Font, W - 24, lh);
#endif
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
        G(TEXT_KIND, G_ZONETEXT, X, row, W - X - 12, lh, "Time zone", PLACETEXT_LEFT, GTTX_Text, (ULONG)zone_line,
          GTTX_Border, TRUE, GTTX_Clipped, TRUE);
        row += lh + 8;
    }
    /* the place: its region above, the places in it below; the pick highlighted in the list itself
     * (GTLV_ShowSelected NULL), each row drawn by row_draw. On AmigaChrome the host's, not to change here. */
    row_hook.h_Entry = (ULONG (*)())row_draw;
    row_hook.h_SubEntry = NULL;
    row_hook.h_Data = NULL;
    G(CYCLE_KIND, G_REGION, 12, row, third, lh, NULL, 0, GTCY_Labels, (ULONG)region_labels, GTCY_Active, sel_region,
      GA_Disabled, host_ok);
    row += lh + 4;
    G(LISTVIEW_KIND, G_PLACE, 12, row, third, lvh, NULL, 0,
      GTLV_Labels, (ULONG)&city_list, GTLV_ShowSelected, (ULONG)NULL, GTLV_Selected, pick >= 0 ? (ULONG)pick : ~0UL,
      GTLV_CallBack, (ULONG)&row_hook, GTLV_Top, (ULONG)(pick > 3 ? pick - 3 : 0), GA_Disabled, host_ok);
    G(LISTVIEW_KIND, G_COUNTRY, 12 + third + 12, row, third, lvh, "Country", PLACETEXT_ABOVE,
      GTLV_Labels, (ULONG)&list_c, GTLV_ShowSelected, (ULONG)NULL, GTLV_Selected, (ULONG)sel_country,
      GTLV_CallBack, (ULONG)&row_hook, GTLV_Top, (ULONG)(sel_country > 3 ? sel_country - 3 : 0));
    G(LISTVIEW_KIND, G_LANGUAGE, 12 + 2 * (third + 12), row, third, lvh, "Language", PLACETEXT_ABOVE,
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
#ifdef LT_ONE
    win = lt_open(scr, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, scr->BarHeight + 20, W, row - scr->WBorTop - fh - 1, glist, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_VANILLAKEY | BUTTONIDCMP | INTEGERIDCMP | CYCLEIDCMP | LISTVIEWIDCMP | TEXTIDCMP, "OpenDateTime 0.2");
#else
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Date and time", WA_ScreenTitle, (ULONG)"OpenDateTime 0.2", WA_PubScreen, (ULONG)scr,
                         WA_Left, (scr->Width - W) / 2 > 0 ? (scr->Width - W) / 2 : 0, WA_Top, scr->BarHeight + 20,
                         WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_VANILLAKEY | BUTTONIDCMP |
                                   INTEGERIDCMP | CYCLEIDCMP | LISTVIEWIDCMP | TEXTIDCMP,
                         TAG_DONE);
#endif
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    lt_note_to(status);

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
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); LT_REFRESH(win); GT_EndRefresh(win, TRUE); }
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
                case G_REGION: show_region(code); break;
                case G_PLACE: sel_place = region_first[sel_region] + code; show_zone(); break;
                case G_COUNTRY: sel_country = code; break;
                case G_LANGUAGE: sel_language = code; break;
                case G_NOW: show_clock(); break;
#ifdef LT_ONE
                default: {                          /* another section: the page's changes kept, as Use, when it has any */
                    int to = lt_hit(gg, 0);
                    if (to >= 0) { if (LT_DIRTY) put_in_place(0); lt_go(win, to); quit = 1; }
                    break;
                }
#endif
                case G_SAVE: if (put_in_place(1)) quit = 1; break;
                case G_USE: if (put_in_place(0)) quit = 1; break;
                case G_CANCEL: quit = 1; break;
                }
            }
        }
    }
out:
#ifdef LT_ONE
    if (win) { ClearMenuStrip(win); lt_close(win, glist); win = NULL; }
#else
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
#endif
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

/* OpenDateTime ZONE place: the place, with its offset now (on AmigaChrome, the host's). */
static int shell_zone(const char *name)
{
    char line[120];
    if (place_index(name) < 0) {
        snprintf(line, sizeof line, "OpenDateTime: no place called %s (they are named as Europe/London)\n", name);
        PutStr((STRPTR)line);
        return 0;
    }
    host_ok = on_amigachrome() && ask_host(&gui_hc);
    if (host_ok && strcmp(gui_hc.zone, name)) {
        snprintf(line, sizeof line, "OpenDateTime: on AmigaChrome the place is the host's, %s\n", gui_hc.zone[0] ? gui_hc.zone : "which has no name");
        PutStr((STRPTR)line);
    }
    put_place(name, clock_local());
    return 1;
}

int main(int argc, char **argv)
{
    LONG args[6] = { 0, 0, 0, 0, 0, 0 };
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S,ZONE/K,KEEP/S,INFO/S", args, NULL) : NULL;
    int rc;
    (void)argv;
    (void)version;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"OpenDateTime"); return RETURN_FAIL; }
    if (!(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39))) { if (rd) FreeArgs(rd); return RETURN_FAIL; }
    load_locale(args[0] ? (const char *)args[0] : LOCALE_ENV);
    if (args[4]) rc = keeper();
    else if (args[5]) rc = info();
    else if (args[1] || args[2] || args[3]) {    /* from the Shell: put a file (FROM), or a place (ZONE), in place, no window */
        if (args[3] && !shell_zone((const char *)args[3])) rc = RETURN_ERROR;
        else {
            if (!lp.lp_CountryData.cp_DateFormat[0]) load_country(lp.lp_CountryName);
            rc = (args[2] && !save_locale(LOCALE_ENVARC)) || !save_locale(LOCALE_ENV) ? RETURN_ERROR : RETURN_OK;
            tell_keeper();
        }
    } else rc = gui();
    if (rd) FreeArgs(rd);
    if (file_data) FreeVec(file_data);
#if defined(IN_OPENBENCH) || defined(LT_ONE)
    file_data = NULL;                            /* OpenBench's data stays: the next start must not free it again */
    file_len = 0;
#endif
    CloseLibrary(UtilityBase);
    return rc;
}
