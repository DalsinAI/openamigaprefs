/* wbclose.h: a program that keeps windows (or locks) on the Workbench screen
 * lets IPrefs reset it.
 *
 * When something changes the Workbench screen (Font prefs' Use, a screen
 * mode), IPrefs calls CloseWorkBench() and asks "Intuition is attempting to
 * reset the Workbench screen. Please close the following windows: <No
 * title>". Every program of the Team that has a small window on the screen
 * (the bar's icons, the dock, the speaker, OpenMeters) was on that list.
 *
 * Measured on OS 3.2.3 (lab t5-wbreset, 16 October 2026, WBResetProbe):
 *   - IPrefs is told of a new ENV:Sys/Font.prefs by DOS notification, and so
 *     is any program that asks. A program that closed its window when its
 *     own notification came had it closed 16 ticks before workbench.library
 *     called its setup/cleanup hook with SCHMSTATE_TryCleanup;
 *   - with a window of another program still open, the requester came and
 *     the hook was never called: workbench.library's hook
 *     (WBCTRLA_AddSetupCleanupHook, V45) comes after that check, too late to
 *     tell anyone;
 *   - the requester goes by itself once the windows it lists have closed;
 *   - SCHMSTATE_Setup comes when Workbench is back.
 * OS 3.2 has no documented notice before the check, so this file uses those
 * documented pieces:
 *   - DOS notification (StartNotify) on ENV:Sys/ScreenMode.prefs,
 *     Overscan.prefs and Font.prefs: the windows close at once, before IPrefs
 *     reaches CloseWorkBench(). Font.prefs counts only when its screen font
 *     or system font differs from the Workbench screen's and graphics' now
 *     (a new icon font alone needs no reset);
 *   - workbench.library's setup/cleanup hook: TryCleanup and Cleanup say a
 *     reset is going ahead (close, if not closed yet); Setup says Workbench
 *     is back;
 *   - a timer (timer.device, every 0.2 s while closed): the windows open
 *     again once the Workbench screen has gone and is back (looked for in
 *     IntuitionBase's screen list: LockPubScreen("Workbench") would open
 *     it); after 5 s when nothing went and no other program's window is
 *     on the screen (no reset was needed); after 30 s at the latest. While
 *     a window that is not Workbench's (WFLG_WBENCHWINDOW) is open, IPrefs
 *     may be waiting for it: its requester went by itself on 3.2.3 once the
 *     window it named closed, and windows open again by then would be named
 *     next.
 * OpenLook's notice still works where OpenLook runs: it patches
 * CloseWorkBench() and OpenWorkBench() and tells the programs registered in
 * the public semaphore "OpenWBNotify" (a list of client records) to close
 * and waits (3 seconds at most) for each; after OpenWorkBench it tells them
 * the screen is back.
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
 * wbc_take() is called each time the program wakes; it says WBC_CLOSE only
 * while the windows are open and WBC_OPEN only after a WBC_CLOSE. It also
 * registers the program when OpenLook has started since. A program
 * includes this once.
 *
 * The record layout must stay the same as opengadtools' look/wbnotify.h.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef WBCLOSE_H
#define WBCLOSE_H

#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/notify.h>
#include <devices/timer.h>
#include <graphics/gfxbase.h>
#include <graphics/text.h>
#include <prefs/font.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <workbench/workbench.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/wb.h>
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

/* this file's own notices: DOS notification, the timer and workbench.library's hook */
#define WBC_FILES 3
#define WBC_FONT_FILE 2                        /* wbc_files[2] */
#define WBC_TICK_US 200000UL                   /* the timer while closed: 0.2 s */
#define WBC_NO_RESET_TICKS 25                  /* closed with no sign of a reset: open again after 5 s */
#define WBC_MOST_TICKS 150                     /* closed at most 30 s while the screen is there */
#define WBC_AFTER_TICKS 10                     /* the timer goes on 2 s after WBC_OPEN, for its retries */
static const char *const wbc_files[WBC_FILES] = {
    "ENV:Sys/ScreenMode.prefs", "ENV:Sys/Overscan.prefs", "ENV:Sys/Font.prefs"
};
static struct NotifyRequest wbc_nr[WBC_FILES];
static int wbc_nr_on[WBC_FILES];
static struct MsgPort *wbc_port;               /* notification messages and the timer's replies */
static struct timerequest *wbc_tr;
static int wbc_tr_out;                         /* the timer request is out */
static struct Library *wbc_wb;                 /* workbench.library V45, for the hook */
static struct Hook wbc_hook;
static int wbc_hook_on;
static volatile LONG wbc_hook_said;            /* from the hook: WBC_CLOSE or WBC_OPEN, 0 when taken */
static struct Task *wbc_task;
static int wbc_shut;                           /* WBC_CLOSE given, no WBC_OPEN since */
static int wbc_reset_seen;                     /* since the close: the hook, OpenLook or the screen said a reset runs */
static int wbc_gone;                           /* since the close: no Workbench screen was seen */
static int wbc_ticks;                          /* timer ticks since the close (or since WBC_OPEN, for the retries) */

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

/* workbench.library's setup/cleanup hook, called on Workbench's process: it only notes and signals */
static ULONG wbc_hook_fn(struct Hook *h __asm("a0"), APTR o __asm("a2"), struct SetupCleanupHookMsg *m __asm("a1"))
{
    (void)h; (void)o;
    if (m && (m->schm_State == SCHMSTATE_TryCleanup || m->schm_State == SCHMSTATE_Cleanup))
        wbc_hook_said = WBC_CLOSE;
    else if (m && m->schm_State == SCHMSTATE_Setup)
        wbc_hook_said = WBC_OPEN;
    if (wbc_task && wbc_sig >= 0) Signal(wbc_task, 1UL << wbc_sig);
    return 0;
}

/* a Workbench screen in Intuition's list now */
static WBC_UNUSED struct Screen *wbc_wb_screen(void)
{
    ULONG lk = LockIBase(0);
    struct Screen *s;
    for (s = ((struct IntuitionBase *)IntuitionBase)->FirstScreen; s; s = s->NextScreen)
        if ((s->Flags & SCREENTYPE) == WBENCHSCREEN) break;
    UnlockIBase(lk);
    return s;
}

/* windows on the Workbench screen that are not Workbench's own (WFLG_WBENCHWINDOW): a Shell, a program's, or
 * IPrefs' requester naming them. While there are any, a reset may still be waiting for them to close. */
static WBC_UNUSED int wbc_others(void)
{
    ULONG lk = LockIBase(0);
    struct Screen *s;
    struct Window *w;
    int n = 0;
    for (s = ((struct IntuitionBase *)IntuitionBase)->FirstScreen; s; s = s->NextScreen)
        if ((s->Flags & SCREENTYPE) == WBENCHSCREEN) {
            for (w = s->FirstWindow; w; w = w->NextWindow)
                if (!(w->Flags & WFLG_WBENCHWINDOW)) n++;
            break;
        }
    UnlockIBase(lk);
    return n;
}

static WBC_UNUSED int wbc_same_font(const char *name, UWORD ysize, const char *now, UWORD now_ysize)
{
    return now && name && !strcmp(name, now) && ysize == now_ysize;
}

/* a new Font.prefs needs a reset only when its screen font or system font is not the one in use */
static WBC_UNUSED int wbc_font_resets(void)
{
    UBYTE buf[1024], *p, *end;
    LONG n;
    BPTR f = Open((STRPTR)"ENV:Sys/Font.prefs", MODE_OLDFILE);
    struct Screen *s;
    struct TextFont *def;
    int differs = 0;
    if (!f) return 1;
    n = Read(f, buf, sizeof buf);
    Close(f);
    if (n < 12 || memcmp(buf, "FORM", 4) || memcmp(buf + 8, "PREF", 4)) return 1;
    p = buf + 12;
    end = buf + n;
    s = wbc_wb_screen();
    def = ((struct GfxBase *)GfxBase)->DefaultFont;
    while (p + 8 <= end) {
        ULONG len = (ULONG)p[4] << 24 | (ULONG)p[5] << 16 | (ULONG)p[6] << 8 | p[7];
        UBYTE *d = p + 8;
        /* struct FontPrefs (prefs/font.h): fp_Type at 14, fp_TextAttr.ta_YSize at 24, fp_Name at 28 */
        if (!memcmp(p, "FONT", 4) && len >= 29 && len <= (ULONG)(end - d)) {
            UWORD type = (UWORD)(d[14] << 8 | d[15]), ysize = (UWORD)(d[24] << 8 | d[25]);
            char name[64];
            strncpy(name, (const char *)d + 28, sizeof name - 1);
            name[sizeof name - 1] = 0;
            if (type == FP_SCREENFONT && !(s && s->Font && wbc_same_font(name, ysize, (const char *)s->Font->ta_Name, s->Font->ta_YSize)))
                differs = 1;
            if (type == FP_SYSFONT && !(def && wbc_same_font(name, ysize, def->tf_Message.mn_Node.ln_Name, def->tf_YSize)))
                differs = 1;
        }
        if (len > (ULONG)(end - d)) break;
        p = d + ((len + 1) & ~1UL);
    }
    return differs;
}

static WBC_UNUSED void wbc_tick(void)
{
    if (!wbc_tr || wbc_tr_out) return;
    wbc_tr->tr_node.io_Command = TR_ADDREQUEST;
    wbc_tr->tr_time.tv_secs = 0;
    wbc_tr->tr_time.tv_micro = WBC_TICK_US;
    SendIO((struct IORequest *)wbc_tr);
    wbc_tr_out = 1;
}

static WBC_UNUSED void wbc_start(void)
{
    int i;
    if (wbc_sig >= 0) return;
    if ((wbc_sig = AllocSignal(-1)) < 0) return;
    wbc_task = FindTask(NULL);
    wbc_register();
    if ((wbc_port = CreateMsgPort())) {
        for (i = 0; i < WBC_FILES; i++) {
            wbc_nr[i].nr_Name = (STRPTR)wbc_files[i];
            wbc_nr[i].nr_Flags = NRF_SEND_MESSAGE;
            wbc_nr[i].nr_stuff.nr_Msg.nr_Port = wbc_port;
            wbc_nr_on[i] = StartNotify(&wbc_nr[i]) != 0;
        }
        if ((wbc_tr = (struct timerequest *)CreateIORequest(wbc_port, sizeof *wbc_tr)) &&
            OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)wbc_tr, 0)) {
            DeleteIORequest((struct IORequest *)wbc_tr);
            wbc_tr = NULL;
        }
    }
    if ((wbc_wb = OpenLibrary((STRPTR)"workbench.library", 45))) {
        struct TagItem tags[2];
        wbc_hook.h_Entry = (ULONG (*)())wbc_hook_fn;
        tags[0].ti_Tag = WBCTRLA_AddSetupCleanupHook; tags[0].ti_Data = (ULONG)&wbc_hook;
        tags[1].ti_Tag = TAG_DONE; tags[1].ti_Data = 0;
        wbc_hook_on = __WorkbenchControlA_base(wbc_wb, NULL, tags) != 0;
    }
}

static WBC_UNUSED ULONG wbc_sigmask(void)
{
    return (wbc_sig < 0 ? 0 : 1UL << wbc_sig) | (wbc_port ? 1UL << wbc_port->mp_SigBit : 0);
}

static WBC_UNUSED int wbc_close_now(int reset)
{
    wbc_shut = 1;
    wbc_reset_seen = reset;
    wbc_gone = 0;
    wbc_ticks = 0;
    wbc_tick();
    return WBC_CLOSE;
}

static WBC_UNUSED int wbc_open_now(void)
{
    wbc_shut = 0;
    wbc_ticks = 0;
    wbc_tick();                                /* a few ticks more: the program retries while the screen isn't ready */
    return WBC_OPEN;
}

/* what OpenLook, IPrefs' files, Workbench or the timer said since the last call: WBC_CLOSE, WBC_OPEN or 0 */
static WBC_UNUSED int wbc_take(void)
{
    struct Message *m;
    int notified = 0, ticked = 0, ol = 0, hk;
    if (wbc_sig < 0) return 0;
    SetSignal(0, 1UL << wbc_sig);
    if (!wbc_client) {
        if (++wbc_calls % 20 == 0) wbc_register();     /* OpenLook may have started since */
    } else {
        ol = wbc_client->want;
        wbc_client->want = 0;
    }
    while (wbc_port && (m = GetMsg(wbc_port))) {
        if (wbc_tr && m == &wbc_tr->tr_node.io_Message) { wbc_tr_out = 0; ticked = 1; continue; }
        notified |= ((struct NotifyMessage *)m)->nm_NReq == &wbc_nr[WBC_FONT_FILE] ? 2 : 1;
        ReplyMsg(m);
    }
    hk = wbc_hook_said;
    wbc_hook_said = 0;
    if (ol == WBC_CLOSE || hk == WBC_CLOSE) {
        if (!wbc_shut) return wbc_close_now(1);
        wbc_reset_seen = 1;
        if (ol == WBC_CLOSE && wbc_client) wbc_client->ack = 1;   /* closed already */
    }
    if ((ol == WBC_OPEN || hk == WBC_OPEN) && wbc_shut)
        return wbc_open_now();
    if (notified && !wbc_shut && ((notified & 1) || wbc_font_resets()))
        return wbc_close_now(0);
    if (ticked) {
        wbc_ticks++;
        if (!wbc_shut) {
            if (wbc_ticks < WBC_AFTER_TICKS) wbc_tick();
            return 0;
        }
        if (!wbc_wb_screen()) wbc_gone = wbc_reset_seen = 1;
        else if (wbc_gone || (!wbc_reset_seen && wbc_ticks >= WBC_NO_RESET_TICKS && !wbc_others())
                 || wbc_ticks >= WBC_MOST_TICKS)
            return wbc_open_now();
        wbc_tick();
    }
    return 0;
}

/* the windows and locks are gone: OpenLook can let Workbench go */
static WBC_UNUSED void wbc_closed(void)
{
    if (wbc_client) wbc_client->ack = 1;
}

static WBC_UNUSED void wbc_stop(void)
{
    int i;
    if (wbc_client && wbc_root) {
        ObtainSemaphore(&wbc_root->sem);
        Remove((struct Node *)&wbc_client->node);
        ReleaseSemaphore(&wbc_root->sem);
        FreeVec(wbc_client);
        wbc_client = NULL;
        wbc_root = NULL;
    }
    if (wbc_wb) {
        if (wbc_hook_on) {
            struct TagItem tags[2];
            tags[0].ti_Tag = WBCTRLA_RemSetupCleanupHook; tags[0].ti_Data = (ULONG)&wbc_hook;
            tags[1].ti_Tag = TAG_DONE; tags[1].ti_Data = 0;
            __WorkbenchControlA_base(wbc_wb, NULL, tags);
            wbc_hook_on = 0;
        }
        CloseLibrary(wbc_wb);
        wbc_wb = NULL;
    }
    if (wbc_port) {
        struct Message *m;
        for (i = 0; i < WBC_FILES; i++)
            if (wbc_nr_on[i]) { EndNotify(&wbc_nr[i]); wbc_nr_on[i] = 0; }
        if (wbc_tr) {
            if (wbc_tr_out) { AbortIO((struct IORequest *)wbc_tr); WaitIO((struct IORequest *)wbc_tr); wbc_tr_out = 0; }
            CloseDevice((struct IORequest *)wbc_tr);
            DeleteIORequest((struct IORequest *)wbc_tr);
            wbc_tr = NULL;
        }
        while ((m = GetMsg(wbc_port))) ReplyMsg(m);
        DeleteMsgPort(wbc_port);
        wbc_port = NULL;
    }
    if (wbc_sig >= 0) { FreeSignal(wbc_sig); wbc_sig = -1; }
    wbc_task = NULL;
}

#endif
