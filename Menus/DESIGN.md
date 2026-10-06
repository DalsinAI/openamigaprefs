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
| Menus open from the screen bar, at the pointer, or by where the pointer is | `open pulldown\|popup\|pointer` |
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

### For the OpenUp part (to add in openamigaup)

    menus = Part(top, "OpenMenus", "0.1", "OpenMenus: our own menus, and their editor")
    menus.add("Prefs/Menus", (a.openamigaprefs / "Menus/build/os3/Menus").read_bytes(), "SYS:Prefs/Menus")
    menus.add("Prefs/Menus.info", icons.icon(icons.WBTOOL, "page", stack=16384), "SYS:Prefs/Menus.info")
    # the engine, C/OpenMenus, from Main Discourse; then:
    menus.line("startup Run >NIL: C:OpenMenus")
    # once, at install: MagicMenu's settings taken over (does nothing without them)
    #   SYS:Prefs/Menus MAGICMENU ENVARC:MagicMenu.prefs SAVE

## Next

Right-click menus stay a design until Dale has seen it. Before anything
hooks Workbench, the design goes back to Dale.
