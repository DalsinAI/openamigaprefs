/* OpenTitle 0.1: the Workbench screen's title bar as Dale drew it on
 * 6 October 2026: AmigaChrome's logo at the far left, Workbench's title with
 * the free chip and fast memory after it, and the day, date and time at the
 * right end, just left of OpenSpeaker. Optionally the display's border
 * around the screen is black.
 *
 * The free memory is Workbench's own: its title format (the WBTF chunk of
 * Sys/Workbench.prefs, as Prefs/Workbench writes it) becomes
 *   "Workbench %r  Chip %mcek KB  Fast %mfem MB"
 * in ENV:, so IPrefs shows it and it updates as Workbench does; spaces
 * before it leave room for the logo.
 *
 * The logo and the clock are two small borderless windows in the title bar,
 * as OpenSpeaker is. The clock is in OpenMenus' tray (ENV:OpenMenus/Tray/Clock,
 * "width order", order 10: left of the speaker, order 0), and goes where
 * OpenMenus' bar is when OpenMenus puts it at an edge. A click on either
 * leaves the active window active.
 *
 *   OpenTitle        (started at boot; Ctrl-C quits, Ctrl-F reads
 *                     ENV:OpenPrefs/TitleBar again and quits when nothing
 *                     is left to show)
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/videocontrol.h>
#include <graphics/layers.h>
#include <utility/date.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/utility.h>
#include <proto/bsdsocket.h>
#include <exec/io.h>
#include <dos/dostags.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tb_prefs.h"
#include "om_prefs.h"
#include "logo.h"

const char version[] __attribute__((used)) = "$VER: OpenTitle 0.4 (6.10.2026) OpenPrefs, Dalsin Limited";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *LayersBase, *UtilityBase, *SocketBase;

#define MENUS_ENV  "ENV:OpenMenus/Menus"
#define TRAY_DIR   "ENV:OpenMenus/Tray"
#define CLOCK_ORDER 10                         /* left of the network icons (5) and OpenSpeaker (0) */
#define NET_ORDER   5
#define NET_TOOL   "SYS:Tools/Commodities/OpenSocketControl"
#define WIFI_DEVICE "opensocketwifi.device"    /* OpenSocket's: the PC's (or the card's) Wi-Fi */
#define WIFI_STATUS 2
#define WB_ENV     "ENV:Sys/Workbench.prefs"
#define TITLE_MEM  "Workbench %r  Chip %mcek KB  Fast %mfem MB"
#define TITLE_PLAIN "Amiga Workbench %r"
#define LOGO_PAD   "    "                      /* room for the logo before Workbench's title */

static tb_prefs prefs;
static struct Screen *scr;
static struct DrawInfo *dri;
static struct Window *logo_w, *clock_w;
static WORD cx, cy, cw, ch;                    /* the clock window's place */
static int bar_edge = OM_BAR_TITLE;
static int border_set;                         /* we made the border black */
static struct Window *last_other;              /* the window that was active before a click on ours */
static char shown[40];

/* the network icons: LAN and Wi-Fi, from OpenSocket */
static struct Window *net_w, *pop_w;
static WORD nx, ny, nw, nh;
static int lan_state;                          /* 0 no bsdsocket.library, 1 no address, 2 connected */
static int wifi_state = -1;                    /* -1 no Wi-Fi to show, 0 not connected, 1 connected */
static int wifi_signal, pop_ticks;
static char lan_addr[20], wifi_ssid[34];
static int net_drawn = -100;                   /* what the icons show, to draw only on change */

static LONG pens[64];
static ULONG pen_rgb[64];
static int npens;

/* ---- files --------------------------------------------------------------------------------- */

static char *read_file(const char *path, LONG *lenp)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf = NULL;
    LONG len = 0;
    if (fh) {
        Seek(fh, 0, OFFSET_END);
        len = Seek(fh, 0, OFFSET_BEGINNING);
        if (len >= 0 && (buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) {
            if (Read(fh, buf, len) != len) { FreeVec(buf); buf = NULL; }
        }
        Close(fh);
    }
    if (lenp) *lenp = buf ? len : 0;
    return buf;
}

static void read_prefs(void)
{
    char *t = read_file(TB_ENV, NULL);
    if (!t) t = read_file(TB_ENVARC, NULL);
    tb_parse(&prefs, t);
    if (t) FreeVec(t);
}

static int read_bar_edge(void)
{
    om_prefs p;
    char *t;
    struct MsgPort *port;
    Forbid();
    port = FindPort((STRPTR)"OpenMenus");
    Permit();
    if (!port) return OM_BAR_TITLE;
    om_defaults(&p);
    if ((t = read_file(MENUS_ENV, NULL))) { om_parse(&p, t); FreeVec(t); }
    return p.enabled ? p.bar : OM_BAR_TITLE;
}

static void tell_openmenus(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)"OpenMenus")) && p->mp_SigTask) Signal((struct Task *)p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
}

static void tray(const char *name, int width, int order)
{
    BPTR fh, lock;
    char t[24], path[64];
    if (width > 0) {
        if ((lock = Lock((STRPTR)TRAY_DIR, ACCESS_READ))) UnLock(lock);
        else {                                 /* ENV:OpenMenus is there only once OpenMenus has settings */
            if ((lock = CreateDir((STRPTR)"ENV:OpenMenus"))) UnLock(lock);
            if ((lock = CreateDir((STRPTR)TRAY_DIR))) UnLock(lock);
        }
        snprintf(path, sizeof path, TRAY_DIR "/%s", name);
        if ((fh = Open((STRPTR)path, MODE_NEWFILE))) {
            LONG n = snprintf(t, sizeof t, "%d %d\n", width, order);
            Write(fh, t, n);
            Close(fh);
        }
    } else {
        snprintf(path, sizeof path, TRAY_DIR "/%s", name);
        DeleteFile((STRPTR)path);
    }
    tell_openmenus();
}

/* the room the tray programs nearer the bar's end take, and how many they are */
static int tray_before(const char *mine, int my_order, int *count)
{
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR lock = Lock((STRPTR)TRAY_DIR, ACCESS_READ);
    int sum = 0;
    *count = 0;
    if (fib && lock && Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            char path[96], *t;
            int w = 0, order = 0;
            const char *name = (const char *)fib->fib_FileName;
            if (fib->fib_DirEntryType > 0 || !strcmp(name, mine)) continue;
            snprintf(path, sizeof path, TRAY_DIR "/%s", name);
            if (!(t = read_file(path, NULL))) continue;
            if (sscanf(t, "%d %d", &w, &order) < 1) w = 0;
            FreeVec(t);
            if (w <= 0 || w > 400) continue;
            if (order < my_order || (order == my_order && strcmp(name, mine) < 0)) { sum += w; (*count)++; }
        }
    }
    if (lock) UnLock(lock);
    if (fib) FreeDosObject(DOS_FIB, fib);
    return sum;
}

/* ---- Workbench's title format (the WBTF chunk of ENV:Sys/Workbench.prefs) ------------------ */

static ULONG get32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }
static void put32(UBYTE *p, ULONG v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

/* The title format Workbench.prefs has now (NULL when none) into fmt. */
static int wb_title(char *fmt, int size)
{
    LONG len, i;
    UBYTE *d = (UBYTE *)read_file(WB_ENV, &len);
    int found = 0;
    fmt[0] = 0;
    if (d && len >= 12 && !memcmp(d, "FORM", 4) && !memcmp(d + 8, "PREF", 4)) {
        for (i = 12; i + 8 <= len; ) {
            ULONG n = get32(d + i + 4);
            if (i + 8 + n > (ULONG)len) break;
            if (!memcmp(d + i, "WBTF", 4)) {
                int k = n < (ULONG)size - 1 ? n : size - 1;
                memcpy(fmt, d + i + 8, k);
                fmt[k] = 0;
                found = 1;
            }
            i += 8 + n + (n & 1);
        }
    }
    if (d) FreeVec(d);
    return found;
}

/* Writes ENV:Sys/Workbench.prefs with fmt as its title format (NULL: none), keeping its other chunks. */
static void wb_set_title(const char *fmt)
{
    LONG len, i, o = 12, flen = fmt ? strlen(fmt) + 1 : 0;
    UBYTE *d = (UBYTE *)read_file(WB_ENV, &len), *out;
    BPTR fh;
    if (!d || len < 12 || memcmp(d, "FORM", 4) || memcmp(d + 8, "PREF", 4)) {
        static const UBYTE prhd[14] = { 'P', 'R', 'H', 'D', 0, 0, 0, 6, 0, 0, 0, 0, 0, 0 };
        if (d) FreeVec(d);
        if (!fmt) return;
        len = 12 + 14;
        if (!(d = AllocVec(len, MEMF_CLEAR))) return;
        memcpy(d, "FORM", 4); memcpy(d + 8, "PREF", 4); memcpy(d + 12, prhd, 14);
    }
    if (!(out = AllocVec(len + flen + 16, MEMF_CLEAR))) { FreeVec(d); return; }
    memcpy(out, d, 12);
    for (i = 12; i + 8 <= len; ) {
        ULONG n = get32(d + i + 4), all = 8 + n + (n & 1);
        if (i + all > (ULONG)len) break;
        if (memcmp(d + i, "WBTF", 4)) { memcpy(out + o, d + i, all); o += all; }
        i += all;
    }
    if (fmt) {
        memcpy(out + o, "WBTF", 4);
        put32(out + o + 4, flen);
        memcpy(out + o + 8, fmt, flen);
        o += 8 + flen + (flen & 1);
    }
    put32(out + 4, o - 8);
    if ((fh = Open((STRPTR)WB_ENV, MODE_NEWFILE))) { Write(fh, out, o); Close(fh); }   /* IPrefs is notified and shows it */
    FreeVec(out);
    FreeVec(d);
}

static int ours(const char *fmt)
{
    const char *f = fmt;
    while (*f == ' ') f++;
    return !strcmp(f, TITLE_MEM) || !strcmp(f, TITLE_PLAIN);
}

static void apply_title(void)
{
    char now[160], want[160];
    int have = wb_title(now, sizeof now);
    if (!prefs.memory && !prefs.logo) {
        if (have && ours(now)) wb_set_title(NULL);       /* back to Workbench's own */
        return;
    }
    if (have && !ours(now) && now[0]) {
        /* someone's own title format: keep it, only make room for the logo */
        if (!prefs.logo || !strncmp(now, LOGO_PAD, strlen(LOGO_PAD))) return;
        snprintf(want, sizeof want, LOGO_PAD "%s", now);
    } else snprintf(want, sizeof want, "%s%s", prefs.logo ? LOGO_PAD : "", prefs.memory ? TITLE_MEM : TITLE_PLAIN);
    if (!have || strcmp(now, want)) wb_set_title(want);
}

/* ---- the border ------------------------------------------------------------------------------ */

static void apply_border(void)
{
    if (!scr || !scr->ViewPort.ColorMap) return;
    if (prefs.border_black == border_set) return;
    VideoControlTags(scr->ViewPort.ColorMap, prefs.border_black ? VTAG_BORDERBLANK_SET : VTAG_BORDERBLANK_CLR, 0, TAG_DONE);
    border_set = prefs.border_black;
    MakeScreen(scr);
    RethinkDisplay();
}

/* ---- the logo -------------------------------------------------------------------------------- */

static LONG pen_for(ULONG rgb)
{
    int i;
    for (i = 0; i < npens; i++) if (pen_rgb[i] == rgb) return pens[i];
    if (npens == 64) return dri->dri_Pens[BARDETAILPEN];
    pen_rgb[npens] = rgb;
    pens[npens] = ObtainBestPen(scr->ViewPort.ColorMap, (rgb >> 16) * 0x01010101UL, ((rgb >> 8) & 255) * 0x01010101UL,
                                (rgb & 255) * 0x01010101UL, OBP_Precision, PRECISION_IMAGE, TAG_DONE);
    if (pens[npens] < 0) pens[npens] = dri->dri_Pens[BARDETAILPEN];
    return pens[npens++];
}

static void free_pens(void)
{
    int i;
    for (i = 0; i < npens; i++) if (pens[i] >= 0 && scr) ReleasePen(scr->ViewPort.ColorMap, pens[i]);
    npens = 0;
}

static void draw_logo(void)
{
    struct RastPort *rp;
    WORD s, x, y;
    if (!logo_w) return;
    rp = logo_w->RPort;
    SetAPen(rp, dri->dri_Pens[BARBLOCKPEN]);
    RectFill(rp, 0, 0, logo_w->Width - 1, logo_w->Height - 1);
    s = logo_w->Height - 2;                    /* the logo, scaled to the bar, a pixel in from the top */
    for (y = 0; y < s; y++)
        for (x = 0; x < s; x++) {
            long c = logo16[(y * 16 / s) * 16 + x * 16 / s];
            if (c < 0) continue;
            SetAPen(rp, pen_for((ULONG)c));
            WritePixel(rp, 2 + x, 1 + y);
        }
}

/* ---- the clock ------------------------------------------------------------------------------- */

static void clock_text(char *t, int size)
{
    static const char *const days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct DateStamp ds;
    struct ClockData cd;
    DateStamp(&ds);
    Amiga2Date(ds.ds_Days * 86400 + ds.ds_Minute * 60 + ds.ds_Tick / TICKS_PER_SECOND, &cd);
    if (prefs.date) snprintf(t, size, "%s %d %s  %02d:%02d", days[cd.wday % 7], cd.mday, months[(cd.month + 11) % 12], cd.hour, cd.min);
    else snprintf(t, size, "%02d:%02d", cd.hour, cd.min);
}

static WORD clock_width(void)
{
    struct RastPort rp;
    const char *widest = prefs.date ? "Wed 28 Sep  00:00" : "00:00";
    InitRastPort(&rp);
    SetFont(&rp, dri->dri_Font);
    return TextLength(&rp, (STRPTR)widest, strlen(widest)) + 10;
}

static void draw_clock(int force)
{
    struct RastPort *rp;
    char t[40];
    WORD h, w;
    if (!clock_w) return;
    clock_text(t, sizeof t);
    if (!force && !strcmp(t, shown)) return;
    strcpy(shown, t);
    rp = clock_w->RPort;
    SetAPen(rp, dri->dri_Pens[BARBLOCKPEN]);
    RectFill(rp, 0, 0, cw - 1, ch - 1);
    SetAPen(rp, dri->dri_Pens[BARDETAILPEN]);
    SetDrMd(rp, JAM1);
    h = ch - (bar_edge == OM_BAR_TITLE ? 1 : 0);
    w = TextLength(rp, (STRPTR)t, strlen(t));
    Move(rp, cw - 6 - w, (h - rp->TxHeight) / 2 + rp->TxBaseline);
    Text(rp, (STRPTR)t, strlen(t));
}

/* where the clock goes: left of the tray programs nearer the bar's end */
static void place(void)
{
    WORD bh = scr->BarHeight + 1;
    int n, off = tray_before("Clock", CLOCK_ORDER, &n);
    cw = clock_width();
    switch (bar_edge) {
    case OM_BAR_BOTTOM: ch = bh; cx = scr->Width - cw - off; cy = scr->Height - bh; break;
    case OM_BAR_LEFT:   ch = bh; cx = 0; cy = scr->Height - bh * (n + 1); break;
    case OM_BAR_RIGHT:  ch = bh; cx = scr->Width - cw; cy = scr->Height - bh * (n + 1); break;
    case OM_BAR_TOP:    ch = bh; cx = scr->Width - 2 * scr->BarHeight - cw - off; cy = 0; break;
    default:            ch = scr->BarHeight; cx = scr->Width - 2 * scr->BarHeight - cw - off; cy = 0; break;
    }
}

/* ---- the windows ----------------------------------------------------------------------------- */

static struct Window *bar_window(WORD x, WORD y, WORD w, WORD h)
{
    return OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                          WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW, TAG_DONE);
}


/* ---- the network icons ----------------------------------------------------------------------- */

struct wifi_row {                              /* opensocketwifi.device's record (acnetwork.h), 64 bytes */
    char ssid[33];
    UBYTE bssid[6], signal, security, flags, reserved0[2];
    ULONG channel, rate_mbps;
    UBYTE reserved1[12];
};
struct wifi_call { ULONG command; const char *ssid; struct wifi_row *networks; ULONG capacity; LONG result; ULONG error; };

/* the PC's (or the card's) Wi-Fi: -1 none to show, 0 not connected, 1 connected */
static int wifi_poll(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *req = port ? (struct IOStdReq *)CreateIORequest(port, sizeof *req) : NULL;
    struct wifi_row row;
    struct wifi_call call;
    int state = -1;
    if (req && !OpenDevice((STRPTR)WIFI_DEVICE, 0, (struct IORequest *)req, 0)) {
        register struct Device *a6 __asm("a6") = req->io_Device;
        register struct wifi_call *a0 __asm("a0") = &call;
        register LONG d0 __asm("d0");
        memset(&call, 0, sizeof call);
        memset(&row, 0, sizeof row);
        call.command = WIFI_STATUS;
        call.networks = &row;
        call.capacity = 1;
        __asm volatile ("jsr -42(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6) : "d1", "a1", "cc", "memory");
        (void)d0;
        if (!call.error) {
            state = call.result > 0;
            if (state) { strncpy(wifi_ssid, row.ssid, sizeof wifi_ssid - 1); wifi_ssid[sizeof wifi_ssid - 1] = 0; wifi_signal = row.signal; }
        }
        CloseDevice((struct IORequest *)req);
    }
    if (req) DeleteIORequest((struct IORequest *)req);
    if (port) DeleteMsgPort(port);
    return state;
}

static void net_poll(void)
{
    ULONG id;
    if (!SocketBase) SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) { lan_state = 0; lan_addr[0] = 0; }
    else if ((id = gethostid()) != 0 && id != 0x7f000001UL) {
        struct in_addr a;
        a.s_addr = id;
        lan_state = 2;
        strncpy(lan_addr, (const char *)Inet_NtoA(a.s_addr), sizeof lan_addr - 1);
        lan_addr[sizeof lan_addr - 1] = 0;
    } else { lan_state = 1; lan_addr[0] = 0; }
    wifi_state = wifi_poll();
}

static WORD net_width(void) { return (wifi_state >= 0 ? 2 : 1) * 20 + 4; }

static void net_place(void)
{
    WORD bh = scr->BarHeight + 1;
    int n, off = tray_before("Network", NET_ORDER, &n);
    nw = net_width();
    switch (bar_edge) {
    case OM_BAR_BOTTOM: nh = bh; nx = scr->Width - nw - off; ny = scr->Height - bh; break;
    case OM_BAR_LEFT:   nh = bh; nx = 0; ny = scr->Height - bh * (n + 1); break;
    case OM_BAR_RIGHT:  nh = bh; nx = scr->Width - nw; ny = scr->Height - bh * (n + 1); break;
    case OM_BAR_TOP:    nh = bh; nx = scr->Width - 2 * scr->BarHeight - nw - off; ny = 0; break;
    default:            nh = scr->BarHeight; nx = scr->Width - 2 * scr->BarHeight - nw - off; ny = 0; break;
    }
}

/* LAN: a box above two, joined; filled when connected, hollow when not, crossed with no network */
static void draw_lan(struct RastPort *rp, WORD x, WORD cy, int state)
{
    WORD t = cy - 5;
    if (state == 2) { RectFill(rp, x + 5, t, x + 10, t + 3); RectFill(rp, x, t + 7, x + 4, t + 10); RectFill(rp, x + 11, t + 7, x + 15, t + 10); }
    else {
        Move(rp, x + 5, t); Draw(rp, x + 10, t); Draw(rp, x + 10, t + 3); Draw(rp, x + 5, t + 3); Draw(rp, x + 5, t);
        Move(rp, x, t + 7); Draw(rp, x + 4, t + 7); Draw(rp, x + 4, t + 10); Draw(rp, x, t + 10); Draw(rp, x, t + 7);
        Move(rp, x + 11, t + 7); Draw(rp, x + 15, t + 7); Draw(rp, x + 15, t + 10); Draw(rp, x + 11, t + 10); Draw(rp, x + 11, t + 7);
    }
    Move(rp, x + 7, t + 4); Draw(rp, x + 7, t + 5); Move(rp, x + 2, t + 5); Draw(rp, x + 13, t + 5);
    Move(rp, x + 2, t + 5); Draw(rp, x + 2, t + 6); Move(rp, x + 13, t + 5); Draw(rp, x + 13, t + 6);
    if (state == 0) { Move(rp, x, t); Draw(rp, x + 15, t + 10); Move(rp, x + 15, t); Draw(rp, x, t + 10); }
}

/* Wi-Fi: a dot and up to three arcs by the signal; only the dot and a cross when not connected */
static void draw_wifi(struct RastPort *rp, WORD x, WORD cy, int state, int signal)
{
    WORD ox = x + 8, oy = cy + 4;
    int arcs = !state ? 0 : signal > 66 ? 3 : signal > 33 ? 2 : 1;
    RectFill(rp, ox - 1, oy - 1, ox, oy);
    for (int k = 1; k <= 3; k++) {
        int r = 3 * k + 1;
        if (k > arcs) continue;
        for (int dx = -r; dx <= r; dx++) {
            int dy = 0;
            while ((dy + 1) * (dy + 1) + dx * dx <= r * r) dy++;
            if (dx * dx <= dy * dy) WritePixel(rp, ox + dx, oy - dy);
        }
    }
    if (!state) { Move(rp, ox + 2, oy - 8); Draw(rp, ox + 7, oy - 3); Move(rp, ox + 7, oy - 8); Draw(rp, ox + 2, oy - 3); }
}

static void draw_net(int force)
{
    struct RastPort *rp;
    int key = lan_state * 1000 + (wifi_state + 1) * 100 + (wifi_state > 0 ? (wifi_signal > 66 ? 3 : wifi_signal > 33 ? 2 : 1) : 0);
    WORD cy;
    if (!net_w || (!force && key == net_drawn)) return;
    net_drawn = key;
    rp = net_w->RPort;
    SetAPen(rp, dri->dri_Pens[BARBLOCKPEN]);
    RectFill(rp, 0, 0, nw - 1, nh - 1);
    SetAPen(rp, dri->dri_Pens[BARDETAILPEN]);
    cy = (nh - (bar_edge == OM_BAR_TITLE ? 1 : 0)) / 2;
    draw_lan(rp, 2, cy, lan_state);
    if (wifi_state >= 0) draw_wifi(rp, 22, cy, wifi_state, wifi_signal);
}

static struct Window *net_window(WORD x, WORD y, WORD w, WORD h)
{
    return OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                          WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW | IDCMP_MOUSEBUTTONS, TAG_DONE);
}

static void net_open(void)
{
    net_poll();
    net_place();
    if ((net_w = net_window(nx, ny, nw, nh))) {
        net_drawn = -100;
        draw_net(1);
        tray("Network", nw, NET_ORDER);
    }
}

/* the panel under the icons: what the network is, and a button for its settings */
#define POP_W 230
static void pop_lines(char l[3][64])
{
    if (lan_state == 0) strcpy(l[0], "Network: OpenSocket isn't running");
    else if (lan_state == 1) strcpy(l[0], "Network: not connected");
    else snprintf(l[0], 64, "Network: connected, %s", lan_addr);
    if (wifi_state < 0) strcpy(l[1], "Wi-Fi: none");
    else if (!wifi_state) strcpy(l[1], "Wi-Fi: not connected");
    else snprintf(l[1], 64, "Wi-Fi: %s, signal %d%%", wifi_ssid, wifi_signal);
    strcpy(l[2], "Network settings...");
}

static void draw_pop(void)
{
    struct RastPort *rp;
    char l[3][64];
    WORD fh, y, i;
    if (!pop_w) return;
    rp = pop_w->RPort;
    fh = rp->TxHeight;
    pop_lines(l);
    SetAPen(rp, dri->dri_Pens[BACKGROUNDPEN]);
    RectFill(rp, 0, 0, pop_w->Width - 1, pop_w->Height - 1);
    SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
    Move(rp, 0, 0); Draw(rp, pop_w->Width - 1, 0); Draw(rp, pop_w->Width - 1, pop_w->Height - 1); Draw(rp, 0, pop_w->Height - 1); Draw(rp, 0, 0);
    SetAPen(rp, dri->dri_Pens[TEXTPEN]);
    SetDrMd(rp, JAM1);
    for (i = 0, y = 6; i < 2; i++, y += fh + 4) { Move(rp, 8, y + rp->TxBaseline); Text(rp, (STRPTR)l[i], strlen(l[i])); }
    /* the button */
    SetAPen(rp, dri->dri_Pens[SHINEPEN]);
    Move(rp, 8, y + fh + 5); Draw(rp, 8, y); Draw(rp, pop_w->Width - 9, y);
    SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
    Draw(rp, pop_w->Width - 9, y + fh + 5); Draw(rp, 8, y + fh + 5);
    SetAPen(rp, dri->dri_Pens[TEXTPEN]);
    Move(rp, (pop_w->Width - TextLength(rp, (STRPTR)l[2], strlen(l[2]))) / 2, y + 3 + rp->TxBaseline);
    Text(rp, (STRPTR)l[2], strlen(l[2]));
}

static void pop_close(void)
{
    if (pop_w) { CloseWindow(pop_w); pop_w = NULL; }
    if (last_other) ActivateWindow(last_other);
}

static void pop_open(void)
{
    WORD fh = dri->dri_Font->tf_YSize, h = 3 * (fh + 4) + 14, x = nx + nw - POP_W, y = ny + nh;
    if (x < 0) x = 0;
    if (bar_edge == OM_BAR_BOTTOM) y = ny - h;
    if (y + h > scr->Height) y = scr->Height - h;
    net_poll();
    draw_net(0);
    if ((pop_w = net_window(x, y, POP_W, h))) {
        SetFont(pop_w->RPort, dri->dri_Font);
        draw_pop();
        pop_ticks = 0;
    }
}

static void pop_click(WORD my)
{
    WORD fh = pop_w->RPort->TxHeight, by = 6 + 2 * (fh + 4);
    if (my >= by && my <= by + fh + 5) {
        BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);
        /* asynchronous: both handles are the new process's, closed when it ends */
        if (!in || !out || SystemTags((STRPTR)NET_TOOL, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, SYS_UserShell, TRUE,
                                      NP_StackSize, 16384, TAG_DONE) == -1) {
            if (in) Close(in);
            if (out) Close(out);
        }
    }
    pop_close();
}

static void close_all(void)
{
    if (clock_w) { CloseWindow(clock_w); clock_w = NULL; tray("Clock", 0, CLOCK_ORDER); }
    if (pop_w) { CloseWindow(pop_w); pop_w = NULL; }
    if (net_w) { CloseWindow(net_w); net_w = NULL; tray("Network", 0, NET_ORDER); }
    if (logo_w) { CloseWindow(logo_w); logo_w = NULL; }
    if (border_set) { prefs.border_black = 0; apply_border(); }
    free_pens();
    if (dri) { FreeScreenDrawInfo(scr, dri); dri = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static int open_all(void)
{
    if (!(scr = LockPubScreen((STRPTR)"Workbench"))) return 0;
    if (!(dri = GetScreenDrawInfo(scr))) { close_all(); return 0; }
    bar_edge = read_bar_edge();
    if (prefs.clock) {
        place();
        if ((clock_w = bar_window(cx, cy, cw, ch))) {
            SetFont(clock_w->RPort, dri->dri_Font);
            shown[0] = 0;
            draw_clock(1);
            tray("Clock", cw, CLOCK_ORDER);
        }
    }
    if (prefs.network) net_open();
    apply_border();
    return 1;
}

/* in front again when a window (not a borderless one: menus, bars) has come over it */
static void keep_in_front(struct Window *w)
{
    struct Layer *l;
    int covered = 0;
    if (!w) return;
    LockLayerInfo(&scr->LayerInfo);
    for (l = w->WLayer->front; l && !covered; l = l->front) {
        struct Window *o = (struct Window *)l->Window;
        if (!o || (o->Flags & WFLG_BORDERLESS)) continue;
        covered = l->bounds.MinX <= w->LeftEdge + w->Width - 1 && l->bounds.MaxX >= w->LeftEdge &&
                  l->bounds.MinY <= w->TopEdge + w->Height - 1 && l->bounds.MaxY >= w->TopEdge;
    }
    UnlockLayerInfo(&scr->LayerInfo);
    if (covered) WindowToFront(w);
}

static void events(struct Window *w)
{
    struct IntuiMessage *m;
    if (!w) return;
    while ((m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        WORD my = m->MouseY;
        ReplyMsg((struct Message *)m);
        if (cls == IDCMP_REFRESHWINDOW) {
            BeginRefresh(w);
            if (w == logo_w) draw_logo(); else if (w == net_w) draw_net(1); else if (w == pop_w) draw_pop(); else draw_clock(1);
            EndRefresh(w, TRUE);
        }
        else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
            if (w == net_w) { if (pop_w) pop_close(); else pop_open(); return; }
            if (w == pop_w) { pop_click(my); return; }
        }
        /* a click on ours: the window before stays the active one (the panel keeps it while open) */
        else if (cls == IDCMP_ACTIVEWINDOW && last_other && w != pop_w) ActivateWindow(last_other);
    }
}

/* 1 when the bar's title has room for the logo: Workbench's, which OpenTitle's
 * title format starts with spaces (another program's starts at the left edge) */
static int wb_title_shown(void)
{
    struct Window *a = IntuitionBase->ActiveWindow;
    const char *t;
    if (!a || a->WScreen != scr || a == logo_w || a == clock_w || a == net_w || a == pop_w) a = last_other;
    t = (const char *)(a && a->WScreen == scr && a->ScreenTitle ? a->ScreenTitle : scr->Title);
    return t && !strncmp(t, LOGO_PAD, strlen(LOGO_PAD));
}

/* the logo only beside Workbench's title: another program's title starts at the bar's left edge */
static void logo_follow(void)
{
    int want = prefs.logo && wb_title_shown();
    if (want && !logo_w) {
        if ((logo_w = bar_window(0, 0, scr->BarHeight + 2, scr->BarHeight))) draw_logo();
    } else if (!want && logo_w) {
        CloseWindow(logo_w);
        logo_w = NULL;
    }
}

static int is_window(struct Window *w)
{
    struct Window *o;
    if (!w || !scr) return 0;
    for (o = scr->FirstWindow; o; o = o->NextWindow) if (o == w) return 1;
    return 0;
}

int main(void)
{
    struct MsgPort *port;
    int tick = 0, quit = 0;
    (void)version;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 39);
    if (!IntuitionBase || !GfxBase || !LayersBase || !UtilityBase) goto out;
    Forbid();
    port = FindPort((STRPTR)TB_PORT);
    Permit();
    if (port) { PutStr((STRPTR)"OpenTitle is already running\n"); goto out; }
    if (!(port = CreateMsgPort())) goto out;
    port->mp_Node.ln_Name = (char *)TB_PORT;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);

    read_prefs();
    apply_title();
    if (!open_all()) quit = 1;
    while (!quit) {
        ULONG sig = SetSignal(0, SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
        struct Window *act;
        if (sig & SIGBREAKF_CTRL_C) break;
        if (sig & SIGBREAKF_CTRL_F) {
            close_all();
            read_prefs();
            apply_title();
            if (!prefs.logo && !prefs.memory && !prefs.clock && !prefs.network && !prefs.border_black) break;
            if (!open_all()) break;
        }
        events(logo_w);
        events(clock_w);
        events(net_w);
        if (pop_w) events(pop_w);
        act = IntuitionBase->ActiveWindow;
        if (act && act != logo_w && act != clock_w && act != net_w && act != pop_w) last_other = act;
        if (pop_w && ++pop_ticks > 20) pop_close();     /* the panel goes after 10 s */
        if (last_other && !is_window(last_other)) last_other = NULL;
        logo_follow();
        draw_clock(0);
        if (++tick % 4 == 0) {                 /* every 2 s: the tray and OpenMenus' bar may have moved */
            int edge = read_bar_edge();
            WORD ox = cx, oy = cy, ow = cw, oh = ch;
            if (edge != bar_edge && (clock_w || net_w)) { close_all(); if (!open_all()) break; }
            else {
                if (net_w) {
                    WORD px = nx, py = ny, pw = nw, ph = nh;
                    if (tick % 20 == 0) net_poll();               /* every 10 s */
                    net_place();
                    if (nx != px || ny != py || nw != pw || nh != ph) {
                        ChangeWindowBox(net_w, nx, ny, nw, nh);
                        tray("Network", nw, NET_ORDER);
                        net_drawn = -100;
                    }
                    draw_net(0);
                }
                if (clock_w) {
                    place();
                    if (cx != ox || cy != oy || cw != ow || ch != oh) { ChangeWindowBox(clock_w, cx, cy, cw, ch); shown[0] = 0; }
                }
            }
            keep_in_front(logo_w);
            keep_in_front(clock_w);
            keep_in_front(net_w);
        }
        Delay(TICKS_PER_SECOND / 2);
    }
    close_all();
    RemPort(port);
    DeleteMsgPort(port);
out:
    if (SocketBase) CloseLibrary(SocketBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
