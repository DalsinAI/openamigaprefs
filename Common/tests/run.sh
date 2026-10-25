#!/bin/sh
# Host tests for Common/prefs_boot_core.c (what intuition's prefs task applies at boot from ENV:OpenAmiga/Sound,
# ENV:OpenAmiga/Stack and ENV:OpenInput/LowLevelPatch): its vectors, and its Sound reader against the Sound
# editor's own (sp_core.c) on the same files; and Common/deskicons.h's words (ENV:Sys/DesktopIcons, the Icons
# page's No icons, which OpenBench follows).
#   sh Common/tests/run.sh
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
C=$HERE/..
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
CC=${CC:-cc}
"$CC" -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$C" -o "$T/v" "$HERE/prefs_boot_core_test.c" "$C/prefs_boot_core.c"
"$T/v" > "$T/v.txt" || true
if [ "$(tail -n 1 "$T/v.txt")" = PASS ]; then echo "ok   the vectors: $(grep -c '^ok' "$T/v.txt") checks"; else grep FAIL "$T/v.txt"; exit 1; fi
"$CC" -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$C" -I"$C/../Sound/src" -o "$T/x" \
    "$HERE/sound_cross.c" "$C/prefs_boot_core.c" "$C/../Sound/src/sp_core.c"
"$T/x"
"$CC" -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$C" -o "$T/d" "$HERE/deskicons_test.c"
"$T/d" > "$T/d.txt" || true
if [ "$(tail -n 1 "$T/d.txt")" = PASS ]; then echo "ok   ENV:Sys/DesktopIcons's words: $(grep -c '^ok' "$T/d.txt") checks"; else grep FAIL "$T/d.txt"; exit 1; fi
