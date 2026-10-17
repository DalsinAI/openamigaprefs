#!/bin/sh
# OpenMenus (the engine) with the os32 stove (m68k-amigaos-gcc, NDK 3.2), the
# editor's om_prefs.c and the theme reader of an opengadtools checkout (OGT=dir).
#   Menus/engine/build.sh [OUT_DIR]      (default Menus/build/os3)
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# The 68040, soft float: the specs file links libnix's soft libm (maths through
# the mathieee libraries), not its FPU libm881, so one program runs on any 68040.
SPECS="-specs=$HERE/../../specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OGT=${OGT:-$HERE/../../../opengadtools}
OUT=${1:-$HERE/../build/os3}
mkdir -p "$OUT"
"$CC" -noixemul -m68040 $SPECS -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -fno-common -I"$OGT/lib" -I"$HERE/../src" -I"$HERE/../../Common" -o "$OUT/OpenMenus" \
    "$HERE/openmenus.c" "$HERE/../src/om_prefs.c" "$HERE/../src/om_kbd.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/OpenMenus ($(wc -c < "$OUT/OpenMenus") bytes)"
