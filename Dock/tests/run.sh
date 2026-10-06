#!/bin/sh
# Host tests for OpenDock's settings and importers (od_dock.c), with the host's cc.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/od_dock_test
${CC:-cc} -std=gnu99 -Wall -Werror -O2 -I"$HERE/../src" -o "$OUT" "$HERE/test_od_dock.c" "$HERE/../src/od_dock.c"
"$OUT" "$HERE"
