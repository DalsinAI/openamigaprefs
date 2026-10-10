# OpenBlanker

The Open family's screen blanker. When nobody has touched the keyboard or the
mouse for a while, it opens a screen of its own and plays a show; a key or
the mouse ends it. AmigaOS 3.2's own Blanker takes no plug-in shows, so the
Open family has one of its own.

![OpenBlanker playing Gods and Angels on AmigaOS 3.2.3](docs/screenshot.png)

## The show: Gods and Angels

AmigaChrome's homage to the people who made the Amiga and kept it alive, and
to the projects the Open family is built on (the roll call in the Team's
marketing material). Each card fades in, holds and fades out, by the palette,
as Amiga demos always have: the card is drawn with its pens at the background
colour, then `SetRGB32` brings them up, frame by frame on the vertical blank.

## How it works

- **A commodity**: `CxCustom` sees every key and mouse event and resets the
  idle count; a timer counts the seconds. Exchange can disable, enable or quit
  it. Started a second time, the running one is left alone (1.1); `OpenBlanker NOW` asks it by a signal to play the show at once. (1.0 played the show at once on any second start, and a User-Startup that lists OpenBlanker twice, as Instance-32's did after an OpenUp upgrade, blanked the screen at boot.)
- **Its own screen**, the Workbench's size, in a 256-colour mode
  (`BestModeID`, depth 8): AGA, or a CLUT mode on an RTG card, so the palette
  can fade the picture. A borderless window covers it: the pointer is hidden,
  and the key that wakes the show goes there, not to the program underneath.
- **Type**: CGTriumvirate and CGTimes, the outline fonts every AmigaOS 3.2 has,
  made at exact sizes for the screen's height (topaz if they are missing).
  Text is ISO-8859-1.

- **Ending cleanly (1.1):** the pointer is the window's again, the window is
  closed so Intuition redraws what it covered, the Workbench screen is brought
  to the front before the show's screen goes, the show's bitmap is cleared and
  the screen closed (it waits for any other window on it), then the fonts and
  the pointer's sprite are freed.

## Settings

| Where | What | Default |
| --- | --- | --- |
| `ENV:OpenPrefs/Blanker` | seconds without a key or the mouse before the show | 300 |
| `OpenBlanker TIMEOUT=n` | the same, from the Shell | |
| `OpenBlanker NOW` | play the show at once, then go on watching | |

Ctrl-F to the task reads `ENV:OpenPrefs/Blanker` again.

## Install

OpenUp installs it as standard: `SYS:Tools/Commodities/OpenBlanker`, started
from `SYS:WBStartup`, with `ENVARC:OpenPrefs/Blanker` set to 300.

## Tested

AmigaOS 3.2.3 on AmigaChrome (AC090, ACRTG, an 800x600 Workbench), 7 October
2026: the show opens on an 8-bit RTG screen, every card fades in and out, the
pointer is hidden, and a key or the mouse ends it.
