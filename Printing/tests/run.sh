#!/bin/sh
# Host tests for the Printing section's fields (Printing/src/pr_prefs.c) on Look's PREF reader and writer.
# With an openamigaup checkout beside this one (OAU=dir), also: the two samples are still the bytes its OpenPrinter
# part writes (tools/build_pack.py printer_prefs and printergfx_prefs), so the page's no-file files stay the part's.
#   sh Printing/tests/run.sh
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/pr_test
${CC:-cc} -std=gnu99 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -I"$HERE/../src" -I"$HERE/../../Look/src" -o "$OUT" \
    "$HERE/test_pr.c" "$HERE/../src/pr_prefs.c" "$HERE/../../Look/src/pf_iff.c"
"$OUT" "$HERE"
OAU=${OAU:-$HERE/../../../openamigaup}
if [ ! -f "$OAU/tools/build_pack.py" ]; then echo "skipped the OpenPrinter comparison: no openamigaup at $OAU (OAU=)"; exit 0; fi
python3 -I - "$OAU/tools" "$HERE" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
import build_pack as bp
here = sys.argv[2]
bad = 0
for name, data in (("openprinter-printer.prefs", bp.printer_prefs()), ("openprinter-printergfx.prefs", bp.printergfx_prefs())):
    if open(f"{here}/{name}", "rb").read() != data:
        print(f"FAIL {name} is no longer what openamigaup's OpenPrinter part writes"); bad = 1
print("ok   the samples are the OpenPrinter part's files" if not bad else "FAIL the samples")
sys.exit(bad)
EOF
