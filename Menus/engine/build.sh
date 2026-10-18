#!/bin/sh
# OpenMenus (the engine) with the os32-gcc16 stove (m68k-amigaos-gcc 16, NDK 3.2),
# the editor's om_prefs.c and the theme reader of an opengadtools checkout (OGT=dir).
#   Menus/engine/build.sh [OUT_DIR]      (default Menus/build/os3)
# The compiler: CC, else OS32_GCC16 (as OpenFTP's build-os3.sh), else STOVE,
# else ~/AmigaChrome/stoves/os32-gcc16. Not the os32 stove's GCC 6.5.0b: at -O2
# its peephole2 clears a word and the field after it with one clr.l, and
# copy_item()'s "c->nsubs = 0; c->subs = NULL;" came out leaving the low word
# of subs (harmless so far: subs is set again or never read, but one change
# away from a stray pointer). This script stops when given a GCC older than
# 16. GCC 16.2.0b has a trap of its own: at -O2 it can turn a loop that
# shifts an array up into a forward copy, so OpenMenus is built
# -fno-tree-loop-distribute-patterns, as OpenFTP is (the stove leaves it on).
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# The 68040, soft float: the specs file links libnix's soft libm (maths through
# the mathieee libraries), not its FPU libm881, so one program runs on any 68040.
SPECS="-specs=$HERE/../../specs/m68k-soft-m68040.specs"
STOVE=${OS32_GCC16:-${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: OpenMenus needs GCC 16 (the os32-gcc16 stove; set OS32_GCC16)"; exit 2; }
OGT=${OGT:-$HERE/../../../opengadtools}
OUT=${1:-$HERE/../build/os3}
mkdir -p "$OUT"
"$CC" -noixemul -m68040 $SPECS -std=gnu99 -Wall -Werror -O2 -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -fno-common -I"$OGT/lib" -I"$HERE/../src" -I"$HERE/../../Common" -o "$OUT/OpenMenus" \
    "$HERE/openmenus.c" "$HERE/../src/om_prefs.c" "$HERE/../src/om_kbd.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/OpenMenus ($(wc -c < "$OUT/OpenMenus") bytes)"
