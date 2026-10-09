/* OpenTypes: which program opens each kind of file (DESIGN.md).
 *
 *   OpenTypes                         the window
 *   OpenTypes LIST                    the kinds of file and their programs
 *   OpenTypes SET <type> TO <program> [SAVE]
 *   OpenTypes SCAN <type> IN <drawer> [APPLY]
 *   OpenTypes TYPE <file>             what kind of file it is
 *   OpenTypes UNDO                    puts back every icon OpenTypes changed
 *   OpenTypes SWITCH <program> TO <program> [SAVE] [IN <drawer>]
 *                                     every kind of file the first program
 *                                     opens goes to the second, sounds and
 *                                     fonts aside; with IN, icons there too
 *
 * - The kinds of file are the OS's default icons for projects
 *   (ENVARC:Sys/def_<type>.info): a file without an icon gets one, and its
 *   default tool opens the file. OpenTypes changes that default tool, in ENV:
 *   (Use) or ENV: and ENVARC: (Save).
 * - Icons files already have that still name the type's old program are
 *   found (SCAN) and, when asked, changed too. A file's kind comes from
 *   datatypes.library, as Workbench's own DefIcons finds it.
 * - Never a silent change: every icon is copied to ENVARC:OpenTypes/Backup
 *   before it changes and listed in ENVARC:OpenTypes/Changes; UNDO puts them
 *   all back. Icons are read and written with icon.library only.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/dosasl.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/icon.h>
#include <proto/datatypes.h>
#include <proto/graphics.h>
#include <proto/utility.h>

#include <string.h>

static const char version[] __attribute__((used)) = "$VER: OpenTypes 0.2 (6.10.2026) MIT, Copyright (c) 2026 Dalsin Limited";

#define STATE "ENVARC:OpenTypes"
#define BACKUP "ENVARC:OpenTypes/Backup"
#define CHANGES "ENVARC:OpenTypes/Changes"

struct Library *IconBase, *DataTypesBase, *AslBase, *GadToolsBase;

/* ---- the kinds of file --------------------------------------------------------- */

#define MAXTYPES 64
typedef struct {
    struct Node node;            /* for the window's list */
    char name[32];
    char tool[200];              /* what opens it now */
    char pick[200];              /* what the window has chosen */
    char label[260];
    int switched;                /* SWITCH changed it */
} ftype;
static ftype types[MAXTYPES];
static int ntypes;

static ftype *find_type(const char *name)
{
    int i;
    for (i = 0; i < ntypes; i++)
        if (!Stricmp((STRPTR)types[i].name, (STRPTR)name))
            return &types[i];
    return NULL;
}

/* The programs we know open a type; shown only where they are installed. */
static const struct { const char *types, *label, *tool; } catalogue[] = {
    { " picture iff anim pdf officedocs ", "OpenView", "C:OpenView" },
    { " picture iff anim pdf officedocs amigaguide font sound music video ", "MultiView", "SYS:Utilities/MultiView" },
    { " ascii asm c cpp h i rexx script src ", "TextEdit", "SYS:Tools/TextEdit" },
    { " ascii asm c cpp h i rexx script src ", "Ed", "C:Ed" },
    { " install ", "Installer", "C:Installer" },
    { " install ", "Installer", "SYS:System/Installer" },
    { " adf diskarchive ", "DAControl", "C:DAControl" },
    { " prefs ", "Prefs", "SYS:Prefs/Pointer" },
};
#define NCAT (int)(sizeof catalogue / sizeof catalogue[0])

static int exists(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (l) UnLock(l);
    return l != 0;
}

static int load_types(void)
{
    struct AnchorPath *ap = AllocVec(sizeof *ap + 256, MEMF_CLEAR);
    LONG err;
    ntypes = 0;
    if (!ap)
        return 0;
    ap->ap_Strlen = 256;
    for (err = MatchFirst((STRPTR)"ENVARC:Sys/def_#?.info", ap); !err && ntypes < MAXTYPES; err = MatchNext(ap)) {
        char path[300], name[64];
        struct DiskObject *dob;
        int n;
        strcpy(name, (char *)ap->ap_Info.fib_FileName + 4);         /* after "def_" */
        n = strlen(name);
        if (n < 6) continue;
        name[n - 5] = 0;                                            /* before ".info" */
        strcpy(path, "ENVARC:Sys/def_");
        strcat(path, name);
        if (!(dob = GetDiskObject((STRPTR)path)))
            continue;
        if (dob->do_Type == WBPROJECT) {
            ftype *t = &types[ntypes++];
            memset(t, 0, sizeof *t);
            strncpy(t->name, name, sizeof t->name - 1);
            if (dob->do_DefaultTool)
                strncpy(t->tool, (char *)dob->do_DefaultTool, sizeof t->tool - 1);
            strcpy(t->pick, t->tool);
        }
        FreeDiskObject(dob);
    }
    MatchEnd(ap);
    FreeVec(ap);
    {   /* by name */
        int i, j;
        for (i = 0; i < ntypes; i++)
            for (j = i + 1; j < ntypes; j++)
                if (Stricmp((STRPTR)types[j].name, (STRPTR)types[i].name) < 0) {
                    ftype t = types[i]; types[i] = types[j]; types[j] = t;
                }
    }
    return ntypes;
}

/* A program's name for comparing: the part after the last ':' or '/'. */
static const char *base(const char *tool)
{
    return (const char *)FilePart((STRPTR)tool);
}

/* ---- files, the backup and the list of changes ------------------------------------ */

/* Big buffers are static, never on the stack: commands run from a script
 * get 4 KB of stack, and Undo overflowed it (5 Oct 2026). */
static UBYTE copy_buf[2048];

static int copy_file(const char *from, const char *to)
{
    BPTR in = Open((STRPTR)from, MODE_OLDFILE), out;
    UBYTE *buf = copy_buf;
    LONG n;
    int ok = 1;
    if (!in)
        return 0;
    if (!(out = Open((STRPTR)to, MODE_NEWFILE))) {
        Close(in);
        return 0;
    }
    while ((n = Read(in, buf, sizeof copy_buf)) > 0)
        if (Write(out, buf, n) != n) { ok = 0; break; }
    if (n < 0) ok = 0;
    Close(out);
    Close(in);
    return ok;
}

static void make_state_dirs(void)
{
    BPTR l;
    if (!exists(STATE) && (l = CreateDir((STRPTR)STATE))) UnLock(l);
    if (!exists(BACKUP) && (l = CreateDir((STRPTR)BACKUP))) UnLock(l);
}

static int changes_count(void)
{
    static UBYTE buf[512];
    BPTR fh = Open((STRPTR)CHANGES, MODE_OLDFILE);
    LONG n;
    int lines = 0, i;
    if (!fh) return 0;
    while ((n = Read(fh, buf, sizeof buf)) > 0)
        for (i = 0; i < n; i++) if (buf[i] == '\n') lines++;
    Close(fh);
    return lines;
}

static void log_change(const char *icon, const char *backup)
{
    BPTR fh;
    if (!(fh = Open((STRPTR)CHANGES, MODE_READWRITE)))
        return;
    Seek(fh, 0, OFFSET_END);
    FPuts(fh, (STRPTR)icon);
    FPutC(fh, '\t');
    FPuts(fh, (STRPTR)backup);
    FPutC(fh, '\n');
    Close(fh);
}

/* 1 when this icon file is already listed (its original is kept). */
static int already_kept(const char *icon)
{
    static char line[512];
    BPTR fh = Open((STRPTR)CHANGES, MODE_OLDFILE);
    int found = 0;
    if (!fh) return 0;
    while (FGets(fh, (STRPTR)line, sizeof line)) {
        char *tab = strchr(line, '\t');
        if (tab) { *tab = 0; if (!Stricmp((STRPTR)line, (STRPTR)icon)) { found = 1; break; } }
    }
    Close(fh);
    return found;
}

/* Keeps the icon file's original, once, before OpenTypes changes it. */
static int keep_original(const char *icon_file)
{
    char backup[64];
    if (already_kept(icon_file))
        return 1;
    make_state_dirs();
    strcpy(backup, BACKUP "/");
    {
        int n = changes_count() + 1, k = strlen(backup), t;
        char digits[8];
        for (t = 0; n; n /= 10) digits[t++] = '0' + n % 10;
        while (t) backup[k++] = digits[--t];
        strcpy(backup + k, ".info");
    }
    if (!copy_file(icon_file, backup))
        return 0;
    log_change(icon_file, backup);
    return 1;
}

/* Sets an icon's default tool, with icon.library. */
static int set_tool(const char *icon, const char *tool)
{
    struct DiskObject *dob = GetDiskObject((STRPTR)icon);
    STRPTR old;
    int ok;
    char file[260];
    if (!dob)
        return 0;
    strcpy(file, icon);
    strcat(file, ".info");
    if (!keep_original(file)) {
        FreeDiskObject(dob);
        return 0;
    }
    old = dob->do_DefaultTool;
    dob->do_DefaultTool = (STRPTR)tool;
    ok = PutDiskObject((STRPTR)icon, dob);
    dob->do_DefaultTool = old;
    FreeDiskObject(dob);
    return ok;
}

/* The type's default icon in ENV: (Use), and ENVARC: too (Save). */
static int set_type(ftype *t, const char *tool, int save)
{
    char env[64], envarc[64];
    int ok = 1;
    strcpy(env, "ENV:Sys/def_"); strcat(env, t->name);
    strcpy(envarc, "ENVARC:Sys/def_"); strcat(envarc, t->name);
    if (save) ok = set_tool(envarc, tool);
    {   /* ENV: is RAM, so its original is the ENVARC: one: copy, don't keep */
        struct DiskObject *dob = GetDiskObject((STRPTR)env);
        if (dob) {
            STRPTR old = dob->do_DefaultTool;
            dob->do_DefaultTool = (STRPTR)tool;
            ok = PutDiskObject((STRPTR)env, dob) && ok;
            dob->do_DefaultTool = old;
            FreeDiskObject(dob);
        }
    }
    if (ok && save) strncpy(t->tool, tool, sizeof t->tool - 1);
    return ok;
}

/* Puts every changed icon back, newest first, and forgets the list. */
static int undo_all(int *count)
{
    static char *lines[512], line[512];
    BPTR fh = Open((STRPTR)CHANGES, MODE_OLDFILE);
    int n = 0, i, problems = 0;
    *count = 0;
    if (!fh) return 1;
    while (n < 512 && FGets(fh, (STRPTR)line, sizeof line)) {
        int len = strlen(line);
        if (len && line[len - 1] == '\n') line[--len] = 0;
        if (!len) continue;
        if ((lines[n] = AllocVec(len + 1, MEMF_ANY))) strcpy(lines[n++], line);
    }
    Close(fh);
    for (i = n - 1; i >= 0; i--) {
        char *tab = strchr(lines[i], '\t');
        if (tab) {
            *tab = 0;
            if (copy_file(tab + 1, lines[i])) {
                DeleteFile((STRPTR)(tab + 1));
                (*count)++;
                /* a default icon goes back in ENV: too */
                if (!Strnicmp((STRPTR)lines[i], (STRPTR)"ENVARC:Sys/", 11)) {
                    static char env[300];
                    strcpy(env, "ENV:Sys/");
                    strcat(env, lines[i] + 11);
                    copy_file(lines[i], env);
                }
            } else
                problems++;
        }
        FreeVec(lines[i]);
    }
    if (!problems) {
        DeleteFile((STRPTR)CHANGES);
        DeleteFile((STRPTR)BACKUP);
        DeleteFile((STRPTR)STATE);
    }
    return !problems;
}

/* ---- what kind of file ------------------------------------------------------------- */

/* The default icon type for a file: its datatype's name if there is a
 * def_<name> icon, else its datatype's group, as DefIcons falls back. */
static const char *file_type(const char *path)
{
    static char name[32];
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    struct DataType *dtn;
    const char *found = NULL;
    if (!l) return NULL;
    if ((dtn = ObtainDataTypeA(DTST_FILE, (APTR)l, NULL))) {
        struct DataTypeHeader *dth = dtn->dtn_Header;
        int i;
        strncpy(name, (char *)dth->dth_Name, sizeof name - 1);
        for (i = 0; name[i]; i++) if (name[i] >= 'A' && name[i] <= 'Z') name[i] += 32;
        if (find_type(name)) found = name;
        else switch (dth->dth_GroupID) {
            case GID_PICTURE: found = "picture"; break;
            case GID_TEXT: found = "ascii"; break;
            case GID_DOCUMENT: found = find_type("pdf") && strstr(name, "pdf") ? "pdf" : "officedocs"; break;
            case GID_SOUND: case GID_INSTRUMENT: found = "sound"; break;
            case GID_MUSIC: found = "music"; break;
            case GID_ANIMATION: found = "anim"; break;
            case GID_MOVIE: found = "video"; break;
        }
        ReleaseDataType(dtn);
    }
    UnLock(l);
    return found;
}

/* ---- finding icons that still name the old program ---------------------------------- */

#define MAXFOUND 400
typedef struct { struct Node node; char path[256]; char label[270]; int on; } found_icon;
static found_icon *found;
static int nfound;

static void scan_dir(const char *dir, ftype *t, int depth)
{
    BPTR l;
    struct FileInfoBlock *fib;
    if (depth > 8 || nfound >= MAXFOUND || !(l = Lock((STRPTR)dir, ACCESS_READ)))
        return;
    if (!(fib = AllocDosObject(DOS_FIB, NULL))) { UnLock(l); return; }
    if (Examine(l, fib)) {
        while (ExNext(l, fib) && nfound < MAXFOUND) {
            char path[256];
            int n = strlen((char *)fib->fib_FileName);
            strncpy(path, dir, 200);
            path[200] = 0;
            AddPart((STRPTR)path, fib->fib_FileName, sizeof path);
            if (fib->fib_DirEntryType > 0) {
                scan_dir(path, t, depth + 1);
            } else if (n > 5 && !Stricmp(fib->fib_FileName + n - 5, (STRPTR)".info")) {
                struct DiskObject *dob;
                path[strlen(path) - 5] = 0;                             /* the file the icon belongs to */
                if ((dob = GetDiskObject((STRPTR)path))) {
                    if (dob->do_Type == WBPROJECT && dob->do_DefaultTool && t->tool[0] &&
                        !Stricmp((STRPTR)base((char *)dob->do_DefaultTool), (STRPTR)base(t->tool))) {
                        const char *k = file_type(path);
                        if (k && !Stricmp((STRPTR)k, (STRPTR)t->name)) {
                            found_icon *f = &found[nfound++];
                            memset(f, 0, sizeof *f);
                            strcpy(f->path, path);
                            f->on = 1;
                        }
                    }
                    FreeDiskObject(dob);
                }
            }
        }
    }
    FreeDosObject(DOS_FIB, fib);
    UnLock(l);
}

static int scan(ftype *t, const char *drawer)
{
    nfound = 0;
    if (!found && !(found = AllocVec(sizeof *found * MAXFOUND, MEMF_CLEAR)))
        return 0;
    scan_dir(drawer, t, 0);
    return nfound;
}

static int apply_found(const char *tool)
{
    int i, done = 0;
    for (i = 0; i < nfound; i++)
        if (found[i].on && set_tool(found[i].path, tool))
            done++;
    return done;
}

/* ---- SWITCH: one program's kinds of file to another ---------------------------------- */

/* Kinds a viewer doesn't take over from MultiView: sounds, music, video and
 * fonts stay with what plays or shows them (OpenPlay, MultiView). */
static int keeps_its_program(const char *name)
{
    static const char *const keep[] = { "sound", "music", "audio", "8svx", "wav", "aif", "mp3", "ogg", "flac",
                                        "mid", "mod", "video", "movie", "font", NULL };
    char low[32];
    int i;
    for (i = 0; name[i] && i < 31; i++) low[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i];
    low[i] = 0;
    for (i = 0; keep[i]; i++)
        if (strstr(low, keep[i])) return 1;
    return 0;
}

static int switched_icons;
/* Icons to change, found first and changed after the walk: rewriting an icon
 * while ExNext walks its drawer can make ExNext skip or repeat entries. */
struct to_switch { struct to_switch *next; char path[1]; };
static struct to_switch *pending;
/* One path for the whole walk, not one per level: Installer's commands run
 * with 4 KB of stack (as UNDO found, 5 Oct 2026). */
static char walk_path[256];

/* Icons under walk_path naming the old program, of a kind SWITCH changed. */
static void switch_dir(const char *from, const char *to, int depth)
{
    BPTR l;
    struct FileInfoBlock *fib;
    int len = strlen(walk_path);
    if (depth > 8 || !(l = Lock((STRPTR)walk_path, ACCESS_READ)))
        return;
    if (!(fib = AllocDosObject(DOS_FIB, NULL))) { UnLock(l); return; }
    if (Examine(l, fib)) {
        while (ExNext(l, fib)) {
            int n = strlen((char *)fib->fib_FileName);
            walk_path[len] = 0;
            if (!AddPart((STRPTR)walk_path, fib->fib_FileName, sizeof walk_path))
                continue;
            if (fib->fib_DirEntryType > 0) {
                switch_dir(from, to, depth + 1);
            } else if (n > 5 && !Stricmp(fib->fib_FileName + n - 5, (STRPTR)".info")) {
                struct DiskObject *dob;
                int match = 0;
                walk_path[strlen(walk_path) - 5] = 0;                   /* the file the icon belongs to */
                if ((dob = GetDiskObject((STRPTR)walk_path))) {
                    match = dob->do_Type == WBPROJECT && dob->do_DefaultTool &&
                            !Stricmp((STRPTR)base((char *)dob->do_DefaultTool), (STRPTR)base(from));
                    FreeDiskObject(dob);
                }
                if (match) {
                    const char *k = file_type(walk_path);
                    ftype *t = k ? find_type(k) : NULL;
                    struct to_switch *p;
                    if (t && t->switched && (p = AllocVec(sizeof *p + strlen(walk_path), MEMF_ANY))) {
                        strcpy(p->path, walk_path);
                        p->next = pending;
                        pending = p;
                    }
                }
            }
        }
    }
    walk_path[len] = 0;
    FreeDosObject(DOS_FIB, fib);
    UnLock(l);
}

/* SWITCH <from> TO <to> [SAVE] [IN <drawer>]: every kind whose default icon
 * names from goes to to, except those keeps_its_program() leaves. Kinds
 * someone set to another program on purpose are left alone. Backed up as SET
 * and SCAN are, so UNDO puts it all back. */
static int shell_switch(LONG *a)
{
    const char *from = (const char *)a[9], *to = (const char *)a[2];
    int i, n = 0, failed = 0;
    if (!to) { PutStr((STRPTR)"OpenTypes: SWITCH needs TO <program>\n"); return RETURN_ERROR; }
    if (!exists(to)) Printf((STRPTR)"OpenTypes: note: %s isn't there (yet)\n", (LONG)to);
    for (i = 0; i < ntypes; i++) {
        ftype *t = &types[i];
        if (!t->tool[0] || Stricmp((STRPTR)base(t->tool), (STRPTR)base(from)) || keeps_its_program(t->name))
            continue;
        if (set_type(t, to, a[3] != 0)) {
            t->switched = 1;
            n++;
            Printf((STRPTR)"  %s\n", (LONG)t->name);
        } else
            failed++;
    }
    Printf((STRPTR)"OpenTypes: %ld kind%s of file now open with %s%s\n", (LONG)n, (LONG)(n == 1 ? "" : "s"), (LONG)to,
           (LONG)(a[3] ? " (saved)" : " (until reboot)"));
    if (a[5] && n) {
        switched_icons = 0;
        strncpy(walk_path, (const char *)a[5], sizeof walk_path - 1);
        walk_path[sizeof walk_path - 1] = 0;
        switch_dir(from, to, 0);
        while (pending) {
            struct to_switch *p = pending;
            pending = p->next;
            if (set_tool(p->path, to))
                switched_icons++;
            FreeVec(p);
        }
        Printf((STRPTR)"OpenTypes: %ld icon%s in %s changed from %s\n", (LONG)switched_icons,
               (LONG)(switched_icons == 1 ? "" : "s"), a[5], (LONG)from);
    }
    if (failed) { Printf((STRPTR)"OpenTypes: %ld default icon%s couldn't be changed\n", (LONG)failed, (LONG)(failed == 1 ? "" : "s")); return RETURN_WARN; }
    return RETURN_OK;
}

/* ---- the Shell -------------------------------------------------------------------------- */

static int shell(LONG *a)
{
    /* LIST/S,SET/K,TO/K,SAVE/S,SCAN/K,IN/K,APPLY/S,TYPE/K,UNDO/S */
    ftype *t;
    if (a[8]) {
        int n;
        if (!undo_all(&n)) { PutStr((STRPTR)"OpenTypes: some icons couldn't be put back; their originals are in " BACKUP "\n"); return RETURN_ERROR; }
        Printf((STRPTR)"OpenTypes: %ld icon%s put back as they were\n", n, (LONG)(n == 1 ? "" : "s"));
        return RETURN_OK;
    }
    if (a[7]) {
        const char *k = file_type((char *)a[7]);
        Printf((STRPTR)"%s: %s\n", a[7], (LONG)(k ? k : "(not known)"));
        return k ? RETURN_OK : RETURN_WARN;
    }
    if (a[1]) {
        if (!(t = find_type((char *)a[1]))) { Printf((STRPTR)"OpenTypes: there is no default icon for %s files\n", a[1]); return RETURN_ERROR; }
        if (!a[2]) { PutStr((STRPTR)"OpenTypes: SET needs TO <program>\n"); return RETURN_ERROR; }
        if (!exists((char *)a[2])) Printf((STRPTR)"OpenTypes: note: %s isn't there (yet)\n", a[2]);
        if (!set_type(t, (char *)a[2], a[3] != 0)) { PutStr((STRPTR)"OpenTypes: couldn't change the default icon\n"); return RETURN_ERROR; }
        Printf((STRPTR)"OpenTypes: %s files open with %s%s\n", (LONG)t->name, a[2], (LONG)(a[3] ? " (saved)" : " (until reboot)"));
        return RETURN_OK;
    }
    if (a[4]) {
        int i, n;
        if (!(t = find_type((char *)a[4]))) { Printf((STRPTR)"OpenTypes: there is no default icon for %s files\n", a[4]); return RETURN_ERROR; }
        /* the old program: what the saved default icon named before OpenTypes */
        n = scan(t, a[5] ? (char *)a[5] : "SYS:");
        Printf((STRPTR)"OpenTypes: %ld %s file%s with icons naming %s\n", n, (LONG)t->name, (LONG)(n == 1 ? "" : "s"), (LONG)t->tool);
        for (i = 0; i < nfound; i++) Printf((STRPTR)"  %s\n", (LONG)found[i].path);
        return RETURN_OK;
    }
    {
        int i;
        for (i = 0; i < ntypes; i++)
            Printf((STRPTR)"%-12s %s\n", (LONG)types[i].name, (LONG)(types[i].tool[0] ? types[i].tool : "(nothing)"));
    }
    return RETURN_OK;
}

/* SCAN ... APPLY: icons naming the program the type had before SET. */
static int shell_apply(LONG *a)
{
    ftype *t = find_type((char *)a[4]);
    char old[200];
    int n;
    if (!t || !a[2]) { PutStr((STRPTR)"OpenTypes: SCAN <type> IN <drawer> APPLY needs TO <program>\n"); return RETURN_ERROR; }
    strcpy(old, t->tool);
    n = scan(t, a[5] ? (char *)a[5] : "SYS:");
    n = apply_found((char *)a[2]);
    Printf((STRPTR)"OpenTypes: %ld icon%s changed from %s to %s\n", n, (LONG)(n == 1 ? "" : "s"), (LONG)old, a[2]);
    return RETURN_OK;
}

/* ---- the window --------------------------------------------------------------------------- */

enum { G_TYPES = 1, G_PROGRAM, G_CHOOSE, G_KNOWN, G_FIND, G_ICONS, G_STATUS, G_SAVE, G_USE, G_UNDO, G_CANCEL };
static struct List type_list, icon_list;
static STRPTR known_labels[NCAT + 2];
static const char *known_tools[NCAT + 2];
static char status_text[160];

static void label_types(void)
{
    int i;
    type_list.lh_Head = (struct Node *)&type_list.lh_Tail; type_list.lh_Tail = NULL; type_list.lh_TailPred = (struct Node *)&type_list.lh_Head;
    for (i = 0; i < ntypes; i++) {
        ftype *t = &types[i];
        strcpy(t->label, t->name);
        strcat(t->label, ": ");
        strcat(t->label, t->pick[0] ? base(t->pick) : "(nothing)");
        if (Stricmp((STRPTR)t->pick, (STRPTR)t->tool)) strcat(t->label, " *");
        t->node.ln_Name = t->label;
        AddTail(&type_list, &t->node);
    }
}

static void label_icons(void)
{
    int i;
    icon_list.lh_Head = (struct Node *)&icon_list.lh_Tail; icon_list.lh_Tail = NULL; icon_list.lh_TailPred = (struct Node *)&icon_list.lh_Head;
    for (i = 0; i < nfound; i++) {
        strcpy(found[i].label, found[i].on ? "[x] " : "[ ] ");
        strncat(found[i].label, found[i].path, sizeof found[i].label - 5);
        found[i].node.ln_Name = found[i].label;
        AddTail(&icon_list, &found[i].node);
    }
}

static int fill_known(ftype *t)
{
    int i, n = 0;
    char key[40];
    strcpy(key, " "); strcat(key, t->name); strcat(key, " ");
    for (i = 0; i < NCAT && n < NCAT; i++)
        if (strstr(catalogue[i].types, key) && exists(catalogue[i].tool)) {
            int j, dup = 0;
            for (j = 0; j < n; j++) if (!strcmp((char *)known_labels[j], catalogue[i].label)) dup = 1;
            if (dup) continue;
            known_labels[n] = (STRPTR)catalogue[i].label;
            known_tools[n++] = catalogue[i].tool;
        }
    if (!n) { known_labels[n] = (STRPTR)"(none known)"; known_tools[n++] = NULL; }
    known_labels[n] = NULL;
    return n;
}

static int gui(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g, *gad[G_CANCEL + 1];
    struct NewGadget ng;
    struct Window *win = NULL;
    int fh, top, sel = -1, quit = 0, rc = RETURN_OK, W = 600, i;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    top = scr->WBorTop + fh + 1 + 6;
    label_types();
    nfound = 0;
    if (!found) found = AllocVec(sizeof *found * MAXFOUND, MEMF_CLEAR);
    label_icons();
    known_labels[0] = (STRPTR)"(choose a kind of file)"; known_labels[1] = NULL;
    strcpy(status_text, "Choose a kind of file, then the program that opens it.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
    ng.ng_LeftEdge = 10; ng.ng_TopEdge = top + fh + 4; ng.ng_Width = 220; ng.ng_Height = 12 * (fh + 1) + 4;
    ng.ng_GadgetText = (STRPTR)"Kinds of file"; ng.ng_Flags = PLACETEXT_ABOVE; ng.ng_GadgetID = G_TYPES;
    g = gad[G_TYPES] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&type_list, GTLV_ShowSelected, NULL, TAG_DONE);

    ng.ng_LeftEdge = 330; ng.ng_TopEdge = top + fh + 4; ng.ng_Width = 170; ng.ng_Height = fh + 6;
    ng.ng_GadgetText = (STRPTR)"Opens with"; ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = G_PROGRAM;
    g = gad[G_PROGRAM] = CreateGadget(STRING_KIND, g, &ng, GTST_MaxChars, 190, GA_Disabled, TRUE, TAG_DONE);
    ng.ng_LeftEdge = 504; ng.ng_Width = 86; ng.ng_GadgetText = (STRPTR)"Choose..."; ng.ng_Flags = 0; ng.ng_GadgetID = G_CHOOSE;
    g = gad[G_CHOOSE] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, TRUE, TAG_DONE);
    ng.ng_LeftEdge = 330; ng.ng_TopEdge += fh + 10; ng.ng_Width = 260; ng.ng_GadgetText = (STRPTR)"Known"; ng.ng_Flags = PLACETEXT_LEFT; ng.ng_GadgetID = G_KNOWN;
    g = gad[G_KNOWN] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)known_labels, GA_Disabled, TRUE, TAG_DONE);
    ng.ng_LeftEdge = 240; ng.ng_TopEdge += fh + 14; ng.ng_Width = 350; ng.ng_GadgetText = (STRPTR)"Find icons that still name the old program..."; ng.ng_Flags = 0; ng.ng_GadgetID = G_FIND;
    g = gad[G_FIND] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, TRUE, TAG_DONE);
    ng.ng_TopEdge += fh + 10; ng.ng_Height = 7 * (fh + 1) + 4; ng.ng_GadgetText = NULL; ng.ng_GadgetID = G_ICONS;
    g = gad[G_ICONS] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&icon_list, TAG_DONE);

    ng.ng_LeftEdge = 10; ng.ng_TopEdge = top + fh + 4 + 12 * (fh + 1) + 12; ng.ng_Width = W - 20; ng.ng_Height = fh + 6; ng.ng_GadgetID = G_STATUS;
    g = gad[G_STATUS] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE, TAG_DONE);
    {
        static const char *const names[4] = { "Save", "Use", "Undo all", "Cancel" };
        static const int ids[4] = { G_SAVE, G_USE, G_UNDO, G_CANCEL };
        int bw = (W - 20 - 3 * 10) / 4;
        ng.ng_TopEdge += fh + 12;
        for (i = 0; i < 4; i++) {
            ng.ng_LeftEdge = 10 + i * (bw + 10); ng.ng_Width = bw; ng.ng_GadgetText = (STRPTR)names[i]; ng.ng_GadgetID = ids[i];
            g = gad[ids[i]] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
        }
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"OpenTypes: which program opens each kind of file", WA_PubScreen, (ULONG)scr,
                         WA_Left, 30, WA_Top, scr->BarHeight + 10, WA_Width, W, WA_Height, ng.ng_TopEdge + ng.ng_Height + 8,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | LISTVIEWIDCMP | BUTTONIDCMP | CYCLEIDCMP | STRINGIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    GT_RefreshWindow(win, NULL);

#define SET(id, ...) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE)
    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) quit = 1;
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_GADGETUP) switch (gg->GadgetID) {
            case G_TYPES: {
                ftype *t = &types[code];
                sel = code;
                fill_known(t);
                SET(G_PROGRAM, GTST_String, (ULONG)t->pick, GA_Disabled, FALSE);
                SET(G_CHOOSE, GA_Disabled, FALSE);
                SET(G_KNOWN, GTCY_Labels, (ULONG)known_labels, GTCY_Active, 0, GA_Disabled, known_tools[0] == NULL);
                SET(G_FIND, GA_Disabled, FALSE);
                SET(G_ICONS, GTLV_Labels, ~0UL); nfound = 0; label_icons(); SET(G_ICONS, GTLV_Labels, (ULONG)&icon_list);
                strcpy(status_text, t->name); strcat(status_text, " files open with "); strcat(status_text, t->tool[0] ? t->tool : "nothing yet");
                SET(G_STATUS, GTTX_Text, (ULONG)status_text);
                break;
            }
            case G_PROGRAM:
            case G_KNOWN:
                if (sel >= 0) {
                    ftype *t = &types[sel];
                    if (gg->GadgetID == G_KNOWN) {
                        if (known_tools[code]) strcpy(t->pick, known_tools[code]);
                        SET(G_PROGRAM, GTST_String, (ULONG)t->pick);
                    } else
                        strncpy(t->pick, (char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof t->pick - 1);
                    SET(G_TYPES, GTLV_Labels, ~0UL); label_types(); SET(G_TYPES, GTLV_Labels, (ULONG)&type_list, GTLV_Selected, sel);
                }
                break;
            case G_CHOOSE:
                if (sel >= 0) {
                    struct FileRequester *fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)win,
                                                                   ASLFR_TitleText, (ULONG)"The program that opens these files",
                                                                   ASLFR_InitialDrawer, (ULONG)"SYS:Utilities", TAG_DONE);
                    if (fr && AslRequest(fr, NULL)) {
                        ftype *t = &types[sel];
                        strncpy(t->pick, (char *)fr->fr_Drawer, 150);
                        t->pick[150] = 0;
                        AddPart((STRPTR)t->pick, fr->fr_File, sizeof t->pick);
                        SET(G_PROGRAM, GTST_String, (ULONG)t->pick);
                        SET(G_TYPES, GTLV_Labels, ~0UL); label_types(); SET(G_TYPES, GTLV_Labels, (ULONG)&type_list, GTLV_Selected, sel);
                    }
                    if (fr) FreeAslRequest(fr);
                }
                break;
            case G_FIND:
                if (sel >= 0) {
                    struct FileRequester *fr = AllocAslRequestTags(ASL_FileRequest, ASLFR_Window, (ULONG)win, ASLFR_DrawersOnly, TRUE,
                                                                   ASLFR_TitleText, (ULONG)"Look for icons in", ASLFR_InitialDrawer, (ULONG)"SYS:", TAG_DONE);
                    if (fr && AslRequest(fr, NULL)) {
                        ftype *t = &types[sel];
                        SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
                        SET(G_ICONS, GTLV_Labels, ~0UL);
                        scan(t, (char *)fr->fr_Drawer);
                        label_icons();
                        SET(G_ICONS, GTLV_Labels, (ULONG)&icon_list);
                        SetWindowPointer(win, TAG_DONE);
                        strcpy(status_text, "Icons of "); strcat(status_text, t->name);
                        strcat(status_text, " files naming "); strcat(status_text, base(t->tool));
                        strcat(status_text, nfound ? ": [x] ones change on Save or Use" : ": none");
                        SET(G_STATUS, GTTX_Text, (ULONG)status_text);
                    }
                    if (fr) FreeAslRequest(fr);
                }
                break;
            case G_ICONS:
                if (code < nfound) {
                    found[code].on = !found[code].on;
                    SET(G_ICONS, GTLV_Labels, ~0UL); label_icons(); SET(G_ICONS, GTLV_Labels, (ULONG)&icon_list);
                }
                break;
            case G_SAVE:
            case G_USE: {
                int save = gg->GadgetID == G_SAVE, k, kinds = 0, icons = 0;
                for (k = 0; k < ntypes; k++)
                    if (Stricmp((STRPTR)types[k].pick, (STRPTR)types[k].tool) && types[k].pick[0]) {
                        if (sel == k && nfound) icons = apply_found(types[k].pick);
                        if (set_type(&types[k], types[k].pick, save)) kinds++;
                    }
                (void)kinds; (void)icons;
                quit = 1;
                break;
            }
            case G_UNDO: {
                int n;
                undo_all(&n);
                load_types();
                SET(G_TYPES, GTLV_Labels, ~0UL); label_types(); SET(G_TYPES, GTLV_Labels, (ULONG)&type_list);
                strcpy(status_text, n ? "Every icon OpenTypes changed is back as it was." : "OpenTypes hasn't changed any icons.");
                SET(G_STATUS, GTTX_Text, (ULONG)status_text);
                break;
            }
            case G_CANCEL: quit = 1; break;
            }
        }
    }
out:
    if (win) CloseWindow(win);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

int main(int argc, char **argv)
{
    LONG a[10] = { 0 };
    struct RDArgs *rda = NULL;
    int rc = RETURN_FAIL;
    IconBase = OpenLibrary((STRPTR)"icon.library", 44);
    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    AslBase = OpenLibrary((STRPTR)"asl.library", 39);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39);
    if (!IconBase || !DataTypesBase || !AslBase || !GadToolsBase) {
        PutStr((STRPTR)"OpenTypes needs AmigaOS 3.5 or later (icon.library 44)\n");
        goto out;
    }
    load_types();
    /* from Workbench (argc 0) there are no arguments: ReadArgs would read them from
     * libnix's console window, which opens empty and waits */
    if (argc > 0 && !(rda = ReadArgs((STRPTR)"LIST/S,SET/K,TO/K,SAVE/S,SCAN/K,IN/K,APPLY/S,TYPE/K,UNDO/S,SWITCH/K", a, NULL))) {
        PrintFault(IoErr(), (STRPTR)"OpenTypes");
        goto out;
    }
    if (a[9])
        rc = shell_switch(a);
    else if (a[0] || a[1] || a[4] || a[7] || a[8])
        rc = a[4] && a[6] ? shell_apply(a) : shell(a);
    else
        rc = gui();
    if (rda) FreeArgs(rda);
out:
    if (found) FreeVec(found);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (AslBase) CloseLibrary(AslBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (IconBase) CloseLibrary(IconBase);
    (void)version;
    return rc;
}
