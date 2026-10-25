/* tb_fade.h: how solid OpenTitle's see-through bar is from moment to moment (OpenTitle 0.7.5).
 *
 * The user, 24 October 2026, 21:25: "make the title bar translucent until moused over then it goes its blue colour".
 * At rest the bar's background is at the rest level (tb_prefs' seethrough.opacity, 40% by default). While the bar
 * is hot (the pointer on it, or a menu open) it goes solid in about TBF_IN_MS. Once it is no longer hot it stays
 * solid for TBF_GRACE_MS, then goes back to the rest level in about TBF_OUT_MS. A change of mind half way turns
 * the fade round from where it is, with no jump.
 *
 * Plain C, no Amiga calls, so the host tests (TitleBar/tests) build it. A program includes it once.
 *
 *   tbf_init(&f, rest, hot)     the level at rest (0 clear to 255 solid); starts there, or solid when hot
 *   tbf_step(&f, dt, hot)       dt ms later: the level now
 *   tbf_next(&f)                ms until a step is due (0: none, so nothing need wake for the bar)
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef TB_FADE_H
#define TB_FADE_H

#define TBF_IN_MS    150                       /* rest to solid */
#define TBF_OUT_MS   300                       /* solid to rest */
#define TBF_GRACE_MS 300                       /* solid this long after it stops being hot */
#define TBF_STEP_MS   40                       /* one step of a fade: 25 a second */
#define TBF_DT_MAX  1000                       /* a longer gap (the task was held up) counts as this */

typedef struct {
    int level;                                 /* how solid the bar is now: rest to 255 */
    int rest;                                  /* the level at rest, 0 to 255 */
    int cool;                                  /* ms since it was last hot, up to TBF_GRACE_MS */
    int hot;                                   /* hot at the last step */
} tb_fade;

static __attribute__((unused)) void tbf_init(tb_fade *f, int rest, int hot)
{
    f->rest = rest < 0 ? 0 : rest > 255 ? 255 : rest;
    f->hot = hot != 0;
    f->level = f->hot ? 255 : f->rest;
    f->cool = f->hot ? 0 : TBF_GRACE_MS;
}

/* hot or not is seen only at the end of dt, so dt passes as the last step left it: a fade starts from the step that
 * sees the change (the first hot step doesn't count the wait before it as hot, nor the first cool one as cool) */
static __attribute__((unused)) int tbf_step(tb_fade *f, int dt, int hot)
{
    int span = 255 - f->rest, up = 0, down = 0;
    if (dt < 0) dt = 0;
    if (dt > TBF_DT_MAX) dt = TBF_DT_MAX;
    if (f->hot) up = dt;
    else {
        /* within the grace it keeps going to solid; the part of dt after the grace fades it out */
        int left = TBF_GRACE_MS - f->cool;
        up = dt < left ? dt : left;
        down = dt - up;
        f->cool = f->cool + dt > TBF_GRACE_MS ? TBF_GRACE_MS : f->cool + dt;
    }
    f->hot = hot != 0;
    if (f->hot) f->cool = 0;                   /* the grace starts again once it is no longer hot */
    if (span <= 0) return f->level = 255;
    /* rounded up, so every step moves */
    if (up > 0 && f->level < 255) {
        f->level += (span * up + TBF_IN_MS - 1) / TBF_IN_MS;
        if (f->level > 255) f->level = 255;
    }
    if (down > 0 && f->level > f->rest) {
        f->level -= (span * down + TBF_OUT_MS - 1) / TBF_OUT_MS;
        if (f->level < f->rest) f->level = f->rest;
    }
    return f->level;
}

static __attribute__((unused)) int tbf_next(const tb_fade *f)
{
    if (f->rest >= 255) return 0;                             /* solid at rest too: nothing ever changes */
    if (f->cool < TBF_GRACE_MS) {
        if (f->level < 255) return TBF_STEP_MS;               /* going solid */
        return f->hot ? 0 : TBF_GRACE_MS - f->cool;          /* solid: hot (the caller's own tick sees it leave), or the grace */
    }
    return f->level > f->rest ? TBF_STEP_MS : 0;              /* going back to rest, or at rest */
}

#endif
