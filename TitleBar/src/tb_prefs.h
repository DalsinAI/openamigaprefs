/* OpenPrefs Title bar: the settings OpenTitle and the editor share.
 *
 * ENV:OpenPrefs/TitleBar (and ENVARC: when saved), one setting a line:
 *   logo on|off            AmigaChrome's logo at the title bar's far left
 *   memory on|off          free chip and fast memory after Workbench's title
 *   clock on|off           the time at the right end, left of OpenSpeaker
 *   clock.date on|off      the day and date before the time
 *   border.black on|off    the display's border around the screen black
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef TB_PREFS_H
#define TB_PREFS_H

#include <string.h>
#include <stdio.h>

#define TB_ENV    "ENV:OpenPrefs/TitleBar"
#define TB_ENVARC "ENVARC:OpenPrefs/TitleBar"
#define TB_PORT   "OpenTitle"                 /* OpenTitle's public port: Ctrl-F to its task reads the settings again */
#define TB_TOOL   "SYS:C/OpenTitle"

typedef struct {
    int logo, memory, clock, date, border_black;
} tb_prefs;

static __attribute__((unused)) void tb_defaults(tb_prefs *p)
{
    p->logo = p->memory = p->clock = p->date = 1;
    p->border_black = 0;
}

/* text is changed (lines are cut); NULL gives the defaults */
static __attribute__((unused)) void tb_parse(tb_prefs *p, char *text)
{
    char *s, *line, *v;
    tb_defaults(p);
    if (!text) return;
    for (s = text; *s; ) {
        line = s;
        while (*s && *s != '\n') s++;
        if (*s) *s++ = 0;
        if (*line == ';' || !(v = strchr(line, ' '))) continue;
        *v++ = 0;
        if (!strcmp(line, "logo")) p->logo = !strncmp(v, "on", 2);
        else if (!strcmp(line, "memory")) p->memory = !strncmp(v, "on", 2);
        else if (!strcmp(line, "clock")) p->clock = !strncmp(v, "on", 2);
        else if (!strcmp(line, "clock.date")) p->date = !strncmp(v, "on", 2);
        else if (!strcmp(line, "border.black")) p->border_black = !strncmp(v, "on", 2);
    }
}

static __attribute__((unused)) int tb_text(const tb_prefs *p, char *out, int size)
{
    static const char *const oo[2] = { "off", "on" };
    return snprintf(out, size, "; OpenPrefs Title bar 0.1: what OpenTitle shows\n"
                    "logo %s\nmemory %s\nclock %s\nclock.date %s\nborder.black %s\n",
                    oo[!!p->logo], oo[!!p->memory], oo[!!p->clock], oo[!!p->date], oo[!!p->border_black]);
}

#endif
