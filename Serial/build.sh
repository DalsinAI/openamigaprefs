#!/bin/sh
# OpenSerial, the serial port editor, with the os32 stove
# (m68k-amigaos-gcc, NDK 3.2).
#   Serial/build.sh [OUT_DIR]     (default build/os3)
# MIT, Copyright (c) 2026 AmigaChrome Team.
# CPU= and OPT= change -m68020 and -O2 (the ROM's build: CPU=-m68040 OPT=-Os).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul ${CPU:--m68020} -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common -o "$OUT/OpenSerial" "$HERE/src/openserial.c"
echo "$OUT/OpenSerial ($(wc -c < "$OUT/OpenSerial") bytes)"
