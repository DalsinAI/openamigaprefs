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
