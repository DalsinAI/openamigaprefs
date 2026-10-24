/* OpenWindows' places: how a window is keyed, where a window with a kept place
 * goes, and the tags that open it there (Windows 0.9.2, 24 October 2026).
 *
 * Pure (no Amiga calls): Windows/tests/test_places.c runs it on the host,
 * built with -DPLACES_HOST, which gives the few NDK types and tags it needs.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENWINDOWS_PLACES_H
#define OPENWINDOWS_PLACES_H

#ifdef PLACES_HOST
struct TagItem { unsigned long ti_Tag, ti_Data; };
#define TAG_DONE   0UL
#define TAG_IGNORE 1UL
#define TAG_MORE   2UL
#define TAG_SKIP   3UL
#define TAG_USER   0x80000000UL
#define WA_Dummy       (TAG_USER + 99)
#define WA_Left        (WA_Dummy + 0x01)
#define WA_Top         (WA_Dummy + 0x02)
#define WA_Width       (WA_Dummy + 0x03)
#define WA_Height      (WA_Dummy + 0x04)
#define WA_Title       (WA_Dummy + 0x0B)
#define WA_MinWidth    (WA_Dummy + 0x0F)
#define WA_MinHeight   (WA_Dummy + 0x10)
#define WA_MaxWidth    (WA_Dummy + 0x11)
#define WA_MaxHeight   (WA_Dummy + 0x12)
#define WA_InnerWidth  (WA_Dummy + 0x13)
#define WA_InnerHeight (WA_Dummy + 0x14)
#else
#include <utility/tagitem.h>
#include <intuition/intuition.h>
#endif

#define PLACES_MAX 128                      /* 0.9.2: 64 before, and the file was full */
#define PLACE_TAGS_MAX 512                  /* a program's tag list longer than this is opened as it asks */

struct place { char key[64]; short l, t, w, h; unsigned long used; };

/* "program:title", the title cut at its first digit, bracket or quote (a count
 * or a file name in it still matches), at 30 characters, spaces off its end */
void place_key(const char *title, const char *prog, char *key, int size);

/* 1 when the window can have a place: not a backdrop or GimmeZeroZero window,
 * and with a drag bar or a title. An empty title is no title (0.9.2: a
 * borderless popup titled "" with no IDCMP was "window:", and took the place
 * of whatever else was) */
int place_candidate(int backdrop_or_gzz, int dragbar, const char *title);

/* 1 for Workbench's own windows: Workbench's, and (0.9.2) OpenBench's, which
 * marks its drawers WFLG_WBENCHWINDOW only up to two seconds after they open */
int place_workbench(const char *prog, int wbench_flag);

/* Where a window with a kept place goes: the place's corner, and its size when
 * the window can be sized and the size is within its limits (maxw, maxh 0 or
 * 0xffff: none), kept on the screen (sw, sh; 0 not known). wd, ht come in as
 * the window's own size (0 when not known) and go out as the size it will have. */
void fit_place(const struct place *pl, int sizeable, short minw, short minh, short maxw, short maxh,
               short sw, short sh, short *l, short *t, short *wd, short *ht);

/* What a program asks for, from its NewWindow and tags */
struct place_ask {
    short w, h;                             /* the outer size asked (0: not known) */
    int inner;                              /* it asks by WA_InnerWidth or WA_InnerHeight */
    int sizeable;                           /* it has a size gadget */
    short minw, minh, maxw, maxh;
};
/* Where it opens, and whether its size is ours too */
struct place_plan { short l, t, w, h; int size; };
void place_plan(const struct place *pl, const struct place_ask *a, short sw, short sh, struct place_plan *p);

/* The real items of a tag list (TAG_MORE followed, TAG_IGNORE and TAG_SKIP
 * honoured), or -1 when there are more than PLACE_TAGS_MAX */
int place_tags_count(const struct TagItem *list);
/* out: with front, the plan's tags (WA_Left, WA_Top; WA_Width and WA_Height
 * when the size is ours); then a copy of the program's own list, flattened,
 * without the tags the plan gives (WA_Left, WA_Top; and WA_Width, WA_Height,
 * WA_InnerWidth, WA_InnerHeight when the size is ours); then TAG_DONE. So the
 * plan's are the only placing tags, whether Intuition reads a tag's first or
 * its last (OpenIntuition reads the last: 0.9.1 put the plan's in front of the
 * program's own, and on the open ROM the program's won). Without front, the
 * copy alone (an ExtNewWindow's
 * Extension). Answers the items written, -1 when max is too small. */
int place_tags(struct TagItem *out, int max, const struct place_plan *plan, const struct TagItem *list, int front);

/* The places table: the index of key, or -1; with make, a new entry (the one
 * used longest ago when it is full). */
int place_index(struct place *pl, int *n, const char *key, int make);
/* "program:title" l t w h, as the file has it; 1 when the line is one */
int place_parse(const char *line, struct place *pl);
/* order[] the table's indexes, the one used longest ago first: the file is
 * written so, and read back with used = the line's number, so which places
 * were used last survives a restart (0.9.2) */
void place_order(const struct place *pl, int n, int *order);

#endif
