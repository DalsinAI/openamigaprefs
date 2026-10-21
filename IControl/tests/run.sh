#!/bin/sh
# Host tests for the IControl section's fields (IControl/src/ic_prefs.c) on Look's PREF reader and writer.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/ic_test
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$HERE/../src" -I"$HERE/../../Look/src" -o "$OUT" \
    "$HERE/test_ic.c" "$HERE/../src/ic_prefs.c" "$HERE/../../Look/src/pf_iff.c"
"$OUT"
