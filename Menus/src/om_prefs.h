/* om_prefs: OpenMenus' settings (DESIGN.md section 2, format 1), and taking
 * over MagicMenu's.
 *
 * Plain C with no Amiga calls, so the same code is tested on the host
 * (tests/test_om_prefs.c) and runs on the Amiga, in the Menus editor
 * and in the OpenMenus engine alike.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OM_PREFS_H
#define OM_PREFS_H

enum { OM_OPEN_PULLDOWN = 0, OM_OPEN_POPUP = 1, OM_OPEN_POINTER = 2 };
enum { OM_USE_HOLD = 0, OM_USE_STICKY = 1, OM_USE_CLICK = 2 };
enum { OM_PD = 0, OM_PU = 1 };              /* pull-down, pop-up */
enum { OM_BG_SOLID = 0, OM_BG_SEE = 1, OM_BG_IMAGE = 2, OM_BG_SEEIMAGE = 3 };
enum { OM_COL_THEME = 0, OM_COL_SCREEN = 1, OM_COL_OWN = 2 };
enum { OM_BAR_TITLE = 0, OM_BAR_TOP = 1, OM_BAR_BOTTOM = 2, OM_BAR_LEFT = 3, OM_BAR_RIGHT = 4 };   /* where the menu bar sits */
enum {
    OM_C_BACKGROUND, OM_C_TEXT, OM_C_SELECTED, OM_C_SELECTED_TEXT,
    OM_C_LIGHT, OM_C_DARK, OM_C_SHADOW, OM_C_COUNT
};

#define OM_DELAY_MAX 10                     /* tenths of a second */
#define OM_SHADOW_SIZE_MAX 10
#define OM_SHADOW_STRENGTH_MAX 100

typedef struct om_prefs {
    int enabled;                            /* off: the OS's own menus, pixel for pixel */
    int open;                               /* OM_OPEN_* */
    int use[2];                             /* OM_USE_*, pull-down and pop-up */
    int delay[2];                           /* 0 (none) to OM_DELAY_MAX tenths of a second */
    int popup_last;                         /* a pop-up opens on the entry last chosen */
    int sub_centre;                         /* submenus centred on the item that opened them */
    int sub_mark;                           /* an arrow on items with a submenu */
    int border_double;
    int shadow, shadow_size, shadow_strength;
    int background;                         /* OM_BG_* */
    char image[256];
    int separators_bold;
    int colours;                            /* OM_COL_* */
    char colour[OM_C_COUNT][8];             /* "#rrggbb", used when colours is OM_COL_OWN */
    int keyboard;
    char key[64];                           /* a commodities key description, "ramiga space" */
    int keyboard_ralt;                      /* Right Amiga + Right Alt as well */
    int keyboard_top;                       /* the pointer goes to the screen bar */
    int keep_running;                       /* programs go on while a menu is open */
    char imported[64];                      /* the MagicMenu file taken over, so it happens once */
    int rightclick;                         /* the right button on Workbench's icons and desktop: their menu */
    int rightclick_extras;                  /* the Open family's entries too (Open with, Extract, Send Out) */
    int rightclick_selection;               /* on a selected icon: the whole selection (else that icon only) */
    int rightclick_name;                    /* the icon's name at the top of its menu */
    int bar;                                /* OM_BAR_*: the screen's title bar, or our own bar at an edge */
    int bar_autohide;                       /* our bar hides until the pointer reaches its edge */
} om_prefs;

extern const char *const om_colour_keys[OM_C_COUNT];

void om_defaults(om_prefs *p);
/* Reads the settings text; unknown lines are skipped, so a newer engine's
 * file still loads. 1, or 0 when text is NULL. */
int om_parse(om_prefs *p, const char *text);
/* Writes the settings text. Its length, or -1 when out is too small. */
int om_write(const om_prefs *p, char *out, int size);

/* MagicMenu's settings file (ENVARC:MagicMenu.prefs: "MagicMenu/2:", then
 * "<tab>Name=value" lines, ending "#") onto p. Read from the file's layout;
 * no MagicMenu code. 1, or 0 with the reason in err. */
int om_from_magicmenu(om_prefs *p, const char *text, char *err, int errlen);

#endif
