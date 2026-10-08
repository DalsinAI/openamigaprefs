#!/bin/sh
# OpenPrefs Gamepads, with the os32 stove (m68k-amigaos-gcc, NDK 3.2) and
# openinput.library's headers (include/, copied from openamigainput).
#   Gamepads/build.sh [OUT_DIR]     (default build/os3)
#   STOVE=~/AmigaChrome/stoves/os32-gcc16 Gamepads/build.sh build/os3-gcc16
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$HERE/build/os3}
# -fno-delete-null-pointer-checks: address 0 is real memory on the Amiga.
# No -lamiga: its 16-bit sprintf would replace C's.
CFLAGS="-noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -fno-common -I$HERE/src -I$HERE/include"
mkdir -p "$OUT"
S="$HERE/src"
"$CC" $CFLAGS -o "$OUT/Gamepads" "$S/gamepads.c" "$S/gp_core.c"
echo "$OUT/Gamepads ($(wc -c < "$OUT/Gamepads") bytes)"
