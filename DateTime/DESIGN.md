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
