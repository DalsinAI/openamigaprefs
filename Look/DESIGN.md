# OpenPrefs Look

The editor for OpenLook, the OpenGadTools look on Workbench and every
GadTools program. OpenLook draws; Look sets what it draws. MIT, Copyright
(c) 2026 Dalsin Limited.

![Look 0.1, testing the Graphite theme](docs/window.png)

## 0.1 (6 October 2026)

| Setting | Where it goes |
| --- | --- |
| Theme, from `SYS:Prefs/Presets/Themes`, with a preview | First line of `ENV:OpenGadTools/Look` |
| Light, dark, or by the clock (dark from/to) | First line, and `mode auto 19 7` |
| Own accent (`#rrggbb`) | `accent` |
| Lite: automatic (on for a real 68040 or 68060 without OpenGPU), on (with no shadows, square, flat), off | `lite`, `lite.shadows`, `lite.rounding`, `lite.gradients` |
| Workbench, other public screens, game screens: themed or Classic | `screen workbench|public|custom` |
| Programs never touched (comma between names) | `never` lines |
| Icon labels: plain, fields, shadow, outline | Font prefs' Workbench icon text (`ENV:Sys/font.prefs`) |
| Desktop profiles: Load one into the editor; By machine lets OpenLook choose at start | `SYS:Prefs/Presets/OpenPrefs/<name>.profile`; `profile auto` |
| Click to front (any click, or with a key), Focus follows (AutoPoint) | The OS's commodities: on at start (copied into `SYS:WBStartup`, ClickToFront's `QUALIFIER` tooltype) and started or stopped now |

Save writes `ENV:` and `ENVARC:`; Use writes `ENV:`; Test writes `ENV:` and
puts it back after 15 seconds unless Use or Save follows; Cancel puts back
what a Test changed. Each tells OpenLook (Ctrl-F to its port's task), which
redraws the open windows' frames and gadgets. From the Shell:
`Look [FROM file] [USE] [SAVE]`.

The three profiles OpenUp installs: **Modern** (AmigaChrome: Open, dark by
the clock), **Lite 68040** (a real 68040 or 68060: Open, Lite) and
**Classic 3.2** (any machine: the OS's own look). A profile is OpenLook's
prefs format with `target` lines (opengadtools DESIGN.md section 2i).

Holding Shift while OpenLook starts gives the OS's own look until the look
is changed again (safe start).

**Views (0.2).** Look opens in Simple: the theme with its preview, the mode,
and the profiles. View > Advanced (Amiga-A, or `ADVANCED` from the Shell)
shows every setting. The choice is shared by all OpenPrefs editors as
`simple` or `advanced` in `ENV:OpenAmiga/PrefsView` (and `ENVARC:`).

**Started from Workbench (8 October 2026).** A double-click on Look's
icon, or Tools > Preferences > Look, opened an empty console window first,
and Look's own window only once that console was closed. The cause: Look
is built with libnix, which gives a program started from Workbench a
console (`CON://///AUTO/CLOSE/WAIT`) as its input and output, and Look
called `ReadArgs()` at every start; with no command line, `ReadArgs()`
reads the arguments from the input, which opened the console and waited
on it. Now the arguments are read only from the Shell (`argc` above 0),
as OpenUp Setup already did, and the commands Look runs itself (copying
ClickToFront or AutoPoint into `WBStartup`, starting them) get `NIL:` for
their input and output. The same `ReadArgs()` fix went into Title bar,
Windows, Sound, Menus and OpenTypes, which had the same console. Tested on
a scratch OS 3.2.3: Look opened from its icon, from OpenTitle's cog and
from Menus' Look... button, each with no console.

![Look opened from its icon in the Prefs drawer, with no console window](docs/from-workbench.png)

## 0.3 (8 October 2026): the approved look

- **Windows**, three switches in the Simple view, for OpenLook 0.5:
  "Scroll bars when needed" (`scrollers auto`), "No size gadget"
  (`sizegadget off`; OpenWindows 0.5 resizes from the edges) and "No zoom
  gadget" (`zoom off`; a double-click on a title bar brings a window to the
  front). The lines are written with the others, and kept when Look saves.
- **The Modern and Lite 68040 profiles** turn all three on: that is the look
  approved on the canvas. Classic 3.2 leaves them off.
- **The Open theme is now flat** (opengadtools: OpenLook 0.5), so the
  default accent is its blue, `#2f6fb3`; the old glassy Open is Glass.

Not yet built on the os32 stove.

## OpenLook 1.1 plan (12 October 2026): Appearance

The combined preferences design (`docs/combined-prefs/DESIGN.md`, revision
of 10 October) makes OpenLook's editor the Appearance editor, P1 of that
design. This is the plan; the sections after it say what was built.

**Tabs.** OpenLook 1.0 has four: Look, Windows, Title bar, Menus. 1.1 adds
six, in the design's order, and names Look's tab Theme:

| Tab | File it reads and writes | What it sets |
| --- | --- | --- |
| Theme | `ENV:OpenGadTools/Look` | As Look does today |
| Pointer | `ENV:Sys/pointer.prefs` | The pointer images (Normal, Busy and the other types the OS 3.2 file holds), from the sets in `SYS:Prefs/Presets/Pointers`, with a preview and the hot spot |
| Fonts | `ENV:Sys/font.prefs` | The screen font, the icon font, the system (fixed-width) font, the icon labels' style and pens |
| Colours | `ENV:Sys/palette.prefs` | The first eight colours, and in Advanced the pens of 4-colour and 8-colour screens |
| Backdrops | `ENV:Sys/wbpattern.prefs` | A picture for the Workbench, drawers and screen, how it is placed, remapped and dithered |
| Workbench | `ENV:Sys/workbench.prefs` | Icon borders, label length, the disk gauge, the title bar and its text, icon support, stack, lists, copying |
| File requesters | `ENV:Sys/asl.prefs` | How files are sorted, where the requester opens and how big |

Windows, Title bar and Menus stay as they are.

**One PREF reader and writer.** `Look/src/pf_iff.c` holds a PREF file as
its list of chunks in file order, PRHD first; `pf_prefs.c` reads and
writes the fields of each OS format inside a chunk's bytes, leaving every
byte it has no field for as it was. `pf_amiga.c` loads and saves the list
with iffparse.library (`ParseIFF()` in raw steps; `PushChunk()`,
`WriteChunkBytes()`, `PopChunk()`). A chunk no tab knows (the 3.2 Workbench
file's hidden devices, a newer release's chunks) is carried across as it
came. Both are plain C, so `Look/tests/run.sh` tests them on the host, as
Dock, Menus and Sound are tested: every sample file reads and writes back
byte for byte, each field change touches only its own bytes.

**Only what changed is written.** A tab writes its file only when the bytes
it would write differ from the file's (ENV: for Use and Test, ENV: and
ENVARC: for Save). Changing a font leaves the palette, pointer and backdrop
files untouched, and the Theme tab writes `font.prefs` only when the icon
labels' style changed.

**The frame.** The six new tabs share one frame (`Look/src/appearance.c`):
the tabs, the page, a status line and Save, Use, Test and Cancel, with the
Project and View menus. Save writes ENV: and ENVARC:, Use ENV:, Test ENV:
and puts the file back after 15 seconds unless Use or Save follows, and
Cancel puts back what a Test changed. A click on another tab keeps what was
changed, as Use, as the four pages do today. Intuition and Workbench pick
the files up through IPrefs, as when the OS editors save them; nothing
hooks Workbench beyond what OpenLook already does. The OS editors stay in
`SYS:Prefs/Classic`.

**Simple and Advanced.** Each tab opens in Simple with its two to five
most used settings (the screen and icon fonts; the eight colours; the
Workbench picture and how it is placed; the pointer set), and View >
Advanced (Amiga-A) shows every setting, shared as now through
`ENV:OpenAmiga/PrefsView`.

**640 x 256.** The tabs wrap into two rows when one row can't hold them
(the ten names in Topaz 8 need about 850 pixels); the row with the open tab
sits next to the page, as on other systems with two rows of tabs. Each new
tab lays out in two columns inside the 620-pixel width the editor has
today, and no taller than the Theme tab's Simple view; when the window is
already taller, the status line and buttons go to its foot. On a screen
under 400 lines the Theme tab's preview gets shorter, so its Simple view
fits a 256-line screen below the title bar.

**Pointer acceleration** is not in 1.1. OpenMouseAccel (openamigaup
`src/openmouseaccel/`) keeps `ENV:OpenUp/OpenMouseAccel.prefs`, OpenInput's
design (`Design-Pointer-Acceleration.md`) names other keys in
`ENV:OpenInput/`, and nothing reads either yet; which file, and how it
meets the OS's own accelerated mouse, needs a decision first (the design's
questions 2 and 3).

**Shell.** `OpenLook POINTER`, `FONTS`, `COLOURS`, `BACKDROPS`,
`WORKBENCH` and `FILEREQUESTERS` open on that tab, as `OpenLook WINDOWS`
does; `THEME` is a second name for `LOOK`.

## Next

Windows (snapping, Amiga+Tab, wheel under the pointer, remembered window
places, new-drawer defaults), then OpenMenus. The frame and title geometry
(theme format 2) and one text-size scale follow the OpenPrefs page.
