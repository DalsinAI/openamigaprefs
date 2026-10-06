#!/bin/sh
# Host tests for the Sound editor's settings and files (sp_core.c), with the host's cc.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/sp_core_test
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$HERE/../src" -o "$OUT" "$HERE/test_sp_core.c" "$HERE/../src/sp_core.c"
"$OUT" "$HERE"
