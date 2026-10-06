#!/bin/sh
# Host tests for OpenMenus' settings (om_prefs.c), with the host's cc.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/om_prefs_test
${CC:-cc} -std=gnu99 -Wall -Werror -O2 -I"$HERE/../src" -o "$OUT" "$HERE/test_om_prefs.c" "$HERE/../src/om_prefs.c"
"$OUT" "$HERE"
