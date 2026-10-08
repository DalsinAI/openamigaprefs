# OpenPrefs Sound and OpenSpeaker

One editor for all of the Amiga's sound, in place of Prefs/Sound and
Prefs/AHI, and a speaker on the menu bar that shows and turns the volume.
Dale asked for them on 6 October 2026 ("a single combined sound prefs and a
menu bar speaker and volume control"), chose that the new editor **replaces**
the two old ones (they move to `Prefs/Classic`), and approved the mockup:
https://claude.ai/artifact/W1mg2U2wxfxtmgRySFsdHG. MIT, Copyright (c) 2026
Dalsin Limited.

## 0.1 (6 October 2026)

### The levels

| Level | On AmigaChrome (the ACAHI board) | On a real Amiga |
| --- | --- | --- |
| Volume | The board's `LEVELS` register: the PC's mixer turns the instance's sound by it | AHI's output volume on every unit, heard when a program next opens AHI |
| Amiga sound (Paula) | `LEVELS`: Paula's share | Not shown: Paula has no master volume, so the line says to use the speakers' own knob |
| AHI sound | `LEVELS`: AHI's share | (the volume) |
| Mute | `LEVELS`: mute | Volume 0 |

The sliders move the sound as they are dragged. The PC's mixer page has the
same numbers under each instance ("Amiga's own volume") and moves them too:
the board's `LEVEL_SEQ` changes on every change from either side, and the
open editor's sliders follow it (Save keeps what the PC set). The
register is in amigachrome #206 (`ACAHI_CAP_LEVELS`); an older runtime
without it reads as no board.

### The rest

- **Mix AHI sound on:** the PC (the ACAHI Host mix mode, which frees the
  Amiga's CPU; recommended) or the Amiga (HiFi, for programs that use AHI's
  echo or DSP). It sets every AHI unit that is on an AmigaChrome mode.
- **When a program beeps:** play a sound, flash the screen, both, or
  nothing; the beep or a sound file, with Test. This is the OS's own
  `sound.prefs`, which IPrefs reads.
- **Show the speaker on the menu bar** (OpenSpeaker).
- **Advanced** (View > Advanced, shared by every OpenPrefs editor): the
  beep's sound file, volume, pitch and length; AHI's units 0-3 and the music
  unit, each with its mode (from AHI's own list), rate, channels and volume;
  Clip, Anti-click and CPU limit; how far a wheel step turns the volume.

### Files

| File | What | Written as |
| --- | --- | --- |
| `ENV:OpenAmiga/Sound` | Our own: `volume`, `paula`, `ahi`, `mute`, `mix pc\|amiga`, `speaker`, `wheel` (format 1, "key value") | Ours |
| `ENV:Sys/sound.prefs` | The beep | The OS's format (FORM PREF, PRHD, SOND) |
| `ENV:Sys/ahi.prefs` | AHI's units and globals | AHI's format (AHIG, AHIU); chunks we don't know go back byte for byte |

Save writes `ENV:` and `ENVARC:`, Use writes `ENV:`, Cancel puts the levels
back as they were. `Sound [FROM file] [USE] [SAVE] [ADVANCED]` from the
Shell. `Sound USE` at boot (OpenUp's startup) gives the board the saved
levels (the board starts at 100%) and starts OpenSpeaker when it is to show.

`src/sp_core.c` (the settings and the three files) is plain C, tested on the
host: `tests/run.sh`. `src/sp_amiga.c` is the board and the files on the
Amiga, shared by the editor and OpenSpeaker. `src/sp_test.c` plays the test
tones (Paula through audio.device, AHI through ahi.device unit 0) and lists
AHI's modes.

## The speaker on the menu bar (OpenSpeaker 0.1)

A small window at the far end of the Workbench screen's menu bar, showing a
speaker and the volume ("70%", or "Mute").

- **Click** it: the levels open under it (above it on a bottom or side bar):
  Mute, Volume, Paula and AHI, the line "Plays on: PC speakers, through
  AmigaChrome", and **Sound prefs...**. A click elsewhere closes them. The
  window you were working in stays active the whole time: the levels never
  take the activation, and give it straight back after a slider is dragged.
- **Mouse wheel** over the speaker or its levels: the volume up or down by
  the wheel step (5% by default). Turning it up unmutes.
- **Middle button** on it: mute, and again to unmute.
- A click on the speaker never takes the activation from the window you are
  working in, as with OpenMenus' bar. It comes back in front when a window
  is dragged over it.
- Where the volume was left is kept (`ENV:` and `ENVARC:`) when the levels
  close, or when the wheel has been still for two seconds.

**Where it sits:** with the menu bar in the screen's title bar (the default,
or when OpenMenus doesn't run), at its right end, before the screen's depth
gadget. With OpenMenus' own bar at an edge (OpenMenus 0.4), at that bar's
end: the right end of a top or bottom bar, the bottom of a side bar. It
reads the bar's place from `ENV:OpenMenus/Menus` every two seconds, and
moves when the bar does.

**The tray:** the end of the menu bar is shared with the other programs
that sit there, the first being the clock (Workbench's title with free memory, and the
date and time at the right). Each keeps a file in `ENV:OpenMenus/Tray/`,
named for it, holding `width order`: its width in pixels, and its place,
where a lower order is nearer the bar's end. The speaker is order 0, at
the very end (Dale's title mockup 2), until OpenTitle 0.5's settings cog
(order -5) took the end on 8 October 2026; the clock is order 10, just left
of it, and the network icons order 5 (TitleBar/DESIGN.md has the table). Each program
places itself after the widths of those with a lower order (on a side bar,
one row above each), looks again every two seconds, and deletes its file
when it quits. OpenSpeaker writes `Speaker` and sends OpenMenus Ctrl-F.
OpenMenus' own bar keeps room for the sum of the widths (a row each on a
side bar), since openamigaprefs #19. In the title bar, the default, the
programs only keep clear of each other.

Ctrl-C or Exchange quits it. Ctrl-F (sent by Sound prefs on Save or Use)
reads the settings again, and it quits when "Show the speaker on the menu
bar" is off. Sound prefs starts it when that is switched on.

## Not yet

- Testing on an instance and on the real 68040 (Main Discourse has the 68k
  build and the instance tests).
- OpenUp: `SYS:Prefs/Sound` in place of the old two (moved to
  `SYS:Prefs/Classic`), `C:OpenSpeaker`, and `Sound USE` in the startup.
- Phase 2 of the menu bar, the start pop-up from OpenDock, may carry the
  speaker too.
- AHI's per-unit Monitor, Input gain and inputs in the Advanced view; the
  echo and DSP effects are AHI's own, set by programs.
- OpenLook's theme colours for the speaker (it uses the screen's bar pens).
