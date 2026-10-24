#!/bin/sh
# OpenPrefs's host checks:
#   - the old names: OpenPrefs/build.sh's stubs are openprefs.c's old names (less OpenPrefs itself and the build
#     names Dock, Sound, Gamepads), and every section name maps to a page;
#   - the pages' names (opframe.c's lt_label) are the 21 the user named, each one once;
#   - with a stove (STOVE=, default ~/AmigaChrome/stoves/os32-gcc16): the app's first $VER is OpenPrefs's, and its
#     version is higher than every page's own $VER inside it and every editor's in this repository; the small body
#     has APPLY's three pages and none of their windows; each stub is under 1 KB and starts with a branch.
#   sh OpenPrefs/tests/run.sh
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
P=$HERE/..
fail=0
names=$(sed -n '/old_names\[\] = {/,/^};/p' "$HERE/src/openprefs.c" | grep -o '{ "[A-Za-z]*"' | sed 's/{ "//; s/"//' |
        grep -v -x -e OpenPrefs -e Dock -e Sound -e Gamepads | sort | tr '\n' ' ')
stubs=$(sed -n 's/^for n in \(.*\); do$/\1/p' "$HERE/build.sh" | tr ' ' '\n' | sort | tr '\n' ' ')
if [ "$names" = "$stubs" ]; then echo "ok   the stubs are the old names: $stubs"; else echo "FAIL stubs [$stubs] names [$names]"; fail=1; fi
python3 - "$HERE/src/openprefs.c" "$P/Common/looktabs.h" <<'EOF' || fail=1
import re, sys
src = open(sys.argv[1]).read()
lt = open(sys.argv[2]).read()
op = lt[lt.index('#ifdef LT_OP'):lt.index('#else', lt.index('#ifdef LT_OP'))]
pages = re.findall(r'\b(LT_[A-Z]+)\b', op[op.index('enum {'):op.index('LT_N };')])
used = set(re.findall(r'\{ "[A-Z]+", (LT_[A-Z]+) \}', src))
missing = [p for p in pages if p not in used]
cases = set(re.findall(r'case (LT_[A-Z]+):', src))
nocase = [p for p in pages if p not in cases]
if missing or nocase:
    print(f'FAIL sections: no name for {missing}, no page for {nocase}')
    sys.exit(1)
print(f'ok   {len(pages)} sections, each with a name and a page')
EOF
# the pages' names in the list, as the user named them (salvor/Design-No-ROM-Volume-20261024.md, section 8,
# question Q9): OpenPrefs takes them for a section too ("Date & Time", a project icon's name), so each is one
python3 - "$HERE/src/opframe.c" <<'EOF' || fail=1
import re, sys
src = open(sys.argv[1]).read()
m = re.search(r'lt_label\[LT_N\] = \{(.*?)\};', src, re.S)
labels = re.findall(r'"([^"]*)"', m.group(1)) if m else []
want = {"Theme", "Screen Mode", "Pointer", "Fonts", "Colours", "Backdrops", "Workbench", "File Requesters", "Windows",
        "Title Bar", "Menus", "IControl", "Dock", "Sound", "Input", "Gamepads", "Date & Time", "Serial", "File Types",
        "System", "Blanker"}
if set(labels) != want or len({l.lower() for l in labels}) != len(labels):
    print(f'FAIL page names: {labels}, missing {sorted(want - set(labels))}, not asked for {sorted(set(labels) - want)}')
    sys.exit(1)
print(f'ok   {len(labels)} page names, the user\'s, each one')
EOF
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
if [ ! -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then echo "skipped the build: no stove at $STOVE"; exit $fail; fi
OGT=${OGT:-$P/../opengadtools}
[ -f "$OGT/lib/ogt_theme.c" ] || { echo "skipped the build: no opengadtools at $OGT (OGT=)"; exit $fail; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
STOVE=$STOVE OGT=$OGT CPU=-m68040 OPT=-Os sh "$HERE/build.sh" "$T/out" > "$T/build.log" 2>&1 || { tail -20 "$T/build.log"; echo "FAIL the build"; exit 1; }
python3 - "$T/out" "$P" <<'EOF' || fail=1
import os, re, sys
out, repo = sys.argv[1], sys.argv[2]
def vers(data):
    return [(m.group(1).decode(), tuple(int(x) for x in m.group(2).decode().split('.')))
            for m in re.finditer(rb'\$VER: ([A-Za-z-]+) ([0-9]+(?:\.[0-9]+)*)', data)]
app = open(os.path.join(out, 'OpenPrefs'), 'rb').read()
v = vers(app)
bad = 0
if not v or v[0][0] != 'OpenPrefs':
    print(f'FAIL the first $VER is {v[:1]}'); bad = 1
top = v[0][1]
for name, ver in v[1:]:
    if ver >= top:
        print(f'FAIL {name} {ver} inside is not older than OpenPrefs {top}'); bad = 1
for root, _, fs in os.walk(repo):
    if '/.git' in root or '/build' in root:
        continue
    for f in fs:
        if f.endswith('.c'):
            for name, ver in vers(open(os.path.join(root, f), 'rb').read()):
                if name != 'OpenPrefs' and ver >= top and name in ('Look', 'OpenLook', 'Windows', 'TitleBar', 'Menus',
                        'OpenDock-Prefs', 'OpenSound', 'OpenInput', 'Gamepads', 'OpenDateTime', 'OpenSerial', 'OpenTypes', 'OpenSystem'):
                    print(f'FAIL {name} {ver} ({f}) is not older than OpenPrefs {top}'); bad = 1
print(f'ok   OpenPrefs {".".join(map(str, top))} is the first $VER and the highest' if not bad else 'FAIL versions')
small = open(os.path.join(out, 'OpenPrefs.small'), 'rb').read()
syms = set(re.findall(rb'_?([a-z_]+_main|gui_once|lt_open)\x00', small))
for want in (b'sound_main', b'gamepads_main', b'system_main'):
    if want not in syms:
        print(f'FAIL the small body has no {want.decode()}'); bad = 1
for no in (b'gui_once', b'lt_open', b'look_main'):
    if no in syms:
        print(f'FAIL the small body has {no.decode()} (a window)'); bad = 1
if not bad:
    print(f'ok   the small body: {len(small)} bytes, APPLY\'s three pages and no window')
for s in sorted(os.listdir(os.path.join(out, 'stubs'))):
    d = open(os.path.join(out, 'stubs', s), 'rb').read()
    i = d.index(b'\x00\x00\x03\xe9') + 8           # HUNK_CODE, its size: the first code word
    if len(d) > 1024 or d[i] != 0x60:
        print(f'FAIL stub {s}: {len(d)} bytes, first word {d[i:i + 2].hex()}'); bad = 1
if not bad:
    print(f'ok   {len(os.listdir(os.path.join(out, "stubs")))} stubs, each under 1 KB and starting with a branch')
sys.exit(bad)
EOF
exit $fail
