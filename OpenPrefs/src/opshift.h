/* opshift.h: what each page of OpenPrefs is compiled with (gcc -include, before its own source), so that the
 * editor it was runs as a page of the one window without changing its layout code:
 *
 *   - its gadgets and its drawing in the window stand lt_dx to the right, beside the list of sections:
 *     CreateGadget(A) while no page is in the window (the page's own gadgets, made before lt_open), and Move,
 *     Draw, RectFill, EraseRect, WritePixel, DrawEllipse, DrawBevelBox, PrintIText and DrawImage into the
 *     window's RastPort while a page is in it (a window of the page's own is left as it is);
 *   - ReadArgs reads lt_args when OpenPrefs gives a page its arguments (OpenPrefs SOUND USE, APPLY);
 *   - LockPubScreen(NULL) is the screen OpenPrefs was asked for (PUBSCREEN), else the default;
 *   - Open, CreateDir and PutDiskObject on ENV: and ENVARC: make the folders a save needs first (Dale,
 *     17 October 2026, 09:42; opframe.c lt_ready);
 *   - GT_GetIMsg tells the frame when the page's own gadgets or keys were used (lt_dirty).
 *
 * The protos are included here first, so each page's own #include of them is empty, and the calls are
 * redefined on the NDK's inline __X_base forms.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPSHIFT_H
#define OPSHIFT_H

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/icon.h>

extern struct Window *lt_win;
extern struct RastPort *lt_rp;
extern int lt_dx;
extern const char *lt_args;                /* the arguments a page's ReadArgs reads, ending in "\n"; NULL: its own */
extern const char *lt_pubname;             /* PUBSCREEN, or NULL */
int lt_ready(const char *path, int isdir); /* 1: write it to NIL: (ENVARC: was refused), else 0 */
void lt_seen(struct IntuiMessage *m);
struct RDArgs *lt_readargs(CONST_STRPTR tmpl, LONG *array);   /* lt_args through ReadArgs */
void lt_freeargs(struct RDArgs *rd);

#define LT_X(rp, x) ((x) + ((struct RastPort *)(rp) == lt_rp && lt_rp ? lt_dx : 0))

/* ---- the page's gadgets: right of the list ---- */
static inline __attribute__((unused)) struct Gadget *lt_cga(ULONG kind, struct Gadget *prev, struct NewGadget *ng,
                                                            CONST struct TagItem *tags)
{
    struct Gadget *g;
    WORD dx = lt_rp ? 0 : (WORD)lt_dx;
    ng->ng_LeftEdge += dx;
    g = __CreateGadgetA_base(GADTOOLS_BASE_NAME, kind, prev, ng, tags);
    ng->ng_LeftEdge -= dx;
    return g;
}
static __stdargs __attribute__((unused)) struct Gadget *lt_cgv(ULONG kind, struct Gadget *prev, struct NewGadget *ng,
                                                               Tag tag1, ...)
{
    return lt_cga(kind, prev, ng, (CONST struct TagItem *)&tag1);
}
#undef CreateGadgetA
#define CreateGadgetA(k, p, ng, t) lt_cga((k), (p), (ng), (t))
#undef CreateGadget
#define CreateGadget(...) lt_cgv(__VA_ARGS__)

/* ---- the page's drawing in the window: right of the list ---- */
#undef Move
#define Move(rp, x, y) __Move_base((GRAPHICS_BASE_NAME), (rp), LT_X(rp, x), (y))
#undef Draw
#define Draw(rp, x, y) __Draw_base((GRAPHICS_BASE_NAME), (rp), LT_X(rp, x), (y))
#undef RectFill
#define RectFill(rp, x0, y0, x1, y1) __RectFill_base((GRAPHICS_BASE_NAME), (rp), LT_X(rp, x0), (y0), LT_X(rp, x1), (y1))
#undef EraseRect
#define EraseRect(rp, x0, y0, x1, y1) __EraseRect_base((GRAPHICS_BASE_NAME), (rp), LT_X(rp, x0), (y0), LT_X(rp, x1), (y1))
#undef WritePixel
#define WritePixel(rp, x, y) __WritePixel_base((GRAPHICS_BASE_NAME), (rp), LT_X(rp, x), (y))
#undef DrawEllipse
#define DrawEllipse(rp, x, y, a, b) __DrawEllipse_base((GRAPHICS_BASE_NAME), (rp), LT_X(rp, x), (y), (a), (b))
#undef PrintIText
#define PrintIText(rp, it, x, y) __PrintIText_base((INTUITION_BASE_NAME), (rp), (it), LT_X(rp, x), (y))
#undef DrawImage
#define DrawImage(rp, im, x, y) __DrawImage_base((INTUITION_BASE_NAME), (rp), (im), LT_X(rp, x), (y))
static __stdargs __attribute__((unused)) void lt_bbv(struct RastPort *rp, LONG x, LONG y, LONG w, LONG h, Tag tag1, ...)
{
    __DrawBevelBoxA_base(GADTOOLS_BASE_NAME, rp, LT_X(rp, x), y, w, h, (CONST struct TagItem *)&tag1);
}
#undef DrawBevelBoxA
#define DrawBevelBoxA(rp, x, y, w, h, t) __DrawBevelBoxA_base((GADTOOLS_BASE_NAME), (rp), LT_X(rp, x), (y), (w), (h), (t))
#undef DrawBevelBox
#define DrawBevelBox(...) lt_bbv(__VA_ARGS__)

/* ---- the page's arguments and screen ---- */
static inline __attribute__((unused)) struct RDArgs *lt_ra(CONST_STRPTR tmpl, LONG *array, struct RDArgs *rd)
{
    if (lt_args && !rd) return lt_readargs(tmpl, array);
    return __ReadArgs_base(DOS_BASE_NAME, tmpl, array, rd);
}
#undef ReadArgs
#define ReadArgs(t, a, r) lt_ra((CONST_STRPTR)(t), (a), (r))
#undef FreeArgs
#define FreeArgs(rd) lt_freeargs(rd)

static inline __attribute__((unused)) struct Screen *lt_lps(CONST_STRPTR name)
{
    struct Screen *s;
    if (!name && lt_pubname && (s = __LockPubScreen_base(INTUITION_BASE_NAME, (CONST_STRPTR)lt_pubname))) return s;
    return __LockPubScreen_base(INTUITION_BASE_NAME, name);
}
#undef LockPubScreen
#define LockPubScreen(n) lt_lps((CONST_STRPTR)(n))

/* ---- ENV: and ENVARC: folders ---- */
static inline __attribute__((unused)) BPTR lt_open_file(CONST_STRPTR name, LONG mode)
{
    if (mode != MODE_OLDFILE && name && lt_ready((const char *)name, 0)) name = (CONST_STRPTR)"NIL:";
    return __Open_base(DOS_BASE_NAME, name, mode);
}
#undef Open
#define Open(n, m) lt_open_file((CONST_STRPTR)(n), (m))
static inline __attribute__((unused)) BPTR lt_mkdir(CONST_STRPTR name)
{
    if (name && lt_ready((const char *)name, 1)) return 0;
    return __CreateDir_base(DOS_BASE_NAME, name);
}
#undef CreateDir
#define CreateDir(n) lt_mkdir((CONST_STRPTR)(n))
#undef PutDiskObject
#define PutDiskObject(n, d) (lt_ready((const char *)(n), 0) ? TRUE : __PutDiskObject_base((ICON_BASE_NAME), (n), (d)))

/* ---- what the page's own gadgets and keys did ---- */
static inline __attribute__((unused)) struct IntuiMessage *lt_getimsg(struct MsgPort *port)
{
    struct IntuiMessage *m = __GT_GetIMsg_base(GADTOOLS_BASE_NAME, port);
    if (m && lt_rp && m->IDCMPWindow == lt_win) lt_seen(m);
    return m;
}
#undef GT_GetIMsg
#define GT_GetIMsg(p) lt_getimsg(p)

#endif /* OPSHIFT_H */
