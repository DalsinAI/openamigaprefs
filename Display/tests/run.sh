#!/bin/sh
# Host tests for the Screen mode section's fields (Display/src/dp_prefs.c) on Look's PREF reader and writer.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/dp_test
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$HERE/../src" -I"$HERE/../../Look/src" -o "$OUT" \
    "$HERE/test_dp.c" "$HERE/../src/dp_prefs.c" "$HERE/../../Look/src/pf_iff.c"
"$OUT"
