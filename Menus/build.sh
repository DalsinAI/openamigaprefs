#!/bin/sh
# OpenPrefs Menus with the os32 stove (m68k-amigaos-gcc, NDK 3.2), and the
# theme reader of an opengadtools checkout beside this one (OGT=dir).
#   Menus/build.sh [OUT_DIR]       (default build/os3)
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OGT=${OGT:-$HERE/../../opengadtools}
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -fno-common -I"$OGT/lib" -I"$HERE/src" -o "$OUT/Menus" \
    "$HERE/src/menus.c" "$HERE/src/om_prefs.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/Menus ($(wc -c < "$OUT/Menus") bytes)"
