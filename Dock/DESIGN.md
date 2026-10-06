# OpenDock and OpenPrefs Dock

OpenDock is a dock on the Workbench screen, and Dock is its prefs editor.
The design comes from openamigaup DESIGN.md: a Mac-like dock that takes
over the docks people already have. MIT, Copyright (c) 2026 Dalsin Limited.

## 0.1 (6 October 2026)

### OpenDock (`C:OpenDock`)

- One row of icons sits along an edge of the Workbench screen: bottom (the
  default), top, left or right, centred.
- A click starts the program. If the program is already running, the click
  brings its frontmost window and screen to the front instead.
- A small mark under an icon shows that its program is running. This is
  checked every 2 seconds. A program counts as running when a process of
  its name exists (Workbench names it so) or a Shell has it as its command.
- Icons dropped on the dock (it is an AppWindow) are added at the end and
  kept, as AmiDock does.
- The menu, opened with the right mouse button while the dock is active,
  has Dock settings..., Remove a button (the next click removes it) and
  Quit.
- Ctrl-F reads the settings again, which is what Dock sends. Ctrl-C ends
  it. Starting a second OpenDock only tells the first to read its settings
  again.
- There is no magnification and no see-through. It draws with the screen's
  pens and the icons as they are (`DrawIconStateA`), so it costs a real
  68040 nothing on its native chipset or a graphics card. Magnification
  stays off with Lite once it exists.
- It needs icon.library 44 and workbench.library 44 (AmigaOS 3.5 or
  later; 3.2 has both).
- Kinds of button:
  - **Workbench** starts the program as a double-click would
    (`OpenWorkbenchObjectA`), so it works for projects and drawers too.
  - **Shell** runs the command line asynchronously from the button's
    drawer, with its stack.
  - **ARexx** runs `SYS:Rexxc/RX` with the script.
  - **Separator** is a line between groups.

### The settings, `ENV:OpenDock/Dock` (format 1)

Each line is `key value`, and a `;` starts a comment. Unknown lines are
skipped. Save writes `ENVARC:` too.

    place bottom|top|left|right
    size small|medium|large            ; 40, 56 or 72 pixel cells; a cell grows to its largest icon
    labels on|off                      ; names under the icons
    running on|off                     ; the running marks
    magnify off                        ; kept for later; never with Lite
    imported "ENVARC:ToolManager.prefs"
    button wb "SYS:Utilities/MultiView" label "MultiView"
    button cli "NewShell" label "Shell" dir "SYS:" stack 8192
    button arexx "REXX:Backup.rexx" label "Backup" icon "SYS:Prefs/Env-Archive/Sys/def_rexx"
    separator

`icon` names an icon without `.info`. Without one, the button shows the
program's own icon, or the default icon for its kind. Quoted values escape
`"` and `\` with a backslash. `src/od_dock.c` is plain C, shared by
OpenDock, Dock and the host tests.

### Taking over another dock

Each reader is written from its file's layout, with no code from the
program that wrote the file.

| From | File | What becomes a button |
| --- | --- | --- |
| ToolManager 2.0-2.2 | `ENV(ARC):ToolManager.prefs`: IFF `FORM PREF`, `PRHD` version 0, then `TMEX` programs, `TMIM` images and `TMDO` docks. Each chunk is a big-endian header whose first long says which NUL-ended strings follow | Every dock's tools in order, with a separator between docks. A tool names its program and image. CLI programs become Shell buttons with their drawer and stack, and `[]` (dropped icons) is taken out. WB programs become Workbench buttons, ARexx becomes ARexx. Dock, hotkey and network tools are left out. An image that is an icon becomes the button's icon; IFF pictures are left out |
| ToolManager 3 | another form | Refused with a plain reason |
| AmiDock (AmigaOS 4) | `ENVARC:Sys/AmiDock.amiga.com.xml`, PrefsObjects XML: `Docks` → `Categories` → `Icons` of { `Name`, `FileName` } | Every dock's icons in order as Workbench buttons, with a separator between docks. `Separator.docky` becomes a separator. Other dockies, such as SubDock and clocks, are left out (a sub-dock's own icons come in as its dock). Entities and UTF-8 become Latin-1 |
| AmiDock (AmigaOS 3.9) | `ENVARC:Sys/amidock.prefs`, a form not yet known | Refused with a plain reason |
| AmiStart | `sm.prefs`, found from its icon's `PREFS` tooltype (`PROGDIR:` is the icon's drawer; `DATAPATH` is where relative icons start); also AmiKit's `sm.config` and Icaros's `ENVARC:Icaros/sm.prefs`. It is text: `NEWDIR NAME="TASKBAR"` ... `ITEM` ... `ENDDIR` | The taskbar's `ITEM`s with `FILE=`: `EXECMODE="0"` becomes Workbench, `"1"` becomes Shell with `ARG=` (hex) as its arguments. With no taskbar programs, the start menu's (`MAIN`). Submenus, drawers (`SYSDIR`) and modules (`EXTERNAL`) are left out |

On the first start, when there is no `ENVARC:OpenDock/Dock`, Dock takes
over the first of these it finds, once. Otherwise it starts with Shell,
Prefs, MultiView and Look. **Take over its buttons** does it on request:
the buttons are added after the dock's own, and when the file isn't where
it usually is, a requester asks for it. From the Shell,
`Dock TOOLMANAGER ENVARC:ToolManager.prefs SAVE` does it with no window
(AMIDOCK and AMISTART work the same way).

`tests/run.sh` runs 43 checks on the host's `cc`. They cover the format
both ways, a ToolManager 2 file built to the format, an AmiDock file and
an AmiStart file written in their layouts, ToolManager 3 and AmiDock 3.9
refusals, entities and UTF-8, and a full dock. The ToolManager sample was
made from the format, because no real file could be fetched here. A real
ToolManager 2 file is the first thing to try on an instance.

### OpenPrefs Dock (`SYS:Prefs/Dock`)

A GadTools prefs editor like the others: Save, Use, Test (15 seconds),
Cancel, and Ctrl-F to the `OpenDock` port.

- **Simple** view: the buttons in order (Up, Down, Remove, add a Line),
  place, size, names under the icons, and taking over from ToolManager,
  AmiDock or AmiStart.
- **Advanced** view (View > Advanced, Amiga-A, shared in
  `ENV:OpenAmiga/PrefsView`): the running marks, and the chosen button in
  full: what it starts with (Workbench, Shell, ARexx), name, command, icon,
  drawer and stack.

    Dock [FROM file] [TOOLMANAGER file] [AMIDOCK file] [AMISTART file] [USE] [SAVE] [ADVANCED]

### Building

`Dock/build.sh [OUT_DIR]` uses the os32 stove (or `CC=`) and builds
`OpenDock` and `Dock` for a 68020 or better, with no FPU needed.

### For the OpenUp part (to add in openamigaup)

    dock = Part(top, "OpenDock", "0.1", "OpenDock: a dock that takes over ToolManager's, AmiDock's or AmiStart's, and its editor")
    dock.add("C/OpenDock", (a.openamigaprefs / "Dock/build/os3/OpenDock").read_bytes(), "SYS:C/OpenDock")
    dock.add("Prefs/Dock", (a.openamigaprefs / "Dock/build/os3/Dock").read_bytes(), "SYS:Prefs/Dock")
    dock.add("Prefs/Dock.info", icons.icon(icons.WBTOOL, "page", stack=16384), "SYS:Prefs/Dock.info")
    dock.line("startup Run >NIL: C:OpenDock")
    # once, at install, before OpenDock first starts: the first dock found taken over
    #   SYS:Prefs/Dock SAVE

`Dock SAVE` with no other arguments does the first-start take-over and
saves the result.

## Next

- Taking over ToolManager 3 and AmigaOS 3.9's AmiDock once their files are
  known (a copy of each from a real machine).
- DOpus button banks.
- Drag to reorder on the dock itself.
- The theme's colours from OpenLook.
- Magnification, off with Lite.
