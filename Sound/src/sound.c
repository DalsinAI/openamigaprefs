/* OpenPrefs Sound 0.1: one editor for all of the Amiga's sound, in place
 * of Prefs/Sound and Prefs/AHI (Dale, 6 October 2026: "a single combined
 * sound prefs"; design: DESIGN.md, mockup in it).
 *
 *   - the levels: the volume, Paula's level, AHI's level and mute. On
 *     AmigaChrome they are the ACAHI board's LEVELS, which the PC's mixer
 *     applies, so they move as the sliders do, and the mixer page on the
 *     PC moves them too. On a real Amiga the volume is AHI's output
 *     volume, and Paula's level is the speakers' knob.
 *   - who mixes AHI's sound: the PC (the Host mix mode) or the Amiga (HiFi)
 *   - the beep: a sound, a flash, both or nothing (the OS's sound.prefs)
 *   - the speaker on the menu bar (OpenSpeaker)
 *   - Advanced: AHI's units, its global settings, and the beep's sound
 *
 * Save writes ENV: and ENVARC:, Use writes ENV:, Cancel puts back the
 * levels as they were. The files are the OS's own where it has one.
 *
 *   Sound [FROM file] [USE] [SAVE] [ADVANCED]
 *
 * "Sound USE" at boot (OpenUp's startup) gives the board the saved levels.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sp_core.h"
#include "sp_amiga.h"
#include "sp_test.h"

const char version[] __attribute__((used)) = "$VER: Sound 0.1 (6.10.2026) OpenPrefs, Dalsin Limited";

#define VIEW_ENV "ENV:OpenAmiga/PrefsView"           /* shared by every OpenPrefs editor: simple or advanced */
#define VIEW_ENVARC "ENVARC:OpenAmiga/PrefsView"
#define MAX_MODES 32

static sp_settings cur, orig;
static volatile ULONG *board;
static ULONG seen_seq;                                /* the board's LEVEL_SEQ as we last left or read it */
static int touched;                                   /* the levels were moved here (Cancel puts back only then) */
static int advanced;
static ULONG mode_ids[MAX_MODES + 1];
static char mode_names[MAX_MODES + 1][48];
static STRPTR mode_labels[MAX_MODES + 2];
static int nmodes;

/* ---- the window ---------------------------------------------------------------------------------- */

enum {
    G_VOL, G_MUTE, G_PAULA, G_TPAULA, G_AHI, G_TAHI, G_MIX, G_BEEP, G_BEEPSND, G_TBEEP, G_SPEAKER, G_NOTE,
    G_UMODE0, G_UMODE1, G_UMODE2, G_UMODE3, G_UMODE4,
    G_UFREQ0, G_UFREQ1, G_UFREQ2, G_UFREQ3, G_UFREQ4,
    G_UCH0, G_UCH1, G_UCH2, G_UCH3, G_UCH4,
    G_UVOL0, G_UVOL1, G_UVOL2, G_UVOL3, G_UVOL4,
    G_CLIP, G_ANTICLICK, G_CPU, G_SAMPLE, G_BVOL, G_BPITCH, G_BLEN, G_WHEEL,
    G_STATUS, G_SAVE, G_USE, G_CANCEL, G_COUNT
};

static const char *mix_labels[] = { "Cradle: frees the Amiga's CPU (recommended)", "The Amiga: for programs that use AHI's echo", NULL };
static const char *beep_labels[] = { "Play a sound", "Flash the screen", "Both", "Nothing", NULL };
static const char *beepsnd_labels[] = { "Beep", "A sound file", NULL };
static const char *freq_labels[] = { "8000", "11025", "16000", "22050", "32000", "44100", "48000", NULL };
static const ULONG freq_values[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000 };
static const char *unit_names[SP_UNITS] = { "Unit 0", "Unit 1", "Unit 2", "Unit 3", "Music" };

static struct Gadget *gad[G_COUNT];
static struct Window *win;
static char status_text[120];

#define SET(id, ...) do { if (gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, __VA_ARGS__, TAG_DONE); } while (0)

static char *read_text(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf;
    LONG n;
    if (!fh) return NULL;
    if ((buf = AllocVec(64, MEMF_ANY | MEMF_CLEAR))) { n = Read(fh, buf, 63); if (n < 0) n = 0; buf[n] = 0; }
    Close(fh);
    return buf;
}

static int read_view(void)
{
    char *t = read_text(VIEW_ENV);
    int a = t && !strncmp(t, "advanced", 8);
    if (t) FreeVec(t);
    return a;
}

static void write_view(int a)
{
    BPTR fh;
    if ((fh = Open((STRPTR)VIEW_ENV, MODE_NEWFILE))) { Write(fh, a ? "advanced\n" : "simple\n", a ? 9 : 7); Close(fh); }
    if ((fh = Open((STRPTR)VIEW_ENVARC, MODE_NEWFILE))) { Write(fh, a ? "advanced\n" : "simple\n", a ? 9 : 7); Close(fh); }
}

static struct NewMenu menus[] = {
    { NM_TITLE, (STRPTR)"Project", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)1 },
    { NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)2 },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)3 },
    { NM_TITLE, (STRPTR)"View", NULL, 0, 0, NULL },
    { NM_ITEM, (STRPTR)"Advanced", (STRPTR)"A", CHECKIT | MENUTOGGLE, 0, (APTR)4 },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* the cycle's entry for a unit's mode; the last entry is "(none)" */
static int mode_index(ULONG id)
{
    for (int i = 0; i < nmodes; i++) if (mode_ids[i] == id) return i;
    return nmodes;
}

static int freq_index(ULONG f)
{
    int best = 0;
    for (int i = 0; i < 7; i++) if (labs((long)freq_values[i] - (long)f) < labs((long)freq_values[best] - (long)f)) best = i;
    return best;
}

static void show(void)
{
    SET(G_VOL, GTSL_Level, cur.volume);
    SET(G_MUTE, GTCB_Checked, cur.muted);
    SET(G_PAULA, GTSL_Level, cur.paula);
    SET(G_AHI, GTSL_Level, cur.ahi);
    SET(G_MIX, GTMX_Active, cur.mix);
    SET(G_BEEP, GTCY_Active, cur.beep);
    SET(G_BEEPSND, GTCY_Active, cur.beep_sample, GA_Disabled, cur.beep == SP_BEEP_FLASH || cur.beep == SP_BEEP_NONE);
    SET(G_SPEAKER, GTCB_Checked, cur.speaker);
    for (int u = 0; u < SP_UNITS; u++) {
        SET(G_UMODE0 + u, GTCY_Active, mode_index(cur.unit[u].mode));
        SET(G_UFREQ0 + u, GTCY_Active, freq_index(cur.unit[u].freq ? cur.unit[u].freq : 44100), GA_Disabled, cur.unit[u].mode == SP_MODE_HOSTMIX);
        SET(G_UCH0 + u, GTIN_Number, cur.unit[u].channels ? cur.unit[u].channels : 8);
        SET(G_UVOL0 + u, GTIN_Number, (LONG)((cur.unit[u].out_volume * 100 + 0x8000) >> 16));
    }
    SET(G_CLIP, GTCB_Checked, cur.clip);
    SET(G_ANTICLICK, GTIN_Number, cur.anticlick);
    SET(G_CPU, GTIN_Number, cur.maxcpu);
    SET(G_SAMPLE, GTST_String, (ULONG)cur.sample, GA_Disabled, !cur.beep_sample);
    SET(G_BVOL, GTSL_Level, cur.beep_volume);
    SET(G_BPITCH, GTSL_Level, 1124 - cur.beep_period);              /* to the right is higher */
    SET(G_BLEN, GTSL_Level, cur.beep_duration);
    SET(G_WHEEL, GTIN_Number, cur.wheel);
}

static void status(const char *s)
{
    strncpy(status_text, s, sizeof status_text - 1);
    SET(G_STATUS, GTTX_Text, (ULONG)status_text);
}

/* the levels, heard now */
static void levels_now(void)
{
    if (board) { sp_board_set(board, sp_levels_word(&cur)); seen_seq = sp_board_seq(board); }
}

/* the levels as the PC's mixer page or the menu bar speaker left them: 1 when they moved */
static int levels_follow(void)
{
    ULONG seq;
    if (!board || (seq = sp_board_seq(board)) == seen_seq) return 0;
    seen_seq = seq;
    sp_from_levels_word(&cur, sp_board_levels(board));
    return 1;
}

static void put_in_place(int save)
{
    levels_follow();                         /* not the levels from before the PC moved them */
    if (!board) sp_volume_to_units(&cur);
    sp_store(&cur, save);
    levels_now();
    sp_tell_speaker();                       /* it reads the settings again, and goes when switched off */
    if (cur.speaker) sp_start_speaker();
}

static void put_back(void)
{
    if (board && touched) sp_board_set(board, sp_levels_word(&orig));
    sp_tell_speaker();
}

static void beep_test(void)
{
    /* the beep as set: IPrefs plays what ENV:Sys/sound.prefs says */
    UBYTE snd[SP_SOUND_PREFS_SIZE];
    BPTR fh;
    LONG n = sp_write_sound_prefs(&cur, snd);
    if ((fh = Open((STRPTR)SP_SOUND_ENV, MODE_NEWFILE))) { Write(fh, snd, n); Close(fh); }
    Delay(10);                                                         /* IPrefs is told by a notification */
    DisplayBeep(NULL);
}

static int slider_value(struct Gadget *g, UWORD code)
{
    (void)g;
    return (WORD)code;
}

/* RETURN_OK or RETURN_FAIL, or 99 when the view changed (open again) */
static int gui_once(void)
{
    struct Screen *scr = LockPubScreen(NULL);
    APTR vi = scr ? GetVisualInfoA(scr, NULL) : NULL;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    int fh, top, row, quit = 0, rc = RETURN_OK, W = advanced ? 560 : 500, L = 10, X = 170, lh, i;
    struct Menu *menu = NULL;
    if (!vi) { if (scr) UnlockPubScreen(NULL, scr); return RETURN_FAIL; }
    memset(gad, 0, sizeof gad);
    fh = scr->Font->ta_YSize;
    lh = fh + (advanced ? 4 : 6);
    top = scr->WBorTop + fh + 1 + 6;
    if (!status_text[0])
        strcpy(status_text, board ? "The sliders move the sound as you drag them." : "No AmigaChrome board: the volume is AHI's.");

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = scr->Font;
    ng.ng_VisualInfo = vi;
#define G(kind, id, x, y, w, h, text, flags, ...) \
    (ng.ng_LeftEdge = (x), ng.ng_TopEdge = (y), ng.ng_Width = (w), ng.ng_Height = (h), ng.ng_GadgetText = (STRPTR)(text), \
     ng.ng_Flags = (flags), ng.ng_GadgetID = (id), g = gad[id] = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE))
#define SLIDER(id, y, text, max) \
    G(SLIDER_KIND, id, X, y, W - X - 130, lh - 2, text, PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, max, GTSL_Level, 0, \
      GTSL_LevelFormat, (ULONG)"%3ld%%", GTSL_MaxLevelLen, 4, GTSL_LevelPlace, PLACETEXT_RIGHT, GA_RelVerify, TRUE, GA_Immediate, TRUE)

    /* the levels */
    row = top;
    SLIDER(G_VOL, row, board ? "Volume" : "Volume (AHI)", 100);
    G(CHECKBOX_KIND, G_MUTE, W - 80, row, 26, lh - 2, "Mute", PLACETEXT_RIGHT, GTCB_Scaled, TRUE); row += lh + 2;
    if (board) {
        SLIDER(G_PAULA, row, "Amiga sound (Paula)", 100);
        G(BUTTON_KIND, G_TPAULA, W - 80, row, 70, lh - 2, "Test", 0, GA_Disabled, FALSE); row += lh + 2;
        SLIDER(G_AHI, row, "AHI sound", 100);
        G(BUTTON_KIND, G_TAHI, W - 80, row, 70, lh - 2, "Test", 0, GA_Disabled, FALSE); row += lh + 8;
        G(MX_KIND, G_MIX, X, row, 17, fh + 1, NULL, PLACETEXT_RIGHT, GTMX_Labels, (ULONG)mix_labels, GTMX_Spacing, 3, GTMX_Scaled, TRUE);
        G(TEXT_KIND, G_NOTE, L, row, X - L - 8, lh - 2, NULL, 0, GTTX_Text, (ULONG)"Mix AHI sound on", GTTX_Justification, GTJ_RIGHT);
        row += 2 * (fh + 4) + 8;
    } else {
        G(TEXT_KIND, G_NOTE, X, row, W - X - 10, lh - 2, NULL, 0, GTTX_Text, (ULONG)"Amiga sound (Paula): your speakers' own knob.");
        row += lh + 2;
        G(BUTTON_KIND, G_TAHI, X, row, 120, lh - 2, "Test AHI", 0, GA_Disabled, FALSE); row += lh + 8;
    }

    /* the beep */
    G(CYCLE_KIND, G_BEEP, X, row, 150, lh, "When a program beeps", PLACETEXT_LEFT, GTCY_Labels, (ULONG)beep_labels);
    G(CYCLE_KIND, G_BEEPSND, X + 156, row, 120, lh, NULL, 0, GTCY_Labels, (ULONG)beepsnd_labels);
    G(BUTTON_KIND, G_TBEEP, W - 80, row, 70, lh, "Test", 0, GA_Disabled, FALSE); row += lh + 6;
    if (advanced) {
        G(STRING_KIND, G_SAMPLE, X, row, W - X - 10, lh, "Sound file", PLACETEXT_LEFT, GTST_MaxChars, 255); row += lh + 2;
        G(SLIDER_KIND, G_BVOL, X, row, 90, lh - 2, "Volume", PLACETEXT_LEFT, GTSL_Min, 0, GTSL_Max, 64, GA_RelVerify, TRUE);
        G(SLIDER_KIND, G_BPITCH, X + 150, row, 90, lh - 2, "Pitch", PLACETEXT_LEFT, GTSL_Min, 124, GTSL_Max, 1000, GA_RelVerify, TRUE);
        G(SLIDER_KIND, G_BLEN, X + 300, row, 70, lh - 2, "Length", PLACETEXT_LEFT, GTSL_Min, 1, GTSL_Max, 100, GA_RelVerify, TRUE);
        row += lh + 8;

        /* AHI's units */
        for (i = 0; i < SP_UNITS; i++) {
            G(CYCLE_KIND, G_UMODE0 + i, 70, row, 250, lh, unit_names[i], PLACETEXT_LEFT, GTCY_Labels, (ULONG)mode_labels);
            G(CYCLE_KIND, G_UFREQ0 + i, 326, row, 76, lh, NULL, 0, GTCY_Labels, (ULONG)freq_labels);
            G(INTEGER_KIND, G_UCH0 + i, 436, row, 36, lh, "Ch", PLACETEXT_LEFT, GTIN_MaxChars, 2);
            G(INTEGER_KIND, G_UVOL0 + i, 506, row, 44, lh, "Vol", PLACETEXT_LEFT, GTIN_MaxChars, 3);
            row += lh + 2;
        }
        row += 4;
        G(CHECKBOX_KIND, G_CLIP, 70, row, 26, lh, "Clip", PLACETEXT_LEFT, GTCB_Scaled, TRUE);
        G(INTEGER_KIND, G_ANTICLICK, 210, row, 40, lh, "Anti-click ms", PLACETEXT_LEFT, GTIN_MaxChars, 3);
        G(INTEGER_KIND, G_CPU, 340, row, 40, lh, "CPU limit %", PLACETEXT_LEFT, GTIN_MaxChars, 3);
        G(INTEGER_KIND, G_WHEEL, 506, row, 44, lh, "Wheel %", PLACETEXT_LEFT, GTIN_MaxChars, 2);
        row += lh + 6;
    }
    G(CHECKBOX_KIND, G_SPEAKER, X, row, 26, lh - 2, "Show the speaker on the menu bar", PLACETEXT_RIGHT, GTCB_Scaled, TRUE); row += lh + 6;

    G(TEXT_KIND, G_STATUS, L, row, W - 20, lh, NULL, 0, GTTX_Text, (ULONG)status_text, GTTX_Border, TRUE); row += lh + 6;
    {
        static const char *const names[3] = { "Save", "Use", "Cancel" };
        static const int ids[3] = { G_SAVE, G_USE, G_CANCEL };
        int bw = (W - 20 - 2 * 10) / 3;
        for (i = 0; i < 3; i++) G(BUTTON_KIND, ids[i], L + i * (bw + 10), row, bw, lh, names[i], 0, GA_Disabled, FALSE);
        row += lh + 6;
    }
    if (!g) { rc = RETURN_FAIL; goto out; }
    menus[6].nm_Flags = CHECKIT | MENUTOGGLE | (advanced ? CHECKED : 0);
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"Sound", WA_ScreenTitle, (ULONG)"OpenPrefs Sound 0.1", WA_PubScreen, (ULONG)scr,
                         WA_Left, 40, WA_Top, scr->BarHeight + 4, WA_InnerWidth, W, WA_InnerHeight, row - scr->WBorTop - fh - 1,
                         WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MENUPICK | IDCMP_INTUITICKS | SLIDERIDCMP | BUTTONIDCMP | CYCLEIDCMP |
                                   MXIDCMP | STRINGIDCMP | CHECKBOXIDCMP | INTEGERIDCMP,
                         TAG_DONE);
    if (!win) { rc = RETURN_FAIL; goto out; }
    if ((menu = CreateMenus(menus, TAG_DONE)) && LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE)) SetMenuStrip(win, menu);
    GT_RefreshWindow(win, NULL);
    show();

    while (!quit) {
        struct IntuiMessage *m;
        WaitPort(win->UserPort);
        while (!quit && (m = GT_GetIMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { put_back(); quit = 1; }
            else if (cls == IDCMP_MENUPICK) {
                UWORD mc = code;
                while (mc != MENUNULL && !quit) {
                    struct MenuItem *it = ItemAddress(menu, mc);
                    if (!it) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(it)) {
                    case 1: put_in_place(1); quit = 1; break;
                    case 2: put_in_place(0); quit = 1; break;
                    case 3: put_back(); quit = 1; break;
                    case 4: advanced = (it->Flags & CHECKED) != 0; write_view(advanced); rc = 99; quit = 1; break;
                    }
                    mc = it->NextSelect;
                }
            }
            else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); }
            else if (cls == IDCMP_INTUITICKS) { if (levels_follow()) show(); }   /* the PC's mixer moved them */
            else if ((cls == IDCMP_MOUSEMOVE || cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN) && gg) {
                int id = gg->GadgetID, again = 0;
                switch (id) {
                case G_VOL: cur.volume = slider_value(gg, code); touched = 1; levels_now(); break;
                case G_PAULA: cur.paula = slider_value(gg, code); touched = 1; levels_now(); break;
                case G_AHI: cur.ahi = slider_value(gg, code); touched = 1; levels_now(); break;
                case G_MUTE: cur.muted = (gg->Flags & GFLG_SELECTED) != 0; touched = 1; levels_now(); break;
                case G_TPAULA:
                    status("Paula: a tone on one of its channels...");
                    status(sp_test_paula(100) ? "That was Paula." : "Paula's channels are all in use.");
                    break;
                case G_TAHI:
                    if (!board) { sp_volume_to_units(&cur); sp_store(&cur, 0); }   /* AHI reads the volume as it opens */
                    status("AHI: a tone through unit 0...");
                    status(sp_test_ahi(0) ? "That was AHI's unit 0." : "AHI could not play: is AHI installed, and its unit free?");
                    break;
                case G_MIX: cur.mix = code; break;
                case G_BEEP: cur.beep = code; again = 1; break;
                case G_BEEPSND: cur.beep_sample = code; again = 1; break;
                case G_TBEEP: beep_test(); break;
                case G_SPEAKER: cur.speaker = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_SAMPLE: strncpy(cur.sample, (const char *)((struct StringInfo *)gg->SpecialInfo)->Buffer, sizeof cur.sample - 1); break;
                case G_BVOL: cur.beep_volume = (WORD)code; break;
                case G_BPITCH: cur.beep_period = 1124 - (WORD)code; break;
                case G_BLEN: cur.beep_duration = (WORD)code; break;
                case G_CLIP: cur.clip = (gg->Flags & GFLG_SELECTED) != 0; break;
                case G_ANTICLICK: cur.anticlick = ((struct StringInfo *)gg->SpecialInfo)->LongInt; again = 1; break;
                case G_CPU: cur.maxcpu = ((struct StringInfo *)gg->SpecialInfo)->LongInt; again = 1; break;
                case G_WHEEL: cur.wheel = ((struct StringInfo *)gg->SpecialInfo)->LongInt; again = 1; break;
                case G_USE: put_in_place(0); quit = 1; break;
                case G_SAVE: put_in_place(1); quit = 1; break;
                case G_CANCEL: put_back(); quit = 1; break;
                default:
                    if (id >= G_UMODE0 && id <= G_UMODE4) {
                        if (code < nmodes) cur.unit[id - G_UMODE0].mode = mode_ids[code];
                        cur.mix = sp_mix_of(&cur);
                        again = 1;
                    } else if (id >= G_UFREQ0 && id <= G_UFREQ4) cur.unit[id - G_UFREQ0].freq = freq_values[code];
                    else if (id >= G_UCH0 && id <= G_UCH4) {
                        LONG n = ((struct StringInfo *)gg->SpecialInfo)->LongInt;
                        cur.unit[id - G_UCH0].channels = n < 1 ? 1 : n > 32 ? 32 : n;
                        again = 1;
                    } else if (id >= G_UVOL0 && id <= G_UVOL4) {
                        LONG n = ((struct StringInfo *)gg->SpecialInfo)->LongInt;
                        cur.unit[id - G_UVOL0].out_volume = (ULONG)(n < 0 ? 0 : n > 100 ? 100 : n) * 0x10000UL / 100;
                        again = 1;
                    }
                }
                if (again) {                                           /* keep what the gadgets show within the settings' limits */
                    cur.anticlick = cur.anticlick < 0 ? 0 : cur.anticlick > 100 ? 100 : cur.anticlick;
                    cur.maxcpu = cur.maxcpu < 10 ? 10 : cur.maxcpu > 100 ? 100 : cur.maxcpu;
                    cur.wheel = cur.wheel < 1 ? 1 : cur.wheel > 25 ? 25 : cur.wheel;
                    show();
                }
            }
        }
    }
out:
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
    if (menu) FreeMenus(menu);
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, scr);
    return rc;
}

static int gui(void)
{
    int r;
    while ((r = gui_once()) == 99) ;
    return r;
}

int main(int argc, char **argv)
{
    LONG args[4] = { 0, 0, 0, 0 };
    /* from Workbench (argc 0) there are no arguments: ReadArgs would read them from
     * libnix's console window, which opens empty and waits */
    struct RDArgs *rd = argc > 0 ? ReadArgs((STRPTR)"FROM,USE/S,SAVE/S,ADVANCED/S", args, NULL) : NULL;
    int rc;
    if (!rd && argc > 0) { PrintFault(IoErr(), (STRPTR)"Sound"); return RETURN_FAIL; }
    board = sp_board();
    if (board) seen_seq = sp_board_seq(board);
    sp_load(&cur, args[0] ? (const char *)args[0] : NULL);
    if (args[1] || args[2]) {                /* from the Shell or at boot: no window */
        if (board && !args[0]) {             /* at boot the board starts at 100%: the saved levels, not the board's */
            sp_settings saved;
            sp_load(&saved, SP_ENV);
            cur.volume = saved.volume; cur.paula = saved.paula; cur.ahi = saved.ahi; cur.muted = saved.muted;
        }
        put_in_place(args[2] != 0);
        if (rd) FreeArgs(rd);
        return RETURN_OK;
    }
    orig = cur;
    advanced = args[3] ? 1 : read_view();
    if (rd) FreeArgs(rd);
    if (!(nmodes = sp_ahi_modes(mode_ids, mode_names, MAX_MODES))) {   /* no AHI: our own modes still have names */
        static const ULONG ids[3] = { SP_MODE_HOSTMIX, SP_MODE_HIFI, SP_MODE_16BIT };
        static const char *names[3] = { "AmigaChrome: Host mix 16 bit stereo++", "AmigaChrome: HiFi 32 bit stereo++", "AmigaChrome: 16 bit stereo++" };
        for (nmodes = 0; nmodes < 3; nmodes++) { mode_ids[nmodes] = ids[nmodes]; strcpy(mode_names[nmodes], names[nmodes]); }
    }
    for (int i = 0; i < nmodes; i++) mode_labels[i] = (STRPTR)mode_names[i];
    mode_labels[nmodes] = (STRPTR)"(none)";
    mode_labels[nmodes + 1] = NULL;
    rc = gui();
    return rc;
}
