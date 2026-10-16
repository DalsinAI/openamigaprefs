/* cogplace.h: a window opened from OpenTitle's cog opens at the screen's top right (the user, 10 October 2026:
 * "have the preferences windows open near the cog wheel when they are opened by it, so right aligned, with 20
 * pixels spacing to the edge of the screen top and right hand side").
 *
 * OpenWindows keeps a public semaphore by this name with the request in it. The cog arms it just before it starts
 * an editor; OpenWindows' OpenWindowTagList patch places the next window with a drag bar that another program
 * opens within ten seconds there, and disarms it. Both read and write it under Forbid (no DOS: the patch runs in
 * any task). MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef COGPLACE_H
#define COGPLACE_H

#include <exec/types.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>

#define COGPLACE_NAME "OpenWindows.cog"
#define COGPLACE_GAP 20                 /* from the screen's right edge, and from its top */
#define COGPLACE_SECS 10                /* how long a request waits for its window */

struct cogplace {
    struct SignalSemaphore sem;         /* named COGPLACE_NAME, in the public list */
    ULONG until;                        /* seconds (intuition's CurrentTime) when the request lapses */
    struct Task *from;                  /* the asker: its own windows are not the one */
    WORD right, top;                    /* the gaps */
    UBYTE armed, pad;
};

#endif
