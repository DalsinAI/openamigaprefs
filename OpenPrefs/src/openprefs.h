/* openprefs.h: OpenPrefs's pages (each editor's main, renamed when it is built into OpenPrefs) and APPLY.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENPREFS_H
#define OPENPREFS_H

#define OP_VERSION "2.3"                   /* higher than every editor it replaces (OpenLook 1.2, Windows 0.6.5, ...) */

int look_main(int argc, char **argv);
int windows_main(int argc, char **argv);
int titlebar_main(int argc, char **argv);
int menus_main(int argc, char **argv);
int appearance_main(int tab);
int dock_main(int argc, char **argv);
int sound_main(int argc, char **argv);
int input_main(int argc, char **argv);
int gamepads_main(int argc, char **argv);
int datetime_main(int argc, char **argv);
int serial_main(int argc, char **argv);
int types_main(int argc, char **argv);
int system_main(int argc, char **argv);
int blanker_main(int argc, char **argv);
int screenmode_main(int argc, char **argv);
int icontrol_main(int argc, char **argv);
int icons_main(int argc, char **argv);    /* the Icons page (2.3): labels' place and gap, the column grid */

/* A page by its name ("Date & Time", "Title Bar"), letters in either case; -1 for none (opframe.c) */
int lt_page_named(const char *name);

/* OpenPrefs APPLY (op_apply.c): what the boot's OpenSound USE, OpenGamepads USE and OpenSystem APPLY did */
int op_apply(void);
extern char *op_argv[];                    /* { "OpenPrefs", NULL }: a page given lt_args */

#endif
