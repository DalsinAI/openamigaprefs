# OpenPrefs Title bar and OpenTitle

OpenTitle fills the Workbench screen's title bar: AmigaChrome's logo at the
far left, the free chip and fast memory after Workbench's title, and at the
right end the day, date and time, the network icons, OpenSpeaker's speaker
and a cog with the settings editors. OpenPrefs Title bar is its editor.
MIT, Copyright (c) 2026 Dalsin Limited.

![The cog's list of settings editors, open on the Workbench screen](docs/cog.png)

## What it shows

| Item | Where | Setting |
| --- | --- | --- |
| AmigaChrome's logo | The bar's far left, beside Workbench's own title only | `logo on` |
| Free chip and fast memory | Workbench's title format (the `WBTF` chunk of `ENV:Sys/Workbench.prefs`), so IPrefs shows it and Workbench updates it | `memory on` |
| Clock | Right end, left of the network icons | `clock on`, `clock.date on` |
| Network icons (0.4) | LAN and Wi-Fi from OpenSocket, between the clock and the speaker; a click shows the address and "Network settings..." | `network on` |
| Settings cog (0.5) | The bar's far end: right of OpenSpeaker, left of the screen's depth gadget | `cog on` |
| Black border | The display's border around the screen | `border.black off` |

The settings are in `ENV:OpenPrefs/TitleBar` (and `ENVARC:` on Save), one
`name on|off` a line. A line that isn't there keeps its default, so a file
written before 0.5 has the cog on. Save writes `ENV:` and `ENVARC:`; Use
writes `ENV:`; Cancel writes nothing. Either way the editor sends Ctrl-F to
the task of the public port `OpenTitle`, or starts `SYS:C/OpenTitle` when
something is on and it isn't running. From the Shell:
`TitleBar [FROM file] [USE] [SAVE]`.

![The Title bar editor 0.2](docs/window.png)

## The tray: who sits where at the right end

Each program at the bar's right end keeps a file in `ENV:OpenMenus/Tray/`
holding `width order`. A lower order is nearer the bar's end; each program
places itself after the widths of those with a lower order and looks again
every two seconds, so the row closes up when one is switched off or isn't
installed. OpenMenus' own bar (at an edge) keeps room for the sum.

| Order | Program | File |
| --- | --- | --- |
| -5 | OpenTitle's cog | `Cog` |
| 0 | OpenSpeaker | `Speaker` |
| 5 | OpenTitle's network icons | `Network` |
| 10 | OpenTitle's clock | `Clock` |

With the menu bar in the screen's title bar (the default), the first item
sits two bar heights in from the right edge, clear of the depth gadget.

## The cog (OpenTitle 0.5, 8 October 2026)

The user asked on 8 October 2026 for "a cog wheel in the workbench screen
bar, after the sound speaker and before the send back and forward". It lives
in OpenTitle, which already places the clock and the network icons, so it
costs one small window and no new process.

- **Look.** Drawn in the screen's DrawInfo pens, so it follows the theme on
  a true-colour screen and on AGA alike: the bar's detail pen on the bar's
  block pen; raised (shine and shadow lines) while the pointer is over it;
  filled (fill pen, fill text pen) while its list is open, as the speaker is.
  It is 13 pixels square on a bar 15 or more high, 11 or 9 on a lower one.
- **Click** it: a list opens under it (above it on a bottom or side bar),
  drawn as OpenMenus draws its menus: the editors that are installed, a
  line, and **All Prefs...**. The entry under the pointer is lit. A click on
  an entry, or Return on a lit one, starts it; Esc, a click elsewhere or a
  click on the cog closes the list; the cursor keys move the light.
- **The entries**, the same as OpenUpMenu's Tools > Preferences
  (openamigaup `src/openupmenu.c`): Look, Windows, Dock, Menus, Title bar,
  Sound, OpenTypes, OpenUp Setup, each shown only when `SYS:Prefs/<name>`
  is there (Sound only with OpenUp's `SYS:C/OpenSpeaker`, since
  `SYS:Prefs/Sound` is OS 3.2's own until OpenUp's replaces it), then
  **All Prefs...**, which opens the `SYS:Prefs` drawer.
- **Starting one** is a double-click on its icon: `OpenWorkbenchObjectA()`
  (workbench.library 44), so the icon's stack and tool types count. Without
  it, the program is run with `NIL:` for its input and output; the drawer
  needs Workbench.
- The window you were working in is active again after the list closes,
  and the bar keeps showing its title while the list is open.
- OpenTitle now waits on its windows and timer.device instead of sleeping:
  a click is answered at once, and it looks at the pointer ten times a
  second only while the pointer is on the bar (for the cog's light), twice
  a second otherwise.

## Tested

On a scratch OS 3.2.3 (AmigaChrome, OpenRTG 800x600, CGTriumvirate 13, the
Open light theme), with OpenUp 0.6.14 installed and these builds put in:

- The cog sits right of the speaker's "100%" and left of the depth gadget;
  off in the editor, the speaker, network icons and clock close up to the
  depth gadget within two seconds, and on again they make room.
- Hover lights it; a click opens the list with all eight editors and All
  Prefs...; the entry under the pointer is lit.
- Look, Windows, Menus, Title bar, Sound and OpenTypes each opened from the
  list, with no console window; All Prefs... opened the Prefs drawer.
- A second click on the cog, a click on the desktop, and Esc each closed the
  list, and the window that was active before was active again.

Not tested: OpenMenus' bar at an edge, an AGA screen (the drawing uses only
DrawInfo pens), and a Workbench older than 44.
