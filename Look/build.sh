#!/bin/sh
# OpenPrefs Look with the os32 stove (m68k-amigaos-gcc, NDK 3.2), and the
# theme reader of an opengadtools checkout beside this one (OGT=dir).
#   Look/build.sh [OUT_DIR]        (default build/os3)
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OGT=${OGT:-$HERE/../../opengadtools}
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -fno-common -I"$OGT/lib" -o "$OUT/Look" "$HERE/src/look.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/Look ($(wc -c < "$OUT/Look") bytes)"
