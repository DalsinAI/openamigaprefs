/* sp_amiga: see sp_amiga.h. MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>

#include <string.h>

#include "sp_amiga.h"

struct ExpansionBase *ExpansionBase;

/* the board: protocol acahi-v1 (vendor/amigachrome-guest common/protocol/acahi.h) */
#define ACAHI_MANUFACTURER     0xDA15
#define ACAHI_MANUFACTURER_OLD 2011
#define ACAHI_PRODUCT          4
#define ACAHI_SIGNATURE        0x41434148UL
#define ACAHI_REG_ID           0x00
#define ACAHI_REG_CAPS         0x08
#define ACAHI_REG_LEVELS       0xA0
#define ACAHI_REG_LEVEL_SEQ    0xA4
#define ACAHI_CAP_LEVELS       (1UL << 18)
#define R(b, off) ((b)[(off) / 4])

volatile ULONG *sp_board(void)
{
    struct ConfigDev *cd = NULL;
    volatile ULONG *b = NULL;
    if (!(ExpansionBase = (struct ExpansionBase *)OpenLibrary((STRPTR)"expansion.library", 37))) return NULL;
    if (!(cd = FindConfigDev(NULL, ACAHI_MANUFACTURER, ACAHI_PRODUCT))) cd = FindConfigDev(NULL, ACAHI_MANUFACTURER_OLD, ACAHI_PRODUCT);
    if (cd) b = (volatile ULONG *)cd->cd_BoardAddr;
    CloseLibrary((struct Library *)ExpansionBase);
    ExpansionBase = NULL;
    if (b && (R(b, ACAHI_REG_ID) != ACAHI_SIGNATURE || !(R(b, ACAHI_REG_CAPS) & ACAHI_CAP_LEVELS))) b = NULL;   /* an older runtime: no levels */
    return b;
}

ULONG sp_board_levels(volatile ULONG *b) { return R(b, ACAHI_REG_LEVELS); }
ULONG sp_board_seq(volatile ULONG *b) { return R(b, ACAHI_REG_LEVEL_SEQ); }
void sp_board_set(volatile ULONG *b, ULONG levels) { R(b, ACAHI_REG_LEVELS) = levels; }

/* ---- files ---------------------------------------------------------------------------------- */

static UBYTE *read_file(const char *path, LONG *lenp, LONG max)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG len;
    UBYTE *buf;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len < 0 || len > max || !(buf = AllocVec(len + 1, MEMF_ANY | MEMF_CLEAR))) { Close(fh); return NULL; }
    if (Read(fh, buf, len) != len) { FreeVec(buf); Close(fh); return NULL; }
    Close(fh);
    if (lenp) *lenp = len;
    return buf;
}

static int write_file(const char *path, const void *data, LONG len)
{
    char dir[64], *slash;
    BPTR fh, lock;
    strncpy(dir, path, sizeof dir - 1); dir[sizeof dir - 1] = 0;
    if ((slash = strrchr(dir, '/'))) {
        *slash = 0;
        if ((lock = Lock((STRPTR)dir, ACCESS_READ))) UnLock(lock);
        else if ((lock = CreateDir((STRPTR)dir))) UnLock(lock);
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) return 0;
    Write(fh, (APTR)data, len);
    Close(fh);
    return 1;
}

void sp_load(sp_settings *s, const char *from)
{
    UBYTE *d;
    LONG len = 0;
    volatile ULONG *b;
    sp_defaults(s);
    if ((d = read_file(from ? from : SP_ENV, NULL, 16 * 1024))) { sp_parse(s, (const char *)d); FreeVec(d); }
    if ((d = read_file(SP_SOUND_ENV, &len, 4096))) { sp_read_sound_prefs(s, d, len); FreeVec(d); }
    if ((d = read_file(SP_AHI_ENV, &len, SP_AHI_MAX))) { sp_read_ahi_prefs(s, d, len); FreeVec(d); }
    if (s->have_ahi_prefs) s->mix = sp_mix_of(s);
    if (!from && (b = sp_board())) sp_from_levels_word(s, sp_board_levels(b));   /* what the PC's mixer page set counts */
}

void sp_store(sp_settings *s, int save)
{
    char text[512];
    UBYTE snd[SP_SOUND_PREFS_SIZE];
    UBYTE *old, *out;
    LONG oldlen = 0, n;
    int board = sp_board() != NULL;
    if ((n = sp_write(s, text, sizeof text)) > 0) {
        write_file(SP_ENV, text, n);
        if (save) write_file(SP_ENVARC, text, n);
    }
    n = sp_write_sound_prefs(s, snd);
    write_file(SP_SOUND_ENV, snd, n);
    if (save) write_file(SP_SOUND_ARC, snd, n);
    if (board) sp_apply_mix(s);
    else sp_volume_to_units(s);
    if (!board && !s->have_ahi_prefs) return;                          /* no AHI settings to change */
    if (!(out = AllocVec(SP_AHI_MAX, MEMF_ANY))) return;
    old = read_file(SP_AHI_ENV, &oldlen, SP_AHI_MAX);
    if ((n = sp_write_ahi_prefs(s, old, oldlen, out, SP_AHI_MAX)) > 0) {
        write_file(SP_AHI_ENV, out, n);
        if (save) write_file(SP_AHI_ARC, out, n);
    }
    if (old) FreeVec(old);
    FreeVec(out);
}

void sp_apply_levels(sp_settings *s, volatile ULONG *board)
{
    if (board) { sp_board_set(board, sp_levels_word(s)); return; }
    sp_store(s, 0);                                                    /* AHI's output volume, in ENV: */
}

void sp_save_levels(const sp_settings *s, int save)
{
    sp_settings f;
    char text[512];
    UBYTE *d;
    LONG n;
    sp_defaults(&f);
    if ((d = read_file(SP_ENV, NULL, 16 * 1024))) { sp_parse(&f, (const char *)d); FreeVec(d); }
    f.volume = s->volume; f.paula = s->paula; f.ahi = s->ahi; f.muted = s->muted;
    if ((n = sp_write(&f, text, sizeof text)) > 0) {
        write_file(SP_ENV, text, n);
        if (save) write_file(SP_ENVARC, text, n);
    }
}

void sp_start_speaker(void)
{
    BPTR in, out;
    struct MsgPort *p;
    Forbid();
    p = FindPort((STRPTR)SP_SPEAKER_PORT);
    Permit();
    if (p) return;
    if (!(in = Open((STRPTR)"NIL:", MODE_OLDFILE))) return;
    if (!(out = Open((STRPTR)"NIL:", MODE_NEWFILE))) { Close(in); return; }
    if (SystemTags((STRPTR)SP_SPEAKER_CMD, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, NP_StackSize, 8192, TAG_DONE) == -1) {
        Close(in); Close(out);
    }
}

void sp_tell_speaker(void)
{
    struct MsgPort *p;
    Forbid();
    if ((p = FindPort((STRPTR)SP_SPEAKER_PORT)) && p->mp_SigTask) Signal((struct Task *)p->mp_SigTask, SIGBREAKF_CTRL_F);
    Permit();
}
