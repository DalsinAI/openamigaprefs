/* OpenDock's timer: how long it sleeps, and what each wake looks at (0.6.11).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 *
 * Most of what the dock does comes to it as a signal: clicks and refreshes (IDCMP), icons dropped on it
 * (AppMessages), Ctrl-F from its editor, Ctrl-C, Workbench closing and opening. Three things come with no
 * message, so the dock has to look:
 *   - where the pointer is, for the name over the icon under it: Intuition reports the mouse only to the
 *     active window, and the dock is never active;
 *   - what is behind a see-through shelf: a backdrop window moved, or the desktop's picture drawn again;
 *   - which programs run, for the marks: taskclock.resource 2 counts the tasks added and removed, but has
 *     no signal (taskclock.h: "reads it on a timer of its own"), and OS 3.2.3's exec counts nothing.
 *
 * Until 0.6.10 the timer ran ten times a second all the time the dock was open. Now it runs once a second
 * while nothing is going on, and ten times a second only while the pointer is over the dock, a name is up
 * or what is behind the shelf has changed and not yet stayed put for 0.3 s. While the dock is closed there
 * is no timer. At rest that is 1 wake a second, was 10; the name comes up within a second of the pointer
 * reaching the dock (was a tenth), then follows it from icon to icon as before.
 *
 * Plain C with no Amiga headers, so the host tests (tests/test_od_dock.c) run it. */
#ifndef OD_WAKE_H
#define OD_WAKE_H

#define OD_TICK_FAST     100000UL     /* microseconds: the pointer over the dock, a name up, a window settling */
#define OD_TICK_SLOW     1000000UL    /* microseconds: nothing going on */
#define OD_STALE_EVERY   1000000UL    /* the look beside the shelf (a graphics card's desktop drawn again) */
#define OD_STALE_HOLD    3000000UL    /* ... and the wait after a copy that look made */
#define OD_RUNNING_EVERY 2000000UL    /* the look at the tasks, for the marks */

/* What keeps the timer fast (od_wake_next). */
#define OD_BUSY_POINTER  1            /* the pointer is over the dock, where the dock shows */
#define OD_BUSY_NAME     2            /* a name is up */
#define OD_BUSY_BEHIND   4            /* what is behind the shelf changed and has not yet stayed put */

/* What a wake looks at besides the pointer and the windows behind (od_wake_due). */
#define OD_DUE_STALE     1
#define OD_DUE_RUNNING   2

typedef struct od_wake {
    unsigned long stale_in;           /* microseconds until the look beside the shelf */
    unsigned long running_in;         /* microseconds until the look at the tasks */
} od_wake;

static inline void od_wake_init(od_wake *w)
{
    w->stale_in = OD_STALE_EVERY;
    w->running_in = OD_RUNNING_EVERY;
}

/* The next request's length in microseconds, from what is going on (OD_BUSY_*). */
static inline unsigned long od_wake_next(unsigned busy)
{
    return busy ? OD_TICK_FAST : OD_TICK_SLOW;
}

/* A request of `elapsed` microseconds came back: the slow looks due now (OD_DUE_*). They keep their own
 * pace whatever the requests' lengths: the tasks every 2 s, beside the shelf every second. */
static inline unsigned od_wake_due(od_wake *w, unsigned long elapsed)
{
    unsigned due = 0;
    if (w->stale_in <= elapsed) { w->stale_in = OD_STALE_EVERY; due |= OD_DUE_STALE; }
    else w->stale_in -= elapsed;
    if (w->running_in <= elapsed) { w->running_in = OD_RUNNING_EVERY; due |= OD_DUE_RUNNING; }
    else w->running_in -= elapsed;
    return due;
}

/* The look beside the shelf found it stale and the dock copied afresh: the next look waits 3 s. */
static inline void od_wake_stale_hold(od_wake *w)
{
    w->stale_in = OD_STALE_HOLD;
}

#endif
