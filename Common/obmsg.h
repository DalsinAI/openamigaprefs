/* obmsg.h: asking OpenBench, our Workbench (openamigabench desktop/openbench.c), to open something as
 * Workbench's OpenWorkbenchObject does: a tool runs, a project opens with its default tool, a drawer
 * or disk opens in a drawer window. Programs of the Team that start things (the dock) ask OpenBench
 * first, and Workbench when OpenBench isn't there.
 *
 *   ob_open(path)   1 opened, 0 OpenBench couldn't, -1 no OpenBench
 *
 * The message's layout is OpenBench's: keep it the same there. MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OBMSG_H
#define OBMSG_H

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

#define OB_PORT_NAME "OPENBENCH"
#define OB_MSG_MAGIC 0x4F424D31UL                  /* 'OBM1' */

struct ob_msg {
    struct Message m;
    ULONG magic;
    LONG ok;                                       /* OpenBench's answer */
    char path[256];
};

static __attribute__((unused)) int ob_open(const char *path)
{
    struct MsgPort *reply = CreateMsgPort(), *ob;
    struct ob_msg *m;
    int ok;
    if (!reply) return -1;
    if (!(m = AllocVec(sizeof *m, MEMF_PUBLIC | MEMF_CLEAR))) { DeleteMsgPort(reply); return -1; }
    m->m.mn_ReplyPort = reply;
    m->m.mn_Length = sizeof *m;
    m->magic = OB_MSG_MAGIC;
    strncpy(m->path, path, sizeof m->path - 1);
    Forbid();
    if ((ob = FindPort((CONST_STRPTR)OB_PORT_NAME))) PutMsg(ob, &m->m);
    Permit();
    if (!ob) ok = -1;
    else {
        WaitPort(reply);
        GetMsg(reply);
        ok = m->ok ? 1 : 0;
    }
    FreeVec(m);
    DeleteMsgPort(reply);
    return ok;
}

#endif
