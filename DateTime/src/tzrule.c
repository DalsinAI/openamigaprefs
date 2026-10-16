/* OpenDateTime's time zone rules: a POSIX TZ rule ("GMT0BST,M3.5.0/1,M10.5.0", the
 * rule a tz database TZif file ends with) read and run, with no library: what offset
 * from UTC a place has at a moment, and when it next changes. Plain C with no
 * AmigaOS calls, so the host tests build it too (DateTime/test). Everything is
 * static: opendatetime.c includes this file, and OpenBench builds that one file.
 *
 * Times are seconds since 1 January 1970, UTC (tz_t). A "local" time is the same
 * count read on the place's own clock, as the Amiga's clock keeps it.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */

typedef long long tz_t;

struct tz_when {                /* one end of summer time */
    char kind;                  /* 'M' month.week.day, 'J' day 1-365 (no 29 Feb), 'N' day 0-365 */
    int month, week, day;       /* M: month 1-12, week 1-5 (5 the last), day 0-6 (0 Sunday) */
    int n;                      /* J and N */
    long at;                    /* seconds after local midnight (may be below 0 or past 24 hours) */
};

struct tz_rule {
    long std_east, dst_east;    /* seconds east of UTC (POSIX writes them west) */
    int has_dst;
    char std_name[12], dst_name[12];
    struct tz_when start, end;  /* summer time starts (on standard time), ends (on summer time) */
};

static int tz_leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/* Days from 1 January 1970 to 1 January of y (y from 1970). */
static long tz_days_to_year(long y)
{
    long p = y - 1;
    return 365L * (y - 1970) + (p / 4 - p / 100 + p / 400) - (1969 / 4 - 1969 / 100 + 1969 / 400);
}

static const short tz_month_days[2][13] = {
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 },
};

/* The year a day (from 1 January 1970) falls in. */
static long tz_year_of(long days)
{
    long y = 1970 + days / 366;
    while (tz_days_to_year(y + 1) <= days) y++;
    return y;
}

/* Floor division, for times before an hour's start. */
static long long tz_floor_div(long long a, long long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* ---- reading a rule ------------------------------------------------------------- */

static const char *tz_name(const char *s, char *out, int size)
{
    int n = 0;
    if (*s == '<') {                                   /* <+0530>: any characters to '>' */
        for (s++; *s && *s != '>'; s++) if (n < size - 1) out[n++] = *s;
        if (*s != '>') return NULL;
        s++;
    } else {
        for (; (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z'); s++) if (n < size - 1) out[n++] = *s;
    }
    out[n] = 0;
    return n > 0 ? s : NULL;
}

/* [+|-]hh[:mm[:ss]] into seconds. */
static const char *tz_hms(const char *s, long *out)
{
    long sign = 1, v = 0, part = 0;
    int i;
    if (*s == '+') s++;
    else if (*s == '-') { sign = -1; s++; }
    if (*s < '0' || *s > '9') return NULL;
    for (i = 0; i < 3; i++) {
        part = 0;
        if (*s < '0' || *s > '9') return NULL;
        while (*s >= '0' && *s <= '9') part = part * 10 + (*s++ - '0');
        v += part * (i == 0 ? 3600L : i == 1 ? 60L : 1L);
        if (*s != ':') break;
        s++;
    }
    *out = sign * v;
    return s;
}

static const char *tz_number(const char *s, int *out)
{
    int v = 0;
    if (*s < '0' || *s > '9') return NULL;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    *out = v;
    return s;
}

static const char *tz_date(const char *s, struct tz_when *w)
{
    w->at = 7200;                                      /* 02:00 unless the rule says */
    if (*s == 'M') {
        w->kind = 'M';
        if (!(s = tz_number(s + 1, &w->month)) || *s++ != '.' || !(s = tz_number(s, &w->week)) || *s++ != '.' ||
            !(s = tz_number(s, &w->day)))
            return NULL;
        if (w->month < 1 || w->month > 12 || w->week < 1 || w->week > 5 || w->day > 6) return NULL;
    } else if (*s == 'J') {
        w->kind = 'J';
        if (!(s = tz_number(s + 1, &w->n)) || w->n < 1 || w->n > 365) return NULL;
    } else {
        w->kind = 'N';
        if (!(s = tz_number(s, &w->n)) || w->n > 365) return NULL;
    }
    if (*s == '/' && !(s = tz_hms(s + 1, &w->at))) return NULL;
    return s;
}

/* A POSIX TZ rule into r. 1 when it was one. */
static int tz_parse(const char *s, struct tz_rule *r)
{
    long west;
    memset(r, 0, sizeof *r);
    if (!s || !(s = tz_name(s, r->std_name, sizeof r->std_name)) || !(s = tz_hms(s, &west))) return 0;
    r->std_east = -west;
    if (!*s) return 1;
    if (!(s = tz_name(s, r->dst_name, sizeof r->dst_name))) return 0;
    r->has_dst = 1;
    r->dst_east = r->std_east + 3600;                  /* an hour ahead unless the rule says */
    if (*s && *s != ',') {
        if (!(s = tz_hms(s, &west))) return 0;
        r->dst_east = -west;
    }
    if (!*s) {                                         /* no dates: POSIX leaves them to the system; the US's */
        r->start.kind = r->end.kind = 'M';
        r->start.month = 3; r->start.week = 2; r->start.day = 0; r->start.at = 7200;
        r->end.month = 11; r->end.week = 1; r->end.day = 0; r->end.at = 7200;
        return 1;
    }
    if (*s++ != ',' || !(s = tz_date(s, &r->start)) || *s++ != ',' || !(s = tz_date(s, &r->end))) return 0;
    return *s == 0;
}

/* ---- running it ----------------------------------------------------------------- */

/* The local midnight a rule's date falls on in year y, in days from 1 January 1970. */
static long tz_day_of(const struct tz_when *w, long y)
{
    long first = tz_days_to_year(y);
    int leap = tz_leap(y);
    if (w->kind == 'J') return first + w->n - 1 + (leap && w->n >= 60 ? 1 : 0);
    if (w->kind == 'N') return first + w->n;
    {
        long day1 = first + tz_month_days[leap][w->month - 1];       /* the 1st of the month */
        int wd1 = (int)((day1 + 4) % 7);                              /* 1 January 1970 was a Thursday */
        int len = tz_month_days[leap][w->month] - tz_month_days[leap][w->month - 1];
        int mday = 1 + (w->day - wd1 + 7) % 7 + 7 * (w->week - 1);
        while (mday > len) mday -= 7;
        return day1 + mday - 1;
    }
}

/* When summer time starts and ends in year y (local years), as UTC times. */
static void tz_changes(const struct tz_rule *r, long y, tz_t *start, tz_t *end)
{
    *start = (tz_t)tz_day_of(&r->start, y) * 86400 + r->start.at - r->std_east;
    *end = (tz_t)tz_day_of(&r->end, y) * 86400 + r->end.at - r->dst_east;
}

static int tz_dst_in(const struct tz_rule *r, long y, tz_t utc)
{
    tz_t s, e;
    tz_changes(r, y, &s, &e);
    return s < e ? utc >= s && utc < e : !(utc >= e && utc < s);   /* south of the equator it spans New Year */
}

/* The offset (seconds east of UTC) at a moment; *dst 1 in summer time. */
static long tz_offset_at(const struct tz_rule *r, tz_t utc, int *dst)
{
    int d = 0;
    if (r->has_dst) d = tz_dst_in(r, tz_year_of((long)tz_floor_div(utc + r->std_east, 86400)), utc);
    if (dst) *dst = d;
    return d ? r->dst_east : r->std_east;
}

/* The next change after utc: when (UTC) and the offset after it. 0 when the place has none. */
static int tz_next_change(const struct tz_rule *r, tz_t utc, tz_t *when, long *east_after)
{
    long y, y0;
    tz_t best = 0, c[2];
    int found = 0, i;
    if (!r->has_dst) return 0;
    y0 = tz_year_of((long)tz_floor_div(utc + r->std_east, 86400));
    for (y = y0 - 1; y <= y0 + 1; y++) {
        tz_changes(r, y, &c[0], &c[1]);
        for (i = 0; i < 2; i++)
            if (c[i] > utc && (!found || c[i] < best)) { best = c[i]; found = 1; }
    }
    if (!found) return 0;
    if (when) *when = best;
    if (east_after) *east_after = tz_offset_at(r, best, NULL);
    return tz_offset_at(r, best, NULL) != tz_offset_at(r, best - 1, NULL);
}

/* The offset a local time (the place's own clock) is on. A time the clocks skip in
 * spring is taken as summer time; one they show twice in autumn, as summer time
 * (the first of the two). */
static long tz_offset_for_local(const struct tz_rule *r, tz_t local, int *dst)
{
    int d;
    if (!r->has_dst) { if (dst) *dst = 0; return r->std_east; }
    if (tz_offset_at(r, local - r->dst_east, &d) == r->dst_east && d) { if (dst) *dst = 1; return r->dst_east; }
    if (tz_offset_at(r, local - r->std_east, &d) == r->std_east && !d) { if (dst) *dst = 0; return r->std_east; }
    if (dst) *dst = 1;
    return r->dst_east;
}

/* ---- a place: its rule, and the changes it can't say ---------------------------- */

/* A few places change in ways a POSIX rule can't put: Morocco's clocks go back for
 * Ramadan, and a place moving to one time all year has its last changes before the
 * rule takes over. zones.h lists those changes (from 2026); after the last of them,
 * the rule holds. */
struct tz_change {
    unsigned long when;         /* seconds since 1970, UTC (unsigned: past 2038) */
    long east;                  /* the offset from then */
    int dst;
};

struct tz_zone {
    struct tz_rule rule;
    const struct tz_change *changes;
    int count;
};

/* The listed change in force at utc, or NULL when the rule holds there. */
static const struct tz_change *tz_listed(const struct tz_zone *z, tz_t utc)
{
    int i;
    if (!z->count || utc < (tz_t)z->changes[0].when || utc >= (tz_t)z->changes[z->count - 1].when) return NULL;
    for (i = z->count - 1; i > 0 && (tz_t)z->changes[i].when > utc; i--) ;
    return &z->changes[i];
}

static long tz_zone_offset_at(const struct tz_zone *z, tz_t utc, int *dst)
{
    const struct tz_change *c = tz_listed(z, utc);
    if (!c) return tz_offset_at(&z->rule, utc, dst);
    if (dst) *dst = c->dst;
    return c->east;
}

static int tz_zone_next_change(const struct tz_zone *z, tz_t utc, tz_t *when, long *east_after)
{
    const struct tz_change *c = tz_listed(z, utc);
    if (c) {
        long now = c->east;
        for (c++; c < z->changes + z->count; c++)
            if ((tz_t)c->when > utc && tz_zone_offset_at(z, (tz_t)c->when, NULL) != now) {
                if (when) *when = (tz_t)c->when;
                if (east_after) *east_after = tz_zone_offset_at(z, (tz_t)c->when, NULL);
                return 1;
            }
        return tz_next_change(&z->rule, (tz_t)z->changes[z->count - 1].when, when, east_after);   /* the rule's, after the list */
    }
    if (z->count && utc < (tz_t)z->changes[0].when) {          /* before the list: the list's first, or the rule's */
        tz_t w;
        long e;
        if (tz_next_change(&z->rule, utc, &w, &e) && w < (tz_t)z->changes[0].when) {
            if (when) *when = w;
            if (east_after) *east_after = e;
            return 1;
        }
        if (when) *when = (tz_t)z->changes[0].when;
        if (east_after) *east_after = z->changes[0].east;
        return 1;
    }
    return tz_next_change(&z->rule, utc, when, east_after);
}

/* The offset a local time (the place's own clock) is on. A time the clocks skip in
 * spring is taken as summer time; one they show twice in autumn, as summer time. */
static long tz_zone_offset_for_local(const struct tz_zone *z, tz_t local, int *dst)
{
    long cand[2];
    int i, d;
    if (!z->count) return tz_offset_for_local(&z->rule, local, dst);
    cand[0] = z->rule.dst_east;
    cand[1] = z->rule.std_east;
    {   /* the listed offsets near this time, and the rule's */
        const struct tz_change *c = tz_listed(z, local - z->rule.std_east);
        if (c) { cand[0] = c->east; cand[1] = c > z->changes ? c[-1].east : c->east; }
    }
    for (i = 0; i < 2; i++)
        if (tz_zone_offset_at(z, local - cand[i], &d) == cand[i]) { if (dst) *dst = d; return cand[i]; }
    return tz_zone_offset_at(z, local - cand[0], dst);
}

/* ---- the background part's one step --------------------------------------------- */

/* What a real Amiga's clock needs now. Its clock keeps local time on the offset Locale
 * prefs saved (east, seconds); the place says what the offset is at that moment.
 * Returns how far to move the clock (seconds; 0 when it is right) and the offset to
 * save in *east_now. */
static long tz_keep_step(const struct tz_zone *z, tz_t local, long saved_east, long *east_now)
{
    tz_t utc = local - saved_east;
    long want = tz_zone_offset_at(z, utc, NULL);
    if (east_now) *east_now = want;
    return want - saved_east;
}

/* Days from 1 January 1970 to a date, and back: for the editor's text and the tests. */
static void tz_civil(tz_t t, int *year, int *month, int *mday, int *hour, int *min, int *sec)
{
    long days = (long)tz_floor_div(t, 86400), y = tz_year_of(days), yd;
    long s = (long)(t - (tz_t)days * 86400);
    int leap = tz_leap(y), m = 1;
    yd = days - tz_days_to_year(y);
    while (m < 12 && tz_month_days[leap][m] <= yd) m++;
    *year = (int)y; *month = m; *mday = (int)(yd - tz_month_days[leap][m - 1] + 1);
    *hour = (int)(s / 3600); *min = (int)(s / 60 % 60); *sec = (int)(s % 60);
}

static __attribute__((unused)) tz_t tz_from_civil(int year, int month, int mday, int hour, int min, int sec)   /* the tests' */
{
    long days = tz_days_to_year(year) + tz_month_days[tz_leap(year)][month - 1] + mday - 1;
    return (tz_t)days * 86400 + hour * 3600L + min * 60L + sec;
}
