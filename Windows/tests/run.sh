#!/bin/sh
# OpenWindows' gesture table (src/gestures.c) built for the host and run.
#   Windows/tests/run.sh          needs a host C compiler (cc or gcc)
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../build/host}
mkdir -p "$OUT"
${CC:-cc} -std=gnu99 -O1 -g -Wall -Wextra -Werror -o "$OUT/test_gestures" "$HERE/test_gestures.c" "$HERE/../src/gestures.c"
"$OUT/test_gestures"
