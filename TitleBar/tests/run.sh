#!/bin/sh
# Host tests for OpenTitle's settings (src/tb_prefs.h) and its see-through bar's fade (src/tb_fade.h), with the
# host's cc.
#   sh TitleBar/tests/run.sh
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
CC=${CC:-cc}
"$CC" -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$HERE/../src" -o "$T/t" "$HERE/test_tb.c"
"$T/t"
