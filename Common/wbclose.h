/* wbclose.h: a program that keeps windows (or locks) on the Workbench screen
 * lets Workbench shut down.
 *
 * When something changes the Workbench screen (Font prefs' Use, a screen
 * mode change), IPrefs resets it, and Intuition asks "Intuition is attempting
 * to reset the Workbench screen. Please close the following windows:
 * <No title>". Every program of the Team that has a small window on the
 * screen (the bar's icons, the dock, the speaker) was on that list. This
 * uses workbench.library's setup/cleanup hook (V45): Workbench calls it
 * before it shuts down (TryCleanup, Cleanup) and when it is working again
 * (Setup). The hook runs in Workbench's task, so it only signals the program
 * and waits (up to 6 seconds) until the program has closed its windows.
 *
 * In the program:
 *     wbc_start();                               after the program's own libraries are open
 *     ... Wait(... | wbc_sigmask() ...);
 *     switch (wbc_take()) {
 *     case WBC_CLOSE: close the windows, free the screen's DrawInfo,
 *                     UnlockPubScreen; wbc_closed(); break;
 *     case WBC_OPEN:  open them again (retry on later turns while the
 *                     Workbench screen isn't there yet); break;
 *     }
 *     wbc_stop();                                before the program ends
 *
 * Where workbench.library is older than 45 nothing happens (wbc_sigmask()
 * is 0). A program includes this once, and defines struct Library
 * *WorkbenchBase.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef WBCLOSE_H
#define WBCLOSE_H

#include <exec/types.h>
#include <exec/tasks.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/wb.h>

#define WBC_CLOSE 1
#define WBC_OPEN  2
#define WBC_UNUSED __attribute__((unused))

extern struct Library *WorkbenchBase;

static struct Hook wbc_hook;
static struct Task *wbc_task;
static LONG wbc_sig = -1;
static volatile LONG wbc_want, wbc_ack, wbc_shut;
static int wbc_opened, wbc_added;

/* runs in Workbench's task: only globals, signals and Delay */
static ULONG __attribute__((used)) wbc_entry(register struct Hook *h __asm("a0"), register APTR obj __asm("a2"),
                                              register struct SetupCleanupHookMsg *m __asm("a1"))
{
    int i;
    (void)h; (void)obj;
    if (!m || wbc_sig < 0) return 0;
    if (m->schm_State == SCHMSTATE_Setup) {
        wbc_shut = 0;
        wbc_want = WBC_OPEN;
        Signal(wbc_task, 1UL << wbc_sig);
    } else if (!wbc_shut) {                        /* TryCleanup, then Cleanup: the windows go once */
        wbc_ack = 0;
        wbc_want = WBC_CLOSE;
        Signal(wbc_task, 1UL << wbc_sig);
        for (i = 0; i < 120 && !wbc_ack; i++) Delay(3);
    }
    return 0;
}

static WBC_UNUSED void wbc_start(void)
{
    struct TagItem tags[2];
    if (wbc_added) return;
    if (!WorkbenchBase) {
        WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 45);
        if (!WorkbenchBase) return;
        wbc_opened = 1;
    } else if (WorkbenchBase->lib_Version < 45) return;
    if ((wbc_sig = AllocSignal(-1)) < 0) return;
    wbc_task = FindTask(NULL);
    wbc_hook.h_Entry = (ULONG (*)())wbc_entry;
    wbc_hook.h_SubEntry = NULL;
    wbc_hook.h_Data = NULL;
    tags[0].ti_Tag = WBCTRLA_AddSetupCleanupHook; tags[0].ti_Data = (ULONG)&wbc_hook;
    tags[1].ti_Tag = TAG_DONE; tags[1].ti_Data = 0;
    if (WorkbenchControlA(NULL, tags)) wbc_added = 1;
    else { FreeSignal(wbc_sig); wbc_sig = -1; }
}

static WBC_UNUSED ULONG wbc_sigmask(void) { return wbc_sig < 0 ? 0 : 1UL << wbc_sig; }

/* what Workbench asked since the last call: WBC_CLOSE, WBC_OPEN or 0 */
static WBC_UNUSED int wbc_take(void)
{
    int w;
    if (wbc_sig < 0) return 0;
    SetSignal(0, 1UL << wbc_sig);
    w = wbc_want;
    wbc_want = 0;
    return w;
}

/* the windows and locks are gone: Workbench can go on */
static WBC_UNUSED void wbc_closed(void)
{
    wbc_shut = 1;
    wbc_ack = 1;
}

static WBC_UNUSED void wbc_stop(void)
{
    struct TagItem tags[2];
    if (wbc_added) {
        tags[0].ti_Tag = WBCTRLA_RemSetupCleanupHook; tags[0].ti_Data = (ULONG)&wbc_hook;
        tags[1].ti_Tag = TAG_DONE; tags[1].ti_Data = 0;
        WorkbenchControlA(NULL, tags);
        wbc_ack = 1;                               /* a hook still waiting goes on */
        Delay(5);
        wbc_added = 0;
    }
    if (wbc_sig >= 0) { FreeSignal(wbc_sig); wbc_sig = -1; }
    if (wbc_opened && WorkbenchBase) { CloseLibrary(WorkbenchBase); WorkbenchBase = NULL; wbc_opened = 0; }
}

#endif
