/* ic_prefs: icontrol.prefs, the fields the IControl section edits (ic_prefs.h). The offsets are those of
 * OpenNDK's struct IControlPrefs, big-endian and unpadded. Plain C: tested on the host by
 * IControl/tests/run.sh.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "ic_prefs.h"

#include <stdlib.h>
#include <string.h>

/* With no file: a second's verify time-out, the left Amiga key to drag screens, Amiga-N, -M, -V and -B,
 * flicker avoided, colours kept, the text gadget filter and menu snap on. Not yet compared with the bytes
 * OS 3.2.3's IControl editor saves from its own defaults: a lab run settles them. */
void ic_defaults(ic_prefs *p)
{
    p->timeout = 50;
    p->metadrag = IC_Q_LCOMMAND;
    p->flags = IC_COERCE_COLORS | IC_COERCE_LACE | IC_STRGAD_FILTER | IC_MENUSNAP;
    p->keys[0] = 'N';
    p->keys[1] = 'M';
    p->keys[2] = 'V';
    p->keys[3] = 'B';
}

int ic_get(const pf_file *f, ic_prefs *out)
{
    int i = pf_find(f, IC_ICTL, 0);
    const uint8_t *d;
    ic_defaults(out);
    if (i < 0 || f->ch[i].len < IC_ICTL_LEN) return 0;
    d = f->ch[i].data;
    out->timeout = pf_get16(d + 16);
    out->metadrag = (int16_t)pf_get16(d + 18);
    out->flags = pf_get32(d + 20);
    memcpy(out->keys, d + 24, 4);
    return 1;
}

int ic_put(pf_file *f, const ic_prefs *in)
{
    int i = pf_find(f, IC_ICTL, 0), ok;
    uint32_t have = i >= 0 ? f->ch[i].len : 0, n = have > IC_ICTL_LEN ? have : IC_ICTL_LEN;
    uint8_t *p = malloc(n);
    if (!p) return 0;
    memset(p, 0, n);
    if (have) memcpy(p, f->ch[i].data, have);
    pf_put16(p + 16, (uint16_t)in->timeout);
    pf_put16(p + 18, (uint16_t)in->metadrag);
    pf_put32(p + 20, in->flags);
    memcpy(p + 24, in->keys, 4);
    ok = i >= 0 ? pf_set(f, i, p, n) : pf_add(f, IC_ICTL, p, n);
    free(p);
    return ok;
}
