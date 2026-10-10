# OpenPrefs Windows and OpenWindows

OpenWindows is a commodity that changes how windows behave on Workbench and
every other screen. OpenPrefs Windows is its editor. Neither patches
Intuition: OpenWindows watches the input stream and moves, sizes and
activates windows with the OS's own calls. MIT, Copyright (c) 2026 Dalsin
Limited.

![OpenPrefs Windows 0.3](docs/window.png)

## 0.1 (6 October 2026)

| Behaviour | What it does | Setting |
| --- | --- | --- |
| Snapping | A window dropped near a screen edge, or near another window's edge, lines up with it | `snap on 12` (the distance in pixels) |
| Halves | A window dropped with the pointer at the screen's left or right edge fills that half (resizable windows only) | `snap.halves on` |
| Switcher | Amiga+Tab brings the window at the back to the front and activates it; Amiga+Shift+Tab sends the front window to the back | `switcher on`, `switcher.key lcommand tab` (commodity words) |
| Wheel | The mouse wheel goes to the window under the pointer, which becomes active first | `wheel on` |
| Places | A program's window opens where it was last left, and at the same size when it can be resized | `places on`, `never <program>` lines |
| Drawers too (0.3) | Workbench's drawer windows are remembered the same way, with no Snapshot needed | `places.drawers on` |
| Double-click to front (0.4) | A double-click in a window's title bar brings it to the front. On by default | `doubleclick.front on` |
| Edges (0.5) | A resizable window is resized by dragging any edge but its title bar. On by default | `edges on` |
| Drive windows (0.6) | A drive's window is titled with the drive's name only: Workbench's "Work  70% full, 453MB free, 62.0MB in use" loses its usage part. On by default | `drive.title on` |
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

**Double-click to front (OpenWindows 0.4, 8 October 2026).** The user asked
for "double click to bring windows forward" as part of the Open tools, on by
default. Intuition brings a window forward only from its depth gadget, and
the common Amiga answer is a double-click: OS 3.2's ClickToFront commodity
brings a window forward on a double-click anywhere in it (Look's "Click to
front" turns it on). OpenWindows does it for the title bar only, where a
double-click means nothing to a program, so a double-click on a file in a
list or an icon in a drawer is left alone. It acts only on two presses of
the left button on the same window's drag bar (on the drag gadget and on no
other gadget: not close, depth, zoom or a program's own), no more than four
pixels apart, within the double-click time of Input prefs (`DoubleClick()`,
with the input events' own time stamps). A window already in front stays
where it is: sending a window back stays with its depth gadget and
Amiga+Shift+Tab, so a double-click never makes a window vanish. The presses
go on to Intuition untouched (the window is made active, a drag starts as
usual); OpenWindows only acts after them, so Workbench's icon double-clicks
and programs that use their title bar see what they always did. With
ClickToFront on too, both bring the window forward, which looks the same.
The editor's switch, in both views: "Double-click a title bar to bring the
window to the front".

![A single click (top) makes Shell B active behind Shell A; a double-click in its title bar (bottom) brings it to the front](docs/double-click.png)

**Resize from any edge (OpenWindows 0.5, 8 October 2026).** The look
approved that day has no size gadget: a resizable window is resized by
dragging any edge but its title bar, the sides, the bottom and the bottom
corners, as on today's desktops. OpenWindows does it without patching
Intuition:

- **The grab zone** (0.6.1) is 4 pixels outside the frame and the frame's
  own line, down both sides and along the bottom, below the title bar, and a
  5 x 5 square on each bottom corner. Nothing is drawn. The title bar's
  height only ever moves a window.
- **The input handler decides at the press.** It reads the screen's layers
  front to back with no lock (it runs on input.device's task and can't wait,
  as the wheel's test already does): the first window whose frame holds the
  pointer is the one under it, and only its own edge counts; a point just
  outside every frame in front of a window belongs to that window's edge. A
  press there is kept from Intuition (it becomes a plain move) and the main
  task is told. A backdrop, a borderless window and one without
  `WFLG_SIZEGADGET` are never grabbed, and a press with Shift, Ctrl, Alt or
  Amiga held is left to Intuition.
- **The main task sizes the window once** (0.6.1; 0.5 and 0.6 called
  `ChangeWindowBox` at every move, which froze the screen, see below): while
  the button is held an outline shows the box, from the box the window had
  at the press: the edge or corner held follows the pointer, the others stay.
  At the button-up one `ChangeWindowBox` follows. The window keeps to its own
  limits (`MinWidth` to `MaxWidth`) and to the screen. The window is made
  active.
- **The press is tested under the screen's layer lock.** The input handler
  can't wait, so it takes the lock with `AttemptSemaphore`; while another
  task holds it, the press goes to Intuition as before 0.5. The edges are the
  window's own box (`LeftEdge`, `TopEdge`, `Width`, `Height`), not a layer's,
  so a GimmeZeroZero window's inner layer and a requester never make an edge.
- **The resize pointer is off by default** (`edges.pointer on` asks for it):
  left-right, up-down, or the corner's diagonal, `pointerclass` images of
  OpenWindows' own, until the button comes up. A window's own pointer (one
  set with `WA_Pointer`, or a busy pointer) can't be read back, so after the
  drag the window would have the default one. The editor keeps the line as
  written; it has no switch for it.
- **Not yet:** the edge lit in the accent as the pointer passes over it (the
  mock-up's hover), which is the cue instead of a pointer: OpenLook draws the
  frame, so it comes with openworkbench.library.
- `edges on` (the default) in `ENV:OpenPrefs/Windows`; the editor's switch,
  in both views: "Resize a window from any edge but its title bar".

**0.6 (10 October 2026): the user's "it only lets us resize vertically".** On
Instance-32 (1920 x 1080, the Open light look, the size gadget off) a window
could be sized from its bottom edge but not from its sides. The edges were
tested in a lab, with pointer positions injected through input.device
exactly on the frame: right, bottom, left and both bottom corners were
grabbed and sized as designed, so the grab itself was sound. What stopped it
was the pointer. OpenRTG gave a mode 18 ticks a pixel and OS 3.2's Intuition
stops the pointer at 30000 ticks, so at 1920 pixels wide the pointer ended at
x 1666 (openrtg.library 0.13.1 fixes it: openamigartg #59): a right edge
beyond it could not be reached and a window could not be widened past it,
while 1080 rows are well inside the limit. The grab zone was also only six
pixels wide against a slim one-pixel frame, with the pointer's position
steered from the viewer; it is now ten wide, with corners that reach along
the edges. Lab: right edge grabbed 6 pixels outside the frame and pulled 60
wider, bottom 6 outside pulled 60 taller, left 4 outside, and both bottom
corners 10 pixels along an edge; each window box came out as set.

**0.6.1 (10 October 2026): "resize crashes the apps", and the whole screen
froze.** On Instance-32 an edge resize left OpenFiles with a blank body and
half a frame line, other programs crashed, and dock, bar and pointer stopped.
Reproduced in a lab at 1920 x 1080 with a test program that holds its layer
for 1.5 seconds while it redraws (a slow program) and one that quits on more
than four NEWSIZE in a second or on a size past its own limit (a fragile
one). The cause was 0.6's resize itself: it called `ChangeWindowBox` for
every move of the pointer, 150 of them in a short drag, so the program got
150 NEWSIZE and REFRESHWINDOW (Intuition's own sizing sends one NEWSIZE at the
end), and every one went through the input task, which waits on the program's
layer lock. With the slow program the input stream stood still for 82 seconds
(the lab's pointer commands did not return in 90).

- **An outline while the button is held, one size at the button-up.** The
  window is left alone during the drag; a two pixel outline of the box it
  would have is drawn inverted straight into the screen's bitmap, as
  Intuition's own sizing does, and moved with the pointer. At the button-up
  the outline is taken away and the window is sized with a single
  `ChangeWindowBox`: the program sees one NEWSIZE, the same as from its size
  gadget or a zoom. The slow program then got one NEWSIZE and the input
  stream never stopped (the same drag returned in 4.8 seconds); the fragile
  one, dragged 1200 pixels wider in 300 pointer events, got one NEWSIZE, at
  its own limit.
- **Limits are kept.** The box is cut to `MinWidth`/`MinHeight` and to
  `MaxWidth`/`MaxHeight`, where 0 and 65535 (a program that gave none) mean the
  screen's size, and to the screen.
- **No lock is held across a wait.** The input handler never waits and holds
  nothing between events: it takes the layer lock once, with
  `AttemptSemaphore`, to decide a press, and lets it go. The main task never
  holds a lock while it waits for anything. The button-up always ends the
  drag in the handler, whether or not the main task or the program has
  answered. A drag that gets no event for 8 seconds, or lasts 120, is
  dropped by the main task (the outline is taken away, the window is left as
  it was): the cover for a button-up that never arrives.
- **SIZEVERIFY** is not sent: Intuition's message cannot be sent by a
  program. A program that asked for it is sized as it is by a zoom or by a
  program's own `SizeWindow`, once.
- **Grab zones, as the user specified (10 October).** Nothing is drawn. A
  side is a strip four pixels outside the frame, with the frame's own line
  (one pixel); the bottom edge is the same along the bottom. Each bottom
  corner is a square of 5 x 5 pixels centred on the corner, two pixels
  outside the window and two inside, and holds both edges. Only the bottom
  corners: a window is never sized from its top, which is its title bar.
  The 16 pixel reach along an edge of 0.6 is gone.
- Lab (OpenWindows 0.6.1, OpenLook 0.7.5): right, bottom, left, bottom left
  and bottom right of a test window, the Work drawer, OpenFiles and a program
  with limits, one NEWSIZE each and the window as set; the Sound and Tata
  windows (no size gadget) stay as they are; OpenFiles redraws its two panes
  at every size. A resize now draws one frame in OpenLook instead of a
  hundred.

**Drive windows (0.6).** Workbench makes a drive's window title from its
format "%s  %lU%% full, %sB free, %sB in use" (the two spaces, the
percentage and the sign are what OpenWindows looks for). Workbench 3.2.3 has
no setting for it (its Workbench prefs only switch the fill gauge off, which
OpenUp's look steps do: `OpenUpTool WBGAUGE`). Once a second OpenWindows
cuts the text at its first space in the title of a Workbench window, in
place, in the string Workbench made (its memory stays its own), and has
Intuition draw the title again with `SetWindowTitles(w, title, ~0)`. Only
Workbench's windows (`WFLG_WBENCHWINDOW`) are looked at, so no other
program's title is touched. `drive.title off` (the editor's switch, in both
views: "Drive windows titled with the name only") leaves Workbench's titles
alone.

The size gadget itself is OpenLook 0.5's (`sizegadget off` draws it as the
window); it still works where it is.

**Views.** The editor opens in Simple, which shows the on and off switches. View >
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

- **Double-click to front (0.4).** Two Shell windows, B partly behind A: a
  single click in B's title bar made it active and left it behind; a
  double-click there brought it to the front; a double-click inside A (not
  its title bar) made A active and left it behind. Switched off with Use,
  a double-click in A's title bar left it behind; switched on again, it
  came to the front. Double-clicks on Workbench icons opened them as before.

Not yet tested: halves, drawer sizes, and a program with a resizable window
of its own.

## For the OpenUp part

```python
ow = Part(top, "OpenWindows", "0.5", "OpenWindows: snapping, resizing from any edge, Amiga+Tab, the wheel under the pointer, window places, double-click to front")
ow.add("Prefs/Windows", (a.openamigaprefs / "Windows/build/os3/Windows").read_bytes(), "SYS:Prefs/Windows")
ow.add("Prefs/Windows.info", icons.icon(icons.WBTOOL, "page", stack=16384), "SYS:Prefs/Windows.info")
ow.add("Tools/Commodities/OpenWindows", (a.openamigaprefs / "Windows/build/os3/OpenWindows").read_bytes(), "SYS:Tools/Commodities/OpenWindows")
ow.line("startup Run >NIL: SYS:Tools/Commodities/OpenWindows")
ow.write()
```
