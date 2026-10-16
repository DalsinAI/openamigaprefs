#!/bin/sh
# OpenDateTime's host tests: the time zone rules and the table of places, built
# with the host's C compiler and checked against the host's own tz data.
#   DateTime/test/run.sh
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="$HERE/../build/test"
mkdir -p "$OUT"
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -O2 -o "$OUT/test_tzrule" "$HERE/test_tzrule.c"
"$OUT/test_tzrule"
