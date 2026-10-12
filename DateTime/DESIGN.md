# OpenDateTime

The date, the time and the region in one OpenPrefs editor (the user, 10
October 2026: "time, locale should slide under a new Open app"; a double-click
on the date and time in the Workbench bar opens it). It takes over the OS's
Time and Locale editors, which OpenUp's RECONCILE moves to SYS:Prefs/Classic.
MIT, Copyright (c) 2026 Dalsin Limited.

## 0.1 (10 October 2026)

| Setting | Where it goes |
| --- | --- |
| Date (day, month, year) and time (hours, minutes, seconds); Now reads the clock again | The system clock through `C:Date`, then the battery clock with `C:SetClock SAVE`; only when a field was changed |
| Time zone, UTC-12:00 to UTC+14:00 by half hours | Locale prefs' GMT offset (minutes west of Greenwich: UTC+1 is -60) |
| Country, from `LOCALE:Countries` | Locale prefs' country name, and the country file's CTRY data (date and number formats) |
| Language, from `LOCALE:Languages` and english | The first preferred language; the others move down one |

Save writes `ENV:Sys/Locale.prefs` and `ENVARC:`; Use writes `ENV:` (IPrefs
reads it at once); Cancel changes nothing. Everything else in Locale prefs is
kept. From the Shell: `OpenDateTime [FROM file] [USE] [SAVE]`.

Installed as `SYS:Prefs/OpenDateTime`. To come: the clock's 12 or 24 hours
and the date's format for OpenTitle, and time-server setting (OpenSocket's
sntp).

## 0.1.2 (12 October 2026): the region when none is saved, and the picks shown

Seen in a lab copy of Instance-32: the time zone read UTC+0:00 while the clock
was on British Summer Time, and no country was highlighted. That Amiga has no
Locale prefs at all (neither `ENV:` nor `ENVARC:` has `Sys/Locale.prefs`), so
locale.library runs on its own built-in region (united_states, GMT offset
-300, which FormatDate's `%z` prints as +0500). Three faults came out of it:

- **The defaults.** With nothing saved, the editor showed the United Kingdom
  at UTC+0:00. It now shows the United Kingdom's zone for the clock's date:
  UTC+1:00 from the last Sunday of March to the last Sunday of October,
  UTC+0:00 otherwise. The status line says that no region is saved yet.
  AmigaOS's zone is one fixed offset with no summer time of its own, so a
  saved zone stays as it was saved: it has to be changed when the clocks
  change.
- **Use and Save with nothing saved wrote an empty country.** The record
  went out with the United Kingdom's name but no date, time or number
  formats, and locale.library didn't take it: the machine kept its
  built-in region. Twice in the lab, `ENV:Sys/Locale.prefs` was then held
  open ("object is in use"), so the next Use couldn't write it; what holds
  it was not found. The country's formats are now read from its file whenever the
  record has none, from the window and from the Shell's `USE` and `SAVE`.
- **No highlight in the lists.** 0.1.1 removed `GTLV_ShowSelected` to lose
  the box beside the picked row, and the highlight went with it. The box was
  the row's name: GadTools draws it in JAM2, and OpenLook gives JAM2 text a
  cell of its own colour. Each row is now drawn by the editor
  (`GTLV_CallBack`): the whole row in the fill colour when picked, the name
  in JAM1 over it. The pick shows as one even bar.

Tested in lab `gcc16-c` (a copy of Instance-32), built into OpenBench and
stand-alone.

## 0.2 (12 October 2026): a place, and summer time by itself

The decision (12 October 2026): the user picks a place, such as
Europe/London, not an offset; on AmigaChrome the host hands the Amiga its zone
and offset; on a real Amiga the Team's locale side carries the summer time
rules and switches by itself; Locale prefs, as the Team writes them, are the
one owner of the zone. The whole design, with the host's side and ACFS's
dates, is amigachrome `docs/design/Design-Time-Zones.md`.

| Setting | Where it goes |
| --- | --- |
| Time zone: a region (Africa ... Pacific, UTC), then a place in it | Locale prefs: the place and its rule in a chunk of the Team's, `ZONE` (`struct ZonePrefs`: place, POSIX rule, flags), beside `LCLE`; the place's offset now in `lp_GMTOffset`, as before |

- **The places** are the tz database's `zone.tab` and UTC: 419, in
  `src/zones.h`, made by `tools/make_zones.py` from the host's
  `/usr/share/zoneinfo` (tz database 2026c, public domain; nothing fetched).
  Each has the POSIX rule its TZif file ends with; the few that change in ways
  a rule can't say (Morocco's Ramadan; Edmonton and Vancouver moving to one
  time all year) carry their changes from 2026, and the rule holds after.
  `src/tzrule.c` runs them, with no library, so the host tests build it too.
- **On AmigaChrome** (the ACBridge board is there, and `accontrol.device`
  answers), the place and offset are the host's: the window shows them
  ("Europe/London, the host's: UTC+1:00 BST, summer time"), and the region and
  place lists are greyed. The battery clock runs on the host's time, so a
  different place would need the runtime to run the clock in it.
- **On a real Amiga** the offset saved is the place's at the clock's time.
  Picking a place doesn't move the clock: the clock is taken as right, and the
  offset as what was wrong.
- **`OpenDateTime KEEP`**, the background part, keeps `lp_GMTOffset` right:
  on AmigaChrome it asks the host once a minute and, when the offset changes,
  sets the system clock on to the battery clock, which has moved with the
  host; on a real Amiga it looks just after each change of the place's rule,
  moves the clock (and the battery clock) by the change and saves the offset.
  `ENV:` is written before the system clock moves and `ENVARC:` before the
  battery clock does, so a failed write never moves a clock twice. One runs at
  a time (the port `OpenDateTime.keeper`); Ctrl-C stops it; Use and Save send
  it Ctrl-E, to look again at once. It writes a line to its output when what
  it did changes. To start it at boot: `Run >NIL: SYS:Prefs/OpenDateTime KEEP`
  in `S:User-Startup` (not added by anything yet).
- **`OpenDateTime ZONE Europe/London [USE|SAVE]`** sets the place from the
  Shell (on its own, as Use). **`OpenDateTime INFO`** says the place, the
  offset saved and in use, the host's, the clock and the next change.
- With no Locale prefs at all on AmigaChrome, KEEP writes `ENV:` with the
  editor's defaults (the United Kingdom, english) and the host's zone, so the
  offset is right; `ENVARC:` waits for a Save.
- **Open: the autumn change holds waits.** Kickstart 3.2's `timer.device`
  counts `Delay()` and `C:Wait` to a moment on the system clock, so when KEEP
  sets the clock back, every wait in progress holds until the clock catches up
  (an hour at a real change). Seen in the lab; the choices are in the amigachrome
  design, section 5a.

Host tests: `DateTime/test/run.sh` (the host's C compiler): Europe/London's
last-Sunday changes in March and October from 2026 to 2030, KEEP's step on
either side of each and after a switch-off; Asia/Tokyo, with no summer time;
other places; and all 419 against the host's own tz data, every hour of 2026
to 2030 and at each change.
