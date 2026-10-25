#!/bin/sh
# OpenPrefs Sound and OpenSpeaker, with the os32 stove
# (m68k-amigaos-gcc, NDK 3.2, AHI's includes).
#   Sound/build.sh [OUT_DIR]     (default build/os3)
# MIT, Copyright (c) 2026 AmigaChrome Team.
# CPU= and OPT= change -m68040 and -O2 (the ROM's build: OPT=-Os).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# The 68040, soft float: the specs file links libnix's soft libm (maths through
# the mathieee libraries), not its FPU libm881, so one program runs on any 68040.
SPECS="-specs=$HERE/../specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OGT=${OGT:-$HERE/../../opengadtools}      # a checkout of DalsinAI/opengadtools: its theme reader (lib/ogt_theme.c)
OUT=${1:-$HERE/build/os3}
CFLAGS="-noixemul ${CPU:--m68040} $SPECS -std=gnu99 -Wall -Werror ${OPT:--O2} -fno-delete-null-pointer-checks -fno-common -I$HERE/src -I$HERE/../Common -I$OGT/lib"
# OpenAmigaGCC 16.2.0-open3's late-combine pass can store a copy one place late (GCC PR 123853; Common/wbclose.h's
# strcpy of "OpenWBNotify" came out so): off until open4 (openamigagcc #29), on a compiler that knows the option.
if "$CC" -fno-late-combine-instructions -S -o /dev/null -x c /dev/null 2>/dev/null; then CFLAGS="$CFLAGS -fno-late-combine-instructions"; fi
mkdir -p "$OUT"
S="$HERE/src"
"$CC" $CFLAGS -o "$OUT/Sound" "$S/sound.c" "$S/sp_core.c" "$S/sp_amiga.c" "$S/sp_test.c"
# OpenSpeaker reads where OpenMenus' bar is with the Menus editor's om_prefs.c
"$CC" $CFLAGS -I"$HERE/../Menus/src" -o "$OUT/OpenSpeaker" "$S/openspeaker.c" "$S/sp_core.c" "$S/sp_amiga.c" "$HERE/../Menus/src/om_prefs.c" "$OGT/lib/ogt_theme.c"
echo "$OUT/Sound ($(wc -c < "$OUT/Sound") bytes), $OUT/OpenSpeaker ($(wc -c < "$OUT/OpenSpeaker") bytes)"
