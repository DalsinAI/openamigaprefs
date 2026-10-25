#!/bin/sh
# OpenPrefs Menus with the os32 stove (m68k-amigaos-gcc, NDK 3.2), and the
# theme reader of an opengadtools checkout beside this one (OGT=dir).
#   Menus/build.sh [OUT_DIR]       (default build/os3)
# MIT, Copyright (c) 2026 AmigaChrome Team.
# CPU= and OPT= change -m68040 and -O2 (the ROM's build: OPT=-Os).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# The 68040, soft float: the specs file links libnix's soft libm (maths through
# the mathieee libraries), not its FPU libm881, so one program runs on any 68040.
SPECS="-specs=$HERE/../specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OGT=${OGT:-$HERE/../../opengadtools}
OUT=${1:-$HERE/build/os3}
mkdir -p "$OUT"
# OpenAmigaGCC 16.2.0-open3's late-combine pass can store a copy one place late (GCC PR 123853; Common/wbclose.h's
# strcpy of "OpenWBNotify" came out so): off until open4 (openamigagcc #29), on a compiler that knows the option.
LATE=""
if "$CC" -fno-late-combine-instructions -S -o /dev/null -x c /dev/null 2>/dev/null; then LATE="-fno-late-combine-instructions"; fi
"$CC" -noixemul ${CPU:--m68040} $SPECS -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common $LATE -I"$OGT/lib" -I"$HERE/src" -o "$OUT/Menus" \
    "$HERE/src/menus.c" "$HERE/src/om_prefs.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/Menus ($(wc -c < "$OUT/Menus") bytes)"
