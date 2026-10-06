# OpenPrefs Windows and OpenWindows

OpenWindows is a commodity that changes how windows behave on Workbench and
every other screen. OpenPrefs Windows is its editor. Neither patches
Intuition: OpenWindows watches the input stream and moves, sizes and
activates windows with the OS's own calls. MIT, Copyright (c) 2026 Dalsin
Limited.

## 0.1 (6 October 2026)

| Behaviour | What it does | Setting |
| --- | --- | --- |
| Snapping | A window dropped near a screen edge, or near another window's edge, lines up with it | `snap on 12` (the distance in pixels) |
| Halves | A window dropped with the pointer at the screen's left or right edge fills that half (resizable windows only) | `snap.halves on` |
| Switcher | Amiga+Tab brings the window at the back to the front and activates it; Amiga+Shift+Tab sends the front window to the back | `switcher on`, `switcher.key lcommand tab` (commodity words) |
| Wheel | The mouse wheel goes to the window under the pointer, which becomes active first | `wheel on` |
| Places | A program's window opens where it was last left, and at the same size when it can be resized | `places on`, `never <program>` lines |
| Drawers too (0.3) | Workbench's drawer windows are remembered the same way, with no Snapshot needed | `places.drawers on` |
| Drawers | Workbench drawer windows open at least this big | `drawer 400 250` (0 0 leaves them) |

The settings are in `ENV:OpenPrefs/Windows`. Save writes `ENV:` and
`ENVARC:`; Use writes `ENV:` only; Cancel writes nothing. Either way the
editor sends Ctrl-F to the task of the public port `OpenWindows`, or starts
`SYS:Tools/Commodities/OpenWindows` when something is switched on and it
isn't running. From the Shell: `Windows [FROM file] [USE] [SAVE] [ADVANCED]`.

Places are kept in `ENV:OpenPrefs/WindowPlaces`, and in `ENVARC:` within
ten seconds of a change, as lines of `"program:title" left top width height`.
The program is the task's name, or the command a Shell runs. A window with
no IDCMP port, such as a console, is known as `window:<title>`. The title is
cut at its first digit, bracket or quote, so a title that shows a count or a
file name still matches. Workbench's own windows are left to Workbench, which has Snapshot, unless
"Workbench drawers too" is on (0.3, Dale 6 Oct 2026 for a 1920x1080
Workbench). Then each drawer is kept as `Workbench:<drawer>` and reopens
where it was left; the root window is never moved. Tested on a scratch
OS 3.2.3: the Work drawer was moved to 188,241 and reopened there after a
reboot. Forget all deletes the file and restarts OpenWindows.

**Views.** The editor opens in Simple, which shows the four switches. View >
Advanced (Amiga-A, or `ADVANCED` from the Shell) shows every setting. The
view is shared by all OpenPrefs editors as `simple` or `advanced` in
`ENV:OpenAmiga/PrefsView` (and `ENVARC:`).

Exchange lists OpenWindows and can disable, enable or quit it.

## Tested

Tested on a scratch OS 3.2.3 on AmigaChrome (OpenRTG 800x600), with input
sent as the instance window sends it:
- **Snapping.** A window dropped 12 pixels from the left edge went to 0. One
  dropped 6 pixels short of another window's right edge lined up with it.
- **Switcher.** Amiga+Tab and Amiga+Shift+Tab rotated three windows both ways.
- **Wheel.** Over an inactive window, the wheel made it active.
- **Places.** A Shell window moved to 28,168 reopened there after a reboot.
- **Editor.** Both views, and the view switch.

Not yet tested: halves, drawer sizes, and a program with a resizable window
of its own.

## For the OpenUp part

```python
ow = Part(top, "OpenWindows", "0.1", "OpenWindows: snapping, Amiga+Tab, the wheel under the pointer, window places")
ow.add("Prefs/Windows", (a.openamigaprefs / "Windows/build/os3/Windows").read_bytes(), "SYS:Prefs/Windows")
ow.add("Prefs/Windows.info", icons.icon(icons.WBTOOL, "page", stack=16384), "SYS:Prefs/Windows.info")
ow.add("Tools/Commodities/OpenWindows", (a.openamigaprefs / "Windows/build/os3/OpenWindows").read_bytes(), "SYS:Tools/Commodities/OpenWindows")
ow.line("startup Run >NIL: SYS:Tools/Commodities/OpenWindows")
ow.write()
```
