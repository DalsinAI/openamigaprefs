#!/bin/sh
# OpenPrefs Title bar and OpenTitle, with the os32 stove (m68k-amigaos-gcc, NDK 3.2).
#   TitleBar/build.sh [OUT_DIR]     (default build/os3)
# src/logo.h comes from tools/mklogo.py and AmigaChrome's logo; it is kept in
# the repository, so the build needs no Python.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$HERE/build/os3}
CFLAGS="-noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-common -I$HERE/src -I$HERE/include"
mkdir -p "$OUT"
"$CC" $CFLAGS -o "$OUT/TitleBar" "$HERE/src/titlebar.c"
# OpenTitle reads where OpenMenus' bar is with the Menus editor's om_prefs.c
"$CC" $CFLAGS -I"$HERE/../Menus/src" -o "$OUT/OpenTitle" "$HERE/src/opentitle.c" "$HERE/../Menus/src/om_prefs.c"
echo "$OUT/TitleBar ($(wc -c < "$OUT/TitleBar") bytes), $OUT/OpenTitle ($(wc -c < "$OUT/OpenTitle") bytes)"
