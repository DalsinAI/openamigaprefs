/* pf_iff: an OS preferences file (IFF FORM PREF) as the list of its chunks, in file order, PRHD first.
 *
 * The Appearance tabs of OpenLook (Look/src/appearance.c) read ENV:Sys/<name>.prefs into a pf_file, change
 * the fields they know inside a chunk's bytes (pf_prefs.c) and write the list back, so a chunk or a byte no
 * tab knows is carried across as it came (combined prefs design: "carry across any chunk we don't
 * understand"). The list, parse and write are plain C and tested on the host (Look/tests/run.sh); loading
 * and saving on the Amiga go through iffparse.library (pf_amiga.c, pf_load and pf_save below).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef PF_IFF_H
#define PF_IFF_H

#include <stdint.h>

#define PF_ID(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define PF_FORM PF_ID('F', 'O', 'R', 'M')
#define PF_PREF PF_ID('P', 'R', 'E', 'F')
#define PF_PRHD PF_ID('P', 'R', 'H', 'D')
#define PF_MAX_CHUNKS 96          /* OS 3.2's pointer.prefs has 42 */
#define PF_MAX_BYTES 262144L      /* a file bigger than this is not one of the OS's prefs files */

typedef struct pf_chunk {
    uint32_t id;
    uint32_t len;
    uint8_t *data;                /* len bytes, the file's own (malloc'd; NULL when len is 0) */
} pf_chunk;

typedef struct pf_file {
    int n;
    pf_chunk ch[PF_MAX_CHUNKS];
} pf_file;

/* big-endian fields, as every OS prefs file keeps them */
uint16_t pf_get16(const uint8_t *p);
uint32_t pf_get32(const uint8_t *p);
void pf_put16(uint8_t *p, uint16_t v);
void pf_put32(uint8_t *p, uint32_t v);

void pf_init(pf_file *f);                                    /* empty: no chunks */
void pf_free(pf_file *f);                                    /* back to empty */
int pf_new(pf_file *f);                                      /* a new file: PRHD (version 0, type 0, flags 0) */
int pf_copy(pf_file *dst, const pf_file *src);               /* dst freed first; 1, or 0 with no memory */
int pf_find(const pf_file *f, uint32_t id, int nth);         /* the nth chunk with that ID (from 0), or -1 */
int pf_insert(pf_file *f, int at, uint32_t id, const void *data, uint32_t len);   /* 1, or 0 when full */
int pf_add(pf_file *f, uint32_t id, const void *data, uint32_t len);              /* at the end */
int pf_set(pf_file *f, int i, const void *data, uint32_t len);                    /* chunk i's bytes */
void pf_remove(pf_file *f, int i);
int pf_same(const pf_file *a, const pf_file *b);            /* the same chunks with the same bytes */

/* Bytes into the list: 1, or 0 when they aren't a whole FORM PREF (f is then empty). A FORM, LIST or CAT
 * inside the PREF is refused too, so a file this can't carry whole is never written back. */
int pf_parse(pf_file *f, const uint8_t *buf, long len);
long pf_size(const pf_file *f);                              /* the bytes pf_write writes */
long pf_write(const pf_file *f, uint8_t *out, long size);   /* bytes written, or -1 when out is too small */

#if defined(__amigaos__) || defined(AMIGA)
/* With iffparse.library (pf_amiga.c). pf_load: 1 read, 0 there is no such file, -1 it isn't a PREF file
 * this can carry whole (f is empty then). pf_save: 1 written. pf_differs: 1 when the file's bytes are not
 * what pf_save would write (or there is no file). The path's own spelling on the disk is kept: writing
 * ENVARC:Sys/font.prefs over Font.prefs keeps Font.prefs. */
int pf_open_lib(void);                                        /* iffparse.library; 1 when open */
void pf_close_lib(void);
int pf_load(pf_file *f, const char *path);
int pf_save(const pf_file *f, const char *path);
int pf_differs(const pf_file *f, const char *path);
#endif

#endif /* PF_IFF_H */
