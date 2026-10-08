#!/bin/sh
# OpenBlanker, the Open family's screen blanker, with the os32 stove (m68k-amigaos-gcc, NDK 3.2).
#   Blanker/build.sh [OUT_DIR]     (default build/os3)
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -fno-common -o "$OUT/OpenBlanker" "$HERE/src/openblanker.c"
echo "$OUT/OpenBlanker ($(wc -c < "$OUT/OpenBlanker") bytes)"
