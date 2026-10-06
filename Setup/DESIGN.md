# OpenUp Setup

The first-start wizard (openamigaup DESIGN.md section 6). It runs once from
`SYS:WBStartup` the first time Workbench starts after OpenUp is installed,
and stays in `SYS:Prefs` to run again. MIT, Copyright (c) 2026 Dalsin Limited.

## 0.1 (6 October 2026)

| Page | What it offers |
| --- | --- |
| 1. Welcome | What this machine is (`ENV:OpenUp/Summary` from `OpenUpTool DETECT`, or the CPU and FPU when that is missing). **Simple** or **Advanced**. "Don't show this at start again" |
| 2. Your desktop | The desktop profiles in `SYS:Prefs/Presets/OpenPrefs` (Modern, Lite 68040, Classic 3.2, and any added), each with its own line saying what it is for. "Choose by machine at every start". In Advanced, one button each for Look, Menus, Windows, Dock and File types (greyed when that editor isn't installed) |
| 3. Screen, printer and network (Advanced) | Screen mode, Printer, OpenPrint, Network (`SYS:Prefs/OpenSocket`), Fonts: the editors themselves |
| 4. What starts with the machine (Advanced) | Every line in OpenUp's marked block in `S:User-Startup`, in plain words ("The theme on windows and gadgets (OpenLook)"), each with a switch |
| 5. All set | Finish |

Simple goes Welcome, Desktop, Done: choose a theme and go. Nothing changes
until **Finish**. **Later** (or the close gadget) leaves everything as it
was.

Setup keeps no settings of its own. On Finish:

- The profile is applied through Look, with
  `SYS:Prefs/Look FROM <profile> SAVE`, adding `profile auto` when "by
  machine" is ticked. Look owns `ENV:OpenGadTools/Look`.
- The view is written to `ENV:OpenAmiga/PrefsView` and `ENVARC:`, so every
  OpenPrefs editor opens in the same view (Menus DESIGN.md, "Simple and
  Advanced").
- The startup switches are applied (see below).
- `ENVARC:OpenAmiga/Setup.done` is written. "Don't show this at start
  again" writes it on Later too.

`OpenUp-Setup [FIRSTSTART] [ADVANCED]`. With FIRSTSTART, or when started
from `SYS:WBStartup`, it does nothing once `Setup.done` exists.

### Startup switches (a proposal for OpenUpTool)

A line that is switched off stays in the block with `;OFF ` in front, so
AmigaDOS skips it:

    ;BEGIN OpenUp
    ; Added by OpenUp. OpenUpTool UNINSTALL takes it out.
    Run >NIL: C:OpenLook
    ;OFF Run >NIL: C:OpenMenus
    ;END OpenUp

Setup also lists the switched-off commands in `ENVARC:OpenAmiga/Startup.off`,
one per line, with `;` comments allowed. `OpenUpTool STARTUP` writes the
block again from the manifest. For it to keep the switches, its
`write_startup` needs one more step: put `;OFF ` before every `startup`
entry whose command is a line of `Startup.off`. Then a reinstall or a new
part keeps what Dale switched off. Until OpenUpTool does this, a new part's
install puts every line back on.

### Building

`Setup/build.sh [OUT_DIR]` uses the os32 stove (or `CC=`) and builds
`OpenUp-Setup` for a 68020 or better, with no FPU needed.

### For the OpenUp part (to add in openamigaup)

    setup = Part(top, "OpenUpSetup", "0.1", "OpenUp Setup: the first-start wizard, with the startup switches")
    setup.add("Prefs/OpenUp-Setup", (a.openamigaprefs / "Setup/build/os3/OpenUp-Setup").read_bytes(), "SYS:Prefs/OpenUp-Setup")
    setup.add("Prefs/OpenUp-Setup.info", icons.icon(icons.WBTOOL, "page", stack=16384), "SYS:Prefs/OpenUp-Setup.info")
    # its first start: a project icon in WBStartup (Setup sees it was started from there)
    setup.add("WBStartup/OpenUp-Setup", b"", "SYS:WBStartup/OpenUp-Setup")   # an empty project file
    setup.add("WBStartup/OpenUp-Setup.info", <a project icon: default tool SYS:Prefs/OpenUp-Setup, tooltype DONOTWAIT>, "SYS:WBStartup/OpenUp-Setup.info")

## Next

OpenUp Picks (ticking programs to add) joins as a page once the Picks list
exists. Screen mode with OpenRTG's boot to RTG joins the Devices page when
OpenRTG has its own editor.
