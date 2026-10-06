/* OpenMenus' keyboard control: where a key moves the highlight in a menu
 * strip, and what it does. Plain C with no Amiga calls, so the host tests
 * (Menus/tests/test_om_kbd.c) check it; the engine draws what it says.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OM_KBD_H
#define OM_KBD_H

/* An item or subitem as the keys see it. */
typedef struct omk_item {
    const char *name;                       /* its text, "" for a picture or a separator */
    char command;                           /* its Amiga-key letter, or 0 */
    int enabled;                            /* it can be chosen (separators can't) */
    int checkit;                            /* a check-mark item */
    int nsubs;
    const struct omk_item *subs;
} omk_item;

typedef struct omk_menu {
    const char *name;
    int enabled;
    int nitems;
    const omk_item *items;
} omk_menu;

typedef struct omk_model {
    int nmenus;
    const omk_menu *menus;
} omk_model;

/* Where the highlight is: a title, then an item of its menu (-1: none yet,
 * the menu's items are open below its title), then a subitem (-1: none open). */
typedef struct omk_pos { int menu, item, sub; } omk_pos;

enum {
    OMK_LEFT, OMK_RIGHT, OMK_UP, OMK_DOWN,
    OMK_FIRST, OMK_LAST,                    /* Shift + Up, Shift + Down */
    OMK_RETURN, OMK_SPACE, OMK_ESC, OMK_HELP, OMK_TAB,
    OMK_CHAR,                               /* a letter or digit: type-ahead */
    OMK_COMMAND                             /* Right Amiga + a letter */
};

enum {
    OMK_NONE,                               /* nothing happens */
    OMK_MOVED,                              /* the highlight moved (pos changed) */
    OMK_OPEN_SUB,                           /* pos.sub is now open on its first item */
    OMK_CHOOSE,                             /* choose pos and close the menus */
    OMK_CHOOSE_STAY,                        /* choose pos (a check mark) and keep the menus open */
    OMK_CLOSE_LEVEL,                        /* one level closed (pos says which is left) */
    OMK_CLOSE_ALL,                          /* close everything */
    OMK_HELP_ITEM                           /* help for pos, then close */
};

/* One key: moves *pos and says what to do. ch is the key's character for
 * OMK_CHAR and OMK_COMMAND, else 0. */
int omk_key(const omk_model *m, omk_pos *pos, int key, int ch);

/* The first title to open on: 0, or the first enabled one. */
int omk_first_menu(const omk_model *m);

#endif
