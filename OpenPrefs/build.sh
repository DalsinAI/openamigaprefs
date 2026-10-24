#!/bin/sh
# OpenPrefs: every editor as a section of one program (OpenPrefs/src/openprefs.c), with the os32 stove
# (m68k-amigaos-gcc, NDK 3.2) and the theme reader of an opengadtools checkout beside this one (OGT=dir).
#   OpenPrefs/build.sh [OUT_DIR]                    (default build/os3)
# writes
#   OUT_DIR/OpenPrefs        the app: SYS:Preferences/OpenPrefs (SYS:Prefs/OpenPrefs on an older disk), and
#                            OpenPrefs APPLY at boot on OS 3.2.3's own ROM (an open ROM applies at boot itself)
# The small body (C:OpenPrefs) and the old names' stubs are no longer built: OpenUp archives them
# (salvor/Design-No-ROM-Volume-20261024.md, section 8).
# Each page is its editor's own source, compiled with LT_OP and OpenPrefs/src/opshift.h, its main renamed
# (as Look/build-one.sh builds OpenLook). CPU= and OPT= change -m68020 and -O2 (the ROM's: -m68040 -Os).
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
P=$HERE/..
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OGT=${OGT:-$P/../opengadtools}
OUT=${1:-$HERE/build/os3}
O=$OUT/obj
mkdir -p "$O/app"
BASE="-noixemul ${CPU:--m68020} -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common"
FRAME="$BASE -DLT_OP -DLT_ONE -I$P/Common -I$HERE/src -I$OGT/lib"
# GCC 16 warns of truncation in the editors' bounded copies (snprintf, strncpy): warnings, not errors, here.
# GCC 6.5 (the os32 stove) has neither warning and refuses -Wno-error= for one it doesn't know.
TRUNC=""
for w in format-truncation stringop-truncation; do
  if echo 'int x;' | "$CC" -Werror -Wno-error=$w -fsyntax-only -x c - 2>/dev/null; then TRUNC="$TRUNC -Wno-error=$w"; fi
done
PAGE="$FRAME$TRUNC -include $HERE/src/opshift.h"

# page DIR FILE.c [extra flags...]: one source as a page of the app
page() { f=$1; shift; "$CC" $PAGE "$@" -c -o "$O/app/$(basename "$f" .c).o" "$f"; }
page "$P/Look/src/look.c" -Dmain=look_main -Dversion=look_version
page "$P/Windows/src/windows.c" -Dmain=windows_main -Dversion=windows_version
page "$P/TitleBar/src/titlebar.c" -Dmain=titlebar_main -Dversion=titlebar_version -I"$P/TitleBar/src" -I"$P/TitleBar/include"
page "$P/Menus/src/menus.c" -Dmain=menus_main -Dversion=menus_version -DAslBase=menus_AslBase -I"$P/Menus/src"
page "$P/Menus/src/om_prefs.c" -I"$P/Menus/src"
page "$P/Look/src/appearance.c"
page "$P/Look/src/pf_iff.c"
page "$P/Look/src/pf_prefs.c"
page "$P/Look/src/pf_amiga.c"
DOCKD="-DIconBase=dock_IconBase -DAslBase=dock_AslBase"
page "$P/Dock/src/dockprefs.c" -Dmain=dock_main -Dversion=dock_version $DOCKD
page "$P/Dock/src/od_dock.c" $DOCKD
page "$P/Sound/src/sound.c" -Dmain=sound_main -Dversion=sound_version
page "$P/Sound/src/sp_core.c"
page "$P/Sound/src/sp_amiga.c"
page "$P/Sound/src/sp_test.c"
page "$P/Input/src/openinput.c" -Dmain=input_main -Dversion=input_version -DUtilityBase=input_UtilityBase
GPD="-DOpenInputBase=gamepads_OpenInputBase -I$P/Gamepads/include"
page "$P/Gamepads/src/gamepads.c" -Dmain=gamepads_main -Dversion=gamepads_version $GPD
page "$P/Gamepads/src/gp_core.c" $GPD
page "$P/Gamepads/src/gp_pad.c" $GPD
page "$P/Gamepads/src/gp_draw.c" $GPD
page "$P/DateTime/src/opendatetime.c" -Dmain=datetime_main -Dversion=datetime_version -DUtilityBase=datetime_UtilityBase
page "$P/Serial/src/openserial.c" -Dmain=serial_main -Dversion=serial_version
page "$P/OpenTypes/src/opentypes.c" -Dmain=types_main -Dversion=types_version -D__stack=types_stack \
  -DIconBase=types_IconBase -DDataTypesBase=types_DataTypesBase -DAslBase=types_AslBase -DGadToolsBase=types_GadToolsBase
page "$P/System/src/opensystem.c" -Dmain=system_main -Dversion=system_version
page "$HERE/src/blankerprefs.c" -Dmain=blanker_main
DISP="-I$P/Look/src -I$P/Display/src"
page "$P/Display/src/screenmode.c" -Dmain=screenmode_main -Dversion=screenmode_version $DISP
page "$P/Display/src/dp_prefs.c" $DISP
ICTL="-I$P/Look/src -I$P/IControl/src"
page "$P/IControl/src/icontrol.c" -Dmain=icontrol_main -Dversion=icontrol_version $ICTL
page "$P/IControl/src/ic_prefs.c" $ICTL
# the Icons page (2.3) and the two variables' words, opengadtools' workbench/wb_grid.c (OpenBench reads them with it)
page "$P/Icons/src/icons.c" -Dmain=icons_main -Dversion=icons_version -I"$OGT/workbench"
"$CC" $FRAME -I"$OGT/workbench" -c -o "$O/app/wb_grid.o" "$OGT/workbench/wb_grid.c"
# the Printing page (2.4): printer.prefs' and printergfx.prefs' paper settings on Look's PREF reader
PRT="-I$P/Look/src -I$P/Printing/src"
page "$P/Printing/src/printing.c" -Dmain=printing_main -Dversion=printing_version $PRT
page "$P/Printing/src/pr_prefs.c" $PRT
# the frame and the program, not moved: openprefs.c first, so its $VER is the file's first
"$CC" $FRAME -o "$OUT/OpenPrefs" "$HERE/src/openprefs.c" "$HERE/src/op_apply.c" "$HERE/src/opframe.c" "$O"/app/*.o \
  "$OGT/lib/ogt_theme.c"

echo "$OUT/OpenPrefs ($(wc -c < "$OUT/OpenPrefs") bytes)"
