/* WBProbe: sends each argument to Workbench's ARexx port and prints the
 * result (a test tool for the right-click menus; not shipped).
 *   WBProbe "GETATTR WINDOWS.COUNT" "GETATTR WINDOW.ICONS.ALL.COUNT NAME root"
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include <string.h>

struct RxsLib *RexxSysBase;

int main(int argc, char **argv)
{
    struct MsgPort *reply;
    if (!(RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36))) { PutStr((STRPTR)"no rexxsyslib\n"); return 20; }
    reply = CreateMsgPort();
    for (int i = 1; i < argc; i++) {
        struct RexxMsg *rm = CreateRexxMsg(reply, NULL, NULL);
        struct MsgPort *wb;
        rm->rm_Action = RXCOMM | RXFF_RESULT;
        rm->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)argv[i], strlen(argv[i]));
        Forbid();
        wb = FindPort((STRPTR)"WORKBENCH");
        if (wb) PutMsg(wb, (struct Message *)rm);
        Permit();
        if (!wb) { PutStr((STRPTR)"no WORKBENCH port\n"); break; }
        WaitPort(reply); GetMsg(reply);
        Printf((STRPTR)"%s -> rc %ld", (LONG)argv[i], rm->rm_Result1);
        if (rm->rm_Result1 == 0 && rm->rm_Result2) { Printf((STRPTR)" \"%s\"", rm->rm_Result2); DeleteArgstring((UBYTE *)rm->rm_Result2); }
        else Printf((STRPTR)" (%ld)", rm->rm_Result2);
        PutStr((STRPTR)"\n");
        DeleteArgstring((UBYTE *)rm->rm_Args[0]);
        DeleteRexxMsg(rm);
    }
    DeleteMsgPort(reply);
    CloseLibrary((struct Library *)RexxSysBase);
    return 0;
}
