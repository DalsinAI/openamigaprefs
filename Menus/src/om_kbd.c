/* OpenMenus' keyboard control (om_kbd.h): the keys of Menus/DESIGN.md and
 * the design note KEYBOARD_CONTROL.md, on a model of the copied strip.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "om_kbd.h"

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

int omk_first_menu(const omk_model *m)
{
    for (int i = 0; i < m->nmenus; i++)
        if (m->menus[i].enabled) return i;
    return 0;
}

/* The list the highlight is in (items or subitems), and whether its entries can be chosen. */
static const omk_item *list_of(const omk_model *m, const omk_pos *p, int *n, int *usable)
{
    const omk_menu *mn = &m->menus[p->menu];
    *usable = mn->enabled;
    if (p->sub >= 0) {
        const omk_item *it = &mn->items[p->item];
        *usable = *usable && it->enabled;
        *n = it->nsubs;
        return it->subs;
    }
    *n = mn->nitems;
    return mn->items;
}

static int can_choose(const omk_item *items, int i, int usable)
{
    return usable && items[i].enabled;
}

/* The next entry that can be chosen from 'from' in direction 'dir' (wrapping),
 * or -1. from -1 with dir 1 starts at the first, with dir -1 at the last. */
static int step(const omk_item *items, int n, int usable, int from, int dir)
{
    if (n <= 0 || !usable) return -1;
    for (int k = 1; k <= n; k++) {
        int i = from < 0 ? (dir > 0 ? k - 1 : n - k) : ((from + dir * k) % n + n) % n;
        if (can_choose(items, i, usable)) return i;
    }
    return -1;
}

static int title_step(const omk_model *m, int from, int dir)
{
    if (m->nmenus <= 0) return from;
    return ((from + dir) % m->nmenus + m->nmenus) % m->nmenus;
}

/* Moves within the list the highlight is in; from the titles, into the items. */
static int move_in_list(const omk_model *m, omk_pos *p, int dir, int from_end)
{
    int n, usable, i;
    const omk_item *items;
    if (p->item < 0) {                       /* on the title: into its items */
        omk_pos q = *p;
        q.item = 0; q.sub = -1;
        items = list_of(m, &q, &n, &usable);
        i = step(items, n, usable, -1, dir);
        if (i < 0) return OMK_NONE;
        p->item = i;
        return OMK_MOVED;
    }
    items = list_of(m, p, &n, &usable);
    i = step(items, n, usable, from_end ? -1 : (p->sub >= 0 ? p->sub : p->item), dir);
    if (i < 0) return OMK_NONE;
    if (p->sub >= 0) { if (i == p->sub) return OMK_NONE; p->sub = i; }
    else { if (i == p->item) return OMK_NONE; p->item = i; }
    return OMK_MOVED;
}

static int has_sub(const omk_model *m, const omk_pos *p)
{
    const omk_menu *mn = &m->menus[p->menu];
    return p->item >= 0 && p->sub < 0 && mn->enabled && mn->items[p->item].enabled && mn->items[p->item].nsubs > 0;
}

static int open_sub(const omk_model *m, omk_pos *p)
{
    const omk_item *it = &m->menus[p->menu].items[p->item];
    int i = step(it->subs, it->nsubs, 1, -1, 1);
    if (i < 0) return OMK_NONE;
    p->sub = i;
    return OMK_OPEN_SUB;
}

static int other_title(const omk_model *m, omk_pos *p, int dir)
{
    int t = title_step(m, p->menu, dir);
    if (t == p->menu && p->item < 0) return OMK_NONE;
    p->menu = t; p->item = -1; p->sub = -1;
    return OMK_MOVED;
}

static int type_ahead(const omk_model *m, omk_pos *p, int ch)
{
    omk_pos q = *p;
    int n, usable, from;
    const omk_item *items;
    if (q.item < 0) q.item = 0, q.sub = -1, from = -1;
    else from = q.sub >= 0 ? q.sub : q.item;
    items = list_of(m, &q, &n, &usable);
    if (!usable) return OMK_NONE;
    for (int k = 1; k <= n; k++) {
        int i = from < 0 ? k - 1 : (from + k) % n;
        if (can_choose(items, i, usable) && items[i].name && lower((unsigned char)items[i].name[0]) == lower(ch)) {
            if (q.sub >= 0) q.sub = i; else q.item = i;
            if (q.menu == p->menu && q.item == p->item && q.sub == p->sub) return OMK_NONE;
            *p = q;
            return OMK_MOVED;
        }
    }
    return OMK_NONE;
}

/* Right Amiga + a letter: the item with that command key, anywhere in the strip. */
static int command(const omk_model *m, omk_pos *p, int ch)
{
    for (int mi = 0; mi < m->nmenus; mi++) {
        const omk_menu *mn = &m->menus[mi];
        if (!mn->enabled) continue;
        for (int ii = 0; ii < mn->nitems; ii++) {
            const omk_item *it = &mn->items[ii];
            if (!it->enabled) continue;
            if (it->command && lower((unsigned char)it->command) == lower(ch) && !it->nsubs) {
                p->menu = mi; p->item = ii; p->sub = -1;
                return OMK_CHOOSE;
            }
            for (int si = 0; si < it->nsubs; si++)
                if (it->subs[si].enabled && it->subs[si].command && lower((unsigned char)it->subs[si].command) == lower(ch)) {
                    p->menu = mi; p->item = ii; p->sub = si;
                    return OMK_CHOOSE;
                }
        }
    }
    return OMK_NONE;
}

/* The entry the highlight is on, when it can be chosen. */
static const omk_item *current(const omk_model *m, const omk_pos *p)
{
    int n, usable, i = p->sub >= 0 ? p->sub : p->item;
    const omk_item *items;
    if (p->item < 0) return 0;
    items = list_of(m, p, &n, &usable);
    return i < n && can_choose(items, i, usable) ? &items[i] : 0;
}

int omk_key(const omk_model *m, omk_pos *p, int key, int ch)
{
    const omk_item *it;
    if (!m || m->nmenus <= 0 || p->menu < 0 || p->menu >= m->nmenus) return OMK_CLOSE_ALL;
    switch (key) {
    case OMK_LEFT:
        if (p->sub >= 0) { p->sub = -1; return OMK_CLOSE_LEVEL; }
        return other_title(m, p, -1);
    case OMK_RIGHT:
        if (has_sub(m, p)) return open_sub(m, p);
        if (p->sub >= 0) return OMK_NONE;
        return other_title(m, p, 1);
    case OMK_TAB:
        if (p->sub >= 0) return OMK_NONE;
        return other_title(m, p, 1);
    case OMK_DOWN: return move_in_list(m, p, 1, 0);
    case OMK_UP: return move_in_list(m, p, -1, 0);
    case OMK_FIRST: return move_in_list(m, p, 1, 1);
    case OMK_LAST: return move_in_list(m, p, -1, 1);
    case OMK_RETURN:
        if (p->item < 0) return move_in_list(m, p, 1, 0);
        if (has_sub(m, p)) return open_sub(m, p);
        return current(m, p) ? OMK_CHOOSE : OMK_NONE;
    case OMK_SPACE:
        it = current(m, p);
        return it && it->checkit && !it->nsubs ? OMK_CHOOSE_STAY : OMK_NONE;
    case OMK_ESC:
        if (p->sub >= 0) { p->sub = -1; return OMK_CLOSE_LEVEL; }
        if (p->item >= 0) { p->item = -1; return OMK_CLOSE_LEVEL; }
        return OMK_CLOSE_ALL;
    case OMK_HELP:
        return p->item >= 0 ? OMK_HELP_ITEM : OMK_NONE;
    case OMK_CHAR:
        return ch ? type_ahead(m, p, ch) : OMK_NONE;
    case OMK_COMMAND:
        return ch ? command(m, p, ch) : OMK_NONE;
    }
    return OMK_NONE;
}
