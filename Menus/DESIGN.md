# OpenPrefs Menus

The editor for OpenMenus, the Open family's own menus (MIT). OpenMenus draws;
Menus sets what it draws. MIT, Copyright (c) 2026 Dalsin Limited.

## 0.1 (6 October 2026)

A GadTools prefs editor like the OS's own: **Save** writes `ENV:` and
`ENVARC:`, **Use** writes `ENV:` (it lasts until the next reboot), **Test**
writes `ENV:` and puts it back after 15 seconds unless Use or Save follows,
and **Cancel** puts back what a Test changed. Each one tells OpenMenus
(Ctrl-F to the task of the `OpenMenus` port), and OpenMenus reads the file
again.

| Setting | Line in `ENV:OpenMenus/Menus` |
| --- | --- |
| OpenMenus on, or the OS's own menus pixel for pixel | `enabled on\|off` |
| Menus open from the screen bar, at the pointer, or by where the pointer is ("Bar or pointer") | `open pulldown\|popup\|pointer` |
| Hold and release, sticky (open on move), sticky (open on click), for each kind | `pulldown hold\|sticky\|click`, `popup ...` |
| A delay before a menu opens, 0 to 10 tenths of a second, for each kind | `delay.pulldown 0-10`, `delay.popup 0-10` |
| A pop-up opens on the item last chosen | `popup.last on\|off` |
| Submenus centred on their item, and marked with an arrow | `submenus.centre`, `submenus.mark` |
| Single or double border; lite or bold separator lines | `border single\|double`, `separators lite\|bold` |
| Shadow, with its size (1-10) and strength (1-100 %) | `shadow`, `shadow.size`, `shadow.strength` |
| Background: solid, see-through, image, see-through image | `background ...`, `background.image "path"` |
| Colours from the theme (OpenLook), the screen's own pens, or the user's | `colours theme\|screen\|own`, `colour.<part> #rrggbb` |
| Keyboard control, its key (a commodities key description), R.Amiga+R.Alt, pointer to the bar | `keyboard`, `keyboard.key "ramiga space"`, `keyboard.ralt`, `keyboard.top` |
| Programs keep running while a menu is open | `programs.keep-running on\|off` |
| Which MagicMenu file was taken over, so the take-over happens once | `imported "ENVARC:MagicMenu.prefs"` |

The colour parts are `background`, `text`, `selected`, `selected.text`,
`light`, `dark` and `shadow`. With `colours theme` the menus use the
OpenLook theme's `menu`, `menu.text`, `accent`, `accent.text`,
`frame.highlight` and `menu.line` keys, light or dark as Look says.

### The settings format (a proposal for the OpenMenus engine)

`ENV:OpenMenus/Menus` is format 1. It is one setting per line, `key value`,
and a `;` starts a comment. Unknown keys are skipped, so a newer file still
loads in an older reader. A missing key takes its default (`om_defaults`).
`src/om_prefs.c` is plain C with no Amiga calls. It reads and writes the
file, so the engine can compile the same file and the two can never
disagree. The engine is Main Discourse's: change this format only together
with it.

Lite (from Look) wins over the menus. When `lite` is on, OpenMenus leaves
out shadows and see-through, whatever this file says. The editor says so on
its status line.

### MagicMenu's settings, taken over

On the first start, when there is no `ENVARC:OpenMenus/Menus`, nothing has
been taken over yet, and `ENVARC:MagicMenu.prefs` (or `ENV:`) exists, Menus
reads MagicMenu's settings into the window. Save keeps them. **Take over
MagicMenu's...** does the same at any time. From the Shell,
`Menus MAGICMENU ENVARC:MagicMenu.prefs SAVE` does it with no window; this
is what OpenUp's part runs once when it installs OpenMenus.

The reader (`om_from_magicmenu`) is written from the file's layout, with no
MagicMenu code (MagicMenu is GPL). The layout is text: `MagicMenu/2:`, then
lines of `<tab>Name=value`, ending with `#`. Values are `Yes`/`No`, numbers
(decimal or `0x` hex), or quoted strings with `\\` and `\ooo` escapes. A
colour is `Name.R`, `Name.G` and `Name.B`, each 32 bits with the level
spread across all four bytes. MagicMenu 1.x's binary file (it starts with
`0x0131CD52`) is recognised and refused with a plain reason.

| MagicMenu | OpenMenus |
| --- | --- |
| `MenuType` 0 pulldown, 1 popup, 2 pointer-dependent | `open` |
| `PDMode`, `PUMode`: 0 hold, 1 sticky on move, 2 sticky on click (3 keyboard: hold) | `pulldown`, `popup` |
| `PDOpenDelay`, `PUOpenDelay`: 255 none, 1-10 tenths; unset: the old `Delayed` switch | `delay.*` |
| `KCPUCenter`, or 3.0's `PUCenterBox` (0 unset, 1 off, 2 on): a pop-up centred on the item last chosen | `popup.last` |
| `MarkSub`, or `PDMarkSub` | `submenus.mark` (MagicMenu has no centred submenus: `submenus.centre` stays) |
| `DblBorder`, or `PDDblBorder` | `border` |
| `CastShadows`, or `PDCastShadows`; `ShadowDistance`/`PDShadowDist`, `ShadowIntensity`/`PDShadowInt` | `shadow*` |
| `PDBackground` 1 colour, 2 see-through, 3 image, 4 see-through image; or `Transparency`, `PDTransparent`, `TransBackfill`; `Backfill` | `background*` |
| `SeparatorBarStyle`, or `PDSeparatorStyle` | `separators` |
| `PDLook` 2 Multicolor (without `PreferScreenColours`): its own colours `Background`, `Text`, `Fill`, `HighText`, `LightEdge`, `DarkEdge`, `ShadowCol`; otherwise the screen's | `colours`, `colour.*` |
| `KCEnabled`, `KCKeyStr`, `KCAltRCommand`, `KCGoTop` | `keyboard*` |
| `NonBlocking` | `programs.keep-running` |

A quoted value in OpenMenus' own file escapes `"` as `\"` and `\` as `\\`, so a
picture's name with a quote in it survives Save.

`tests/run.sh` runs 46 checks on the host's `cc`. They cover reading and
writing the format, a sample MagicMenu file (`tests/sample-MagicMenu.prefs`),
the 1.x binary refusal, and escapes both ways.

### Simple and Advanced

Menus opens in the **Simple** view, which puts the theme first: Use
OpenMenus, how menus open and how they work (one choice for both kinds),
colours, shadow, the theme in use with a **Look...** button, and Take over
MagicMenu's. **View > Advanced** (Amiga-A) shows every setting. The view
hides gadgets only and never changes a setting. `ADVANCED` on the Shell
line opens in Advanced.

The view is shared by every OpenPrefs editor. It is stored in
`ENV:OpenAmiga/PrefsView` and `ENVARC:`, which holds `simple` or
`advanced`, and it is written whenever the view changes. OpenUp Setup asks
for it on its first page.

### From the Shell

    Menus [FROM file] [MAGICMENU file] [USE] [SAVE] [ADVANCED]

### Building

`Menus/build.sh [OUT_DIR]` uses the os32 stove (or `CC=`). The theme reader
comes from an opengadtools checkout beside this one (or `OGT=`). It needs a
68020 or better, an FPU is not needed, and it never uses OpenGPU.

### The engine, OpenMenus 0.1

`Menus/engine/openmenus.c` builds `C:OpenMenus` (`Menus/engine/build.sh`).
It is a commodity and does not patch Intuition:

- **Taking the button.** A custom commodity object takes the right mouse
  button from the input stream when the active window has a menu strip and
  no RMBTRAP. Intuition never opens its own menus for that press.
- **Drawing.** The strip is copied under Forbid(), and the menus are drawn
  from the copy in borderless windows that never take the activation. These
  are the screen bar's titles (pull-down), or a list of titles at the
  pointer (pop-up), then the items, then the subitems.
- **Following the mouse.** The mouse is followed every 20 ms, and the buttons
  go to OpenMenus while a menu is open. Esc closes the menu.
- **Giving the choice.** The choice goes to the program as Intuition gives
  it: IDCMP_MENUPICK with FULLMENUNUM on the window's port, with check marks
  and mutual exclusion set first in the window's own strip. MENUNULL is sent
  when nothing is chosen. Before anything is changed, the item is looked up
  again in the strip as it is then. A program that changed or cleared its
  menus meanwhile gets MENUNULL, and a window closed meanwhile gets nothing.
- **MENUVERIFY.** It is asked first, as Intuition asks it. A program's
  MENUCANCEL keeps the menu shut.
- **Settings.** It reads this file at start and on Ctrl-F to the task of its
  port `OpenMenus`. Colours come from the theme, the screen's pens, or the
  user's. Lite leaves out the shadow.

Tested on a scratch OS 3.2.3 (OpenRTG 800x600, 6 October 2026), all with
"Bar or pointer" and hold and release:
- Workbench's menus from the bar: Backdrop? toggled Workbench's backdrop.
- Workbench's menus from the bar: Open volume > RAM Disk opened the RAM Disk.
- A pop-up at the pointer: the check mark on Backdrop? showed.
- OpenPrefs Windows' GadTools menus as a pop-up: View > Advanced switched
  the editor's view.

Not in 0.1: keyboard control, the opening delays, see-through and pictures,
and a pop-up that opens on the item last chosen. The sticky modes are written
but not yet tested. The Menus editor keeps these settings, and changing one
says on its status line that OpenMenus 0.1 keeps it for later.

## Keyboard control (0.3)

`keyboard.key` (Right Amiga + Space) or Right Amiga + Right Alt opens the
active window's menus from the bar, on the first title. Left and Right move
between titles (Right on an item with a submenu opens it, Left closes it),
Up and Down between the items that can be chosen (Shift for the first and
last), a letter moves to the next item starting with it, Right Amiga + a
letter chooses by command key, Return chooses, Space chooses a check mark
and keeps the menu open (the choices go as one NextSelect chain), Esc closes
one level and Help sends IDCMP_MENUHELP. The same keys work a menu opened
with the mouse, and the mouse takes over from the keys at once. While a menu
is open every key is OpenMenus'; with no menu strip the key goes on to the
program. Where a key moves the highlight is `src/om_kbd.c`, tested on the
host by `tests/test_om_kbd.c`. Design: KEYBOARD_CONTROL.md (6 October 2026).

Tried on OS 3.2.3 (OpenRTG off, Picasso96), 6 October 2026: Right Amiga +
Space opened Workbench's menus; Down, Right, Return on Open volume > RAM Disk
opened it; "u" in Window moved to Update; Space on Backdrop? set the check
and kept the menu open, and Esc then switched Workbench to backdrop; Right
Amiga + B in the open menu switched it back; the mouse took over from the
keys and the keys from the mouse.

### Right-click on icons and the desktop (OpenMenus 0.2)

The design Dale approved on 6 October 2026 ("build it"). The right button on
an icon, or on the background of a Workbench window, opens a menu at the
pointer:

- **Icon:** Open, Open with (OpenView, MultiView, Choose a program), Information,
  Rename, Copy, Snapshot, Leave out (Put away on the desktop), then Extract here and
  Open in OpenCompress for archives, Send to PC, then Delete. A disk gets Format
  and Eject, and the Trashcan gets Empty trash. Several selected icons get the
  entries that make sense for several, under "N icons".
- **Background:** for the desktop, Execute command, Shell, Arrange icons, Show,
  Select contents, Redraw all, Update all, Backdrop picture, Look, Screen mode,
  Backdrop and About. A drawer window gets New drawer, Open parent, Arrange icons,
  Show, View by, Select contents, Update, Snapshot window and Close.

**How it works.** It goes through Workbench's own ARexx port, `WORKBENCH`, and
patches nothing:
- Workbench is asked which window was clicked (matched by its box), where that
  window's icons are, and which icons are selected.
- Right-clicking an icon that isn't selected selects it, as a click would.
- The item chosen is carried out with `WINDOW <name> ACTIVATE` and then
  `MENU INVOKE <ICONS.…|WINDOW.…|WORKBENCH.…>`. `MENU WINDOW <name> INVOKE` did
  nothing on 3.2.3.
- If Workbench is busy with a requester, the menu gives up after a second, so
  the mouse is never held.
- The right button on the screen bar or in a program's window is unchanged.

`Menus/engine/wbprobe.c` sends commands to the port and prints the answers.

**Settings** (format 1): `rightclick` (on by default since Dale approved it), `rightclick.extras`
(the Open family's entries), `rightclick.selection all|one` and
`rightclick.name`.

**In the Menus editor** (board 4 of the design): the Simple view has an
**Icon menus** checkbox, with the line "The right button on the screen bar or
in a program's window always opens that program's own menus." under both
columns. The Advanced view puts Icon menus beside Use OpenMenus, then
**They show** (Workbench's only, or with Open's too), **Selected** (that icon
only, or the whole selection) and **Name at the top**; the line shows on the
status bar when Icon menus is switched. To fit a 256-line PAL Workbench with
these, the two delays share a row, Open on last sits beside Programs run on,
the shadow's size and strength share a row, and Take over MagicMenu's moves
under the preview. The Advanced view keeps PAL's tighter spacing on every screen,
so it is 251 lines high (Topaz 8) wherever it is measured.

### For the OpenUp part (to add in openamigaup)

    menus = Part(top, "OpenMenus", "0.1", "OpenMenus: our own menus, and their editor")
    menus.add("Prefs/Menus", (a.openamigaprefs / "Menus/build/os3/Menus").read_bytes(), "SYS:Prefs/Menus")
    menus.add("Prefs/Menus.info", icons.icon(icons.WBTOOL, "page", stack=16384), "SYS:Prefs/Menus.info")
    menus.add("C/OpenMenus", (a.openamigaprefs / "Menus/build/os3/OpenMenus").read_bytes(), "SYS:C/OpenMenus")
    menus.line("startup Run >NIL: C:OpenMenus")
    # once, at install: MagicMenu's settings taken over (does nothing without them)
    #   SYS:Prefs/Menus MAGICMENU ENVARC:MagicMenu.prefs SAVE

## Where the menu bar sits (designed, not built)

We, 6 October 2026: "we should support the same menu bar options as Windows
and Linux, top and bottom, left and right sides of the screen, or as a 'start'
menu like pop up from OpenDock. The latter might need to follow in a phase 2."

**Phase 1: the bar at any edge.** Intuition always draws the menu titles in
the screen's title bar, at the top. OpenMenus already draws its own panels
(borderless windows that never take the focus), so it can draw the bar the
same way, along whichever edge is chosen:

| `bar` | What the user sees |
| --- | --- |
| `title` (the default) | The screen's own title bar, as now: nothing moves |
| `top` | OpenMenus' own bar along the top, the screen's title bar hidden behind it |
| `bottom` | The bar along the bottom, as Windows' taskbar sits |
| `left`, `right` | A bar down the side, the menu titles one under another; a menu opens beside its title, towards the middle of the screen |

- **Which titles:** the active window's MenuStrip, read as the pull-downs read
  it now (copied under Forbid). With no window active, Workbench's own.
- **On which screens:** `bar.screens workbench|all|rtg`. A game's own screen
  never gets one: only screens that show a title bar of their own.
- **Room for it:** for `bottom`, `left` and `right`, Workbench's backdrop
  window and new windows keep clear of the bar, through OpenWindows' places
  (it already places windows). The bar's width is the widest title.
- **Autohide:** `bar.autohide on|off`. Hidden, it shows when the pointer
  touches its edge, as a Linux panel does.
- **Keyboard control (0.3)** moves along the bar whichever way it lies: Left
  and Right across, Up and Down down the side.
- **The editor:** a "Menu bar" cycle (Title bar, Top, Bottom, Left, Right),
  the screens, and Autohide, in Simple view; the Advanced view adds the bar's
  width and font.
- **Programs that draw in the title bar** (a clock commodity, a screen's
  title text) still have it with `title`; with an OpenMenus bar on top, the
  title bar's own text shows at the bar's far end.

**Phase 2: a start-style menu from OpenDock.** A button at the dock's end opens
a pop-up of programs, the Workbench menu's own items, and Shut down/Reboot,
laid out as the dock's edge says (up from a bottom dock, across from a side
one). It reuses OpenMenus' panels and its keyboard control. It follows the
phase 1 bar, so the two share the edge rules and how the windows keep clear.

## Next

- The menu bar at any edge (above, phase 1), then the start-style pop-up from
  OpenDock (phase 2).

- In the engine: the opening delays, see-through and pictures, and a pop-up
  opening on the item last chosen. The editor keeps these settings already.
