#!/bin/sh
# Host tests for the Gamepads editor's settings and formats (gp_core.c), with the host's cc.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/gp_core_test
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$HERE/../src" -o "$OUT" "$HERE/test_gp_core.c" "$HERE/../src/gp_core.c"
"$OUT"
