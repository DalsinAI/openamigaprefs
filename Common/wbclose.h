/* wbclose.h: a program that keeps windows (or locks) on the Workbench screen
 * lets IPrefs reset it.
 *
 * When something changes the Workbench screen (Font prefs' Use, a screen
 * mode), IPrefs calls CloseWorkBench() and Intuition asks "Intuition is
 * attempting to reset the Workbench screen. Please close the following
 * windows: <No title>". Every program of the Team that has a small window on
 * the screen (the bar's icons, the dock, the speaker) was on that list.
 * (workbench.library's setup/cleanup hook is called later, when Workbench
 * closes its drawers, which is after that requester.)
 *
 * OpenLook (opengadtools, look/wbnotify.h) patches CloseWorkBench() and
 * OpenWorkBench(). Before the first it tells every program registered here
 * to close its windows and waits (3 seconds at most) until each has said it
 * has; after the second it tells them the screen is back. The programs are
 * registered in the public semaphore "OpenWBNotify" (a list of client
 * records). Where OpenLook isn't running nothing is asked, and nothing
 * changes: the requester comes as it always did.
 *
 * In the program:
 *     wbc_start();                               before the main loop
 *     ... Wait(... | wbc_sigmask() ...);
 *     switch (wbc_take()) {
 *     case WBC_CLOSE: close the windows, free the screen's DrawInfo,
 *                     UnlockPubScreen; wbc_closed(); break;
 *     case WBC_OPEN:  open them again (retry on later turns while the
 *                     Workbench screen isn't there yet); break;
 *     }
 *     wbc_stop();                                before the program ends
 *
 * wbc_take() is called each time the program wakes; it also registers the
 * program when OpenLook has started since. A program includes this once.
 *
 * The record layout must stay the same as opengadtools' look/wbnotify.h.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef WBCLOSE_H
#define WBCLOSE_H

#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

#define WBN_SEM_NAME "OpenWBNotify"
#define WBN_MAGIC    0x57424e31UL              /* 'WBN1' */
#define WBC_CLOSE 1
#define WBC_OPEN  2
#define WBC_UNUSED __attribute__((unused))

struct WBNClient {
    struct MinNode node;
    struct Task *task;                         /* signalled with sigmask */
    ULONG sigmask;
    volatile LONG want;                        /* WBC_CLOSE or WBC_OPEN, 0 when taken */
    volatile LONG ack;                         /* 1 when a close has been done */
};

struct WBNRoot {
    struct SignalSemaphore sem;
    struct MinList clients;
    ULONG magic;
    char name[16];
};

static struct WBNRoot *wbc_root;
static struct WBNClient *wbc_client;
static LONG wbc_sig = -1;
static int wbc_calls;

/* the root, made when it isn't there (never freed: it is a few bytes, and a patch may point at it) */
static WBC_UNUSED struct WBNRoot *wbc_find_root(void)
{
    struct WBNRoot *r;
    Forbid();
    r = (struct WBNRoot *)FindSemaphore((STRPTR)WBN_SEM_NAME);
    if (!r && (r = AllocMem(sizeof *r, MEMF_PUBLIC | MEMF_CLEAR))) {
        strcpy(r->name, WBN_SEM_NAME);
        r->sem.ss_Link.ln_Name = r->name;
        r->sem.ss_Link.ln_Pri = 0;
        InitSemaphore(&r->sem);
        r->clients.mlh_Head = (struct MinNode *)&r->clients.mlh_Tail;
        r->clients.mlh_Tail = NULL;
        r->clients.mlh_TailPred = (struct MinNode *)&r->clients.mlh_Head;
        r->magic = WBN_MAGIC;
        AddSemaphore(&r->sem);
    }
    Permit();
    return r && r->magic == WBN_MAGIC ? r : NULL;
}

static WBC_UNUSED void wbc_register(void)
{
    struct WBNRoot *r;
    if (wbc_client || wbc_sig < 0) return;
    if (!(r = wbc_find_root())) return;
    if (!(wbc_client = AllocVec(sizeof *wbc_client, MEMF_PUBLIC | MEMF_CLEAR))) return;
    wbc_client->task = FindTask(NULL);
    wbc_client->sigmask = 1UL << wbc_sig;
    ObtainSemaphore(&r->sem);
    AddTail((struct List *)&r->clients, (struct Node *)&wbc_client->node);
    ReleaseSemaphore(&r->sem);
    wbc_root = r;
}

static WBC_UNUSED void wbc_start(void)
{
    if (wbc_sig >= 0) return;
    if ((wbc_sig = AllocSignal(-1)) < 0) return;
    wbc_register();
}

static WBC_UNUSED ULONG wbc_sigmask(void) { return wbc_sig < 0 ? 0 : 1UL << wbc_sig; }

/* what OpenLook asked since the last call: WBC_CLOSE, WBC_OPEN or 0 */
static WBC_UNUSED int wbc_take(void)
{
    int w;
    if (wbc_sig < 0) return 0;
    if (!wbc_client) {
        if (++wbc_calls % 20 == 0) wbc_register();     /* OpenLook may have started since */
        return 0;
    }
    SetSignal(0, 1UL << wbc_sig);
    w = wbc_client->want;
    wbc_client->want = 0;
    return w;
}

/* the windows and locks are gone: OpenLook can let Workbench go */
static WBC_UNUSED void wbc_closed(void)
{
    if (wbc_client) wbc_client->ack = 1;
}

static WBC_UNUSED void wbc_stop(void)
{
    if (wbc_client && wbc_root) {
        ObtainSemaphore(&wbc_root->sem);
        Remove((struct Node *)&wbc_client->node);
        ReleaseSemaphore(&wbc_root->sem);
        FreeVec(wbc_client);
        wbc_client = NULL;
        wbc_root = NULL;
    }
    if (wbc_sig >= 0) { FreeSignal(wbc_sig); wbc_sig = -1; }
}

#endif
