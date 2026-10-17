#!/bin/sh
# OpenDock and OpenPrefs Dock with the os32 stove (m68k-amigaos-gcc, NDK 3.2).
#   Dock/build.sh [OUT_DIR]        (default build/os3)
# MIT, Copyright (c) 2026 AmigaChrome Team.
# CPU= and OPT= change -m68040 and -O2 (the ROM's build: OPT=-Os).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# The 68040, soft float: the specs file links libnix's soft libm (maths through
# the mathieee libraries), not its FPU libm881, so one program runs on any 68040.
SPECS="-specs=$HERE/../specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OUT=${1:-$HERE/build/os3}
FLAGS="-noixemul ${CPU:--m68040} $SPECS -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common -I$HERE/src -I$HERE/../Common"
mkdir -p "$OUT"
"$CC" $FLAGS -o "$OUT/OpenDock" "$HERE/src/opendock.c" "$HERE/src/od_dock.c"
"$CC" $FLAGS -o "$OUT/Dock" "$HERE/src/dockprefs.c" "$HERE/src/od_dock.c"
echo "$OUT/OpenDock ($(wc -c < "$OUT/OpenDock") bytes), $OUT/Dock ($(wc -c < "$OUT/Dock") bytes)"
