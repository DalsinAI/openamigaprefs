#!/bin/sh
# OpenLook, the editor: Look, Windows, Title bar and Menus as the pages of one window (Look/src/openlookprefs.c),
# with the os32 stove (m68k-amigaos-gcc, NDK 3.2).
#   Look/build-one.sh [OUT_DIR]     (default build/os3)    -> OUT_DIR/OpenLook
# Each editor is compiled with LT_ONE and its main renamed; the four stand-alone editors still build as before.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
P=$HERE/..
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OGT=${OGT:-$P/../opengadtools}
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT/one"
CF="-noixemul -m68040 -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -fno-common -DLT_ONE -I$OGT/lib -I$P/Common"
"$CC" $CF -Dmain=look_main -Dversion=look_version -c -o "$OUT/one/look.o" "$HERE/src/look.c"
"$CC" $CF -Dmain=windows_main -Dversion=windows_version -c -o "$OUT/one/windows.o" "$P/Windows/src/windows.c"
"$CC" $CF -Dmain=titlebar_main -Dversion=titlebar_version -I"$P/TitleBar/src" -I"$P/TitleBar/include" -c -o "$OUT/one/titlebar.o" "$P/TitleBar/src/titlebar.c"
"$CC" $CF -Dmain=menus_main -Dversion=menus_version -I"$P/Menus/src" -c -o "$OUT/one/menus.o" "$P/Menus/src/menus.c"
# Appearance's tabs (OpenLook 1.1): Pointer, Fonts, Colours, Backdrops, Workbench, File requesters, and the PREF
# reader and writer they share (pf_*.c; host tests in Look/tests/run.sh)
"$CC" $CF -c -o "$OUT/one/appearance.o" "$HERE/src/appearance.c"
"$CC" $CF -I"$P/Menus/src" -o "$OUT/OpenLook" "$HERE/src/openlookprefs.c" "$OUT/one/look.o" "$OUT/one/windows.o" "$OUT/one/titlebar.o" \
    "$OUT/one/menus.o" "$OUT/one/appearance.o" "$HERE/src/pf_iff.c" "$HERE/src/pf_prefs.c" "$HERE/src/pf_amiga.c" \
    "$P/Menus/src/om_prefs.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/OpenLook ($(wc -c < "$OUT/OpenLook") bytes)"
