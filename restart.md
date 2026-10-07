# Restart: OpenPrefs

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenPrefs: GadTools preferences editors for AmigaOS 3.x (Use, Save, Test, Cancel) that keep the OS's own settings files: Menus, OpenDock, Sound, OpenSpeaker, OpenTitle, OpenTypes, OpenUp Setup, Look. Also home to the combined-prefs design.

## Where it stands

OpenDock 0.2 (#29: item sizes, edges off, just above the backdrop) is merged and queued for OpenUp 0.6.14. OpenTitle 0.4 network icons (#27) and Sound/OpenSpeaker (#18) shipped in 0.6.12/0.6.13. The combined-prefs design (nine editors replacing 23) is approved; phase 1 (frame + Appearance) went to Main Discourse to build and no PR has come back.

## Merged lately

- #29 (14e22af, 2026-10-06): OpenDock 0.2: item sizes, edges off, just above the backdrop
- #28 (27ea816, 2026-10-06): Credit who made OpenPrefs: CONTRIBUTORS.md
- #27 (659ae4d, 2026-10-06): OpenTitle 0.4: LAN and Wi-Fi icons between the clock and the speaker
- #26 (6245152, 2026-10-06): OpenTypes SWITCH, and OpenFiles and OpenView on the dock
- #18 (a23055e, 2026-10-06): Sound 0.1 and OpenSpeaker 0.1: one sound prefs, and a speaker on the menu bar
- #25 (86fa880, 2026-10-06): OpenTitle 0.3: make ENV:OpenMenus before the tray drawer

## Open pull requests

- None.

## Next step

1. Main Discourse: fix async SystemTags in Windows.c (needs NIL: handles), and the OpenLook theme not reaching frames and gadgets.
2. Combined prefs P1 PR, then review and screenshots before anything is installed.
3. OpenDock backlog: magnification and auto-hide.

## Waiting on @SacredTrees

- Approve the combined-prefs P1 build when its screenshots arrive.

## Who owns it

Main Discourse (builds, OpenDock, editors); OpenPrefs thread; Combined prefs thread.

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenPrefs_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)
- [`20261006_AmigaChrome_Combined_Prefs_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)
- [`20261006_AmigaChrome_OpenUp_MainDiscourse_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
