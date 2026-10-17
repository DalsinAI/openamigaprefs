/* A name stub of OpenPrefs on a disk (OpenUp puts one in SYS:Prefs for each old editor's name, with no icon:
 * OpenSound, Windows, Look, ...): it runs OpenPrefs under its own name, so OpenPrefs opens that editor's
 * section and takes its arguments, as the editor did (OpenSound USE, SYS:Prefs/Look FROM x SAVE).
 *
 *   from the Shell     PROGDIR:OpenPrefs (else SYS:Prefs/OpenPrefs, else ROM:Prefs/OpenPrefs) by RunCommand,
 *                      with this command's name and arguments
 *   from Workbench     the same program in a process of its own (64 KB of stack), given a copy of this
 *                      process's WBStartup message, whose first argument is this stub's name; this process
 *                      waits for it to end, then replies its own message. Calling the program's start code
 *                      in this process instead hung the machine when the dock started SYS:Prefs/OpenLook
 *                      (stock 3.2.3 ROM, lab t5-prefs-one, 17 October 2026).
 *
 * No C library: under 1 KB. Built once per name, STUB_NAME (OpenPrefs/build.sh).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#define __NOLIBBASE__
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <workbench/startup.h>
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
    struct WBStartup *wb = NULL;
    BPTR seg = 0;
    LONG rc = RETURN_FAIL;
    APTR was;
    int i;
    __asm__("" : "+r"(at4));                 /* address 4 is exec's base, not an array GCC may reason about */
    SysBase = *at4;
    me = (struct Process *)FindTask(NULL);
    if (!me->pr_CLI) {                       /* from Workbench: its message first, as every tool takes it */
        WaitPort(&me->pr_MsgPort);
        wb = (struct WBStartup *)GetMsg(&me->pr_MsgPort);
    }
    if (!(DOSBase = OpenLibrary((STRPTR)"dos.library", 37))) {
        if (wb) { Forbid(); ReplyMsg(&wb->sm_Message); }
        return RETURN_FAIL;
    }
    was = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    for (i = 0; !seg && i < 3; i++) seg = LoadSeg((STRPTR)apps[i]);
    me->pr_WindowPtr = was;
    if (seg && !wb) {
        STRPTR a = GetArgStr();
        LONG n = 0;
        while (a && a[n]) n++;
        rc = RunCommand(seg, 65536, a ? a : (STRPTR)"\n", a ? n : 1);
    } else if (seg) {
        struct MsgPort *port = CreateMsgPort();
        struct Process *p = NULL;
        struct WBStartup copy = *wb;
        BPTR home = wb->sm_NumArgs > 0 && wb->sm_ArgList[0].wa_Lock ? DupLock(wb->sm_ArgList[0].wa_Lock) : 0;
        /* as OpenBench starts a tool (openbench.c start_tool): no input or output, no current directory, the
         * tool's drawer as its home. With DOS's own input and output (NIL:) its process ended in alert
         * 8700000E, a file closed twice; with DOS's own current and home directories the disk stopped
         * answering when it ended (stock 3.2.3 ROM, lab t5-prefs-one, 17 October 2026) */
        if (port && (p = CreateNewProcTags(NP_Seglist, (ULONG)seg, NP_FreeSeglist, FALSE, NP_StackSize, 65536,
                                           NP_Name, (ULONG)STUB_NAME, NP_CurrentDir, 0, NP_HomeDir, (ULONG)home,
                                           NP_Input, 0, NP_CloseInput, FALSE, NP_Output, 0, NP_CloseOutput, FALSE,
                                           TAG_DONE))) {
            copy.sm_Message.mn_ReplyPort = port;
            copy.sm_Process = &p->pr_MsgPort;
            copy.sm_Segment = seg;
            PutMsg(&p->pr_MsgPort, &copy.sm_Message);
            WaitPort(port);                  /* its reply: the program has ended */
            GetMsg(port);
            rc = RETURN_OK;
        } else if (home) UnLock(home);
        if (port) DeleteMsgPort(port);
    } else if (!wb) PutStr((STRPTR)STUB_NAME ": no SYS:Prefs/OpenPrefs\n");
    if (seg) UnLoadSeg(seg);
    CloseLibrary(DOSBase);
    if (wb) { Forbid(); ReplyMsg(&wb->sm_Message); }
    return rc;
}

static const char version[] __attribute__((used)) = "$VER: " STUB_NAME " 2.0 (17.10.2026) OpenPrefs's name stub, MIT, AmigaChrome Team";
