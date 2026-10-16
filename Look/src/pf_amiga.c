/* pf_amiga: loading and saving a pf_file (pf_iff.h) with iffparse.library, as the OS's own editors do.
 *
 * pf_load steps through the file with ParseIFF(IFFPARSE_RAWSTEP), reads each chunk of the FORM PREF with
 * ReadChunkBytes() and keeps it in file order; a FORM, LIST or CAT inside it makes the file one this can't
 * carry whole, and it is refused. pf_save writes FORM PREF and every chunk with PushChunk(),
 * WriteChunkBytes() and PopChunk() (which pads an odd chunk). A file that is there already is written under
 * its own name (Font.prefs stays Font.prefs on a disk that keeps the case it was given).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>

#include <stdlib.h>
#include <string.h>

#include "pf_iff.h"

struct Library *IFFParseBase;

int pf_open_lib(void)
{
    if (!IFFParseBase) IFFParseBase = OpenLibrary((CONST_STRPTR)"iffparse.library", 39);
    return IFFParseBase != NULL;
}

void pf_close_lib(void)
{
    if (IFFParseBase) CloseLibrary(IFFParseBase);
    IFFParseBase = NULL;
}

/* path with its last part spelt as the file on the disk spells it, when it is there (out, size bytes); else
 * path itself. The drawer part stays as given (ENVARC:Sys/), only the name's case can change. */
static const char *own_name(const char *path, char *out, int size)
{
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    char full[256];
    int ok = 0;
    if (lock) {
        if (NameFromLock(lock, (STRPTR)full, sizeof full)) {
            const char *mine = (const char *)FilePart((CONST_STRPTR)path), *theirs = (const char *)FilePart((CONST_STRPTR)full);
            int dir = (int)(mine - path);
            if (dir + (int)strlen(theirs) < size && strlen(theirs) == strlen(mine)) {
                memcpy(out, path, (size_t)dir);
                strcpy(out + dir, theirs);
                ok = 1;
            }
        }
        UnLock(lock);
    }
    return ok ? out : path;
}

int pf_load(pf_file *f, const char *path)
{
    struct IFFHandle *iff;
    BPTR fh;
    int rc = -1, top = 0;
    pf_free(f);
    if (!pf_open_lib()) return -1;
    if (!(fh = Open((CONST_STRPTR)path, MODE_OLDFILE))) return 0;
    if (!(iff = AllocIFF())) { Close(fh); return -1; }
    iff->iff_Stream = (ULONG)fh;
    InitIFFasDOS(iff);
    if (!OpenIFF(iff, IFFF_READ)) {
        for (;;) {
            LONG err = ParseIFF(iff, IFFPARSE_RAWSTEP);
            struct ContextNode *cn;
            if (err == IFFERR_EOC) continue;
            if (err == IFFERR_EOF) { rc = top ? 1 : -1; break; }
            if (err || !(cn = CurrentChunk(iff))) break;
            if (cn->cn_ID == ID_FORM || cn->cn_ID == ID_LIST || cn->cn_ID == ID_CAT || cn->cn_ID == ID_PROP) {
                if (top || cn->cn_ID != ID_FORM || cn->cn_Type != PF_PREF) break;   /* only one FORM PREF, nothing inside */
                top = 1;
                continue;
            }
            if (!top || cn->cn_Size < 0 || cn->cn_Size > PF_MAX_BYTES) break;
            {
                LONG n = cn->cn_Size;
                UBYTE *buf = n ? malloc((size_t)n) : NULL;
                if (n && !buf) break;
                if (n && ReadChunkBytes(iff, buf, n) != n) { free(buf); break; }
                if (!pf_add(f, (uint32_t)cn->cn_ID, buf, (uint32_t)n)) { free(buf); break; }
                free(buf);
            }
        }
        CloseIFF(iff);
    }
    FreeIFF(iff);
    Close(fh);
    if (rc != 1) pf_free(f);
    return rc;
}

int pf_save(const pf_file *f, const char *path)
{
    struct IFFHandle *iff;
    char name[256];
    BPTR fh;
    int ok = 0, i;
    if (!pf_open_lib()) return 0;
    if (!(fh = Open((CONST_STRPTR)own_name(path, name, sizeof name), MODE_NEWFILE))) return 0;
    if (!(iff = AllocIFF())) { Close(fh); return 0; }
    iff->iff_Stream = (ULONG)fh;
    InitIFFasDOS(iff);
    if (!OpenIFF(iff, IFFF_WRITE)) {
        if (!PushChunk(iff, (LONG)PF_PREF, ID_FORM, IFFSIZE_UNKNOWN)) {
            ok = 1;
            for (i = 0; i < f->n && ok; i++) {
                const pf_chunk *c = &f->ch[i];
                ok = !PushChunk(iff, 0, (LONG)c->id, (LONG)c->len) &&
                     (!c->len || WriteChunkBytes(iff, c->data, (LONG)c->len) == (LONG)c->len) &&
                     !PopChunk(iff);
            }
            if (PopChunk(iff)) ok = 0;
        }
        CloseIFF(iff);
    }
    FreeIFF(iff);
    Close(fh);
    return ok;
}

int pf_differs(const pf_file *f, const char *path)
{
    long need = pf_size(f), len;
    UBYTE *mine, *theirs;
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    int differs = 1;
    if (!fh) return 1;
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len == need && (mine = malloc((size_t)need)) != NULL) {
        if ((theirs = malloc((size_t)need)) != NULL) {
            if (pf_write(f, mine, need) == need && Read(fh, theirs, need) == need) differs = memcmp(mine, theirs, (size_t)need) != 0;
            free(theirs);
        }
        free(mine);
    }
    Close(fh);
    return differs;
}
