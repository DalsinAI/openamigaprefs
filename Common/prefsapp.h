/* prefsapp.h: where OpenPrefs is, for the programs that open it at a section (OpenTitle's cog, OpenSpeaker,
 * Menus, OpenMenus, OpenDock, OpenUp-Setup). salvor/Design-No-ROM-Volume-20261024.md, section 8: its place is
 * SYS:Preferences/OpenPrefs; an older disk has SYS:Prefs/OpenPrefs; the ROM has no copy (no ROM:).
 *
 *   op_app_path(0)   the first of SYS:Preferences/OpenPrefs and SYS:Prefs/OpenPrefs that is there, or NULL
 *   op_app_path(1)   the same, or else "OpenPrefs", for the Shell's path to find
 *
 * Header-only (each program includes it once); a process's call (pr_WindowPtr: no "insert volume" requester).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENPREFS_PREFSAPP_H
#define OPENPREFS_PREFSAPP_H

#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

static const char *op_app_path(int any)
{
    static const char *const places[] = { "SYS:Preferences/OpenPrefs", "SYS:Prefs/OpenPrefs" };
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    const char *found = any ? "OpenPrefs" : NULL;
    unsigned i;
    me->pr_WindowPtr = (APTR)-1;
    for (i = 0; i < sizeof places / sizeof places[0]; i++) {
        BPTR l = Lock((STRPTR)places[i], ACCESS_READ);
        if (l) {
            UnLock(l);
            found = places[i];
            break;
        }
    }
    me->pr_WindowPtr = was;
    return found;
}

#endif
