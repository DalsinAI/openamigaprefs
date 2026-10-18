/* OpenWindows' action filter: the gesture table (docs/design/action-filter.md).
 *
 * One row a gesture: where the press was (the target), the button, the
 * qualifiers held, what the hand did with it (a click, a double-click, a
 * press and a move past the threshold), and the actions that follow, in
 * order, with whether the press is OpenWindows' own (take) or goes on to
 * Intuition (pass). One table, one decision a gesture: OpenWindows' input
 * handler decides at the press whether it takes it, and at the release, or
 * at the first move past the threshold, which gesture it was.
 *
 * Pure (no Amiga calls): Windows/tests/test_gestures.c runs it on the host.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENWINDOWS_GESTURES_H
#define OPENWINDOWS_GESTURES_H

enum { GT_TITLE, GT_BODY, GT_GADGET, GT_EDGE, GT_BACKDROP, GT_SCREENBAR, GT_COUNT };   /* targets */
enum { GB_SELECT, GB_MENU, GB_MIDDLE, GB_COUNT };                                     /* buttons */
enum { GK_CLICK, GK_DOUBLE, GK_MOVE, GK_COUNT };                                      /* gestures */

#define GA_FRONT    0x01u                   /* the window to the front (none in front of it covers it any more) */
#define GA_BACK     0x02u                   /* the window to the back */
#define GA_ACTIVATE 0x04u                   /* the window made active */
#define GA_DRAG     0x08u                   /* the window follows the pointer (Intuition's drag, or OpenWindows' own) */
#define GA_RESIZE   0x10u                   /* the edge or corner held follows the pointer */
#define GA_SNAP     0x20u                   /* at the release, the window lines up with an edge near it */

#define GQ_SHIFT    0x01u                   /* the qualifiers a row names: either Shift, Alt, Ctrl, Amiga */
#define GQ_ALT      0x02u
#define GQ_CTRL     0x04u
#define GQ_AMIGA    0x08u
#define GQ_ANY      0xFFu                   /* whatever is held */

#define GESTURE_ROWS 48
#define GESTURE_MOVE 4                      /* the threshold: a press that goes this many pixels either way is a move */

struct gesture_row {
    unsigned char target, button, kind, take;
    unsigned char qual;                     /* GQ_ bits that must be held, exactly; GQ_ANY: any */
    unsigned char actions;                  /* GA_ bits, done in the order of the bits */
};

struct gesture_table {
    struct gesture_row row[GESTURE_ROWS];
    int n;
};

/* The built-in table: OS 3.2.3's own behaviour plus the extras the
 * settings turn on (ENV:OpenPrefs/Windows): a click with no move fronts the
 * window (title bar or inside it), press-and-move on the title bar drags
 * at once (Intuition's drag) and snaps at the release, a double-click on
 * the title bar fronts it, and the edges resize. */
void gestures_default(struct gesture_table *t, int dblfront, int edges, int snap);
/* Rows from a Gestures file's text (lines "target button qualifier gesture
 * actions press", ';' comments). Answers how many rows were read, or
 * -(line number) at the first line it can't read; the table is then left as
 * it was. */
int gestures_parse(struct gesture_table *t, const char *text);
/* The amiga's qualifier word (IEQUALIFIER_*) as the table's GQ_ bits */
unsigned gestures_qual(unsigned ie_qualifier);
/* The actions of the row for this gesture (0 none); *take, when given, 1
 * when a row of this target, button and qualifiers takes the press. */
unsigned gestures_actions(const struct gesture_table *t, int target, int button, int kind, unsigned qual, int *take);
/* 1 when the pointer has gone past the threshold from where the press was */
int gestures_moved(int x0, int y0, int x, int y);
/* A row as text, for the doc and the tests: "title select none move drag,snap pass" */
void gestures_row_text(const struct gesture_row *r, char *out, int size);

#endif
