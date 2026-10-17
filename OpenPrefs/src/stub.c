/* A name stub of OpenPrefs on a disk (OpenUp puts one in SYS:Prefs for each old editor's name, with no icon:
 * OpenSound, Windows, Look, ...): it runs OpenPrefs in its own process under its own name, so OpenPrefs opens
 * that editor's section and takes its arguments, as the editor did (OpenSound USE, SYS:Prefs/Look FROM x SAVE).
 *
 *   from the Shell     PROGDIR:OpenPrefs (else SYS:Prefs/OpenPrefs, else ROM:Prefs/OpenPrefs) by RunCommand,
 *                      with this command's name and arguments
 *   from Workbench     the same program's start code is called here, and takes this process's WBStartup
 *                      message, whose first argument is this stub's name
 *
 * No C library: about 500 bytes. Built once per name, STUB_NAME (OpenPrefs/build.sh).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#define __NOLIBBASE__
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>

#ifndef STUB_NAME
#define STUB_NAME "OpenPrefs-stub"
#endif

/* the hunk's first bytes: a jump to the entry (GCC puts this file's strings in .text ahead of the function) */
__asm__(".text\n\tjra _stub_start\n");

__attribute__((section(".text"))) LONG stub_start(void)
{
    struct ExecBase **at4 = (struct ExecBase **)4, *SysBase;
    struct Library *DOSBase;
    struct Process *me;
    static const char *const apps[3] = { "PROGDIR:OpenPrefs", "SYS:Prefs/OpenPrefs", "ROM:Prefs/OpenPrefs" };
    BPTR seg = 0;
    LONG rc = RETURN_FAIL;
    APTR was;
    int i;
    __asm__("" : "+r"(at4));                 /* address 4 is exec's base, not an array GCC may reason about */
    SysBase = *at4;
    me = (struct Process *)FindTask(NULL);
    if (!(DOSBase = OpenLibrary((STRPTR)"dos.library", 37))) return RETURN_FAIL;
    was = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    for (i = 0; !seg && i < 3; i++) seg = LoadSeg((STRPTR)apps[i]);
    me->pr_WindowPtr = was;
    if (seg && me->pr_CLI) {
        STRPTR a = GetArgStr();
        LONG n = 0;
        while (a && a[n]) n++;
        rc = RunCommand(seg, 65536, a ? a : (STRPTR)"\n", a ? n : 1);
    } else if (seg) {
        rc = ((LONG (*)(void))((UBYTE *)BADDR(seg) + 4))();   /* it takes the WBStartup message and replies it */
    } else if (!me->pr_CLI) {
        struct Message *m;
        WaitPort(&me->pr_MsgPort);
        m = GetMsg(&me->pr_MsgPort);
        Forbid();
        ReplyMsg(m);
    } else PutStr((STRPTR)STUB_NAME ": no SYS:Prefs/OpenPrefs\n");
    if (seg) UnLoadSeg(seg);
    CloseLibrary(DOSBase);
    return rc;
}

static const char version[] __attribute__((used)) = "$VER: " STUB_NAME " 2.0 (17.10.2026) OpenPrefs's name stub, MIT, AmigaChrome Team";
