/* The taskspace: icons of other programs in the Workbench screen's title
 * bar, beside OpenTitle's own (network, OpenSpeaker, the cog), next to the
 * screen's depth gadget. OpenTitle owns it and draws every icon there, so
 * they all look alike and follow the theme; a program only says what its
 * icon looks like and hears when it is clicked.
 *
 * The protocol, version 1 (8 October 2026):
 *
 *  1. Send a TaskspaceMsg with tsm_Command TSC_ADD to the public port
 *     TASKSPACE_PORT ("OpenTitle"), found and sent to under Forbid():
 *         Forbid();
 *         if ((p = FindPort(TASKSPACE_PORT))) PutMsg(p, &m.tsm_Message);
 *         Permit();
 *     with mn_ReplyPort set. OpenTitle copies everything it needs and
 *     replies at once; tsm_Result says how it went (TSR_OK and so on). The
 *     message is yours again after the reply. When the port isn't there,
 *     OpenTitle isn't running: try again now and then (every few seconds).
 *  2. Change the picture shown with TSC_SET (tsm_Name and tsm_State only
 *     are read), and take the icon away with TSC_REMOVE before your
 *     program ends. A program that ends without TSC_REMOVE leaves its icon;
 *     OpenTitle drops it when a click finds the program's port gone.
 *  3. Clicks come to the public port named in tsm_Port as TaskspaceEvent
 *     messages: TSE_CLICK with the icon's box on the screen (to open a
 *     window under it). Reply each one. TSE_GONE says OpenTitle is
 *     ending: the icon is gone, add it again when OpenTitle is back.
 *
 * The pictures: tsm_States pictures of tsm_Width x tsm_Height pixels (at
 * most TS_MAXW x TS_MAXH), one byte a pixel, one picture after the other:
 *     0          see-through (the bar's own colour)
 *     1          the bar's text colour (BARDETAILPEN): follows the theme
 *     2, 3       shine and shadow
 *     4 to 15    tsm_RGB[0] to tsm_RGB[11], 0xRRGGBB, the nearest pens
 *                OpenTitle can get (on an 8-colour screen, the theme's)
 * The picture is centred in a box the bar's height high and tsm_Width + 10
 * wide; OpenTitle raises the box while the pointer is over it and shows
 * tsm_Help under it after a moment.
 *
 * Where it sits: OpenTitle keeps each icon in OpenMenus' tray
 * (ENV:OpenMenus/Tray/<tsm_Name>, "width order"), so the row closes up as
 * the clock and network icons do. Orders taken: the cog -5, OpenSpeaker 0,
 * the network icons 5, the clock 10. Programs use 1 to 4 (Tata is 2).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef TASKSPACE_H
#define TASKSPACE_H

#include <exec/types.h>
#include <exec/ports.h>

#define TASKSPACE_PORT    "OpenTitle"
#define TASKSPACE_MAGIC   0x54535043UL    /* 'TSPC' */
#define TASKSPACE_VERSION 1

#define TS_MAXW   16
#define TS_MAXH   13
#define TS_STATES 4
#define TS_ICONS  8                       /* icons OpenTitle keeps at once */

enum { TSC_ADD = 1, TSC_SET = 2, TSC_REMOVE = 3 };
enum { TSR_OK = 0, TSR_BADMSG = 1, TSR_FULL = 2, TSR_UNKNOWN = 3, TSR_NOMEM = 4 };
enum { TSE_CLICK = 1, TSE_GONE = 2 };

struct TaskspaceMsg {
    struct Message tsm_Message;           /* mn_ReplyPort required; mn_Length = sizeof */
    ULONG  tsm_Magic;                     /* TASKSPACE_MAGIC */
    UWORD  tsm_Version;                   /* TASKSPACE_VERSION */
    UWORD  tsm_Command;                   /* TSC_* */
    char   tsm_Name[16];                  /* the icon's name, unique: also its tray file */
    WORD   tsm_Order;                     /* where in the row: 1 to 4 for programs */
    UWORD  tsm_State;                     /* the picture to show, 0 to tsm_States - 1 */
    UWORD  tsm_Width, tsm_Height;         /* each picture's size */
    UWORD  tsm_States;                    /* how many pictures */
    const UBYTE *tsm_Pixels;              /* tsm_States * tsm_Width * tsm_Height bytes (TSC_ADD only) */
    ULONG  tsm_RGB[12];                   /* the colours of pixels 4 to 15 */
    char   tsm_Help[48];                  /* what the icon is, in plain words */
    char   tsm_Port[24];                  /* your public port for clicks, or "" */
    LONG   tsm_Result;                    /* TSR_*, set by OpenTitle */
};

struct TaskspaceEvent {
    struct Message tse_Message;           /* reply it */
    ULONG  tse_Magic;                     /* TASKSPACE_MAGIC */
    UWORD  tse_Kind;                      /* TSE_* */
    UWORD  tse_Pad;
    char   tse_Name[16];                  /* which icon */
    WORD   tse_Left, tse_Top, tse_Width, tse_Height;   /* its box on the screen */
};

#endif
