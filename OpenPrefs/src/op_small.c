/* C:OpenPrefs, OpenPrefs's small body (Dale, 17 October 2026, 08:24: the boot's one OpenPrefs APPLY "is
 * served by a small separate body, so boot doesn't unpack the app"). In ROM:C it is a resident command, so
 * OpenPrefs by name finds it before the path's SYS:Prefs and ROM:Prefs; OpenUp puts the same in C:.
 *
 *   OpenPrefs APPLY        Sound's, Gamepads' and System's settings, here (op_apply.c, the editors' own
 *                          code for USE and APPLY, built with OP_APPLY and without their windows)
 *   OpenPrefs ...          anything else: the app, SYS:Prefs/OpenPrefs (the Shell's path finds SYS:Prefs
 *                          first) or else ROM:Prefs/OpenPrefs, run in this process with these arguments
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "openprefs.h"

static const char version[] __attribute__((used)) = "$VER: OpenPrefs " OP_VERSION " (17.10.2026) MIT, Copyright (c) 2026 AmigaChrome Team";

static const char *const apps[] = { "SYS:Prefs/OpenPrefs", "ROM:Prefs/OpenPrefs" };

static BPTR load_app(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR seg = 0, l;
    unsigned i;
    me->pr_WindowPtr = (APTR)-1;               /* no "please insert volume" for a missing SYS: or ROM: */
    for (i = 0; !seg && i < sizeof apps / sizeof apps[0]; i++)
        if ((l = Lock((STRPTR)apps[i], SHARED_LOCK))) { UnLock(l); seg = LoadSeg((STRPTR)apps[i]); }
    me->pr_WindowPtr = was;
    return seg;
}

/* the arguments are APPLY alone (any case, spaces and the newline around it) */
static int is_apply(const char *s)
{
    static const char w[] = "APPLY";
    int i;
    while (*s == ' ' || *s == '\t') s++;
    for (i = 0; w[i]; i++, s++) if ((*s & ~0x20) != w[i]) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return !*s;
}

int main(int argc, char **argv)
{
    const char *args = (const char *)GetArgStr();
    BPTR seg;
    LONG rc;
    (void)argv;
    __asm__ volatile ("" : : "r" (version));   /* a reference, so --gc-sections keeps $VER (without it Version found none) */
    if (argc > 0 && args && is_apply(args)) return op_apply();
    if (!(seg = load_app())) {
        PutStr((STRPTR)"OpenPrefs: no SYS:Prefs/OpenPrefs or ROM:Prefs/OpenPrefs\n");
        return RETURN_FAIL;
    }
    rc = RunCommand(seg, 65536, (STRPTR)(args ? args : "\n"), args ? strlen(args) : 1);
    UnLoadSeg(seg);
    return rc;
}
