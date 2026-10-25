/* deskicons.h: the desktop's No icons (Salvor's OS list, item 59; the user, 24 October 2026, 21:08: "offer a no-icons
 * mode too... for workbench itself"), shared by OpenPrefs' Icons page, which sets it, and OpenBench, which follows it.
 *
 * One variable beside icon.library's ENV:Sys/IconLabels and the icons' layout, ENV:Sys/IconLayout, kept the same way:
 * ENV:Sys/DesktopIcons, one word.
 *   - "none": the desktop (OpenBench's root window) shows no icons at all: no disks, no icons left out, no AppIcons.
 *     The backdrop, the bar, the dock and the meters stay. Disks still mount, and drawers still open from the dock,
 *     the bar or a Shell.
 *   - "show", no variable, or a word not known: the icons, as before.
 * The Icons page's Use writes ENV:, its Save ENV: and ENVARC: (SetVar, GVF_SAVE_VAR), as it writes the other two.
 * OpenBench's Window > No icons writes both too, and its Icons > Clean Up with No icons on writes "show" and lays the
 * icons out. OpenBench follows a change at once (a DOS notify on ENV:Sys/DesktopIcons, as on the other two): the
 * icons go, and come back where they were.
 *
 * A variable of its own rather than a word in IconLayout: IconLayout's words are read and written by opengadtools'
 * workbench/wb_grid.c, which the drawers share and which this setting has nothing to say to.
 *
 * Plain C with no Amiga headers, so Common/tests builds it on the host as it is.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef DESKICONS_H
#define DESKICONS_H

#define DESKICONS_VAR "Sys/DesktopIcons"    /* GetVar and SetVar's name: ENV:Sys/DesktopIcons (ENVARC: on Save) */
#define DESKICONS_SHOW 0                    /* the desktop's icons shown (the default) */
#define DESKICONS_NONE 1                    /* no icons on the desktop */

/* The first word of w (n its length, or -1 to its NUL), case aside, after any spaces: DESKICONS_SHOW for "show",
 * "icons", "on" or "yes"; DESKICONS_NONE for "none", "noicons", "off", "no" or "hide"; -1 for no word or one not
 * known (OpenPrefs ICONS DESKTOP leaves the setting alone then). */
static inline int deskicons_word(const char *w, long n)
{
    static const char *const show[] = { "show", "icons", "on", "yes", 0 };
    static const char *const none[] = { "none", "noicons", "off", "no", "hide", 0 };
    char word[12];
    long i = 0, k = 0;
    int j;
    if (!w) return -1;
    if (n < 0)
        for (n = 0; w[n]; n++) ;
    while (i < n && (w[i] == ' ' || w[i] == '\t' || w[i] == '\r' || w[i] == '\n')) i++;
    while (i < n && w[i] && w[i] != ' ' && w[i] != '\t' && w[i] != '\r' && w[i] != '\n') {
        char c = w[i++];
        if (k == (long)sizeof word - 1) return -1;          /* longer than any word known */
        word[k++] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
    }
    word[k] = 0;
    if (!k) return -1;
    for (j = 0; show[j]; j++) {
        const char *a = show[j], *b = word;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return DESKICONS_SHOW;
    }
    for (j = 0; none[j]; j++) {
        const char *a = none[j], *b = word;
        while (*a && *a == *b) { a++; b++; }
        if (!*a && !*b) return DESKICONS_NONE;
    }
    return -1;
}

/* The variable's text (n its length, or -1 to its NUL; NULL when there is no variable): DESKICONS_NONE only when its
 * first word says so, DESKICONS_SHOW otherwise, so a damaged variable never hides the icons. */
static inline int deskicons_parse(const char *text, long n)
{
    return deskicons_word(text, n) == DESKICONS_NONE ? DESKICONS_NONE : DESKICONS_SHOW;
}

/* The word written for a setting: "none" or "show" */
static inline const char *deskicons_text(int mode)
{
    return mode == DESKICONS_NONE ? "none" : "show";
}

#endif
