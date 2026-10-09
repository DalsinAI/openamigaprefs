/* OpenTitle 0.1: the Workbench screen's title bar as Dale drew it on
 * 6 October 2026: AmigaChrome's logo at the far left, Workbench's title with
 * the free chip and fast memory after it, and the day, date and time at the
 * right end, just left of OpenSpeaker. Optionally the display's border
 * around the screen is black.
 *
 * 0.5 (8 October 2026): a cog at the bar's far end, right of OpenSpeaker and
 * left of the screen's depth gadget. A click lists the Open settings editors
 * that are installed, and "All Prefs..." (the SYS:Prefs drawer); choosing
 * one starts it as a double-click on its icon does.
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
 * 0.6 (8 October 2026): the taskspace. Other programs put icons in the row
 * beside the cog, the speaker and the network icons through OpenTitle's
 * port (include/taskspace.h); OpenTitle draws them, lights them under the
 * pointer, shows their help under them and tells the program of a click.
 * Tata is the first.
 *
 * 0.6.2 (10 October 2026): the bar's icons sit under windows that overlap the
 * bar (they are windows of their own and are no longer raised over the
 * others), are painted over the bar's own gradient, not a white box (the
 * colours are read from the screen's bar, Common/barpaint.h), and are
 * centred in the bar.
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
#include <devices/timer.h>
#include <workbench/workbench.h>
#include <proto/wb.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tb_prefs.h"
#include "om_prefs.h"
#include "logo.h"
#include "taskspace.h"
#include "barpaint.h"
#include "wbclose.h"

const char version[] __attribute__((used)) = "$VER: OpenTitle 0.6.2 (10.10.2026) OpenPrefs, Dalsin Limited";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *LayersBase, *UtilityBase, *SocketBase, *WorkbenchBase, *CyberGfxBase;

#define MENUS_ENV  "ENV:OpenMenus/Menus"
#define TRAY_DIR   "ENV:OpenMenus/Tray"
#define CLOCK_ORDER 10                         /* left of the network icons (5) and OpenSpeaker (0) */
#define NET_ORDER   5
#define COG_ORDER   (-5)                       /* the far end: right of OpenSpeaker (0), left of the depth gadget */
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
static int wb_down;                            /* Workbench is shutting down or shut: 1 our windows are closed, 2 to open them again */

/* the network icons: LAN and Wi-Fi, from OpenSocket */
static struct Window *net_w, *pop_w;
static WORD nx, ny, nw, nh;
static int lan_state;                          /* 0 no bsdsocket.library, 1 no address, 2 connected */
static int wifi_state = -1;                    /* -1 no Wi-Fi to show, 0 not connected, 1 connected */
static int wifi_signal, pop_ticks;
static char lan_addr[20], wifi_ssid[34];
static int net_drawn = -100;                   /* what the icons show, to draw only on change */

/* the cog and its list of settings editors */
static struct Window *cog_w, *menu_w;
static WORD gx, gy, gw, gh;
static int cog_drawn = -1;                     /* 0 plain, 1 the pointer over it, 2 its list open */
static int cog_skip;                           /* the click that closed the list was on the cog: don't open it again */
static int menu_hot = -1;

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
            char path[sizeof TRAY_DIR "/" + sizeof fib->fib_FileName], *t;
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

/* ---- the bar's own colours ------------------------------------------------------------------ */

static int ts_is(struct Window *w);

static int bar_ours(struct Window *o)
{
    return o == logo_w || o == clock_w || o == net_w || o == pop_w || o == cog_w || o == menu_w || ts_is(o);
}

static int bar_sample_now(void) { return bp_sample(scr, bar_edge == OM_BAR_TITLE, bar_ours); }

/* the bar's colour behind a box of a bar window, not the pen's white */
static void bar_paint(struct Window *win, WORD x, WORD y, WORD pw, WORD ph)
{
    bp_paint(win, dri->dri_Pens[BARBLOCKPEN], x, y, pw, ph);
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

#define LOGO_W 17                              /* the logo's window: the logo is 16 wide, its picture 15 */

static void draw_logo(void)
{
    struct RastPort *rp;
    WORD x, y, oy;
    if (!logo_w) return;
    rp = logo_w->RPort;
    bar_paint(logo_w, 0, 0, logo_w->Width, logo_w->Height);
    /* the picture as it is (16 x 16, rows 4 to 12 used), its used rows centred in the bar's colour rows */
    oy = (logo_w->Height - 9) / 2 - 4;
    for (y = 4; y < 13; y++)
        for (x = 0; x < 16; x++) {
            long c = logo16[y * 16 + x];
            if (c < 0) continue;
            SetAPen(rp, pen_for((ULONG)c));
            WritePixel(rp, 1 + x, oy + y);
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
    bar_paint(clock_w, 0, 0, cw, ch);
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

/* a bar window sits in front of the bar and the Workbench backdrop but under every other window:
 * a window that overlaps the bar covers the bar's icons, as it covers the bar's title */
static struct Window *to_back(struct Window *w)
{
    if (w) WindowToBack(w);
    return w;
}

static struct Window *bar_window(WORD x, WORD y, WORD w, WORD h)
{
    return to_back(OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                          WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW, TAG_DONE));
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
    bar_paint(net_w, 0, 0, nw, nh);
    SetAPen(rp, dri->dri_Pens[BARDETAILPEN]);
    cy = (nh - (bar_edge == OM_BAR_TITLE ? 1 : 0)) / 2;
    draw_lan(rp, 2, cy, lan_state);
    if (wifi_state >= 0) draw_wifi(rp, 22, cy, wifi_state, wifi_signal);
}

static struct Window *net_window(WORD x, WORD y, WORD w, WORD h)
{
    return to_back(OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                          WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE,
                          WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW | IDCMP_MOUSEBUTTONS, TAG_DONE));
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

/* ---- the cog: the settings editors --------------------------------------------------------- */

/* The cog, drawn at three sizes; the largest that fits the bar is used. */
static const char *const cog9[9] = {
    "....#....", ".#.###.#.", "..##.##..", ".##...##.", "###...###", ".##...##.", "..##.##..", ".#.###.#.", "....#....",
};
static const char *const cog11[11] = {
    "....###....", ".##.###.##.", ".#########.", "..##...##..", "###.....###", "###.....###",
    "###.....###", "..##...##..", ".#########.", ".##.###.##.", "....###....",
};
static const char *const cog13[13] = {
    ".....###.....", ".##..###..##.", ".###########.", "..#########..", "..###...###..", "####.....####", "####.....####",
    "####.....####", "..###...###..", "..#########..", ".###########.", ".##..###..##.", ".....###.....",
};

/* The settings editors, as in OpenUpMenu's Tools > Preferences (openamigaup src/openupmenu.c):
 * each shows when its program is there (needs, or path when NULL). */
static const struct { const char *label, *path, *needs; } cog_items[] = {
    { "Look",         "SYS:Prefs/Look",         NULL },
    { "Windows",      "SYS:Prefs/Windows",      NULL },
    { "Dock",         "SYS:Prefs/Dock",         NULL },
    { "Menus",        "SYS:Prefs/Menus",        NULL },
    { "Title bar",    "SYS:Prefs/TitleBar",     NULL },
    { "Sound",        "SYS:Prefs/Sound",        "SYS:C/OpenSpeaker" },    /* OS 3.2's own until OpenUp's Sound replaces it */
    { "OpenTypes",    "SYS:Prefs/OpenTypes",    NULL },
    { "OpenUp Setup", "SYS:Prefs/OpenUp-Setup", NULL },
    { "Tata",         "SYS:Prefs/Tata",         NULL },                   /* 0.6: Tata's window, when Tata is installed */
    { NULL,           NULL,                     NULL },                   /* a line */
    { "All Prefs...", "SYS:Prefs",              NULL },
};
#define COG_ITEMS (int)(sizeof cog_items / sizeof cog_items[0])
#define COG_PAD 10
static int menu_idx[COG_ITEMS], menu_n;        /* what the list shows: indexes into cog_items */
static WORD menu_y[COG_ITEMS + 1], menu_rh;
static const char *menu_title;

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

static int cog_size(void)
{
    int h = scr->BarHeight - 2;
    return h >= 13 ? 13 : h >= 11 ? 11 : 9;
}

static void cog_place(void)
{
    WORD bh = scr->BarHeight + 1;
    int n, off = tray_before("Cog", COG_ORDER, &n);
    gw = cog_size() + 10;
    switch (bar_edge) {
    case OM_BAR_BOTTOM: gh = bh; gx = scr->Width - gw - off; gy = scr->Height - bh; break;
    case OM_BAR_LEFT:   gh = bh; gx = 0; gy = scr->Height - bh * (n + 1); break;
    case OM_BAR_RIGHT:  gh = bh; gx = scr->Width - gw; gy = scr->Height - bh * (n + 1); break;
    case OM_BAR_TOP:    gh = bh; gx = scr->Width - 2 * scr->BarHeight - gw - off; gy = 0; break;
    default:            gh = scr->BarHeight; gx = scr->Width - 2 * scr->BarHeight - gw - off; gy = 0; break;
    }
}

static int over_cog(void)
{
    WORD x = scr->MouseX, y = scr->MouseY;
    return cog_w && IntuitionBase->FirstScreen == scr && x >= gx && x < gx + gw && y >= gy && y < gy + gh;
}

/* plain in the bar's pens; raised under the pointer; filled while its list is open, as OpenSpeaker is */
static void draw_cog(int force)
{
    struct RastPort *rp;
    const char *const *rows;
    int state = menu_w ? 2 : over_cog() ? 1 : 0, size, x, y;
    WORD h, ox, oy;
    if (!cog_w || (!force && state == cog_drawn)) return;
    cog_drawn = state;
    rp = cog_w->RPort;
    h = gh;
    if (state == 2) { SetAPen(rp, dri->dri_Pens[FILLPEN]); RectFill(rp, 0, 0, gw - 1, gh - 1); }
    else bar_paint(cog_w, 0, 0, gw, gh);
    if (state == 1) {
        SetAPen(rp, dri->dri_Pens[SHINEPEN]);
        Move(rp, 1, h - 1); Draw(rp, 1, 0); Draw(rp, gw - 2, 0);
        SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
        Draw(rp, gw - 2, h - 1); Draw(rp, 2, h - 1);
    }
    size = cog_size();
    rows = size == 13 ? cog13 : size == 11 ? cog11 : cog9;
    ox = (gw - size) / 2;
    oy = (h - size) / 2;
    if (oy < 0) oy = 0;
    SetAPen(rp, dri->dri_Pens[state == 2 ? FILLTEXTPEN : BARDETAILPEN]);
    for (y = 0; y < size && oy + y < gh; y++)
        for (x = 0; x < size; ) {
            int run;
            if (rows[y][x] != '#') { x++; continue; }
            for (run = x; run < size && rows[y][run] == '#'; run++) ;
            RectFill(rp, ox + x, oy + y, ox + run - 1, oy + y);
            x = run;
        }
}

static void cog_open(void)
{
    cog_place();
    if ((cog_w = net_window(gx, gy, gw, gh))) {
        cog_drawn = -1;
        draw_cog(1);
        tray("Cog", gw, COG_ORDER);
    }
}

static void draw_menu(void)
{
    struct RastPort *rp;
    WORD w, h;
    int i;
    if (!menu_w) return;
    rp = menu_w->RPort;
    w = menu_w->Width; h = menu_w->Height;
    SetAPen(rp, dri->dri_Pens[BARBLOCKPEN]);
    RectFill(rp, 1, 1, w - 2, h - 2);
    SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
    Move(rp, 0, 0); Draw(rp, w - 1, 0); Draw(rp, w - 1, h - 1); Draw(rp, 0, h - 1); Draw(rp, 0, 0);
    SetDrMd(rp, JAM1);
    for (i = 0; i < menu_n; i++) {
        const char *t = cog_items[menu_idx[i]].label;
        WORD y0 = menu_y[i], y1 = menu_y[i + 1] - 1;
        if (!t) {
            SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
            Move(rp, 4, (y0 + y1) / 2); Draw(rp, w - 5, (y0 + y1) / 2);
            SetAPen(rp, dri->dri_Pens[SHINEPEN]);
            Move(rp, 4, (y0 + y1) / 2 + 1); Draw(rp, w - 5, (y0 + y1) / 2 + 1);
            continue;
        }
        if (i == menu_hot) { SetAPen(rp, dri->dri_Pens[FILLPEN]); RectFill(rp, 2, y0, w - 3, y1); }
        SetAPen(rp, dri->dri_Pens[i == menu_hot ? FILLTEXTPEN : BARDETAILPEN]);
        Move(rp, COG_PAD, y0 + (y1 - y0 + 1 - rp->TxHeight) / 2 + rp->TxBaseline);
        Text(rp, (STRPTR)t, strlen(t));
    }
}

static int menu_row(WORD my)
{
    int i;
    for (i = 0; i < menu_n; i++)
        if (my >= menu_y[i] && my < menu_y[i + 1]) return cog_items[menu_idx[i]].label ? i : -1;
    return -1;
}

static void menu_close(int give_back)
{
    if (!menu_w) return;
    CloseWindow(menu_w);
    menu_w = NULL;
    menu_hot = -1;
    if (give_back && last_other) ActivateWindow(last_other);
    draw_cog(1);
}

static void menu_open(void)
{
    struct RastPort trp;
    WORD W = 0, H, x, y;
    int i;
    InitRastPort(&trp);
    SetFont(&trp, dri->dri_Font);
    menu_rh = dri->dri_Font->tf_YSize + 4;
    menu_n = 0;
    H = 3;
    for (i = 0; i < COG_ITEMS; i++) {
        const char *t = cog_items[i].label;
        if (t && !exists(cog_items[i].needs ? cog_items[i].needs : cog_items[i].path)) continue;
        if (!t && !menu_n) continue;                   /* no line at the top */
        menu_y[menu_n] = H;
        menu_idx[menu_n++] = i;
        if (t) { WORD tw = TextLength(&trp, (STRPTR)t, strlen(t)); if (tw > W) W = tw; }
        H += t ? menu_rh : 6;
    }
    menu_y[menu_n] = H;
    H += 3;
    W += 2 * COG_PAD;
    if (W < 120) W = 120;
    x = gx + gw - W;                                    /* under the cog, towards the middle of the screen */
    if (bar_edge == OM_BAR_LEFT) x = 0;
    if (x < 0) x = 0;
    y = (bar_edge == OM_BAR_BOTTOM || bar_edge == OM_BAR_LEFT || bar_edge == OM_BAR_RIGHT) ? gy - H : gy + gh + (bar_edge == OM_BAR_TITLE ? 1 : 0);
    if (y < 0) y = 0;
    if (y + H > scr->Height) y = scr->Height - H;
    menu_hot = -1;
    {   /* the bar keeps the title it shows now while the list is active */
        static char title[120];
        strncpy(title, scr->Title ? (const char *)scr->Title : "", sizeof title - 1);
        title[sizeof title - 1] = 0;
        menu_title = title;
    }
    /* active while open, so the pointer lights the entries and a click elsewhere closes it */
    menu_w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, W, WA_Height, H,
                            WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
                            WA_ScreenTitle, (ULONG)menu_title,
                            WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_INACTIVEWINDOW | IDCMP_RAWKEY,
                            TAG_DONE);
    if (menu_w) {
        SetFont(menu_w->RPort, dri->dri_Font);
        draw_menu();
    }
    draw_cog(1);
}

/* starts an editor as a double-click on its icon does (its icon's stack and tool types count) */
static void cog_start(const char *path)
{
    BPTR in, out;
    char cmd[80];
    if (!WorkbenchBase) WorkbenchBase = OpenLibrary((STRPTR)"workbench.library", 44);
    if (WorkbenchBase && OpenWorkbenchObjectA((STRPTR)path, NULL)) return;
    if (!strcmp(path, "SYS:Prefs")) return;             /* a drawer needs Workbench */
    snprintf(cmd, sizeof cmd, "\"%s\"", path);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, SYS_UserShell, TRUE,
                                  NP_StackSize, 16384, TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
    }
}

static void menu_pick(int row)
{
    const char *path;
    if (row < 0 || row >= menu_n || !(path = cog_items[menu_idx[row]].path)) return;
    menu_close(1);
    cog_start(path);
}

/* the list's messages; first, so a click on the cog that closed it isn't taken as one to open it again */
static void menu_events(void)
{
    struct IntuiMessage *m;
    while (menu_w && (m = (struct IntuiMessage *)GetMsg(menu_w->UserPort))) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        WORD my = m->MouseY, mx = m->MouseX;
        int inside = mx >= 0 && mx < menu_w->Width && my >= 0 && my < menu_w->Height;
        ReplyMsg((struct Message *)m);
        if (cls == IDCMP_REFRESHWINDOW) { BeginRefresh(menu_w); draw_menu(); EndRefresh(menu_w, TRUE); }
        else if (cls == IDCMP_MOUSEMOVE) {
            int hot = inside ? menu_row(my) : -1;
            if (hot != menu_hot) { menu_hot = hot; draw_menu(); }
        }
        else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTUP && inside) menu_pick(menu_row(my));
        else if (cls == IDCMP_INACTIVEWINDOW) {          /* a click elsewhere */
            cog_skip = over_cog();
            menu_close(0);
        }
        else if (cls == IDCMP_RAWKEY) {
            int i = menu_hot;
            if (code == 0x45) menu_close(1);                                  /* Esc */
            else if (code == 0x44 || code == 0x43) menu_pick(menu_hot);       /* Return, Enter */
            else if (code == 0x4C || code == 0x4D) {                          /* up, down */
                do i = code == 0x4D ? (i + 1) % menu_n : (i <= 0 ? menu_n - 1 : i - 1);
                while (!cog_items[menu_idx[i]].label);
                menu_hot = i;
                draw_menu();
            }
        }
    }
}

/* ---- the taskspace: other programs' icons (include/taskspace.h) --------------------------- */

struct ts_icon {
    char name[16], help[48], port[24];
    WORD order;
    UWORD state, w, h, states;
    UBYTE pixels[TS_STATES * TS_MAXW * TS_MAXH];
    ULONG rgb[12];
    struct Window *win;
    WORD x, y, bw, bh;
    int drawn;                                 /* what is drawn: state * 2 + lit, -1 nothing yet */
};
static struct ts_icon *ts[TS_ICONS];
static struct MsgPort *ts_reply;               /* where the programs reply our events */
static int ts_out;                             /* events not replied yet */
static struct Window *help_w;                  /* the help under an icon */
static int help_for = -1, hover_for = -1, hover_ticks;

static int ts_find(const char *name)
{
    int i;
    for (i = 0; i < TS_ICONS; i++) if (ts[i] && !strcmp(ts[i]->name, name)) return i;
    return -1;
}

static void ts_place(struct ts_icon *t)
{
    WORD bh = scr->BarHeight + 1;
    int n, off = tray_before(t->name, t->order, &n);
    t->bw = t->w + 10;
    switch (bar_edge) {
    case OM_BAR_BOTTOM: t->bh = bh; t->x = scr->Width - t->bw - off; t->y = scr->Height - bh; break;
    case OM_BAR_LEFT:   t->bh = bh; t->x = 0; t->y = scr->Height - bh * (n + 1); break;
    case OM_BAR_RIGHT:  t->bh = bh; t->x = scr->Width - t->bw; t->y = scr->Height - bh * (n + 1); break;
    case OM_BAR_TOP:    t->bh = bh; t->x = scr->Width - 2 * scr->BarHeight - t->bw - off; t->y = 0; break;
    default:            t->bh = scr->BarHeight; t->x = scr->Width - 2 * scr->BarHeight - t->bw - off; t->y = 0; break;
    }
}

static int ts_over(const struct ts_icon *t)
{
    WORD x = scr->MouseX, y = scr->MouseY;
    return t->win && IntuitionBase->FirstScreen == scr && x >= t->x && x < t->x + t->bw && y >= t->y && y < t->y + t->bh;
}

/* the picture in the bar's pens and the program's colours; raised under the pointer, as the cog is */
static void ts_draw(struct ts_icon *t, int force)
{
    struct RastPort *rp;
    int lit, key, x, y;
    WORD h, ox, oy;
    const UBYTE *px;
    if (!t->win) return;
    lit = ts_over(t);
    key = t->state * 2 + lit;
    if (!force && key == t->drawn) return;
    t->drawn = key;
    rp = t->win->RPort;
    h = t->bh;
    bar_paint(t->win, 0, 0, t->bw, t->bh);
    if (lit) {
        SetAPen(rp, dri->dri_Pens[SHINEPEN]);
        Move(rp, 1, h - 1); Draw(rp, 1, 0); Draw(rp, t->bw - 2, 0);
        SetAPen(rp, dri->dri_Pens[SHADOWPEN]);
        Draw(rp, t->bw - 2, h - 1); Draw(rp, 2, h - 1);
    }
    ox = (t->bw - t->w) / 2;
    oy = (h - t->h) / 2;
    if (oy < 0) oy = 0;
    px = t->pixels + (ULONG)t->state * t->w * t->h;
    for (y = 0; y < t->h && oy + y < t->bh; y++)
        for (x = 0; x < t->w; ) {
            UBYTE c = px[y * t->w + x];
            int run;
            LONG pen;
            if (!c) { x++; continue; }
            for (run = x; run < t->w && px[y * t->w + run] == c; run++) ;
            pen = c == 1 ? dri->dri_Pens[BARDETAILPEN] : c == 2 ? dri->dri_Pens[SHINEPEN] : c == 3 ? dri->dri_Pens[SHADOWPEN]
                : pen_for(t->rgb[(c - 4) & 15]);
            SetAPen(rp, pen);
            RectFill(rp, ox + x, oy + y, ox + run - 1, oy + y);
            x = run;
        }
}

static void ts_open_one(struct ts_icon *t)
{
    if (!scr || t->win) return;
    ts_place(t);
    if ((t->win = net_window(t->x, t->y, t->bw, t->bh))) {
        t->drawn = -1;
        ts_draw(t, 1);
        tray(t->name, t->bw, t->order);
    }
}

static void help_close(void)
{
    if (help_w) { CloseWindow(help_w); help_w = NULL; }
    help_for = -1;
}

static void ts_close_one(struct ts_icon *t, int forget_place)
{
    int i;
    for (i = 0; i < TS_ICONS; i++) if (ts[i] == t && help_for == i) help_close();
    if (t->win) { CloseWindow(t->win); t->win = NULL; }
    if (forget_place) tray(t->name, 0, t->order);
}

static void ts_drop(int i)
{
    if (!ts[i]) return;
    ts_close_one(ts[i], 1);
    FreeVec(ts[i]);
    ts[i] = NULL;
    if (hover_for == i) hover_for = -1;
}

/* the help under an icon, after the pointer has rested on it a moment */
static void help_open(int i)
{
    struct ts_icon *t = ts[i];
    struct RastPort trp;
    WORD w, h, x, y;
    if (!t || !t->win || !t->help[0] || help_w) return;
    InitRastPort(&trp);
    SetFont(&trp, dri->dri_Font);
    w = TextLength(&trp, (STRPTR)t->help, strlen(t->help)) + 12;
    h = dri->dri_Font->tf_YSize + 6;
    x = t->x + t->bw - w;
    if (x < 0) x = 0;
    y = bar_edge == OM_BAR_BOTTOM ? t->y - h : t->y + t->bh + (bar_edge == OM_BAR_TITLE ? 1 : 0);
    if (y + h > scr->Height) y = scr->Height - h;
    if (!(help_w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, x, WA_Top, y, WA_Width, w, WA_Height, h,
                                  WA_Borderless, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, WA_SmartRefresh, TRUE, TAG_DONE)))
        return;
    help_for = i;
    SetFont(help_w->RPort, dri->dri_Font);
    SetAPen(help_w->RPort, dri->dri_Pens[BARBLOCKPEN]);
    RectFill(help_w->RPort, 1, 1, w - 2, h - 2);
    SetAPen(help_w->RPort, dri->dri_Pens[SHADOWPEN]);
    Move(help_w->RPort, 0, 0); Draw(help_w->RPort, w - 1, 0); Draw(help_w->RPort, w - 1, h - 1); Draw(help_w->RPort, 0, h - 1); Draw(help_w->RPort, 0, 0);
    SetAPen(help_w->RPort, dri->dri_Pens[BARDETAILPEN]);
    SetDrMd(help_w->RPort, JAM1);
    Move(help_w->RPort, 6, 3 + help_w->RPort->TxBaseline);
    Text(help_w->RPort, (STRPTR)t->help, strlen(t->help));
}

/* every tenth of a second while the pointer is on the bar: the light and the help */
static void ts_hover(void)
{
    int i, over = -1;
    for (i = 0; i < TS_ICONS; i++) if (ts[i]) { ts_draw(ts[i], 0); if (ts_over(ts[i])) over = i; }
    if (over != hover_for) { hover_for = over; hover_ticks = 0; if (help_w) help_close(); }
    else if (over >= 0 && ++hover_ticks == 6) help_open(over);
}

/* tells the program; 0 when its port is gone (it ended without TSC_REMOVE) */
static int ts_tell(struct ts_icon *t, UWORD kind)
{
    struct TaskspaceEvent *e;
    struct MsgPort *p;
    if (!t->port[0]) return 1;
    if (!ts_reply || !(e = AllocVec(sizeof *e, MEMF_PUBLIC | MEMF_CLEAR))) return 1;
    e->tse_Message.mn_ReplyPort = ts_reply;
    e->tse_Message.mn_Length = sizeof *e;
    e->tse_Magic = TASKSPACE_MAGIC;
    e->tse_Kind = kind;
    strcpy(e->tse_Name, t->name);
    e->tse_Left = t->x; e->tse_Top = t->y; e->tse_Width = t->bw; e->tse_Height = t->bh;
    Forbid();
    if ((p = FindPort((STRPTR)t->port))) { PutMsg(p, &e->tse_Message); ts_out++; }
    Permit();
    if (!p) FreeVec(e);
    return p != NULL;
}

static void ts_replies(void)
{
    struct Message *m;
    while (ts_reply && (m = GetMsg(ts_reply))) { FreeVec(m); ts_out--; }
}

/* a program's message on OpenTitle's port */
static void ts_command(struct TaskspaceMsg *m)
{
    int i;
    if (m->tsm_Message.mn_Length < sizeof *m || m->tsm_Magic != TASKSPACE_MAGIC || m->tsm_Version < 1) { m->tsm_Result = TSR_BADMSG; return; }
    m->tsm_Name[sizeof m->tsm_Name - 1] = 0;
    i = ts_find(m->tsm_Name);
    m->tsm_Result = TSR_OK;
    switch (m->tsm_Command) {
    case TSC_ADD: {
        struct ts_icon *t;
        ULONG n = (ULONG)m->tsm_Width * m->tsm_Height;
        if (!m->tsm_Name[0] || !m->tsm_Pixels || !m->tsm_Width || !m->tsm_Height || m->tsm_Width > TS_MAXW || m->tsm_Height > TS_MAXH
            || !m->tsm_States || m->tsm_States > TS_STATES) { m->tsm_Result = TSR_BADMSG; return; }
        if (i < 0) for (i = 0; i < TS_ICONS && ts[i]; i++) ;
        if (i == TS_ICONS) { m->tsm_Result = TSR_FULL; return; }
        if (ts[i]) ts_close_one(ts[i], 0);
        else if (!(ts[i] = AllocVec(sizeof *t, MEMF_ANY | MEMF_CLEAR))) { m->tsm_Result = TSR_NOMEM; return; }
        t = ts[i];
        memset(t, 0, sizeof *t);
        strcpy(t->name, m->tsm_Name);
        strlcpy(t->help, m->tsm_Help, sizeof t->help);  /* the sender may fill the field: always ended */
        strlcpy(t->port, m->tsm_Port, sizeof t->port);
        t->order = m->tsm_Order;
        t->w = m->tsm_Width; t->h = m->tsm_Height; t->states = m->tsm_States;
        t->state = m->tsm_State < t->states ? m->tsm_State : 0;
        memcpy(t->pixels, m->tsm_Pixels, n * t->states);
        memcpy(t->rgb, m->tsm_RGB, sizeof t->rgb);
        ts_open_one(t);
        break;
    }
    case TSC_SET:
        if (i < 0) { m->tsm_Result = TSR_UNKNOWN; return; }
        if (m->tsm_State < ts[i]->states) ts[i]->state = m->tsm_State;
        m->tsm_Help[sizeof m->tsm_Help - 1] = 0;
        if (m->tsm_Help[0] && strcmp(m->tsm_Help, ts[i]->help)) {
            strcpy(ts[i]->help, m->tsm_Help);
            if (help_for == i) help_close();
        }
        ts_draw(ts[i], 0);
        break;
    case TSC_REMOVE:
        if (i < 0) { m->tsm_Result = TSR_UNKNOWN; return; }
        ts_drop(i);
        break;
    default:
        m->tsm_Result = TSR_BADMSG;
    }
}

static void port_events(struct MsgPort *port)
{
    struct Message *m;
    while ((m = GetMsg(port))) {
        ts_command((struct TaskspaceMsg *)m);
        ReplyMsg(m);
    }
}

/* OpenTitle is ending: each program hears its icon has gone, and has two seconds to reply */
static void ts_end(void)
{
    int i, n;
    for (i = 0; i < TS_ICONS; i++) if (ts[i]) { ts_tell(ts[i], TSE_GONE); ts_drop(i); }
    for (n = 0; n < 40 && ts_out > 0; n++) { Delay(TICKS_PER_SECOND / 20); ts_replies(); }
}

static int ts_count(void)
{
    int i, n = 0;
    for (i = 0; i < TS_ICONS; i++) if (ts[i]) n++;
    return n;
}

static int ts_is(struct Window *w)
{
    int i;
    if (w && w == help_w) return 1;
    for (i = 0; i < TS_ICONS; i++) if (ts[i] && ts[i]->win == w && w) return 1;
    return 0;
}

static void close_all(void)
{
    if (clock_w) { CloseWindow(clock_w); clock_w = NULL; tray("Clock", 0, CLOCK_ORDER); }
    if (pop_w) { CloseWindow(pop_w); pop_w = NULL; }
    if (net_w) { CloseWindow(net_w); net_w = NULL; tray("Network", 0, NET_ORDER); }
    if (menu_w) { CloseWindow(menu_w); menu_w = NULL; }
    if (cog_w) { CloseWindow(cog_w); cog_w = NULL; tray("Cog", 0, COG_ORDER); }
    if (logo_w) { CloseWindow(logo_w); logo_w = NULL; }
    {
        int i;
        help_close();
        for (i = 0; i < TS_ICONS; i++) if (ts[i]) ts_close_one(ts[i], 1);
    }
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
    bar_sample_now();
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
    if (prefs.cog) cog_open();
    {
        int i;
        for (i = 0; i < TS_ICONS; i++) if (ts[i]) ts_open_one(ts[i]);
    }
    apply_border();
    return 1;
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
            int i;
            BeginRefresh(w);
            for (i = 0; i < TS_ICONS; i++) if (ts[i] && ts[i]->win == w) break;
            if (i < TS_ICONS) ts_draw(ts[i], 1);
            else if (w == logo_w) draw_logo(); else if (w == net_w) draw_net(1); else if (w == pop_w) draw_pop();
            else if (w == cog_w) draw_cog(1); else draw_clock(1);
            EndRefresh(w, TRUE);
        }
        else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
            if (w == net_w) { if (pop_w) pop_close(); else pop_open(); return; }
            if (w == pop_w) { pop_click(my); return; }
            if (w == cog_w) {
                if (menu_w) menu_close(1);
                else if (cog_skip) cog_skip = 0;        /* the click closed the list */
                else menu_open();
                return;
            }
            {   /* a program's icon: tell it; drop the icon when the program is gone */
                int i;
                for (i = 0; i < TS_ICONS; i++)
                    if (ts[i] && ts[i]->win == w) {
                        help_close();
                        if (!ts_tell(ts[i], TSE_CLICK)) ts_drop(i);
                        return;
                    }
            }
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
    if (!a || a->WScreen != scr || a == logo_w || a == clock_w || a == net_w || a == pop_w || a == cog_w || a == menu_w || ts_is(a)) a = last_other;
    t = (const char *)(a && a->WScreen == scr && a->ScreenTitle ? a->ScreenTitle : scr->Title);
    return t && !strncmp(t, LOGO_PAD, strlen(LOGO_PAD));
}

/* the logo only beside Workbench's title: another program's title starts at the bar's left edge */
static void logo_follow(void)
{
    int want = prefs.logo && wb_title_shown();
    if (want && !logo_w) {
        bar_sample_now();
        if ((logo_w = bar_window(0, 0, LOGO_W, scr->BarHeight))) draw_logo();
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

/* the windows that wake the task */
static ULONG window_sigs(void)
{
    struct Window *const ws[6] = { logo_w, clock_w, net_w, pop_w, cog_w, menu_w };
    ULONG m = 0;
    int i;
    for (i = 0; i < 6; i++) if (ws[i]) m |= 1UL << ws[i]->UserPort->mp_SigBit;
    for (i = 0; i < TS_ICONS; i++) if (ts[i] && ts[i]->win) m |= 1UL << ts[i]->win->UserPort->mp_SigBit;
    return m;
}

int main(void)
{
    struct MsgPort *port, *tport = NULL;
    struct timerequest *tr = NULL;
    int tick = 0, quit = 0, timer = 0;
    ULONG waited = 0;                          /* ms since the last half-second's work */
    (void)version;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 39);
    UtilityBase = OpenLibrary((STRPTR)"utility.library", 39);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 41);   /* the bar's own colours; without it the bar's pen */
    if (!IntuitionBase || !GfxBase || !LayersBase || !UtilityBase) goto out;
    wbc_start();
    Forbid();
    port = FindPort((STRPTR)TB_PORT);
    Permit();
    if (port) { PutStr((STRPTR)"OpenTitle is already running\n"); goto out; }
    if (!(port = CreateMsgPort())) goto out;
    port->mp_Node.ln_Name = (char *)TB_PORT;
    port->mp_Node.ln_Pri = 0;
    AddPort(port);
    ts_reply = CreateMsgPort();
    if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof *tr)))
        timer = !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0);

    read_prefs();
    apply_title();
    if (!open_all()) quit = 1;
    while (!quit) {
        ULONG sig, ms;
        struct Window *act;
        /* a tenth of a second while the pointer is on the bar or the list is open (the cog lights up), else half */
        ms = menu_w || cog_drawn > 0 || hover_for >= 0 || (scr && IntuitionBase->FirstScreen == scr && scr->MouseY <= scr->BarHeight) ? 100 : 500;
        if (timer) {
            tr->tr_node.io_Command = TR_ADDREQUEST; tr->tr_time.tv_secs = 0; tr->tr_time.tv_micro = ms * 1000;
            SendIO((struct IORequest *)tr);
            sig = Wait(SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F | (1UL << tport->mp_SigBit) | (1UL << port->mp_SigBit)
                       | (ts_reply ? 1UL << ts_reply->mp_SigBit : 0) | window_sigs() | wbc_sigmask());
            /* Only a request that came back by itself counts as time waited. An
             * aborted one comes back too, and its reply sets the timer port's
             * signal after Wait() has returned; WaitIO() takes the reply but leaves
             * the signal. Left set, the next Wait() returned at once, its request
             * was aborted again, and OpenTitle went round without stopping: any
             * message woke it into that, a taskspace icon's first of all, and no
             * task below priority 0 ran again (OpenFTP's transfers, 8 Oct 2026). */
            {
                int done = CheckIO((struct IORequest *)tr) != NULL;
                if (!done) AbortIO((struct IORequest *)tr);
                WaitIO((struct IORequest *)tr);
                SetSignal(0, 1UL << tport->mp_SigBit);
                if (done) waited += ms;
            }
        } else {
            Delay(TICKS_PER_SECOND / 2);
            sig = SetSignal(0, SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F);
            waited += 500;
        }
        if (sig & SIGBREAKF_CTRL_C) break;
        /* Workbench is shutting down (Font prefs' Use, a screen mode): no window, no lock on its screen until it is back */
        switch (wbc_take()) {
        case WBC_CLOSE: close_all(); wb_down = 1; wbc_closed(); break;
        case WBC_OPEN:  wb_down = 2; break;
        }
        if (wb_down == 2 && open_all()) wb_down = 0;
        if (wb_down) { port_events(port); ts_replies(); continue; }
        if (sig & SIGBREAKF_CTRL_F) {
            close_all();
            read_prefs();
            apply_title();
            if (!prefs.logo && !prefs.memory && !prefs.clock && !prefs.network && !prefs.cog && !prefs.border_black && !ts_count()) break;
            if (!open_all()) break;
        }
        port_events(port);
        ts_replies();
        menu_events();
        events(logo_w);
        events(clock_w);
        events(net_w);
        events(cog_w);
        if (pop_w) events(pop_w);
        {
            int i;
            for (i = 0; i < TS_ICONS; i++) if (ts[i] && ts[i]->win) events(ts[i]->win);
        }
        if (scr) ts_hover();
        if (cog_skip && !over_cog()) cog_skip = 0;
        draw_cog(0);
        act = IntuitionBase->ActiveWindow;
        if (act && act != logo_w && act != clock_w && act != net_w && act != pop_w && act != cog_w && act != menu_w && !ts_is(act)) last_other = act;
        if (last_other && !is_window(last_other)) last_other = NULL;
        if (waited < 500) continue;
        waited = 0;                            /* every half second, as before */
        if (pop_w && ++pop_ticks > 20) pop_close();     /* the panel goes after 10 s */
        logo_follow();
        draw_clock(0);
        if (++tick % 4 == 0) {                 /* every 2 s: the tray and OpenMenus' bar may have moved */
            int edge = read_bar_edge();
            WORD ox = cx, oy = cy, ow = cw, oh = ch;
            if (edge != bar_edge && (clock_w || net_w || cog_w || ts_count())) { close_all(); if (!open_all()) break; }
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
                {
                    int i;
                    for (i = 0; i < TS_ICONS; i++) {
                        struct ts_icon *t = ts[i];
                        WORD px, py, pw, ph;
                        if (!t || !t->win) continue;
                        px = t->x; py = t->y; pw = t->bw; ph = t->bh;
                        ts_place(t);
                        if (t->x != px || t->y != py || t->bw != pw || t->bh != ph) {
                            if (help_for == i) help_close();
                            ChangeWindowBox(t->win, t->x, t->y, t->bw, t->bh);
                            tray(t->name, t->bw, t->order);
                            t->drawn = -1;
                        }
                    }
                }
                if (cog_w && !menu_w) {
                    WORD px = gx, py = gy, pw = gw, ph = gh;
                    cog_place();
                    if (gx != px || gy != py || gw != pw || gh != ph) {
                        ChangeWindowBox(cog_w, gx, gy, gw, gh);
                        tray("Cog", gw, COG_ORDER);
                        cog_drawn = -1;
                    }
                }
            }
            if (scr && bar_sample_now()) {        /* the theme changed the bar: paint again */
                int i;
                draw_logo();
                shown[0] = 0; draw_clock(1);
                net_drawn = -100; draw_net(1);
                cog_drawn = -1; draw_cog(1);
                for (i = 0; i < TS_ICONS; i++) if (ts[i]) ts_draw(ts[i], 1);
            }
        }
    }
    RemPort(port);                             /* no new icons from here on */
    port_events(port);
    ts_end();
    close_all();
    DeleteMsgPort(port);
    if (ts_reply && !ts_out) DeleteMsgPort(ts_reply);   /* a program that never replied keeps it: better a leak than a crash */
out:
    if (timer) CloseDevice((struct IORequest *)tr);
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    wbc_stop();
    if (WorkbenchBase) CloseLibrary(WorkbenchBase);
    if (SocketBase) CloseLibrary(SocketBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (LayersBase) CloseLibrary(LayersBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
