#!/bin/sh
# Host tests for the Appearance tabs' PREF reader and writer (pf_iff.c, pf_prefs.c), with the host's cc.
# The samples were written by OS 3.2.3's own editors (Font, Asl, Workbench, WBPattern, Palette) and by
# OpenUp, on a scratch copy of an instance; the pointer files are made in the test.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/pf_test
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$HERE/../src" -o "$OUT" \
    "$HERE/test_pf.c" "$HERE/../src/pf_iff.c" "$HERE/../src/pf_prefs.c"
"$OUT" "$HERE"
