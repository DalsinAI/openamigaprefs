/* Host tests for OpenDateTime's time zone rules (src/tzrule.c) and its table of
 * places (src/zones.h), built with the host's C compiler:
 *
 *   DateTime/test/run.sh
 *
 * - Europe/London: summer time from 01:00 UTC on the last Sunday of March to
 *   01:00 UTC on the last Sunday of October, 2026 to 2030, second by second
 *   at each change, and the background part's step on either side of each;
 * - Asia/Tokyo, a zone without summer time: the same offset all year, no change;
 * - a place south of the equator, half-hour and odd offsets;
 * - every place in the table against the host's own tz data (glibc's localtime
 *   with TZ set to the place), each hour of 2026 to 2030.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/tzrule.c"
#include "../src/zones.h"

static int failed, checks;

#define CHECK(what, cond) do { checks++; if (!(cond)) { failed++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

/* A place from the table, as the editor and its background part make it. */
static int zone_at(int i, struct tz_zone *z)
{
    memset(z, 0, sizeof *z);
    if (!tz_parse(zone_rules[zone_table[i].rule], &z->rule)) return 0;
    z->changes = zone_table[i].changes ? &zone_changes[zone_table[i].first] : NULL;
    z->count = zone_table[i].changes;
    return 1;
}

static struct tz_zone zone(const char *place)
{
    struct tz_zone z;
    int i;
    for (i = 0; i < ZONE_COUNT; i++) if (!strcmp(zone_table[i].name, place)) break;
    if (i == ZONE_COUNT || !zone_at(i, &z)) { printf("FAIL no rule for %s\n", place); failed++; memset(&z, 0, sizeof z); }
    return z;
}

static struct tz_rule rule(const char *place) { return zone(place).rule; }

static tz_t utc(int y, int mo, int d, int h, int mi, int s) { return tz_from_civil(y, mo, d, h, mi, s); }

/* The last Sunday of a month, worked out here the long way. */
static int last_sunday(int y, int mo)
{
    int d, last = 0;
    for (d = 1; d <= 31; d++) {
        struct tm tm;
        time_t t = (time_t)utc(y, mo, d, 12, 0, 0);
        gmtime_r(&t, &tm);
        if (tm.tm_mon + 1 != mo) break;
        if (tm.tm_wday == 0) last = d;
    }
    return last;
}

static void london(void)
{
    struct tz_zone z = zone("Europe/London");
    struct tz_rule r = z.rule;
    int y, dst;
    char what[96];
    CHECK("London: GMT and BST", r.has_dst && r.std_east == 0 && r.dst_east == 3600 && !strcmp(r.std_name, "GMT") &&
                                 !strcmp(r.dst_name, "BST"));
    for (y = 2026; y <= 2030; y++) {
        int mar = last_sunday(y, 3), oct = last_sunday(y, 10);
        tz_t spring = utc(y, 3, mar, 1, 0, 0), autumn = utc(y, 10, oct, 1, 0, 0), when = 0;
        long east = 0, now_east = 0;
        snprintf(what, sizeof what, "London %d: GMT to 00:59:59 UTC on Sunday %d March", y, mar);
        CHECK(what, tz_offset_at(&r, spring - 1, &dst) == 0 && !dst);
        snprintf(what, sizeof what, "London %d: BST from 01:00:00 UTC on Sunday %d March", y, mar);
        CHECK(what, tz_offset_at(&r, spring, &dst) == 3600 && dst);
        snprintf(what, sizeof what, "London %d: BST to 00:59:59 UTC on Sunday %d October", y, oct);
        CHECK(what, tz_offset_at(&r, autumn - 1, &dst) == 3600 && dst);
        snprintf(what, sizeof what, "London %d: GMT from 01:00:00 UTC on Sunday %d October", y, oct);
        CHECK(what, tz_offset_at(&r, autumn, &dst) == 0 && !dst);
        snprintf(what, sizeof what, "London %d: the next change after New Year is in March", y);
        CHECK(what, tz_next_change(&r, utc(y, 1, 1, 0, 0, 0), &when, &east) && when == spring && east == 3600);
        snprintf(what, sizeof what, "London %d: the next change after the spring one is in October", y);
        CHECK(what, tz_next_change(&r, spring, &when, &east) && when == autumn && east == 0);

        /* the background part on a real Amiga: the clock on GMT reads 00:59:59, then 01:00:00 at the change */
        snprintf(what, sizeof what, "London %d: no move a second before spring", y);
        CHECK(what, tz_keep_step(&z, spring - 1, 0, &now_east) == 0 && now_east == 0);
        snprintf(what, sizeof what, "London %d: an hour on at the spring change (01:00 GMT reads 02:00 BST)", y);
        CHECK(what, tz_keep_step(&z, spring, 0, &now_east) == 3600 && now_east == 3600);
        snprintf(what, sizeof what, "London %d: no move a second before autumn (01:59:59 BST)", y);
        CHECK(what, tz_keep_step(&z, autumn - 1 + 3600, 3600, &now_east) == 0 && now_east == 3600);
        snprintf(what, sizeof what, "London %d: an hour back at the autumn change (02:00 BST reads 01:00 GMT)", y);
        CHECK(what, tz_keep_step(&z, autumn + 3600, 3600, &now_east) == -3600 && now_east == 0);
        /* switched off over the change: on in November with the clock still on BST */
        snprintf(what, sizeof what, "London %d: started in November on a BST clock, an hour back", y);
        CHECK(what, tz_keep_step(&z, utc(y, 11, 10, 9, 0, 0) + 3600, 3600, &now_east) == -3600 && now_east == 0);
        snprintf(what, sizeof what, "London %d: started in April on a GMT clock, an hour on", y);
        CHECK(what, tz_keep_step(&z, utc(y, 4, 10, 9, 0, 0), 0, &now_east) == 3600 && now_east == 3600);

        /* the clock's own times: 01:30 on the spring Sunday is skipped; 01:30 in autumn comes twice */
        snprintf(what, sizeof what, "London %d: 00:30 local on the spring Sunday is GMT", y);
        CHECK(what, tz_offset_for_local(&r, utc(y, 3, mar, 0, 30, 0), &dst) == 0 && !dst);
        snprintf(what, sizeof what, "London %d: 03:00 local on the spring Sunday is BST", y);
        CHECK(what, tz_offset_for_local(&r, utc(y, 3, mar, 3, 0, 0), &dst) == 3600 && dst);
        snprintf(what, sizeof what, "London %d: 02:30 local on the autumn Sunday is GMT", y);
        CHECK(what, tz_offset_for_local(&r, utc(y, 10, oct, 2, 30, 0), &dst) == 0 && !dst);
        snprintf(what, sizeof what, "London %d: noon local in July is BST", y);
        CHECK(what, tz_offset_for_local(&r, utc(y, 7, 1, 12, 0, 0), &dst) == 3600 && dst);
    }
    CHECK("London 2026: spring is 29 March", last_sunday(2026, 3) == 29);
    CHECK("London 2026: autumn is 25 October", last_sunday(2026, 10) == 25);
}

static void tokyo(void)
{
    struct tz_zone z = zone("Asia/Tokyo");
    struct tz_rule r = z.rule;
    tz_t t, when;
    long east, now_east;
    int dst, same = 1;
    CHECK("Tokyo: JST, UTC+9, no summer time", !r.has_dst && r.std_east == 9 * 3600 && !strcmp(r.std_name, "JST"));
    for (t = utc(2026, 1, 1, 0, 0, 0); t < utc(2027, 1, 1, 0, 0, 0); t += 1800)
        if (tz_offset_at(&r, t, &dst) != 9 * 3600 || dst) same = 0;
    CHECK("Tokyo: UTC+9 every half hour of 2026", same);
    CHECK("Tokyo: no change to come", !tz_next_change(&r, utc(2026, 3, 29, 1, 0, 0), &when, &east));
    CHECK("Tokyo: the background part never moves the clock",
          tz_keep_step(&z, utc(2026, 3, 29, 10, 0, 0), 9 * 3600, &now_east) == 0 && now_east == 9 * 3600 &&
          tz_keep_step(&z, utc(2026, 10, 25, 10, 0, 0), 9 * 3600, &now_east) == 0);
    CHECK("Tokyo: a clock saved on UTC+0 is moved once, nine hours", tz_keep_step(&z, utc(2026, 6, 1, 0, 0, 0), 0, &now_east) == 9 * 3600);
}

static void others(void)
{
    struct tz_rule r = rule("Australia/Sydney");
    tz_t when;
    long east;
    int dst;
    CHECK("Sydney: AEDT in January", tz_offset_at(&r, utc(2027, 1, 15, 0, 0, 0), &dst) == 11 * 3600 && dst);
    CHECK("Sydney: AEST in July", tz_offset_at(&r, utc(2026, 7, 15, 0, 0, 0), &dst) == 10 * 3600 && !dst);
    CHECK("Sydney: summer time from 4 October 2026, 02:00 local", tz_next_change(&r, utc(2026, 7, 1, 0, 0, 0), &when, &east) &&
                                                               when == utc(2026, 10, 4, 2, 0, 0) - 10 * 3600 && east == 11 * 3600);
    r = rule("Asia/Kolkata");
    CHECK("Kolkata: UTC+5:30", !r.has_dst && r.std_east == 5 * 3600 + 1800);
    r = rule("America/New_York");
    CHECK("New York: EDT in July (UTC-4)", tz_offset_at(&r, utc(2026, 7, 4, 16, 0, 0), &dst) == -4 * 3600 && dst);
    CHECK("New York: EST in January (UTC-5)", tz_offset_at(&r, utc(2026, 1, 4, 16, 0, 0), &dst) == -5 * 3600 && !dst);
    r = rule("Australia/Lord_Howe");
    CHECK("Lord Howe: half an hour of summer time", r.has_dst && r.dst_east - r.std_east == 1800);
    {   /* places whose last changes the table lists (the rule holds after them) */
        struct tz_zone z = zone("America/Edmonton");
        long ok = 1;
        tz_t t;
        CHECK("Edmonton: changes listed", z.count >= 2);
        for (t = utc(2026, 1, 2, 0, 0, 0); t < utc(2028, 1, 1, 0, 0, 0); t += 3 * 3600) {
            int d1, d2;
            long e = tz_zone_offset_at(&z, t, &d1);
            if (tz_zone_offset_for_local(&z, t + e, &d2) != e) ok = 0;
        }
        CHECK("Edmonton: each local time read back to its offset, 2026-2027", ok);
        z = zone("Africa/Casablanca");
        CHECK("Casablanca: UTC+0 for Ramadan 2026 (local 1 March, noon)", tz_zone_offset_for_local(&z, utc(2026, 3, 1, 12, 0, 0), &dst) == 0);
        CHECK("Casablanca: UTC+1 in January 2026", tz_zone_offset_at(&z, utc(2026, 1, 15, 12, 0, 0), &dst) == 3600);
    }
    r = rule("UTC");
    CHECK("UTC: no offset, no summer time", !r.has_dst && r.std_east == 0);
    CHECK("a bad rule is refused", !tz_parse("GMT0BST,M13.5.0,M10.5.0", &r) && !tz_parse("", &r) && !tz_parse("123", &r));
    CHECK("no dates: the US's", tz_parse("EST5EDT", &r) && r.has_dst && r.start.month == 3 && r.end.month == 11);
}

static void civil(void)
{
    int y, mo, d, h, mi, s, ok = 1;
    tz_t t;
    for (t = utc(1978, 1, 1, 0, 0, 0); t < utc(2100, 1, 1, 0, 0, 0); t += 86400 * 7 + 3661) {
        struct tm tm;
        time_t tt = (time_t)t;
        gmtime_r(&tt, &tm);
        tz_civil(t, &y, &mo, &d, &h, &mi, &s);
        if (y != tm.tm_year + 1900 || mo != tm.tm_mon + 1 || d != tm.tm_mday || h != tm.tm_hour || mi != tm.tm_min || s != tm.tm_sec ||
            tz_from_civil(y, mo, d, h, mi, s) != t)
            ok = 0;
    }
    CHECK("dates from 1978 to 2099 as gmtime has them, and back", ok);
}

/* Every place against the host's tz data, each hour of 2026 to 2030. */
static void every_place(void)
{
    int i, bad = 0;
    for (i = 0; i < ZONE_COUNT; i++) {
        struct tz_zone z;
        tz_t t;
        int wrong = 0;
        tz_t first = 0;
        if (!zone_at(i, &z)) { printf("FAIL %s: rule %s not read\n", zone_table[i].name, zone_rules[zone_table[i].rule]); failed++; continue; }
        setenv("TZ", zone_table[i].name, 1);
        tzset();
        for (t = utc(2026, 1, 1, 0, 0, 0); t < utc(2031, 1, 1, 0, 0, 0); t += 3600) {
            struct tm tm;
            time_t tt = (time_t)t;
            localtime_r(&tt, &tm);
            if (tm.tm_gmtoff != tz_zone_offset_at(&z, t, NULL)) { if (!wrong++) first = t; }
        }
        {   /* each change the place says comes, the host's tz data has at that second */
            tz_t at = utc(2026, 1, 1, 0, 0, 0), when;
            long east;
            while (tz_zone_next_change(&z, at, &when, &east) && when < utc(2031, 1, 1, 0, 0, 0)) {
                struct tm before, after;
                time_t a = (time_t)(when - 1), b = (time_t)when;
                localtime_r(&a, &before);
                localtime_r(&b, &after);
                if (before.tm_gmtoff == after.tm_gmtoff || after.tm_gmtoff != east) { if (!wrong++) first = when; }
                at = when;
            }
        }
        if (wrong) {
            int y, mo, d, h, mi, s;
            tz_civil(first, &y, &mo, &d, &h, &mi, &s);
            printf("DIFFERS %s (%s): %d hours, first %04d-%02d-%02d %02d:00 UTC\n", zone_table[i].name, zone_rules[zone_table[i].rule],
                   wrong, y, mo, d, h);
            bad++;
        }
    }
    unsetenv("TZ");
    tzset();
    printf("every place: %d of %d agree with the host's tz data, each hour of 2026-2030\n", ZONE_COUNT - bad, ZONE_COUNT);
    checks++;
    if (bad) failed++;
}

int main(void)
{
    london();
    tokyo();
    others();
    civil();
    every_place();
    printf("%s: %d checks, %d failed\n", failed ? "FAIL" : "PASS", checks, failed);
    return failed ? 1 : 0;
}
