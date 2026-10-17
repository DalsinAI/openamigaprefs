#!/bin/sh
# OpenPrefs Look with the os32 stove (m68k-amigaos-gcc, NDK 3.2), and the
# theme reader of an opengadtools checkout beside this one (OGT=dir).
#   Look/build.sh [OUT_DIR]        (default build/os3)
# MIT, Copyright (c) 2026 AmigaChrome Team.
# CPU= and OPT= change -m68040 and -O2 (the ROM's build: OPT=-Os).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# The 68040, soft float: the specs file links libnix's soft libm (maths through
# the mathieee libraries), not its FPU libm881, so one program runs on any 68040.
SPECS="-specs=$HERE/../specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OGT=${OGT:-$HERE/../../opengadtools}
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul ${CPU:--m68040} $SPECS -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common -I"$OGT/lib" -o "$OUT/Look" "$HERE/src/look.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/Look ($(wc -c < "$OUT/Look") bytes)"
