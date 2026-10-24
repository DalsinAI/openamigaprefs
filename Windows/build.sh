#!/bin/sh
# OpenPrefs Windows and the OpenWindows commodity, with the os32 stove
# (m68k-amigaos-gcc, NDK 3.2).
#   Windows/build.sh [OUT_DIR]     (default build/os3)
# MIT, Copyright (c) 2026 AmigaChrome Team.
# CPU= and OPT= change -m68040 and -O2 (the ROM's build: OPT=-Os).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# The 68040, soft float: the specs file links libnix's soft libm (maths through
# the mathieee libraries), not its FPU libm881, so one program runs on any 68040.
SPECS="-specs=$HERE/../specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul ${CPU:--m68040} $SPECS -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common -o "$OUT/Windows" "$HERE/src/windows.c"
"$CC" -noixemul ${CPU:--m68040} $SPECS -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common -o "$OUT/OpenWindows" "$HERE/src/openwindows.c" "$HERE/src/gestures.c" "$HERE/src/places.c"
echo "$OUT/Windows ($(wc -c < "$OUT/Windows") bytes), $OUT/OpenWindows ($(wc -c < "$OUT/OpenWindows") bytes)"
