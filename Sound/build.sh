#!/bin/sh
# OpenPrefs Sound and OpenSpeaker, with the os32 stove
# (m68k-amigaos-gcc, NDK 3.2, AHI's includes).
#   Sound/build.sh [OUT_DIR]     (default build/os3)
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OGT=${OGT:-$HERE/../../opengadtools}      # a checkout of DalsinAI/opengadtools: its theme reader (lib/ogt_theme.c)
OUT=${1:-$HERE/build/os3}
CFLAGS="-noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -fno-common -I$HERE/src -I$HERE/../Common -I$OGT/lib"
mkdir -p "$OUT"
S="$HERE/src"
"$CC" $CFLAGS -o "$OUT/Sound" "$S/sound.c" "$S/sp_core.c" "$S/sp_amiga.c" "$S/sp_test.c"
# OpenSpeaker reads where OpenMenus' bar is with the Menus editor's om_prefs.c
"$CC" $CFLAGS -I"$HERE/../Menus/src" -o "$OUT/OpenSpeaker" "$S/openspeaker.c" "$S/sp_core.c" "$S/sp_amiga.c" "$HERE/../Menus/src/om_prefs.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/Sound ($(wc -c < "$OUT/Sound") bytes), $OUT/OpenSpeaker ($(wc -c < "$OUT/OpenSpeaker") bytes)"
