# OpenWindows' action filter

OpenWindows 0.9, 18 October 2026 (Team 5, W66). The code is `Windows/src/gestures.c` (the table) and the handler in `Windows/src/openwindows.c` (`press_begin`, `press_moved`, `press_end`, `gestures_done`).

## Why

Dale, 18 October 01:24 to 01:30:
- "you can let go of the mouse button and the movement you're making still takes over";
- "you can frequently no longer drag the window that's selected";
- "select, pause, select again and drag";
- "action filter is the key here, we clearly don't have one";
- the rules: "pause, click once, with no movement, bring forward" and "click, hold mouse, and movement → drag".

OpenWindows 0.8 took every title-bar press from Intuition (drag.solid) and moved the window from its own task. That task runs at priority 0, after everything at the same priority, and it read the pointer from the screen whenever it ran. So the drag:
- started when the task got round to it;
- ended where the pointer was by then, not where the release was.

On OpenIntuition, Intuition already drags windows with their contents, at the drag opacity, inside the input task, in the events' order.

The press and the release also came late and out of order from the runtime and input.device. That part is fixed there: amigachrome's pointer window VERSION 2 and openamigainput's input.device 50.3 (Design-Open-Input-Library.md section 3).

## The rules

- **Every left press goes through one handler, in the events' order.** OpenWindows' commodity, ahead of Intuition.
- **The press decides one thing at once: whether OpenWindows takes it.**
  - It takes a press on an edge (OpenWindows resizes).
  - It takes a press on a title bar only where Intuition's drag is an outline: drag.solid on another Intuition than OpenIntuition.
  - Any other press goes on to Intuition at once. A drag therefore starts on the first move, as on OS 3.2.3.
- **Moves.** A move past 4 pixels either way from the press makes the gesture a move. Before that it is still a click.
- **The release decides the gesture.**
  - A click: no move past the threshold.
  - A double-click: a second click on the same window and target, within 4 pixels, inside Input prefs' double-click time.
  - A move.
  - The table gives that gesture's actions. The main task does them, in order, after Intuition has had the release.
- **Secondary actions never cancel a primary one.**
  - To the front happens at the release of a click, never at the press. A press and move is a drag and does not front first.
  - A snap happens after the drag has ended.
- **The release ends the action at its own place.**
  - Intuition's drag ends at the release event itself.
  - OpenWindows' own drag or resize ends at the place the handler saw with the release (`grab_ex`, `grab_ey`), not the screen's `MouseX` when the main task runs.
  - A press that comes before the main task has finished the last one is kept and done next (`grab_gen`), not lost.

## The table: ENV:OpenPrefs/Gestures

One row per line, with `;` starting a comment:

```
target  button  qualifier  gesture  actions  press
```

| Field | Values |
| --- | --- |
| target | `title` (the drag bar), `body` (inside the window, off its gadgets), `gadget` (0.9.1: any gadget, the program's own or the border's, and anything in a window with a requester up), `edge` (OpenWindows' grips), `backdrop`, `screenbar` |
| button | `select` (the left button: the only one the filter watches for now), `menu`, `middle` |
| qualifier | `none`, `any`, or `shift`, `alt`, `ctrl`, `amiga` joined by `+` (exactly those held) |
| gesture | `click`, `double`, `move` |
| actions | `none`, or `front`, `back`, `activate`, `drag`, `resize`, `snap` joined by `,` |
| press | `pass` (Intuition gets the press at once) or `take` (OpenWindows' own; only for `edge`, or `title` with `drag`) |

How rows are read:
- The first row that fits a gesture gives its actions.
- A `double` with no row of its own is a `click`.
- A target takes the press if any of its rows says `take`.
- With no file, or one with a line it can't read, OpenWindows uses the built-in table and says which line.

**The built-in table.** This is OS 3.2.3's behaviour with the extras, and the settings in ENV:OpenPrefs/Windows decide some rows:

```
title  select none move   drag,snap  pass   ; snap: "snap on"
title  select none click  front      pass
title  select none double front      pass   ; "doubleclick.front on"
body   select none click  front      pass
edge   select none move   resize     take   ; "edges on"
edge   select none click  activate   take
```

- `drag` on a passed press is Intuition's own drag, so its look (solid, drag opacity) is Intuition's.
- With qualifiers held, no built-in row fits. Shift, Alt and Amiga presses are Intuition's, as on 3.2.3, for example Left Amiga dragging the screen.

## 0.9.1: a click never covers what it brought up

Dale, 18 October 07:09 to 07:22, on Instance-32 with 0.9:
- "odd three dots behaviour on file windows, window jumps forward and menu winds up behind";
- "open files is now buggy and wont close".

The built-in `body click front` row fronted a window at the release of every click inside it. When the press itself had opened a window (OpenBench's "..." popup opens at the press, OpenFiles' toolbar opens a requester), the clicked window was then covered by that window, so the release brought it in front of it. A requester hidden that way keeps its program waiting in the requester's loop, so the window's close gadget does nothing.

What 0.9.1 does (W67):
- **A press on a gadget is the gadget's.** Any gadget of the program's own (a scroller, a button, a list, OpenFiles' toolbar), a border gadget (close, depth, zoom, size), or anything in a window while an Intuition requester is up is the target `gadget`. The built-in table has no `gadget` rows, so nothing of OpenWindows' follows such a press. A disabled gadget counts as the body. A GimmeZeroZero window's own gadgets are found in its inner part.
- **What the press brought up stays in front.** The screen's front layer is noted at the press. If another window has come in front by the release (a popup, a requester, a new drawer), the release does not front. The main task looks again before it calls `WindowToFront`, for a window opened just after the release.
- **A window whose program is busy with a requester of its own is not brought over it.** If the window's task is waiting, but not on the window's own port, and a window of that same task is in front of it and overlaps it, the window is not fronted. For example, a click on OpenFiles' body or title bar while its Search requester is up leaves the requester in front. OpenBench's drawers share one port, so a click on one drawer still fronts it over another.
- **The menu button.** A left press while the right button is held belongs to the menus, and no gesture starts. A right press during a left press ends that gesture with no actions. The right button's events always go on untouched.
- **Kept:** a plain click on a window's own body or title bar, with no move, still fronts it at the release.

The decision helpers are pure and run on the host: `gestures_target()` (what a press is on) and `gestures_release()` (a click's actions at its release), both in `Windows/tests/test_gestures.c`.

## What each commodity does now

- **OpenWindows:**
  - It owns the decision for left presses on windows: activate and front, drag, snap, edges, and the double-click.
  - The wheel, Amiga+Tab and the places are unchanged.
- **Intuition (OpenIntuition):** it drags and sizes with its own gadgets as it always did. It now gets every press that is not on an edge.
- **OpenMenus:** it takes presses on its own bar (`screenbar`) and in its menu sessions, in its own input handler. The filter has no `screenbar` rows by default, so the two never act on the same press.
- **OpenTitle, OpenDock, Tata:** they read their own windows' IDCMP (MOUSEBUTTONS), so they hear a press only after Intuition has given it to their window. They act on no raw event, and none of them fronts or drags another window.
- **Not done yet:** a public port that tells other commodities the decided gesture. None of them needs it today.

## Measured

Lab t5-mouse (a copy of Instance-32), with a press-and-drag on a title bar driven as the instance window sends it: `team5-work/w66/tools/seq.py`, 50 runs each. The figures are in the W66 report (`team5-work/reports/W66-mouse-release-20261018.md`).

MIT, Copyright (c) 2026 AmigaChrome Team.
